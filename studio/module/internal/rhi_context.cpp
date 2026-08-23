module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <type_traits>
#include <optional>
#include <variant>
#endif // !defined(__cpp_lib_modules)

module fyuu_studio:rhi_context;

import fyuu_engine;
import fyuu_desktop;
import fyuu_rhi;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace {

	fyuu_rhi::execution::PlatformHandle MakePresentationHandle(fyuu_desktop::PresentationTarget const& target) {
		return std::visit(
			[](auto const& native_target) -> fyuu_rhi::execution::PlatformHandle {
				using Target = std::decay_t<decltype(native_target)>;
#if defined(_WIN32)
				if constexpr (std::same_as<Target, fyuu_desktop::Win32PresentationTarget>) {
					return static_cast<fyuu_rhi::execution::PlatformHandle>(native_target.window);
				}
#elif defined(__linux__)
				if constexpr (std::same_as<Target, fyuu_desktop::X11PresentationTarget>) {
					using Display = decltype(fyuu_rhi::execution::X11PlatformHandle::display);
					using Window = decltype(fyuu_rhi::execution::X11PlatformHandle::window);
					return fyuu_rhi::execution::X11PlatformHandle{
						static_cast<Display>(native_target.display),
						static_cast<Window>(native_target.window)
					};
				}
				else if constexpr (std::same_as<Target, fyuu_desktop::WaylandPresentationTarget>) {
					using Display = decltype(fyuu_rhi::execution::WaylandPlatformHandle::display);
					using Surface = decltype(fyuu_rhi::execution::WaylandPlatformHandle::surface);
					return fyuu_rhi::execution::WaylandPlatformHandle{
						static_cast<Display>(native_target.display),
						static_cast<Surface>(native_target.surface)
					};
				}
#elif defined(__APPLE__)
				if constexpr (std::same_as<Target, fyuu_desktop::CocoaPresentationTarget>) {
					return native_target.window;
				}
#endif
				else {
					throw fyuu_engine::Error{
						fyuu_engine::Result::PlatformError,
						"Desktop presentation target does not match the current RHI platform"
					};
				}
			},
			target
		);
	}

}

namespace fyuu_studio {

	/// Owns the backend-neutral device and scheduler selected at runtime.
	/// Call chain: RunBackend -> InitializeRHIContext -> RequestInstance
	/// -> EnumeratePhysicalDevices -> LogicalDevice -> CommandScheduler.
	class RHIContext {
	private:
		fyuu_rhi::execution::PlatformHandle m_presentation_handle;
		fyuu_rhi::LogicalDevice m_logical_device;
		fyuu_rhi::execution::CommandScheduler m_scheduler;

	public:
		RHIContext(fyuu_rhi::Backend backend, fyuu_desktop::PresentationTarget const& presentation_target) 
			: m_presentation_handle(MakePresentationHandle(presentation_target)),
			m_logical_device(
				[backend](){
					fyuu_rhi::InitializeRHIContext(
						"Fyuu Studio",
						fyuu_rhi::Version{ 0u, 1u, 0u, 0u },
						"FyuuEngine",
						fyuu_rhi::Version{ 0u, 1u, 0u, 0u },
						nullptr
					);
					fyuu_rhi::LogicalDevice logical_device;
					fyuu_rhi::RequestInstance(
						backend,
						[&logical_device](fyuu_rhi::Instance instance) {
							auto physical_devices = instance.EnumeratePhysicalDevices();
							if (physical_devices.empty()) {
								throw fyuu_engine::Error{
									fyuu_engine::Result::ApplicationError,
									"FyuuRHI did not find a physical device"
								};
							}
							logical_device = fyuu_rhi::BestPerformance(physical_devices).CreateLogicalDevice();
						}
					);
					return logical_device;
				}()),
			m_scheduler(m_logical_device.CreateScheduler()) {

		}

		fyuu_rhi::execution::PlatformHandle const& GetPresentationHandle() const noexcept {
			return m_presentation_handle;
		}

		fyuu_rhi::LogicalDevice& GetLogicalDevice() noexcept {
			return m_logical_device;
		}

		fyuu_rhi::execution::CommandScheduler& GetScheduler() noexcept {
			return m_scheduler;
		}
	};

}
