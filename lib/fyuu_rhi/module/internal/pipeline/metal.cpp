module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#include <vector>

#include <algorithm>
#include <iterator>

#include <cstdint>

#include <variant>

#include <ranges>
#include <span>
#endif // !defined(__cpp_lib_modules)
#if defined(__APPLE__)
#include <Metal/Metal.hpp>
#endif // defined(__APPLE__)

module fyuu_rhi:metal_pipeline;
#if defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :metal_data;
import :pipeline;
import :pipeline_dispatch;
import :pipeline_resource_group_factory;
import :resource_factory;
import :sampler_factory;
import :view_factory;

namespace fyuu_rhi {

	template <>
	struct CreatePipelineResourceGroup<metal::Pipeline> {
		metal::Pipeline* native;

		static metal::Resource const& NativeResource(Resource const* resource) {
			if (!resource || !resource->m_impl) {
				throw std::invalid_argument("A Metal buffer binding is empty");
			}
			auto result = std::get_if<metal::Resource>(&resource->m_impl->native);
			if (!result) {
				throw std::invalid_argument(
					"A Metal pipeline cannot bind a foreign resource"
				);
			}
			return *result;
		}

		static metal::View const& NativeView(View const* view) {
			if (!view || !view->m_impl) {
				throw std::invalid_argument("A Metal view binding is empty");
			}
			auto result = std::get_if<metal::View>(&view->m_impl->native);
			if (!result) {
				throw std::invalid_argument(
					"A Metal pipeline cannot bind a foreign view"
				);
			}
			return *result;
		}

		static metal::Sampler const& NativeSampler(Sampler const* sampler) {
			if (!sampler || !sampler->m_impl) {
				throw std::invalid_argument("A Metal sampler binding is empty");
			}
			auto result = std::get_if<metal::Sampler>(&sampler->m_impl->native);
			if (!result) {
				throw std::invalid_argument(
					"A Metal pipeline cannot bind a foreign sampler"
				);
			}
			return *result;
		}

