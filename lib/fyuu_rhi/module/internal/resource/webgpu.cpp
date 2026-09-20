module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <chrono>
#include <cstddef>
#include <format>
#include <memory>
#include <stdexcept>

#include <string>

#include <cstdint>

#include <condition_variable>
#include <mutex>

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

		/// How long a mapping may wait for the device. A mapping completes when the GPU work
		/// that touches the buffer has finished, which is bounded for any frame that runs at
		/// all; a device that rejected the work that would have freed this resource never
		/// completes it, and the latch above is what reports that case.
		static constexpr auto MappingTimeout = std::chrono::seconds(30);

		ResourceMapScope operator()(ResourceDataRange range, bool writable) const {
			auto const& buffer = std::get<wgpu::Buffer>(resource->impl);
			// Host-visible buffers are created unmapped, so this maps one on demand; a buffer
			// the caller already mapped is left alone. Either way the returned scope unmaps on
			// release, which is what lets the next Map() map it again.
			if (buffer.GetMapState() != wgpu::BufferMapState::Mapped) {
				if (resource->errors && resource->errors->Failed()) {
					throw std::runtime_error(std::format(
						"WebGPU buffer mapping failed: {}",
						resource->errors->Message()
					));
				}
				// The wait state lives on the heap: a mapping that is given up on below can
				// still have its callback delivered afterwards, and a callback writing into a
				// dead stack frame is worse than the hang it would replace.
				struct MapState {
					std::mutex mutex;
					std::condition_variable condition;
					bool completed = false;
					wgpu::MapAsyncStatus status = wgpu::MapAsyncStatus::Error;
					std::string error;
				};
				auto state = std::make_shared<MapState>();
				buffer.MapAsync(
					writable ? wgpu::MapMode::Write : wgpu::MapMode::Read,
					range.offset,
					range.size,
					wgpu::CallbackMode::AllowSpontaneous,
					[state](wgpu::MapAsyncStatus result, wgpu::StringView message) {
						{
							std::unique_lock lock(state->mutex);
							state->status = result;
							state->error.assign(message.data, message.length);
							state->completed = true;
						}
						state->condition.notify_one();
					}
				);
				{
					std::unique_lock lock(state->mutex);
					// Bounded: the device's own latch is checked again on the way out, because a
					// rejected submit leaves the mapping pending forever rather than failing it.
					if (!state->condition.wait_for(
						lock,
						MappingTimeout,
						[&state]() {
							return state->completed;
						}
					)) {
						throw std::runtime_error(
							resource->errors && resource->errors->Failed()
								? std::format(
									"WebGPU buffer mapping failed: {}",
									resource->errors->Message()
								)
								: std::string("WebGPU buffer mapping did not complete")
						);
					}
					if (state->status != wgpu::MapAsyncStatus::Success) {
						throw std::runtime_error(
							state->error.empty()
								? "Failed to map a WebGPU readback buffer"
								: state->error
						);
					}
				}
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
