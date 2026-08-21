module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)
#define FYUU_RHI_USE_VULKAN_HEADER
#include <vulkan/vulkan_shared.hpp>
#endif // !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)

module fyuu_rhi:vulkan_pipeline;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
#if !defined(FYUU_RHI_USE_VULKAN_HEADER)
import vulkan;
#endif // !defined(FYUU_RHI_USE_VULKAN_HEADER)
import :pipeline;
import :pipeline_dispatch;
import :pipeline_resource_group_dispatch;
import :pipeline_resource_group_factory;
import :resource_factory;
import :sampler_factory;
import :view_factory;
import :vulkan_data;

namespace {

	vk::DescriptorType DescriptorType(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		if (flags.Test(Bits::UniformBuffer)) {
			return vk::DescriptorType::eUniformBuffer;
		}
		if (flags.Test(Bits::StorageBuffer)) {
			return vk::DescriptorType::eStorageBuffer;
		}
		bool view = flags.Test(Bits::TextureBinding) || flags.Test(Bits::StorageBinding);
		bool sampler = flags.Test(Bits::SamplerBinding);
		if (view && sampler) {
			return vk::DescriptorType::eCombinedImageSampler;
		}
		if (flags.Test(Bits::StorageBinding)) {
			return vk::DescriptorType::eStorageImage;
		}
		if (view) {
			return vk::DescriptorType::eSampledImage;
		}
		if (sampler) {
			return vk::DescriptorType::eSampler;
		}
		throw std::invalid_argument("Unsupported Vulkan pipeline binding type");
	}

} // namespace

namespace fyuu_rhi {

	template <>
	struct CreatePipelineResourceGroup<vulkan::Pipeline> {
		vulkan::Pipeline* native;

		static vulkan::Resource const& NativeResource(Resource const* resource) {
			if (!resource || !resource->m_impl) {
				throw std::invalid_argument("A Vulkan buffer binding is empty");
			}
			auto result = std::get_if<vulkan::Resource>(&resource->m_impl->native);
			if (!result) {
				throw std::invalid_argument("A Vulkan pipeline cannot bind a foreign resource");
			}
			return *result;
		}

		static vulkan::View const& NativeView(View const* view) {
			if (!view || !view->m_impl) {
				throw std::invalid_argument("A Vulkan view binding is empty");
			}
			auto result = std::get_if<vulkan::View>(&view->m_impl->native);
			if (!result) {
				throw std::invalid_argument("A Vulkan pipeline cannot bind a foreign view");
			}
			return *result;
		}

		static vulkan::Sampler const& NativeSampler(Sampler const* sampler) {
			if (!sampler || !sampler->m_impl) {
				throw std::invalid_argument("A Vulkan sampler binding is empty");
			}
			auto result = std::get_if<vulkan::Sampler>(&sampler->m_impl->native);
			if (!result) {
				throw std::invalid_argument("A Vulkan pipeline cannot bind a foreign sampler");
			}
			return *result;
		}

