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
#include <dawn/webgpu_cpp.h>

module fyuu_rhi:webgpu_pipeline;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :pipeline;
import :pipeline_dispatch;
import :pipeline_resource_group_factory;
import :resource_factory;
import :sampler_factory;
import :view_factory;
import :webgpu_data;

namespace fyuu_rhi {

	template <>
	struct CreatePipelineResourceGroup<webgpu::Pipeline> {
		webgpu::Pipeline* native;

		static webgpu::Resource const& NativeResource(Resource const* resource) {
			if (!resource || !resource->m_impl) {
				throw std::invalid_argument("A WebGPU buffer binding is empty");
			}
			auto result = std::get_if<webgpu::Resource>(
				&resource->m_impl->native
			);
			if (!result) {
				throw std::invalid_argument(
					"A WebGPU pipeline cannot bind a foreign resource"
				);
			}
			return *result;
		}

		static webgpu::View const& NativeView(View const* view) {
			if (!view || !view->m_impl) {
				throw std::invalid_argument("A WebGPU view binding is empty");
			}
			auto result = std::get_if<webgpu::View>(&view->m_impl->native);
			if (!result) {
				throw std::invalid_argument(
					"A WebGPU pipeline cannot bind a foreign view"
				);
			}
			return *result;
		}

		static webgpu::Sampler const& NativeSampler(Sampler const* sampler) {
			if (!sampler || !sampler->m_impl) {
				throw std::invalid_argument("A WebGPU sampler binding is empty");
			}
			auto result = std::get_if<webgpu::Sampler>(&sampler->m_impl->native);
			if (!result) {
				throw std::invalid_argument(
					"A WebGPU pipeline cannot bind a foreign sampler"
				);
			}
			return *result;
		}

		PipelineResourceGroup operator()(
			std::uint32_t space,
			std::span<pipeline::ResourceBinding const> bindings
		) const {
			if (space >= native->bind_group_layouts.size()) {
				throw std::out_of_range(
					"The WebGPU pipeline has no requested resource space"
				);
			}

			auto metadata = native->bindings |
				std::views::filter(
					[space](auto const& binding) {
						return binding.space == space;
					}
				);
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
							"The WebGPU pipeline space has no requested slot"
						);
					}
					if (declaration->count != 1u || binding.array_element != 0u) {
						throw std::invalid_argument(
							"WebGPU binding arrays are not implemented"
						);
					}
					auto duplicate_count = std::ranges::count_if(
						bindings,
						[&binding](auto const& candidate) {
							return candidate.slot == binding.slot;
						}
					);
					if (duplicate_count != 1) {
						throw std::invalid_argument(
							"A WebGPU pipeline binding is specified more than once"
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
							"The WebGPU pipeline binding value does not match its declaration"
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
					"A WebGPU resource group requires every declared binding"
				);
			}

			std::vector<wgpu::BindGroupEntry> entries;
			std::vector<webgpu::PipelineResourceGroup::DynamicBuffer> dynamic_buffers;
			entries.reserve(bindings.size() * 2u);
			std::ranges::for_each(
				bindings,
				[&](auto const& binding) {
					auto declaration = std::ranges::find_if(
						metadata,
						[&binding](auto const& candidate) {
							return candidate.slot == binding.slot;
						}
					);
					wgpu::BindGroupEntry entry{
						.binding = binding.slot
					};
					if (auto buffer = binding.value.Buffer()) {
						auto const& resource = NativeResource(buffer);
						auto native_buffer = std::get_if<wgpu::Buffer>(&resource.impl);
						if (!native_buffer) {
							throw std::invalid_argument(
								"A WebGPU buffer binding requires a buffer resource"
							);
						}
						entry.buffer = *native_buffer;
						entry.offset = binding.value.Offset();
						entry.size = binding.value.Size();
						if (entry.size == pipeline::PipelineWholeBuffer) {
							entry.size = wgpu::kWholeSize;
						}
						auto capacity = native_buffer->GetSize();
						if (entry.offset > capacity) {
							throw std::out_of_range(
								"The WebGPU buffer binding offset exceeds the buffer"
							);
						}
						auto size = entry.size == wgpu::kWholeSize
							? capacity - entry.offset
							: entry.size;
						if (
							size > capacity - entry.offset
						) {
							throw std::out_of_range(
								"The WebGPU buffer binding range exceeds the buffer"
							);
						}
						dynamic_buffers.emplace_back(
							webgpu::PipelineResourceGroup::DynamicBuffer{
								.entry = entries.size(),
								.base_offset = entry.offset,
								.size = size,
								.capacity = capacity
							}
						);
					}
					else if (auto view = binding.value.BoundView()) {
						auto const& native_view = NativeView(view);
						if (auto texture_view = std::get_if<wgpu::TextureView>(&native_view.impl)) {
							entry.textureView = *texture_view;
						}
						else if (auto buffer_view = std::get_if<webgpu::View::Buffer>(
							&native_view.impl
						)) {
							entry.buffer = buffer_view->impl;
							entry.offset = buffer_view->offset;
							entry.size = buffer_view->size;
						}
						else {
							throw std::invalid_argument(
								"A WebGPU resource binding has an empty view"
							);
						}
						entries.emplace_back(std::move(entry));
						if (binding.value.BoundSampler()) {
							// WGSL has no combined sampler, so Slang emits the pair as a texture at
							// one binding and a sampler at another, and the layout Dawn derives from
							// that same source is the authority the entry has to match. Slang's WGSL
							// layout reports the logical index for both halves - the texture's - so
							// the reflection only names a usable binding when it differs from it.
							// Otherwise the pairing the WGSL backend emits applies: a Sampler2D at
							// register(t1, space1) becomes @binding(1) texture_2d plus @binding(2)
							// sampler, which is the shape verified against the generated source.
							auto sampler_binding = declaration->sampler_slot;
							if (sampler_binding == declaration->slot) {
								sampler_binding = declaration->slot + 1u;
							}
							entries.emplace_back(
								wgpu::BindGroupEntry{
									.binding = sampler_binding,
									.sampler = NativeSampler(
										binding.value.BoundSampler()
									).impl
								}
							);
						}
						return;
					}
					else if (auto sampler = binding.value.BoundSampler()) {
						entry.sampler = NativeSampler(sampler).impl;
					}
					else {
						throw std::invalid_argument(
							"Unsupported WebGPU pipeline binding value"
						);
					}
					entries.emplace_back(std::move(entry));
				}
			);
			std::ranges::sort(
				dynamic_buffers,
				{},
				[&entries](auto const& binding) {
					return entries[binding.entry].binding;
				}
			);

			wgpu::BindGroupDescriptor descriptor{
				.layout = native->bind_group_layouts[space],
				.entryCount = entries.size(),
				.entries = entries.data()
			};
			auto impl = native->device.CreateBindGroup(&descriptor);
			return MakePipelineResourceGroup(
				webgpu::PipelineResourceGroup{
					.space = space,
					.device = native->device,
					.layout = native->bind_group_layouts[space],
					.entries = std::move(entries),
					.dynamic_buffers = std::move(dynamic_buffers),
					.impl = std::move(impl)
				}
			);
		}
	};

} // namespace fyuu_rhi
