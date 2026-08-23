module;
#include <version>
#if !defined(__cpp_lib_modules)
// C++17: borrowed backend names.
#include <string_view>
#endif // !defined(__cpp_lib_modules)

export module fyuu_studio;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_studio {

	enum class RenderBackend {
		D3D12,
		Vulkan,
		OpenGL,
		WebGPU,
		Metal
	};

	RenderBackend DefaultRenderBackend() noexcept;
	RenderBackend ParseRenderBackend(std::string_view name);

	/// Constructs the Studio service graph and runs it until Runtime stops.
	/// Call chain: main -> Run -> Desktop::Run -> Runtime::Run -> StudioApplication callbacks.
	void Run(RenderBackend backend);

}