		PipelineResourceGroup operator()(
			std::uint32_t space,
			std::span<pipeline::ResourceBinding const> bindings
		) const {
			auto metadata = native->bindings |
				std::views::filter(
					[space](auto const& binding) {
						return binding.metadata.space == space;
					}
				);
			if (std::ranges::empty(metadata)) {
				throw std::out_of_range(
					"The Metal pipeline has no requested resource space"
				);
			}

			std::ranges::for_each(
				bindings,
				[&](auto const& binding) {
					auto declaration = std::ranges::find_if(
						metadata,
						[&binding](auto const& candidate) {
							return candidate.metadata.slot == binding.slot;
						}
					);
					if (declaration == std::ranges::end(metadata)) {
						throw std::invalid_argument(
							"The Metal pipeline space has no requested slot"
						);
					}
					if (binding.array_element >= declaration->metadata.count) {
						throw std::out_of_range(
							"The Metal binding array element exceeds its declared count"
						);
					}
					auto duplicate_count = std::ranges::count_if(
						bindings,
						[&binding](auto const& candidate) {
							return candidate.slot == binding.slot &&
								candidate.array_element == binding.array_element;
						}
					);
					if (duplicate_count != 1) {
						throw std::invalid_argument(
							"A Metal pipeline binding is specified more than once"
						);
					}

					using Bits = ResourceFlagBits;
					auto const& flags = declaration->metadata.flags;
					bool expects_buffer =
						flags.Test(Bits::UniformBuffer) ||
						flags.Test(Bits::StorageBuffer);
					bool expects_view =
						flags.Test(Bits::UniformTexelBuffer) ||
						flags.Test(Bits::StorageTexelBuffer) ||
						flags.Test(Bits::TextureBinding) ||
						flags.Test(Bits::StorageBinding);
					bool expects_sampler = flags.Test(Bits::SamplerBinding);
					bool has_buffer = binding.value.Buffer() != nullptr;
					bool has_view = binding.value.BoundView() != nullptr;
					bool has_sampler = binding.value.BoundSampler() != nullptr;
					if (
						expects_buffer != has_buffer ||
						expects_view != has_view ||
						expects_sampler != has_sampler
					) {
						throw std::invalid_argument(
							"The Metal binding value does not match its declaration"
						);
					}
				}
			);

			if (!std::ranges::all_of(
				metadata,
				[bindings](auto const& declaration) {
					return std::ranges::count_if(
						bindings,
						[&declaration](auto const& binding) {
							return binding.slot == declaration.metadata.slot;
						}
					) == declaration.metadata.count;
				}
			)) {
				throw std::invalid_argument(
					"A Metal resource group requires every descriptor array element"
				);
			}

			std::vector<metal::PipelineResourceGroup::Binding> native_bindings;
			native_bindings.reserve(bindings.size());
			std::ranges::transform(
				bindings,
				std::back_inserter(native_bindings),
				[&](auto const& binding) {
					auto declaration = std::ranges::find_if(
						metadata,
						[&binding](auto const& candidate) {
							return candidate.metadata.slot == binding.slot;
						}
					);
					metal::PipelineResourceGroup::Binding result{
						.resource_slot =
							declaration->resource_slot + binding.array_element,
						.sampler_slot =
							declaration->sampler_slot + binding.array_element,
						.visibility = declaration->visibility,
						.buffer = {},
						.buffer_offset = 0u,
						.buffer_size = pipeline::PipelineWholeBuffer,
						.texture = {},
						.sampler = {}
					};
					if (auto resource = binding.value.Buffer()) {
						auto const& native_resource = NativeResource(resource);
						auto buffer = std::get_if<NS::SharedPtr<MTL::Buffer>>(
							&native_resource.impl
						);
						if (!buffer) {
							throw std::invalid_argument(
								"A Metal buffer binding requires a buffer resource"
							);
						}
						if ((*buffer)->device() != native->device.get()) {
							throw std::invalid_argument(
								"The Metal buffer belongs to another logical device"
							);
						}
						auto offset = binding.value.Offset();
						auto size = binding.value.Size();
						auto length = static_cast<std::size_t>((*buffer)->length());
						if (offset > length) {
							throw std::out_of_range(
								"The Metal buffer binding offset exceeds the buffer"
							);
						}
						if (size == pipeline::PipelineWholeBuffer) {
							size = length - offset;
						}
						if (size > length - offset) {
							throw std::out_of_range(
								"The Metal buffer binding range exceeds the buffer"
							);
						}
						result.buffer = *buffer;
						result.buffer_offset = offset;
						result.buffer_size = size;
					}
					if (auto view = binding.value.BoundView()) {
						auto const& native_view = NativeView(view);
						if (auto buffer_view = std::get_if<metal::BufferView>(
							&native_view.impl
						)) {
							if (buffer_view->buffer->device() != native->device.get()) {
								throw std::invalid_argument(
									"The Metal buffer view belongs to another logical device"
								);
							}
							result.buffer = buffer_view->buffer;
							result.buffer_offset = buffer_view->offset;
							result.buffer_size = buffer_view->size;
						}
						else if (auto texture = std::get_if<NS::SharedPtr<MTL::Texture>>(
							&native_view.impl
						)) {
							if ((*texture)->device() != native->device.get()) {
								throw std::invalid_argument(
									"The Metal texture view belongs to another logical device"
								);
							}
							result.texture = *texture;
						}
						else {
							throw std::invalid_argument(
								"A Metal resource binding has an empty view"
							);
						}
					}
					if (auto sampler = binding.value.BoundSampler()) {
						auto const& native_sampler = NativeSampler(sampler);
						if (native_sampler.impl->device() != native->device.get()) {
							throw std::invalid_argument(
								"The Metal sampler belongs to another logical device"
							);
						}
						result.sampler = native_sampler.impl;
					}
					return result;
				}
			);

			return MakePipelineResourceGroup(
				metal::PipelineResourceGroup{
					.space = space,
					.bindings = std::move(native_bindings)
				}
			);
		}
	};

} // namespace fyuu_rhi
#endif // defined(__APPLE__)
