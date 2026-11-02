module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <utility>
#include <variant>
#endif // !defined(__cpp_lib_modules)

#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#include <boost/scope/unique_resource.hpp>
#endif // !defined(__APPLE__) && !defined(__EMSCRIPTEN__)

module fyuu_rhi:view_factory;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :view;
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
import :metal_data;
#endif // defined(__APPLE__)
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
import :d3d12_data;
#endif // defined(_WIN32)
#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
import :opengl_data;
import :vulkan_data;
#endif // !defined(__APPLE__)
import :webgpu_data;

namespace fyuu_rhi {

	struct ViewImplementation {
		std::variant<
			std::monostate,
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
			d3d12::View,
#endif // defined(_WIN32)
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
			metal::View,
#elif !defined(__EMSCRIPTEN__)
			vulkan::View,
			opengl::View,
#endif // !defined(__APPLE__)
			webgpu::View
		> native;
	};

	/** @brief Emits inline-storage lifecycle operations where native template definitions are available. */
	template class details::FastPimpl<ViewImplementation, 224u>;

	template <class NativeView>
	View MakeView(NativeView&& native) {
		return View(
			View::ImplementationStorage(
				std::in_place, std::forward<NativeView>(native)
			)
		);
	}

} // namespace fyuu_rhi
