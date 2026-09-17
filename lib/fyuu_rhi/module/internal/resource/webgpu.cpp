module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>

#include <string>

#include <cstdint>

#include <mutex>
#include <condition_variable>

#include <variant>
#endif // !defined(__cpp_lib_modules)
#include <dawn/webgpu_cpp.h>

module fyuu_rhi:webgpu_resource;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource_dispatch;
import :view_factory;
import :webgpu_data;
import :webgpu_utility;

namespace fyuu_rhi {
	template <>
	struct MapResource<webgpu::Resource> {
		webgpu::Resource* resource;

		static void Unmap(void* context, ResourceDataRange, bool) noexcept {
			auto native = static_cast<webgpu::Resource*>(context);
			std::get<wgpu::Buffer>(native->impl).Unmap();
		}

		ResourceMapScope operator()(ResourceDataRange range, bool writable) const {
			auto const& buffer = std::get<wgpu::Buffer>(resource->impl);
			std::mutex mutex;
			std::condition_variable condition;
			bool completed = false;
			auto status = wgpu::MapAsyncStatus::Error;
			std::string error;
			buffer.MapAsync(
				writable ? wgpu::MapMode::Write : wgpu::MapMode::Read,
				range.offset,
				range.size,
				wgpu::CallbackMode::AllowSpontaneous,
				[&](wgpu::MapAsyncStatus result, wgpu::StringView message) {
					{
						std::unique_lock lock(mutex);
						status = result;
						error.assign(message.data, message.length);
						completed = true;
					}
					condition.notify_one();
				}
			);
			{
				std::unique_lock lock(mutex);
				condition.wait(
					lock,
					[&]() {
						return completed;
					}
				);
			}
			if (status != wgpu::MapAsyncStatus::Success) {
				throw std::runtime_error(
					error.empty()
						? "Failed to map a WebGPU readback buffer"
						: error
				);
			}
			void* data = writable
				? buffer.GetMappedRange(range.offset, range.size)
				: const_cast<void*>(buffer.GetConstMappedRange(range.offset, range.size));
			if (!data) {
				buffer.Unmap();
				throw std::runtime_error("WebGPU returned an empty mapped range");
			}
			return ResourceMapScope(
				resource,
				static_cast<std::byte*>(data),
				range.offset,
				range.size,
				writable,
				Unmap
			);
		}
	};

	template <>
	struct CreateBufferView<webgpu::Resource> {
		webgpu::Resource* resource;

		View operator()(std::size_t offset, std::size_t range, ResourceFlags const& flags) const {
			(void)flags;
			auto const& buffer = std::get<wgpu::Buffer>(resource->impl);
			auto size = buffer.GetSize();
			if (offset > size || range > size - offset) {
				throw std::out_of_range(
					"A WebGPU buffer view exceeds the source buffer"
				);
			}
			// WebGPU has no buffer-view object; a view is just a (buffer, offset,
			// size) window applied at bind time.
			return MakeView(webgpu::View{ webgpu::View::Buffer{ buffer, offset, range } });
		}
	};

	template <>
	struct CreateTextureView<webgpu::Resource> {
		webgpu::Resource* resource;

		View operator()(
			std::size_t base_mip_lvl,
			std::size_t mip_lvl_cnt,
			std::size_t base_arr_layer,
			std::size_t arr_layer_cnt,
			ResourceFlags const& flags
		) const {
			auto const& tex = std::get<wgpu::Texture>(resource->impl);
			wgpu::TextureViewDescriptor view_desc = {
				.format = webgpu::ResourceFormat(flags),
				.dimension = webgpu::TextureViewDimension(flags),
				.baseMipLevel = static_cast<std::uint32_t>(base_mip_lvl),
				.mipLevelCount = static_cast<std::uint32_t>(mip_lvl_cnt),
				.baseArrayLayer = static_cast<std::uint32_t>(base_arr_layer),
				.arrayLayerCount = static_cast<std::uint32_t>(arr_layer_cnt),
				.aspect = webgpu::TextureViewAspect(flags)
			};
			return MakeView(webgpu::View{ tex.CreateView(&view_desc) });
		}
	};

} // namespace fyuu_rhi
