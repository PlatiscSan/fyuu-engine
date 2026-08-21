module;
#include <version>
#if !defined(__cpp_lib_modules)

#include <stdexcept>

#include <string_view>

#include <format>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#include <glad/glad.h>
#endif // !defined(__APPLE__)

module fyuu_rhi:opengl_instance_common;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :log;

namespace {

	void GLAPIENTRY DebugMessenger(
		GLenum source,
		GLenum type,
		GLuint id,
		GLenum severity,
		GLsizei length,
		GLchar const* text,
		void const*
	) noexcept {
		try {
			auto message = std::format(
				"OpenGL [source=0x{:X}, type=0x{:X}, id={}, severity=0x{:X}]: {}",
				source,
				type,
				id,
				severity,
				text ? std::string_view(text, static_cast<std::size_t>(length)) :
					std::string_view{}
			);
			if (severity == GL_DEBUG_SEVERITY_HIGH) {
				fyuu_rhi::log::Error(message);
			}
			else if (severity == GL_DEBUG_SEVERITY_MEDIUM) {
				fyuu_rhi::log::Warning(message);
			}
			else if (severity == GL_DEBUG_SEVERITY_LOW) {
				fyuu_rhi::log::Info(message);
			}
			else {
				fyuu_rhi::log::Debug(message);
			}
		}
		catch (...) {
			fyuu_rhi::log::Error("Failed to format an OpenGL debug message");
		}
	}

} // namespace

namespace fyuu_rhi::opengl {

	void InitializeOpenGL() {
		if (!gladLoadGL()) {
			throw std::runtime_error("Failed to load OpenGL functions");
		}
#if !defined(NDEBUG)
		if (GLAD_GL_KHR_debug || GLAD_GL_VERSION_4_3) {
			glEnable(GL_DEBUG_OUTPUT);
			glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
			glDebugMessageCallback(DebugMessenger, nullptr);
		}
#endif // !defined(NDEBUG)
	}

} // namespace fyuu_rhi::opengl
#endif // !defined(__APPLE__)
