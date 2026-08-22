module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <utility>
#include <variant>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:completion_token_factory;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :execution;
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

namespace fyuu_rhi::execution {

	struct CompletionTokenImplementation {
		std::variant<
			std::monostate,
#if defined(__APPLE__)
			metal::CompletionToken,
#endif // defined(__APPLE__)
#if defined(_WIN32)
			d3d12::CompletionToken,
#endif // defined(_WIN32)
#if !defined(__APPLE__)
			vulkan::CompletionToken,
			opengl::CompletionToken,
#endif // !defined(__APPLE__)
			webgpu::CompletionToken
		> native;
	};

	template <class NativeCompletionToken>
	CompletionToken MakeCompletionToken(NativeCompletionToken&& native) {
		return CompletionToken(
			CompletionToken::UniqueHandle(
				new CompletionTokenImplementation{ std::forward<NativeCompletionToken>(native) },
				[](CompletionTokenImplementation* implementation) noexcept {
					delete implementation;
				}
			)
		);
	}

} // namespace fyuu_rhi::execution
