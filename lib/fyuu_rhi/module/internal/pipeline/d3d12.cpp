module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <cstddef>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include <cstdint>

#include <span>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <D3D12MemAlloc.h>
#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl.h>
#endif // defined(_WIN32)

module fyuu_rhi:d3d12_pipeline;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :d3d12_data;
import :d3d12_descriptor_allocator;
import :d3d12_utility;
import :resource;
import :view;
import :sampler;
import :pipeline;
import :pipeline_dispatch;
import :pipeline_resource_group_dispatch;
import :pipeline_resource_group_factory;
import :resource_factory;
import :sampler_factory;
import :view_factory;

namespace fyuu_rhi {

	template <>
	struct CreatePipelineResourceGroup<d3d12::Pipeline> {
		d3d12::Pipeline* native;

		static d3d12::Resource const& NativeResource(Resource const* resource) {
			if (!resource || !resource->m_impl) {
				throw std::invalid_argument("A D3D12 buffer binding is empty");
			}
			auto result = std::get_if<d3d12::Resource>(
				&resource->m_impl->native
			);
			if (!result) {
				throw std::invalid_argument("A D3D12 pipeline cannot bind a foreign resource");
			}
			return *result;
		}

		static d3d12::View const& NativeView(View const* view) {
			if (!view || !view->m_impl) {
				throw std::invalid_argument("A D3D12 view binding is empty");
			}
			auto result = std::get_if<d3d12::View>(
				&view->m_impl->native
			);
			if (!result) {
				throw std::invalid_argument("A D3D12 pipeline cannot bind a foreign view");
			}
			return *result;
		}

		static d3d12::Sampler const& NativeSampler(Sampler const* sampler) {
			if (!sampler || !sampler->m_impl) {
				throw std::invalid_argument("A D3D12 sampler binding is empty");
			}
			auto result = std::get_if<d3d12::Sampler>(
				&sampler->m_impl->native
			);
			if (!result) {
				throw std::invalid_argument("A D3D12 pipeline cannot bind a foreign sampler");
			}
			return *result;
		}

