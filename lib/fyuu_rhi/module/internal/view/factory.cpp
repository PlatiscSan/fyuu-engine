module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <utility>
#include <variant>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:view_factory;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :view;
#if defined(__APPLE__)
import :metal_data;
#endif // defined(__APPLE__)
#if defined(_WIN32)
import :d3d12_data;
#endif // defined(_WIN32)
#if !defined(__APPLE__)
import :opengl_data;
import :vulkan_data;
#endif // !defined(__APPLE__)
import :webgpu_data;

namespace fyuu_rhi {

	struct ViewImplementation {
		std::variant<
			std::monostate,
#if defined(_WIN32)
			d3d12::View,
#endif // defined(_WIN32)
#if defined(__APPLE__)
			metal::View,
#else
			vulkan::View,
			opengl::View,
#endif // !defined(__APPLE__)
			webgpu::View
		> native;
	};

	template <class NativeView>
	View MakeView(NativeView&& native) {
		return View(
			View::UniqueHandle(
				new ViewImplementation{ std::forward<NativeView>(native) },
				[](ViewImplementation* implementation) noexcept {
					delete implementation;
				}
			)
		);
	}

} // namespace fyuu_rhi