		PipelineResourceGroup operator()(std::uint32_t space, std::span<pipeline::ResourceBinding const> bindings) const {
			using Bits = ResourceFlagBits;
			if (space >= native->descriptor_set_layouts.size()) {
				throw std::out_of_range("The Vulkan pipeline resource-group space is out of range");
			}

			auto FindMetadata = [this, space](std::uint32_t slot) {
				return std::ranges::find_if(
					native->bindings,
					[space, slot](pipeline::BindingMetadata const& metadata) {
						return metadata.space == space && metadata.slot == slot;
					}
				);
				};

			for (auto const& binding : bindings) {
				auto metadata = FindMetadata(binding.slot);
				if (metadata == native->bindings.end()) {
					throw std::invalid_argument("The Vulkan pipeline space has no requested slot");
				}
				if (binding.array_element >= metadata->count) {
					throw std::out_of_range(
						"The Vulkan pipeline binding array element exceeds its declared count"
					);
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
					throw std::invalid_argument("A Vulkan pipeline binding is specified more than once");
				}
			}
			for (auto const& metadata : native->bindings) {
				if (metadata.space != space) {
					continue;
				}
				for (std::uint32_t element = 0u; element < metadata.count; ++element) {
					auto found = std::ranges::find_if(
						bindings,
						[&metadata, element](pipeline::ResourceBinding const& binding) {
							return binding.slot == metadata.slot &&	binding.array_element == element;
						}
					);
					if (found == bindings.end()) {
						throw std::invalid_argument("A Vulkan resource group requires every descriptor array element");
					}
				}
			}

			std::vector<vk::DescriptorPoolSize> pool_sizes;
			for (auto const& metadata : native->bindings) {
				if (metadata.space != space) {
					continue;
				}
				auto type = DescriptorType(metadata.flags);
				auto existing = std::ranges::find_if(
					pool_sizes,
					[type](vk::DescriptorPoolSize const& size) {
						return size.type == type;
					}
				);
				if (existing == pool_sizes.end()) {
					pool_sizes.emplace_back(type, metadata.count);
				}
				else {
					existing->descriptorCount += metadata.count;
				}
			}

			auto const& device = native->layout.getDestructorType();
			auto raw_pool = device->createDescriptorPool(
				{
					{},
					1u,
					pool_sizes
				},
				nullptr,
				*native->dispatcher
			);
			vk::SharedDescriptorPool pool(
				raw_pool,
				device,
				{ nullptr, *native->dispatcher }
			);
			auto raw_layout = *native->descriptor_set_layouts[space];
			auto descriptor_sets = device->allocateDescriptorSets(
				vk::DescriptorSetAllocateInfo(
					raw_pool,
					1u,
					&raw_layout
				),
				*native->dispatcher
			);

			std::vector<vk::DescriptorBufferInfo> buffer_infos;
			std::vector<vk::DescriptorImageInfo> image_infos;
			std::vector<vk::WriteDescriptorSet> writes;
			buffer_infos.reserve(bindings.size());
			image_infos.reserve(bindings.size());
			writes.reserve(bindings.size());
			for (auto const& binding : bindings) {
				auto metadata = FindMetadata(binding.slot);
				auto type = DescriptorType(metadata->flags);
				vk::WriteDescriptorSet write;
				write.dstSet = descriptor_sets.front();
				write.dstBinding = binding.slot;
				write.dstArrayElement = binding.array_element;
				write.descriptorCount = 1u;
				write.descriptorType = type;
				if (auto buffer = binding.value.Buffer()) {
					auto const& resource = NativeResource(buffer);
					if (resource.allocation.GetLogicalDevice().get() != device.get()) {
						throw std::invalid_argument("The Vulkan resource belongs to another logical device");
					}
					auto range = binding.value.Size() == pipeline::PipelineWholeBuffer ? vk::WholeSize : binding.value.Size();
					buffer_infos.emplace_back(
						vk::Buffer(resource.allocation.GetBuffer()),
						binding.value.Offset(),
						range
					);
					write.pBufferInfo = &buffer_infos.back();
				}
				else if (auto view = binding.value.BoundView()) {
					auto const& native_view = NativeView(view);
					auto image_view = std::get_if<vk::SharedImageView>(&native_view.impl);
					if (!image_view) {
						throw std::invalid_argument(
							"A Vulkan image binding requires an image view"
						);
					}
					if (image_view->getDestructorType().get() != device.get()) {
						throw std::invalid_argument("The Vulkan view belongs to another logical device");
					}
					auto layout = type == vk::DescriptorType::eStorageImage	? vk::ImageLayout::eGeneral	: vk::ImageLayout::eShaderReadOnlyOptimal;
					vk::Sampler sampler;
					if (auto bound_sampler = binding.value.BoundSampler()) {
						auto const& native_sampler = NativeSampler(bound_sampler);
						if (native_sampler.impl.getDestructorType().get() != device.get()) {
							throw std::invalid_argument("The Vulkan sampler belongs to another logical device");
						}
						sampler = *native_sampler.impl;
					}
					image_infos.emplace_back(
						sampler,
						**image_view,
						layout
					);
					write.pImageInfo = &image_infos.back();
				}
				else if (auto sampler = binding.value.BoundSampler()) {
					auto const& native_sampler = NativeSampler(sampler);
					if (native_sampler.impl.getDestructorType().get() != device.get()) {
						throw std::invalid_argument("The Vulkan sampler belongs to another logical device");
					}
					image_infos.emplace_back(
						*native_sampler.impl,
						vk::ImageView{},
						vk::ImageLayout::eUndefined
					);
					write.pImageInfo = &image_infos.back();
				}
				else {
					throw std::invalid_argument("Unsupported Vulkan pipeline binding value");
				}
				writes.emplace_back(write);
			}
			device->updateDescriptorSets(
				writes,
				std::array<vk::CopyDescriptorSet, 0u>{},
				*native->dispatcher
			);
			return MakePipelineResourceGroup(
				vulkan::PipelineResourceGroup{
					.space = space,
					.pool = std::move(pool),
					.set = descriptor_sets.front(),
					.layout = native->layout
				}
			);
		}
	};

} // namespace fyuu_rhi
#endif // !defined(__APPLE__)