		PipelineResourceGroup operator()(std::uint32_t space, std::span<pipeline::ResourceBinding const> bindings) const {
			using Bits = ResourceFlagBits;
			using ViewType = d3d12::View::Type;

			Microsoft::WRL::ComPtr<ID3D12Device> device;
			d3d12::ThrowIfFailed(native->root_signature->GetDevice(IID_PPV_ARGS(&device)));

			auto FindMetadata = [this, space](std::uint32_t slot) {
				return std::ranges::find_if(
					native->bindings,
					[space, slot](pipeline::BindingMetadata const& metadata) {
						return metadata.space == space && metadata.slot == slot;
					}
				);
				};
			auto FindBinding = [&bindings](std::uint32_t slot, std::uint32_t array_element) {
				return std::ranges::find_if(
					bindings,
					[slot, array_element](pipeline::ResourceBinding const& binding) {
						return binding.slot == slot && binding.array_element == array_element;
					}
				);
				};
			auto ValidateResourceDevice = [&device](d3d12::Resource const& resource) {
				Microsoft::WRL::ComPtr<ID3D12Device> resource_device;
				d3d12::ThrowIfFailed(
					resource.allocation->GetResource()->GetDevice(
						IID_PPV_ARGS(&resource_device)
					)
				);
				if (resource_device != device) {
					throw std::invalid_argument(
						"The D3D12 resource belongs to another logical device"
					);
				}
				};
			auto SourceViewDescriptor = [&device](d3d12::View const& view, ViewType type) {
				auto const& descriptor = view.descriptors[static_cast<std::size_t>(type)];
				descriptor.ValidateDevice(device);
				return descriptor.CPU();
				};

			for (auto const& binding : bindings) {
				auto metadata = FindMetadata(binding.slot);
				if (metadata == native->bindings.end()) {
					throw std::invalid_argument("The D3D12 pipeline space has no requested slot");
				}
				if (binding.array_element >= metadata->count) {
					throw std::out_of_range("The D3D12 pipeline binding array element exceeds its declared count");
				}
				if (
					std::count_if(
						bindings.begin(),
						bindings.end(),
						[&binding](pipeline::ResourceBinding const& candidate) {
							return candidate.slot == binding.slot && candidate.array_element == binding.array_element;
						}
					) != 1
				) {
					throw std::invalid_argument("A D3D12 pipeline binding is specified more than once");
				}
				bool expects_buffer = metadata->flags.Test(Bits::UniformBuffer) ||
					metadata->flags.Test(Bits::StorageBuffer);
				bool expects_view = metadata->flags.Test(Bits::TextureBinding) ||
					metadata->flags.Test(Bits::StorageBinding);
				bool expects_sampler = metadata->flags.Test(Bits::SamplerBinding);
				bool matches = false;
				if (expects_buffer) {
					matches = binding.value.Buffer() && !expects_view && !expects_sampler;
				}
				else if (expects_view && expects_sampler) {
					matches = binding.value.BoundView() && binding.value.BoundSampler();
				}
				else if (expects_view) {
					matches = binding.value.BoundView() && !binding.value.BoundSampler();
				}
				else if (expects_sampler) {
					matches = binding.value.BoundSampler() && !binding.value.BoundView();
				}
				if (!matches) {
					throw std::invalid_argument("A resource does not match the D3D12 pipeline slot");
				}
			}

			d3d12::PipelineResourceGroup result{
				.space = space,
				.root_signature = native->root_signature,
				.resource_heap = native->resource_descriptors.Heap(),
				.sampler_heap = native->sampler_descriptors.Heap()
			};
			std::uint32_t root_parameter = 0u;
			for (auto const& metadata : native->bindings) {
				bool combined = metadata.flags.Test(Bits::TextureBinding) &&
					metadata.flags.Test(Bits::SamplerBinding);
				auto root_parameter_count = combined ? 2u : 1u;
				if (metadata.space != space) {
					root_parameter += root_parameter_count;
					continue;
				}

				if (combined) {
					auto resource_descriptors = native->resource_descriptors.Allocate(metadata.count);
					auto sampler_descriptors = native->sampler_descriptors.Allocate(metadata.count);
					for (std::uint32_t element = 0u; element < metadata.count; ++element) {
						auto binding = FindBinding(metadata.slot, element);
						if (binding == bindings.end()) {
							throw std::invalid_argument(
								"A D3D12 resource group requires every descriptor array element"
							);
						}
						auto const& view = NativeView(binding->value.BoundView());
						auto const& sampler = NativeSampler(binding->value.BoundSampler());
						sampler.descriptor.ValidateDevice(device);
						device->CopyDescriptorsSimple(
							1u,
							resource_descriptors.CPU(element),
							SourceViewDescriptor(view, ViewType::ShaderResource),
							D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
						);
						device->CopyDescriptorsSimple(
							1u,
							sampler_descriptors.CPU(element),
							sampler.descriptor.CPU(),
							D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
						);
					}
					result.tables.emplace_back(
						root_parameter,
						std::move(resource_descriptors)
					);
					result.tables.emplace_back(
						root_parameter + 1u,
						std::move(sampler_descriptors)
					);
					root_parameter += 2u;
					continue;
				}

				bool sampler_binding = metadata.flags.Test(Bits::SamplerBinding);
				auto descriptors = sampler_binding ? 
					native->sampler_descriptors.Allocate(metadata.count) : 
					native->resource_descriptors.Allocate(metadata.count);
				for (std::uint32_t element = 0u; element < metadata.count; ++element) {
					auto binding = FindBinding(metadata.slot, element);
					if (binding == bindings.end()) {
						throw std::invalid_argument("A D3D12 resource group requires every descriptor array element");
					}
					auto destination = descriptors.CPU(element);
					if (auto view_binding = binding->value.BoundView()) {
						auto const& view = NativeView(view_binding);
						auto type = metadata.flags.Test(Bits::StorageBinding)
							? ViewType::UnorderedAccess
							: ViewType::ShaderResource;
						device->CopyDescriptorsSimple(
							1u,
							destination,
							SourceViewDescriptor(view, type),
							D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
						);
					}
					else if (auto sampler = binding->value.BoundSampler()) {
						auto const& native_sampler = NativeSampler(sampler);
						native_sampler.descriptor.ValidateDevice(device);
						device->CopyDescriptorsSimple(
							1u,
							destination,
							native_sampler.descriptor.CPU(),
							D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
						);
					}
					else {
						auto const& resource = NativeResource(binding->value.Buffer());
						ValidateResourceDevice(resource);
						auto buffer = resource.allocation->GetResource();
						auto resource_size = static_cast<std::size_t>(buffer->GetDesc().Width);
						auto offset = binding->value.Offset();
						if (offset > resource_size) {
							throw std::out_of_range("The D3D12 pipeline buffer offset exceeds the resource");
						}
						auto size = binding->value.Size() == pipeline::PipelineWholeBuffer
							? resource_size - offset
							: binding->value.Size();
						if (size == 0u || size > resource_size - offset) {
							throw std::out_of_range("The D3D12 pipeline buffer range exceeds the resource");
						}
						if (metadata.flags.Test(Bits::UniformBuffer)) {
							constexpr auto alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
							if (offset % alignment != 0u) {
								throw std::invalid_argument("A D3D12 constant-buffer offset must be 256-byte aligned");
							}
							if (size > (std::numeric_limits<std::size_t>::max)() - (alignment - 1u)) {
								throw std::out_of_range("The D3D12 constant-buffer range cannot be represented");
							}
							auto aligned_size = (size + alignment - 1u) & ~(alignment - 1u);
							if (aligned_size > resource_size - offset || aligned_size > (std::numeric_limits<UINT>::max)()) {
								throw std::out_of_range("The D3D12 constant-buffer range cannot be represented");
							}
							D3D12_CONSTANT_BUFFER_VIEW_DESC descriptor{
								.BufferLocation = buffer->GetGPUVirtualAddress() + offset,
								.SizeInBytes = static_cast<UINT>(aligned_size)
							};
							device->CreateConstantBufferView(
								&descriptor,
								destination
							);
						}
						else {
							if (offset % sizeof(std::uint32_t) != 0u || size % sizeof(std::uint32_t) != 0u || size / sizeof(std::uint32_t) >(std::numeric_limits<UINT>::max)()) {
								throw std::invalid_argument("The D3D12 storage-buffer range is invalid");
							}
							D3D12_UNORDERED_ACCESS_VIEW_DESC descriptor;
							descriptor.Format = DXGI_FORMAT_R32_TYPELESS;
							descriptor.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
							descriptor.Buffer = {
								.FirstElement = offset / sizeof(std::uint32_t),
								.NumElements = static_cast<UINT>(size / sizeof(std::uint32_t)),
								.StructureByteStride = 0u,
								.CounterOffsetInBytes = 0u,
								.Flags = D3D12_BUFFER_UAV_FLAG_RAW
							};
							device->CreateUnorderedAccessView(
								buffer,
								nullptr,
								&descriptor,
								destination
							);
						}
					}
				}
				result.tables.emplace_back(
					root_parameter,
					std::move(descriptors)
				);
				++root_parameter;
			}
			return MakePipelineResourceGroup(std::move(result));
		}
	};

} // namespace fyuu_rhi
#endif // defined(_WIN32)
