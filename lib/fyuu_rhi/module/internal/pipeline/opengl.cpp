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
#if !defined(__APPLE__)
#include <glad/glad.h>
#endif // !defined(__APPLE__)

module fyuu_rhi:opengl_pipeline;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :opengl_data;
import :pipeline;
import :pipeline_dispatch;
import :pipeline_resource_group_factory;
import :resource_factory;
import :sampler_factory;
import :view_factory;

namespace fyuu_rhi {

	template <>
	struct CreatePipelineResourceGroup<opengl::Pipeline> {
		opengl::Pipeline* native;

		static opengl::Resource const& NativeResource(Resource const* resource) {
			if (!resource || !resource->m_impl) {
				throw std::invalid_argument("An OpenGL buffer binding is empty");
			}
			auto result = std::get_if<opengl::Resource>(
				&resource->m_impl->native
			);
			if (!result) {
				throw std::invalid_argument(
					"An OpenGL pipeline cannot bind a foreign resource"
				);
			}
			return *result;
		}

		static opengl::View const& NativeView(View const* view) {
			if (!view || !view->m_impl) {
				throw std::invalid_argument("An OpenGL view binding is empty");
			}
			auto result = std::get_if<opengl::View>(&view->m_impl->native);
			if (!result) {
				throw std::invalid_argument(
					"An OpenGL pipeline cannot bind a foreign view"
				);
			}
			return *result;
		}

		static opengl::Sampler const& NativeSampler(Sampler const* sampler) {
			if (!sampler || !sampler->m_impl) {
				throw std::invalid_argument("An OpenGL sampler binding is empty");
			}
			auto result = std::get_if<opengl::Sampler>(&sampler->m_impl->native);
			if (!result) {
				throw std::invalid_argument(
					"An OpenGL pipeline cannot bind a foreign sampler"
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
						return binding.space == space;
					}
				);
			if (space != 0u || std::ranges::empty(metadata)) {
				throw std::out_of_range(
					"The OpenGL pipeline has no requested resource space"
				);
			}

			std::ranges::for_each(
				bindings,
				[&](auto const& binding) {
					auto declaration = std::ranges::find_if(
						metadata,
						[&binding](auto const& candidate) {
							return candidate.slot == binding.slot;
						}
					);
					if (declaration == std::ranges::end(metadata)) {
						throw std::invalid_argument(
							"The OpenGL pipeline space has no requested slot"
						);
					}
					if (binding.array_element >= declaration->count) {
						throw std::out_of_range(
							"The OpenGL pipeline binding array element exceeds its declared count"
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
							"An OpenGL pipeline binding is specified more than once"
						);
					}

					using Bits = ResourceFlagBits;
					bool expects_buffer =
						declaration->flags.Test(Bits::UniformBuffer) ||
						declaration->flags.Test(Bits::StorageBuffer);
					bool expects_view =
						declaration->flags.Test(Bits::UniformTexelBuffer) ||
						declaration->flags.Test(Bits::StorageTexelBuffer) ||
						declaration->flags.Test(Bits::TextureBinding) ||
						declaration->flags.Test(Bits::StorageBinding);
					bool expects_sampler = declaration->flags.Test(
						Bits::SamplerBinding
					);
					bool has_buffer = binding.value.Buffer() != nullptr;
					bool has_view = binding.value.BoundView() != nullptr;
					bool has_sampler = binding.value.BoundSampler() != nullptr;
					if (
						expects_buffer != has_buffer ||
						expects_view != has_view ||
						expects_sampler != has_sampler
					) {
						throw std::invalid_argument(
							"The OpenGL pipeline binding value does not match its declaration"
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
							return binding.slot == declaration.slot;
						}
					) == declaration.count;
				}
			)) {
				throw std::invalid_argument(
					"An OpenGL resource group requires every descriptor array element"
				);
			}

			std::vector<opengl::PipelineResourceGroup::Binding> native_bindings;
			native_bindings.reserve(bindings.size());
			std::ranges::transform(
				bindings,
				std::back_inserter(native_bindings),
				[](auto const& binding) {
					opengl::PipelineResourceGroup::Binding result{
						.slot = binding.slot,
						.array_element = binding.array_element,
						.buffer = 0u,
						.buffer_offset = 0u,
						.buffer_size = pipeline::PipelineWholeBuffer,
						.view = 0u,
						.view_target = 0u,
						.view_format = 0u,
						.sampler = 0u
					};
					if (auto resource = binding.value.Buffer()) {
						auto const& native_resource = NativeResource(resource);
						if (native_resource.type != opengl::ResourceType::Buffer) {
							throw std::invalid_argument(
								"An OpenGL buffer binding requires a buffer resource"
							);
						}
						result.buffer = native_resource.impl.get();
						result.buffer_offset = binding.value.Offset();
						result.buffer_size = binding.value.Size();
					}
					else if (auto view = binding.value.BoundView()) {
						auto const& native_view = NativeView(view);
						result.view = native_view.impl.get();
						result.view_target = native_view.target;
						result.view_format = native_view.format;
						if (auto sampler = binding.value.BoundSampler()) {
							result.sampler = NativeSampler(sampler).impl.get();
						}
					}
					else if (auto sampler = binding.value.BoundSampler()) {
						result.sampler = NativeSampler(sampler).impl.get();
					}
					else {
						throw std::invalid_argument(
							"Unsupported OpenGL pipeline binding value"
						);
					}
					return result;
				}
			);

			return MakePipelineResourceGroup(
				opengl::PipelineResourceGroup{
					.space = space,
					.bindings = std::move(native_bindings)
				}
			);
		}
	};

} // namespace fyuu_rhi
#endif // !defined(__APPLE__)
