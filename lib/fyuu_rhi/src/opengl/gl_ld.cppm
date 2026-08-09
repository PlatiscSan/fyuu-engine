module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <type_traits>
#include <array>
#include <vector>
#include <memory>
#include <memory_resource>
#include <optional>
#include <format>
#include <ranges>
#include <fstream>
#include <filesystem>
#include <string>
#include <string_view>
#include <mutex>
#include <system_error>
#include <functional>
#include <thread>
#include <cstring>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#include "glad/glad.h"

#if defined(_WIN32)
#include "glad/glad_wgl.h"

#elif defined(__linux__)
#include "glad/glad_glx.h"
#include "glad/glad_egl.h"

#elif defined(__ANDROID__)
#include "glad/glad_egl.h"
#include <android/native_window.h>
#include <android/android_native_app_glue.h>

#endif // defined(_WIN32)

#include <boost/scope/defer.hpp>
#include <boost/hash2/xxhash.hpp>

#include <slang.h>

#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <glslang/Include/ResourceLimits.h>
#include <spirv_cross.hpp>
#include <spirv_glsl.hpp>

#include "log.hpp"

#endif // !defined(__APPLE__)
module fyuu_rhi:opengl_logical_device;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :opengl_traits;
import :resource_types;
import :sampler_types;
import :pipeline_types;
import :slang_pipeline_interface;
import :slang;
import :cache_system;
import :native_pipeline_binding;

import plastic.static_hash_table;

namespace fs = std::filesystem;

namespace {

	using namespace fyuu_rhi;
	using namespace fyuu_rhi::pipeline;
	using namespace fyuu_rhi::opengl;

	GLbitfield ExtractBufferStorageFlags(ResourceFlags const& flags) noexcept {
		GLbitfield gl_flags = 0;
		if (flags.Test(ResourceFlagBits::HostVisible)) {
			gl_flags |= GL_MAP_WRITE_BIT | GL_DYNAMIC_STORAGE_BIT;
		}
		if (flags.Test(ResourceFlagBits::DeviceReadback)) {
			gl_flags |= GL_MAP_READ_BIT;
		}
		return gl_flags;
	}

	GLenum ExtractTextureTarget(ResourceFlags const& flags, std::size_t depth_arr_layers, GLsizei sample_cnt) {
		
		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::Texture1D, ResourceFlagBits::Texture3D);
		if (is_conflicting) {
			throw std::invalid_argument("Texture1D Texture2D or Texture3D are set simultaneously");
		}

		bool is_1d = false;
		bool is_2d = false;
		bool is_3d = false;
		if (flags.Test(ResourceFlagBits::Texture1D)) {
			is_1d = true;
		}
		else if (flags.Test(ResourceFlagBits::Texture2D)) {
			is_2d = true;
		}
		else if (flags.Test(ResourceFlagBits::Texture3D)) {
			is_3d = true;
		}
		else {
			is_2d = true;
		}

		if (is_1d) {
			if (sample_cnt > 1) {
				throw std::invalid_argument("Multisample not supported for 1D textures");
			}
			return (depth_arr_layers > 1) ? GL_TEXTURE_1D_ARRAY : GL_TEXTURE_1D;
		}
		else if (is_2d) {
			if (sample_cnt > 1) {
				return (depth_arr_layers > 1) ? GL_TEXTURE_2D_MULTISAMPLE_ARRAY : GL_TEXTURE_2D_MULTISAMPLE;
			} else {
				return (depth_arr_layers > 1) ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
			}
		}
		else if (is_3d) {
			if (sample_cnt > 1) {
				throw std::invalid_argument("Multisample not supported for 3D textures");
			}
			if (depth_arr_layers <= 1) {
				throw std::invalid_argument("3D texture must have depth > 1");
			}
			return GL_TEXTURE_3D;
		}
		else {
			return GL_TEXTURE_2D;
		}

	}

	GLsizei ExtractSampleCount(ResourceFlags const& flags) {
		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::Sample1, ResourceFlagBits::Sample64);
		if (is_conflicting) {
			throw std::invalid_argument("Only one sample count can be set");
		}
		if (flags.Test(ResourceFlagBits::Sample1)) return 1;
		else if (flags.Test(ResourceFlagBits::Sample2)) return 2;
		else if (flags.Test(ResourceFlagBits::Sample4)) return 4;
		else if (flags.Test(ResourceFlagBits::Sample8)) return 8;
		else if (flags.Test(ResourceFlagBits::Sample16)) return 16;
		else if (flags.Test(ResourceFlagBits::Sample32)) return 32;
		else if (flags.Test(ResourceFlagBits::Sample64)) return 64;
		else return 1;
	}

	GLenum ExtractInternalFormat(ResourceFlags const& flags) {

		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::R8Unorm, ResourceFlagBits::Bc7UnormSrgb);
		if (is_conflicting) {
			throw std::invalid_argument("Only one format can be set");
		}

		if (flags.Test(ResourceFlagBits::R8Unorm)) return GL_R8;
		else if (flags.Test(ResourceFlagBits::R8Snorm)) return GL_R8_SNORM;
		else if (flags.Test(ResourceFlagBits::R8Uint)) return GL_R8UI;
		else if (flags.Test(ResourceFlagBits::R8Sint)) return GL_R8I;
	
		// 8‑bit per component (2‑channel)
		else if (flags.Test(ResourceFlagBits::R8G8Unorm)) return GL_RG8;
		else if (flags.Test(ResourceFlagBits::R8G8Snorm)) return GL_RG8_SNORM;
		else if (flags.Test(ResourceFlagBits::R8G8Uint)) return GL_RG8UI;
		else if (flags.Test(ResourceFlagBits::R8G8Sint)) return GL_RG8I;
	
		// 8‑bit per component (4‑channel)
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Unorm)) return GL_RGBA8;
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Snorm)) return GL_RGBA8_SNORM;
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Uint)) return GL_RGBA8UI;
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Sint)) return GL_RGBA8I;
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Srgb)) return GL_SRGB8_ALPHA8;
		else if (flags.Test(ResourceFlagBits::B8G8R8A8Srgb)) return GL_SRGB8_ALPHA8;
	
		// 16‑bit per component (1‑channel)
		else if (flags.Test(ResourceFlagBits::R16Unorm)) return GL_R16;
		else if (flags.Test(ResourceFlagBits::R16Snorm)) return GL_R16_SNORM;
		else if (flags.Test(ResourceFlagBits::R16Uint)) return GL_R16UI;
		else if (flags.Test(ResourceFlagBits::R16Sint)) return GL_R16I;
		else if (flags.Test(ResourceFlagBits::R16Float)) return GL_R16F;
	
		// 16‑bit per component (2‑channel)
		else if (flags.Test(ResourceFlagBits::R16G16Unorm)) return GL_RG16;
		else if (flags.Test(ResourceFlagBits::R16G16Snorm)) return GL_RG16_SNORM;
		else if (flags.Test(ResourceFlagBits::R16G16Uint)) return GL_RG16UI;
		else if (flags.Test(ResourceFlagBits::R16G16Sint)) return GL_RG16I;
		else if (flags.Test(ResourceFlagBits::R16G16Float)) return GL_RG16F;
	
		// 16‑bit per component (4‑channel)
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Unorm)) return GL_RGBA16;
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Snorm)) return GL_RGBA16_SNORM;
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Uint)) return GL_RGBA16UI;
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Sint)) return GL_RGBA16I;
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Float)) return GL_RGBA16F;
	
		// 32‑bit per component (1‑channel)
		else if (flags.Test(ResourceFlagBits::R32Uint)) return GL_R32UI;
		else if (flags.Test(ResourceFlagBits::R32Sint)) return GL_R32I;
		else if (flags.Test(ResourceFlagBits::R32Float)) return GL_R32F;
	
		// 32‑bit per component (2‑channel)
		else if (flags.Test(ResourceFlagBits::R32G32Uint)) return GL_RG32UI;
		else if (flags.Test(ResourceFlagBits::R32G32Sint)) return GL_RG32I;
		else if (flags.Test(ResourceFlagBits::R32G32Float)) return GL_RG32F;
	
		// 32‑bit per component (4‑channel)
		else if (flags.Test(ResourceFlagBits::R32G32B32A32Uint)) return GL_RGBA32UI;
		else if (flags.Test(ResourceFlagBits::R32G32B32A32Sint)) return GL_RGBA32I;
		else if (flags.Test(ResourceFlagBits::R32G32B32A32Float)) return GL_RGBA32F;
	
		// Packed formats
		else if (flags.Test(ResourceFlagBits::R10G10B10A2Unorm)) return GL_RGB10_A2;
		else if (flags.Test(ResourceFlagBits::R10G10B10A2Uint)) return GL_RGB10_A2UI;
		else if (flags.Test(ResourceFlagBits::R11G11B10Float)) return GL_R11F_G11F_B10F;
		else if (flags.Test(ResourceFlagBits::R9G9B9E5SharedExp)) return GL_RGB9_E5;
	
		// Depth/stencil
		else if (flags.Test(ResourceFlagBits::D16Unorm)) return GL_DEPTH_COMPONENT16;
		else if (flags.Test(ResourceFlagBits::D24UnormS8Uint)) return GL_DEPTH24_STENCIL8;
		else if (flags.Test(ResourceFlagBits::D32Float)) return GL_DEPTH_COMPONENT32F;
		else if (flags.Test(ResourceFlagBits::D32FloatS8X24Uint)) return GL_DEPTH32F_STENCIL8;
	
		// BC compressed formats (using standard EXT/ARB constants)
		else if (flags.Test(ResourceFlagBits::Bc1Unorm)) return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
		else if (flags.Test(ResourceFlagBits::Bc1UnormSrgb)) return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
		else if (flags.Test(ResourceFlagBits::Bc2Unorm)) return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
		else if (flags.Test(ResourceFlagBits::Bc2UnormSrgb)) return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT;
		else if (flags.Test(ResourceFlagBits::Bc3Unorm)) return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
		else if (flags.Test(ResourceFlagBits::Bc3UnormSrgb)) return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT;
		else if (flags.Test(ResourceFlagBits::Bc4Unorm)) return GL_COMPRESSED_RED_RGTC1;
		else if (flags.Test(ResourceFlagBits::Bc4Snorm)) return GL_COMPRESSED_SIGNED_RED_RGTC1;
		else if (flags.Test(ResourceFlagBits::Bc5Unorm)) return GL_COMPRESSED_RG_RGTC2;
		else if (flags.Test(ResourceFlagBits::Bc5Snorm)) return GL_COMPRESSED_SIGNED_RG_RGTC2;
		else if (flags.Test(ResourceFlagBits::Bc6HUfloat)) return GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT;
		else if (flags.Test(ResourceFlagBits::Bc6HSfloat)) return GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT;
		else if (flags.Test(ResourceFlagBits::Bc7Unorm)) return GL_COMPRESSED_RGBA_BPTC_UNORM;
		else if (flags.Test(ResourceFlagBits::Bc7UnormSrgb)) return GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM;
	
		else return GL_RGBA8UI;

	}

	GLenum ExtractTextureViewTarget(ResourceFlags const& flags) {

		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::TextureView1D, ResourceFlagBits::TextureView3D);
		if (is_conflicting) {
			throw std::invalid_argument("Only one texture view type can be set");
		}

		if (flags.Test(ResourceFlagBits::TextureView1D))
			return GL_TEXTURE_1D;
		else if (flags.Test(ResourceFlagBits::TextureView2D))
			return GL_TEXTURE_2D;
		else if (flags.Test(ResourceFlagBits::TextureView2DArray))
			return GL_TEXTURE_2D_ARRAY;
		else if (flags.Test(ResourceFlagBits::TextureViewCube))
			return GL_TEXTURE_CUBE_MAP;
		else if (flags.Test(ResourceFlagBits::TextureViewCubeArray))
			return GL_TEXTURE_CUBE_MAP_ARRAY;
		else if (flags.Test(ResourceFlagBits::TextureView3D))
			return GL_TEXTURE_3D;
		else
			return GL_TEXTURE_2D;

	}

	GLenum MapAddressMode(AddressMode mode) noexcept {
		switch (mode) {
		case AddressMode::ClampToEdge:		return GL_CLAMP_TO_EDGE;
		case AddressMode::Repeat:			return GL_REPEAT;
		case AddressMode::MirroredRepeat:	return GL_MIRRORED_REPEAT;
		default:							return GL_CLAMP_TO_EDGE;
		}
	}

	GLenum MapFilterMode(FilterMode mode) noexcept {
		switch (mode) {
		case FilterMode::Nearest:	return GL_NEAREST;
		case FilterMode::Linear:	return GL_LINEAR;
		default:					return GL_NEAREST;
		}
	}

	GLenum MapCompare(CompareFunction func) noexcept {
		switch (func) {
		case CompareFunction::Never:		return GL_NEVER;
		case CompareFunction::Less:			return GL_LESS;
		case CompareFunction::Equal:		return GL_EQUAL;
		case CompareFunction::LessEqual:	return GL_LEQUAL;
		case CompareFunction::Greater:		return GL_GREATER;
		case CompareFunction::NotEqual:		return GL_NOTEQUAL;
		case CompareFunction::GreaterEqual: return GL_GEQUAL;
		case CompareFunction::Always:		return GL_ALWAYS;
		default:							return GL_NONE;
		}
	}

	bool SupportsProgramBinary() noexcept {
		return GLAD_GL_VERSION_4_1 || GLAD_GL_ES_VERSION_3_0 || GLAD_GL_ARB_get_program_binary;
	}

	struct OpenGLShaderTarget {
		SlangCompileTarget format;
		std::string_view profile;
		std::string_view cache_name;
		std::uint32_t essl_version = 0;
	};

	OpenGLShaderTarget SelectOpenGLShaderTarget() {
		if (GLAD_GL_ES_VERSION_3_2) return { SLANG_SPIRV, "spirv_1_0", "essl_320", 320 };
		if (GLAD_GL_ES_VERSION_3_1) return { SLANG_SPIRV, "spirv_1_0", "essl_310", 310 };
		if (GLAD_GL_ES_VERSION_3_0) return { SLANG_SPIRV, "spirv_1_0", "essl_300", 300 };
		if (GLAD_GL_ES_VERSION_2_0) {
			throw std::runtime_error("Graphics pipelines require OpenGL ES 3.0 or newer");
		}
		if (GLAD_GL_VERSION_4_6) return { SLANG_GLSL, "glsl_460", "glsl_460" };
		if (GLAD_GL_VERSION_4_5) return { SLANG_GLSL, "glsl_450", "glsl_450" };
		if (GLAD_GL_VERSION_4_4) return { SLANG_GLSL, "glsl_440", "glsl_440" };
		if (GLAD_GL_VERSION_4_3) return { SLANG_GLSL, "glsl_430", "glsl_430" };
		if (GLAD_GL_VERSION_4_2) return { SLANG_GLSL, "glsl_420", "glsl_420" };
		if (GLAD_GL_VERSION_4_1) return { SLANG_GLSL, "glsl_410", "glsl_410" };
		if (GLAD_GL_VERSION_4_0) return { SLANG_GLSL, "glsl_400", "glsl_400" };
		if (GLAD_GL_VERSION_3_3) return { SLANG_GLSL, "glsl_330", "glsl_330" };
		throw std::runtime_error("OpenGL graphics pipelines require OpenGL 3.3 or newer");
	}

	GLenum MapPipelineStage(PipelineStage stage) {
		switch (stage) {
		case PipelineStage::Vertex:					return GL_VERTEX_SHADER;
		case PipelineStage::Fragment:				return GL_FRAGMENT_SHADER;
		case PipelineStage::TessellationControl:	return GL_TESS_CONTROL_SHADER;
		case PipelineStage::TessellationEvaluation:return GL_TESS_EVALUATION_SHADER;
		case PipelineStage::Geometry:				return GL_GEOMETRY_SHADER;
		case PipelineStage::Compute:				return GL_COMPUTE_SHADER;
		case PipelineStage::Task:
		case PipelineStage::Mesh:
			throw std::invalid_argument("OpenGL mesh shading pipelines are not implemented");
		default:
			throw std::invalid_argument("Unsupported OpenGL pipeline stage");
		}
	}

	std::string GetShaderInfoLog(GLuint shader) {
		GLint length = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
		if (length <= 1) {
			return {};
		}
		std::string result(static_cast<std::size_t>(length), '\0');
		glGetShaderInfoLog(shader, length, nullptr, result.data());
		return result;
	}

	std::string GetProgramInfoLog(GLuint program) {
		GLint length = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
		if (length <= 1) {
			return {};
		}
		std::string result(static_cast<std::size_t>(length), '\0');
		glGetProgramInfoLog(program, length, nullptr, result.data());
		return result;
	}

	std::string ConvertSPIRVToESSL(
		shader::SlangCompiledEntryPoint const& entry,
		std::uint32_t version
	) {
		if (entry.code.empty() || entry.code.size() % sizeof(std::uint32_t) != 0) {
			throw std::runtime_error(
				std::format("Slang produced invalid SPIR-V for OpenGL ES entry point '{}'", entry.name)
			);
		}
		std::vector<std::uint32_t> spirv(entry.code.size() / sizeof(std::uint32_t));
		std::memcpy(spirv.data(), entry.code.data(), entry.code.size());
		spirv_cross::CompilerGLSL compiler(std::move(spirv));
		auto options = compiler.get_common_options();
		options.version = version;
		options.es = true;
		options.vertex.fixup_clipspace = true;
		options.vertex.flip_vert_y = false;
		options.fragment.default_float_precision = spirv_cross::CompilerGLSL::Options::Mediump;
		options.fragment.default_int_precision = spirv_cross::CompilerGLSL::Options::Highp;
		compiler.set_common_options(options);
		return compiler.compile();
	}

	GLuint CompilePipelineStage(
		shader::SlangCompiledEntryPoint const& entry,
		std::uint32_t essl_version
	) {
		auto shader_type = MapPipelineStage(entry.stage);
		GLuint shader = glCreateShader(shader_type);
		if (!shader) {
			throw std::runtime_error("Failed to create an OpenGL pipeline stage");
		}

		std::string converted_source;
		GLchar const* source = nullptr;
		GLint length = 0;
		if (essl_version) {
			converted_source = ConvertSPIRVToESSL(entry, essl_version);
			source = converted_source.data();
			length = static_cast<GLint>(converted_source.size());
		}
		else {
			source = reinterpret_cast<GLchar const*>(entry.code.data());
			length = static_cast<GLint>(entry.code.size());
		}
		glShaderSource(shader, 1, &source, &length);
		glCompileShader(shader);

		GLint compiled = GL_FALSE;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if (compiled == GL_FALSE) {
			auto diagnostics = GetShaderInfoLog(shader);
			// The driver compiles exactly this source, so dump it: shader
			// diagnostics are not actionable without the shader that failed.
			std::string source_text(source, static_cast<std::size_t>(length));
			glDeleteShader(shader);
			throw std::runtime_error(
				std::format(
					"Failed to compile OpenGL pipeline entry point '{}': {}\nShader source:\n{}",
					entry.name,
					diagnostics,
					source_text
				)
			);
		}
		return shader;
	}

	template <class Hasher>
	void HashString(Hasher& hash, std::string_view value) {
		auto size = static_cast<std::uint64_t>(value.size());
		hash.update(&size, sizeof(size));
		hash.update(value.data(), value.size());
	}

	fs::path GetPipelineCachePath(
		shader::SlangProgram const& program,
		std::string_view profile
	) {
		boost::hash2::xxhash_64 hash;
		HashString(hash, profile);
		for (auto value : { GL_VENDOR, GL_RENDERER, GL_VERSION, GL_SHADING_LANGUAGE_VERSION }) {
			auto text = reinterpret_cast<char const*>(glGetString(value));
			HashString(hash, text ? std::string_view(text) : std::string_view{});
		}
		for (auto const& entry : program.EntryPoints()) {
			auto stage = static_cast<std::uint8_t>(entry.stage);
			hash.update(&stage, sizeof(stage));
			HashString(hash, entry.name);
			hash.update(entry.code.data(), entry.code.size());
		}
		return cache::GetCacheFilePath(std::format("opengl-pipeline-{:016x}.bin", hash.result()));
	}

	std::string GetOpenGLCacheTag(std::string_view profile) {
		boost::hash2::xxhash_64 hash;
		HashString(hash, profile);
		for (auto value : { GL_VENDOR, GL_RENDERER, GL_VERSION, GL_SHADING_LANGUAGE_VERSION }) {
			auto text = reinterpret_cast<char const*>(glGetString(value));
			HashString(hash, text ? std::string_view(text) : std::string_view{});
		}
		return std::format("opengl-{}-{:016x}", profile, hash.result());
	}

	GLuint LoadProgramBinary(fs::path const& path) {

		if (!SupportsProgramBinary()) {
			return 0u;
		}

		auto cached = cache::ReadFile(path);
		if (cached.size() <= sizeof(GLenum)) {
			return 0u;
		}
		GLenum bin_fmt = 0;
		std::memcpy(&bin_fmt, cached.data(), sizeof(GLenum));
		auto bytes = std::span(cached).subspan(sizeof(GLenum));

		GLuint program = glCreateProgram();
		if (!program) {
#if defined(NDEBUG)
			throw std::runtime_error("LoadCache(): Calling glCreateProgram() but failed");
#else
			throw std::runtime_error(
				std::format("LoadCache(): Calling glCreateProgram() but failed, OpenGL reports {}", glGetError())
			);
#endif // defined(NDEBUG)
		}

		glProgramBinary(program, bin_fmt, bytes.data(), static_cast<GLsizei>(bytes.size()));

		GLint link_status = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &link_status);
		if (link_status == GL_FALSE) {
			glDeleteProgram(program);
			return 0u;
		}

		return program;

	}

	void SaveProgramBinary(GLuint program, fs::path const& path) {

		GLint bin_len = 0;
		glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &bin_len);
		if (bin_len <= 0) {
			return;
		}

		GLenum bin_fmt = 0;
		std::vector<std::byte> bytes(bin_len);
		GLsizei written = 0;
		glGetProgramBinary(program, bin_len, &written, &bin_fmt, bytes.data());
		if (written <= 0) {
			return;
		}

		std::vector<std::byte> serialized(sizeof(GLenum) + static_cast<std::size_t>(written));
		std::memcpy(serialized.data(), &bin_fmt, sizeof(GLenum));
		std::memcpy(serialized.data() + sizeof(GLenum), bytes.data(), written);
		cache::WriteFileAtomically(path, serialized);
	}

}

namespace fyuu_rhi::opengl {

	using namespace fyuu_rhi::pipeline;

	/// Makes the instance's main context (which shares with the scheduler's render
	/// context via shared_rc) current on the calling thread. Host-side buffer and
	/// texture creation must run under this context so the objects land in the
	/// scheduler's share group; the per-thread shared context created by
	/// ShareContextOnThisThread does not reliably share with the scheduler.
	void MakeHostContextCurrent(Backend::Instance const& instance) {
#if defined(_WIN32)
		if (!wglMakeCurrent(instance.dc, instance.rc)) {
			throw std::runtime_error("OpenGL: failed to make host context current");
		}
#else
		std::visit(
			[&](auto&& arg) {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::same_as<T, Backend::Instance::EGL>) {
					if (!eglMakeCurrent(arg.display, arg.draw, arg.read, arg.context)) {
						throw std::runtime_error("OpenGL: failed to make host EGL context current");
					}
				}
#if defined(__linux__)
				else if constexpr (std::same_as<T, Backend::Instance::GLX>) {
					if (!glXMakeCurrent(arg.dpy, arg.drawable, arg.ctx)) {
						throw std::runtime_error("OpenGL: failed to make host GLX context current");
					}
				}
#endif // defined(__linux__)
			},
			instance.gl_handle
		);
#endif
	}

	Backend::Resource Backend::CreateBuffer(Backend::LogicalDevice const& ld, std::size_t size_in_bytes, ResourceFlags const& flags) {

		// Host-side buffer creation may run on any thread (e.g. the engine thread
		// allocating a per-frame staging buffer); bind the instance's main context
		// (shared with the scheduler) so the object lands in the share group.
		MakeHostContextCurrent(*ld.instance);
		GLuint buf = 0u;
		GLbitfield buffer_flags = ExtractBufferStorageFlags(flags);

		if (GLAD_GL_ARB_direct_state_access) {
			// modern OpenGL
			glCreateBuffers(1u, &buf);
			if (!buf) {
#if defined(NDEBUG)
				throw std::runtime_error("CreateBuffer(): Calling glCreateBuffers() but failed");
#else
				throw std::runtime_error(
					std::format("CreateBuffer(): Calling glCreateBuffers() but failed, OpenGL reports {}", glGetError())
				);
#endif // defined(NDEBUG)
			}
			glNamedBufferStorage(buf, size_in_bytes, nullptr, buffer_flags);
			// Diagnostic: the allocation must succeed on this (host) context. If it
			// came back 0 the host context was not actually current/valid here.
			GLint64 created_size = 0;
			glGetNamedBufferParameteri64v(buf, GL_BUFFER_SIZE, &created_size);
			if (created_size != static_cast<GLint64>(size_in_bytes)) {
				throw std::runtime_error(std::format(
					"OpenGL CreateBuffer requested {} bytes but allocated {} (buf {})",
					size_in_bytes, created_size, buf
				));
			}
		}
		else {
			// Fallback for older OpenGL versions - create buffer and bind to set storage

			glGenBuffers(1, &buf);
			if (!buf) {
#if defined(NDEBUG)
				throw std::runtime_error("CreateBuffer(): Calling glGenBuffers() but failed");
#else
				throw std::runtime_error(
					std::format("CreateBuffer(): Calling glGenBuffers() but failed, OpenGL reports {}", glGetError())
				);
#endif // defined(NDEBUG)
			}

			GLenum bind_target = GL_COPY_WRITE_BUFFER;

			glBindBuffer(bind_target, buf);
			if (GLAD_GL_ARB_buffer_storage) {
				glBufferStorage(bind_target, size_in_bytes, nullptr, buffer_flags);
			}
			else {
				// very old OpenGL fallback

				GLenum usage = GL_STATIC_DRAW;
				if (buffer_flags & GL_DYNAMIC_STORAGE_BIT) {
					usage = GL_DYNAMIC_DRAW;
				}
				glBufferData(bind_target, size_in_bytes, nullptr, usage);
			}

			glBindBuffer(bind_target, 0);

		}
		glFlush();

		return Backend::Resource(buf, Backend::Resource::Type::Buffer, size_in_bytes);

	}

	Backend::Resource Backend::CreateTexture(Backend::LogicalDevice const& ld, std::size_t width, std::size_t height, std::size_t depth_arr_layers, std::size_t mip_lvl_cnt, ResourceFlags const& flags) {

		// Same as CreateBuffer: host-side texture creation needs the instance's
		// main (scheduler-shared) context current.
		MakeHostContextCurrent(*ld.instance);
		GLsizei sample_cnt = ExtractSampleCount(flags);
		GLenum target = ExtractTextureTarget(flags, depth_arr_layers, sample_cnt);
		GLenum internal_format = ExtractInternalFormat(flags);

		if (sample_cnt > 1 && mip_lvl_cnt != 1) {
			throw std::invalid_argument("Multisample textures must have mip_lvl_cnt == 1");
		}

		GLuint tex = 0u;

		if (GLAD_GL_ARB_direct_state_access) {
			// modern OpenGL 
			glCreateTextures(target, 1u, &tex);
			if (!tex) {
#if defined(NDEBUG)
				throw std::runtime_error("CreateTexture(): Calling glCreateTextures() but failed");
#else
				throw std::runtime_error(
					std::format("CreateTexture(): Calling glCreateTextures() but failed, OpenGL reports {}", glGetError())
				);
#endif // defined(NDEBUG)
			}

			switch (target) {
			case GL_TEXTURE_1D:
				glTextureStorage1D(tex, static_cast<GLsizei>(mip_lvl_cnt), internal_format, static_cast<GLsizei>(width));
				break;
			case GL_TEXTURE_1D_ARRAY:
				glTextureStorage2D(
					tex, static_cast<GLsizei>(mip_lvl_cnt), internal_format,
					static_cast<GLsizei>(width), static_cast<GLsizei>(depth_arr_layers)
				);
				break;
			case GL_TEXTURE_2D:
				glTextureStorage2D(
					tex, static_cast<GLsizei>(mip_lvl_cnt), internal_format,
					static_cast<GLsizei>(width), static_cast<GLsizei>(height)
				);
				break;
			case GL_TEXTURE_2D_ARRAY:
				glTextureStorage3D(
					tex, static_cast<GLsizei>(mip_lvl_cnt), internal_format,
					static_cast<GLsizei>(width), static_cast<GLsizei>(height), static_cast<GLsizei>(depth_arr_layers)
				);
				break;
			case GL_TEXTURE_2D_MULTISAMPLE:
				glTextureStorage2DMultisample(
					tex, sample_cnt, internal_format,
					static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_TRUE
				);
				break;
			case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
				glTextureStorage3DMultisample(
					tex, sample_cnt, internal_format,
					static_cast<GLsizei>(width), static_cast<GLsizei>(height),
					static_cast<GLsizei>(depth_arr_layers), GL_TRUE
				);
				break;
			case GL_TEXTURE_3D:
				glTextureStorage3D(
					tex, static_cast<GLsizei>(mip_lvl_cnt), internal_format,
					static_cast<GLsizei>(width), static_cast<GLsizei>(height), static_cast<GLsizei>(depth_arr_layers)
				);
				break;
			default:
				glDeleteTextures(1u, &tex);
				throw std::runtime_error("Unsupported texture target");
			}
		}
		else {
			// Fallback for older OpenGL versions - create texture and bind to set storage
			glGenTextures(1, &tex);
			if (!tex) {
#if defined(NDEBUG)
				throw std::runtime_error("CreateTexture(): Calling glGenTextures() but failed");
#else
				throw std::runtime_error(
					std::format("CreateTexture(): Calling glGenTextures() but failed, OpenGL reports {}", glGetError())
				);
#endif // defined(NDEBUG)
			}
			glBindTexture(target, tex);
			switch (target) {
			case GL_TEXTURE_1D:
				glTexStorage1D(target, static_cast<GLsizei>(mip_lvl_cnt), internal_format, static_cast<GLsizei>(width));
				break;
			case GL_TEXTURE_1D_ARRAY:
				glTexStorage2D(
					target, static_cast<GLsizei>(mip_lvl_cnt), internal_format,
					static_cast<GLsizei>(width), static_cast<GLsizei>(depth_arr_layers)
				);
				break;
			case GL_TEXTURE_2D:
				glTexStorage2D(
					target, static_cast<GLsizei>(mip_lvl_cnt), internal_format,
					static_cast<GLsizei>(width), static_cast<GLsizei>(height)
				);
				break;
			case GL_TEXTURE_2D_ARRAY:
				glTexStorage3D(
					target, static_cast<GLsizei>(mip_lvl_cnt), internal_format,
					static_cast<GLsizei>(width), static_cast<GLsizei>(height), static_cast<GLsizei>(depth_arr_layers)
				);
				break;
			case GL_TEXTURE_2D_MULTISAMPLE:
				glTexStorage2DMultisample(
					target, sample_cnt, internal_format,
					static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_TRUE
				);
				break;
			case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
				glTexStorage3DMultisample(
					target, sample_cnt, internal_format,
					static_cast<GLsizei>(width), static_cast<GLsizei>(height),
					static_cast<GLsizei>(depth_arr_layers), GL_TRUE
				);
				break;
			case GL_TEXTURE_3D:
				glTexStorage3D(
					target, static_cast<GLsizei>(mip_lvl_cnt), internal_format,
					static_cast<GLsizei>(width), static_cast<GLsizei>(height), static_cast<GLsizei>(depth_arr_layers)
				);
				break;
			default:
				glDeleteTextures(1, &tex);
				throw std::runtime_error("Unsupported texture target");
			}
			glBindTexture(target, 0);

		}

		if (!GLAD_GL_ARB_direct_state_access) {
			glBindTexture(target, tex);
		}
		auto SetParam = [&](GLenum pname, GLint value) {
			if (GLAD_GL_ARB_direct_state_access)
				glTextureParameteri(tex, pname, value);
			else {
				glTexParameteri(target, pname, value);
			}
			};

		SetParam(GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		SetParam(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		SetParam(GL_TEXTURE_WRAP_S, GL_REPEAT);
		SetParam(GL_TEXTURE_WRAP_T, GL_REPEAT);
		if (target == GL_TEXTURE_3D || target == GL_TEXTURE_2D_ARRAY || target == GL_TEXTURE_1D_ARRAY) {
			SetParam(GL_TEXTURE_WRAP_R, GL_REPEAT);
		}

		glBindTexture(target, 0);
		glFlush();

		return Backend::Resource(
			tex, target, internal_format, static_cast<std::uint32_t>(width),
			static_cast<std::uint32_t>(height)
		);

	}

	Backend::View Backend::CreateTextureView(Backend::LogicalDevice const& ld, Resource const& res, std::size_t base_mip_lvl, std::size_t mip_lvl_cnt, std::size_t base_arr_layer, std::size_t arr_layer_cnt, ResourceFlags const& flags) {
		
		if (res.type != Backend::Resource::Type::Texture) {
			throw std::invalid_argument("CreateTextureView(): source is not a texture");
		}

		GLenum view_target = ExtractTextureViewTarget(flags);
		GLenum internal_format = ExtractInternalFormat(flags);
		if (flags.Test(ResourceFlagBits::RenderAttachment) &&
			base_mip_lvl == 0u && mip_lvl_cnt == 1u &&
			base_arr_layer == 0u && arr_layer_cnt == 1u &&
			view_target == res.target && internal_format == res.format &&
			res.target == GL_TEXTURE_2D) {
			return Backend::View(
				Backend::GLTextureView(res.impl, res.target, res.format, false)
			);
		}
		if (!GLAD_GL_ARB_texture_view) {
			if (base_mip_lvl == 0u && mip_lvl_cnt == 1u &&
				base_arr_layer == 0u && arr_layer_cnt == 1u &&
				view_target == res.target && internal_format == res.format) {
				return Backend::View(
					Backend::GLTextureView(res.impl, res.target, res.format, false)
				);
			}
			throw std::runtime_error(
				"CreateTextureView(): subresource views require GL_ARB_texture_view"
			);
		}

		GLuint view = 0u;
		glGenTextures(1u, &view);

		if (!view) {
#if defined(NDEBUG)
			throw std::runtime_error("CreateTextureView(): Calling glGenTextures() but failed");
#else
			throw std::runtime_error(
				std::format("CreateTextureView(): Calling glGenTextures() but failed, OpenGL reports {}", glGetError())
			);
#endif // defined(NDEBUG)
		}

		glTextureView(
			view, view_target, res.impl, internal_format,
			static_cast<GLuint>(base_mip_lvl), static_cast<GLuint>(mip_lvl_cnt),
			static_cast<GLuint>(base_arr_layer), static_cast<GLuint>(arr_layer_cnt)
		);

		return Backend::View(
			Backend::GLTextureView(view, view_target, internal_format)
		);
			
	}

	Backend::View Backend::CreateBufferView(Backend::LogicalDevice const& ld, Resource const& res, std::size_t offset, std::size_t range, ResourceFlags const& flags) {
		
		if (res.type != Backend::Resource::Type::Buffer) {
			throw std::invalid_argument("CreateBufferView(): source resource is not a buffer");
		}

		static constexpr GLenum target = GL_TEXTURE_BUFFER;
		if (!GLAD_GL_ARB_texture_buffer_object) {
			throw std::runtime_error("CreateBufferView(): GL_ARB_texture_buffer_object is not supported");
		}
		GLuint view = 0;
		if (GLAD_GL_ARB_direct_state_access) glCreateTextures(target, 1u, &view);
		else glGenTextures(1, &view);
		if (!view) {
			throw std::runtime_error("CreateBufferView(): Failed to create texture buffer view");
		}
		GLenum internal_format = ExtractInternalFormat(flags);
		if (GLAD_GL_ARB_direct_state_access) {
			if (GLAD_GL_ARB_texture_buffer_range && (offset != 0u || range != 0u)) {
				glTextureBufferRange(view, internal_format, res.impl, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(range));
			}
			else {
				glTextureBuffer(view, internal_format, res.impl);
			}
		}
		else {
			glBindTexture(target, view);
			if (GLAD_GL_ARB_texture_buffer_range && (offset != 0u || range != 0u)) {
				glTexBufferRange(target, internal_format, res.impl, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(range));
			}
			else {
				glTexBuffer(target, internal_format, res.impl);
			}
			glBindTexture(target, 0u);
		}
		return Backend::View(
			Backend::GLBufferView(
				view,
				GLAD_GL_ARB_texture_buffer_range
					? std::nullopt
					: std::optional(Backend::GLBufferView::Range{ offset, range })
			)
		);

	}

	Backend::Sampler Backend::CreateSampler(Backend::LogicalDevice const& ld, SamplerDescriptor const& descriptor) {

		if (!GLAD_GL_ARB_sampler_objects) {
			throw std::runtime_error("CreateSampler(): Sampler objects are not supported on this version of OpenGL");
		}

		GLuint sampler = 0;

		if (GLAD_GL_ARB_direct_state_access) {
			glCreateSamplers(1, &sampler);
			if (!sampler) {
#if defined(NDEBUG)
				throw std::runtime_error("CreateSampler(): Calling glCreateSamplers() but failed");
#else
				throw std::runtime_error(
					std::format("CreateSampler(): Calling glCreateSamplers() but failed, OpenGL reports {}", glGetError())
				);
#endif // defined(NDEBUG)
			}
		}
		else {
			glGenSamplers(1, &sampler);
			if (!sampler) {
#if defined(NDEBUG)
				throw std::runtime_error("CreateSampler(): Calling glGenSamplers() but failed");
#else
				throw std::runtime_error(
					std::format("CreateSampler(): Calling glGenSamplers() but failed, OpenGL reports {}", glGetError())
				);
#endif // defined(NDEBUG)
			}
		}

		glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, MapAddressMode(descriptor.address_mode_u));
		glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, MapAddressMode(descriptor.address_mode_v));
		glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, MapAddressMode(descriptor.address_mode_w));

		glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, MapFilterMode(descriptor.mag_filter));

		GLenum min_filter = MapFilterMode(descriptor.min_filter);
		if (descriptor.max_lod > descriptor.min_lod) {
			switch (descriptor.min_filter) {
			case FilterMode::Nearest:
				min_filter = (descriptor.mipmap_filter == MipmapFilterMode::Nearest)
					? GL_NEAREST_MIPMAP_NEAREST
					: GL_NEAREST_MIPMAP_LINEAR;
				break;
			case FilterMode::Linear:
				min_filter = (descriptor.mipmap_filter == MipmapFilterMode::Nearest)
					? GL_LINEAR_MIPMAP_NEAREST
					: GL_LINEAR_MIPMAP_LINEAR;
				break;
			default:
				min_filter = GL_LINEAR_MIPMAP_LINEAR;
			}
		}
		glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, min_filter);

		if (descriptor.max_anisotropy > 1u && GLAD_GL_ARB_texture_filter_anisotropic) {
			glSamplerParameterf(sampler, GL_TEXTURE_MAX_ANISOTROPY, static_cast<GLfloat>(descriptor.max_anisotropy));
		}

		GLenum compare_func = MapCompare(descriptor.compare_function);

		if (compare_func != GL_NONE) {
			glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
			glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_FUNC, compare_func);
		}
		else {
			glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE, GL_NONE);
		}

		glSamplerParameterf(sampler, GL_TEXTURE_MIN_LOD, descriptor.min_lod);
		glSamplerParameterf(sampler, GL_TEXTURE_MAX_LOD, descriptor.max_lod);

		std::array border_color = { 0.0f, 0.0f, 0.0f, 0.0f };
		glSamplerParameterfv(sampler, GL_TEXTURE_BORDER_COLOR, border_color.data());

		return Backend::Sampler(sampler);

	}

	Backend::Pipeline Backend::CreateGraphicsPipeline(
		Backend::LogicalDevice const& ld,
		GraphicsPipelineDescriptor const& descriptor
	) {
		Backend::ShareContextOnThisThread(*ld.instance);
		auto shader_target = SelectOpenGLShaderTarget();
		slang::TargetDesc target{ .format = shader_target.format };
		target.profile = shader::SlangGlobalSession()->findProfile(shader_target.profile.data());
		shader::SlangProgram slang_program(
			target,
			descriptor.program,
			GetOpenGLCacheTag(shader_target.cache_name)
		);

		for (auto const& binding : slang_program.Interface().bindings) {
			if (binding.space != 0) {
				throw std::invalid_argument("OpenGL pipeline resource bindings must use space 0");
			}
		}
		if (!slang_program.Interface().push_constants.empty()) {
			throw std::invalid_argument("OpenGL pipelines do not support push constants");
		}

		bool has_vertex_stage = false;
		std::uint32_t stage_mask = 0;
		for (auto const& entry : slang_program.EntryPoints()) {
			if (entry.stage == PipelineStage::Compute) {
				throw std::invalid_argument("A compute entry point cannot be used by an OpenGL graphics pipeline");
			}
			MapPipelineStage(entry.stage);
			if (
				shader_target.essl_version &&
				shader_target.essl_version < 320 &&
				entry.stage != PipelineStage::Vertex &&
				entry.stage != PipelineStage::Fragment
			) {
				throw std::invalid_argument(
					"OpenGL ES 3.0 and 3.1 graphics pipelines support only vertex and fragment stages"
				);
			}
			auto stage_bit = 1u << static_cast<std::uint32_t>(entry.stage);
			if (stage_mask & stage_bit) {
				throw std::invalid_argument("An OpenGL graphics pipeline cannot contain duplicate stages");
			}
			stage_mask |= stage_bit;
			has_vertex_stage |= entry.stage == PipelineStage::Vertex;
		}
		if (!has_vertex_stage) {
			throw std::invalid_argument("OpenGL graphics pipeline has no vertex entry point");
		}

		auto cache_path = GetPipelineCachePath(slang_program, shader_target.cache_name);
		GLuint program = LoadProgramBinary(cache_path);
		if (!program) {
			program = glCreateProgram();
			if (!program) {
				throw std::runtime_error("Failed to create an OpenGL pipeline program");
			}
			if (SupportsProgramBinary()) {
				glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
			}

			std::vector<GLuint> stages;
			stages.reserve(slang_program.EntryPoints().size());
			try {
				for (auto const& entry : slang_program.EntryPoints()) {
					auto stage = CompilePipelineStage(entry, shader_target.essl_version);
					stages.push_back(stage);
					glAttachShader(program, stage);
				}
				glLinkProgram(program);
				GLint linked = GL_FALSE;
				glGetProgramiv(program, GL_LINK_STATUS, &linked);
				if (linked == GL_FALSE) {
					throw std::runtime_error(
						std::format("Failed to link OpenGL graphics pipeline: {}", GetProgramInfoLog(program))
					);
				}
			}
			catch (...) {
				for (auto stage : stages) {
					glDetachShader(program, stage);
					glDeleteShader(stage);
				}
				glDeleteProgram(program);
				throw;
			}
			for (auto stage : stages) {
				glDetachShader(program, stage);
				glDeleteShader(stage);
			}
			if (SupportsProgramBinary()) {
				SaveProgramBinary(program, cache_path);
			}
		}

		return Backend::Pipeline(
			program,
			std::vector<VertexBufferLayout>(descriptor.vertex.buffers.begin(), descriptor.vertex.buffers.end()),
			std::vector<VertexAttribute>(descriptor.vertex.attributes.begin(), descriptor.vertex.attributes.end()),
			descriptor.primitive,
			descriptor.rasterization,
			descriptor.multisample,
			descriptor.depth_stencil,
			std::vector<ColorTargetState>(descriptor.color_targets.begin(), descriptor.color_targets.end()),
			MakePipelineBindingMetadata(slang_program.Interface())
		);
	}

	Backend::Pipeline Backend::CreateComputePipeline(
		Backend::LogicalDevice const& ld,
		ComputePipelineDescriptor const& descriptor
	) {
		Backend::ShareContextOnThisThread(*ld.instance);
		if (!GLAD_GL_VERSION_4_3 && !GLAD_GL_ES_VERSION_3_1 && !GLAD_GL_ARB_compute_shader) {
			throw std::runtime_error("Compute pipelines require OpenGL 4.3 or OpenGL ES 3.1");
		}
		auto shader_target = SelectOpenGLShaderTarget();
		slang::TargetDesc target{ .format = shader_target.format };
		target.profile = shader::SlangGlobalSession()->findProfile(shader_target.profile.data());
		shader::SlangProgram slang_program(
			target,
			descriptor.program,
			GetOpenGLCacheTag(shader_target.cache_name)
		);
		for (auto const& binding : slang_program.Interface().bindings) {
			if (binding.space != 0u) {
				throw std::invalid_argument("OpenGL compute resource bindings must use space 0");
			}
		}
		if (!slang_program.Interface().push_constants.empty()) {
			throw std::invalid_argument("OpenGL compute pipelines do not support push constants");
		}
		if (slang_program.EntryPoints().size() != 1u ||
			slang_program.EntryPoints().front().stage != PipelineStage::Compute) {
			throw std::invalid_argument("OpenGL compute pipelines require exactly one compute entry point");
		}

		auto cache_path = GetPipelineCachePath(slang_program, shader_target.cache_name);
		GLuint program = LoadProgramBinary(cache_path);
		if (!program) {
			program = glCreateProgram();
			if (!program) {
				throw std::runtime_error("Failed to create an OpenGL compute pipeline program");
			}
			if (SupportsProgramBinary()) {
				glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
			}
			GLuint stage = 0u;
			try {
				stage = CompilePipelineStage(slang_program.EntryPoints().front(), shader_target.essl_version);
				glAttachShader(program, stage);
				glLinkProgram(program);
				GLint linked = GL_FALSE;
				glGetProgramiv(program, GL_LINK_STATUS, &linked);
				if (linked == GL_FALSE) {
					throw std::runtime_error(
						std::format("Failed to link OpenGL compute pipeline: {}", GetProgramInfoLog(program))
					);
				}
			}
			catch (...) {
				if (stage) {
					glDetachShader(program, stage);
					glDeleteShader(stage);
				}
				glDeleteProgram(program);
				throw;
			}
			glDetachShader(program, stage);
			glDeleteShader(stage);
			if (SupportsProgramBinary()) {
				SaveProgramBinary(program, cache_path);
			}
		}
		return Backend::Pipeline(
			program,
			MakePipelineBindingMetadata(slang_program.Interface())
		);
	}

	Backend::PipelineResourceGroup Backend::CreatePipelineResourceGroup(
		Backend::LogicalDevice const& ld,
		Backend::Pipeline const& pipeline,
		std::uint32_t space,
		std::span<NativePipelineResourceBinding<Backend> const> bindings
	) {
		if (!ld) {
			throw std::invalid_argument("OpenGL logical device must not be empty");
		}
		if (!pipeline.impl) {
			throw std::invalid_argument("Cannot create an OpenGL resource group for an empty pipeline");
		}

		auto native = MakePipelineResourceGroup<Backend>(
			pipeline.bindings,
			space,
			bindings
		);
		Backend::PipelineResourceGroup result;
		result.bindings.reserve(native.bindings.size());
		for (auto const& binding : native.bindings) {
			Backend::PipelineResourceGroup::Binding entry{
				.slot = binding.slot,
				.array_element = binding.array_element
			};
			std::visit(
				[&entry](auto const& value)
				{
					using Value = std::remove_cvref_t<decltype(value)>;
					if constexpr (std::same_as<Value, NativePipelineBufferBinding<Backend>>) {
						auto const& resource = value.impl.get();
						if (resource.type == Backend::Resource::Type::Buffer) {
							entry.buffer = resource.impl;
						}
					}
					else if constexpr (std::same_as<Value, NativePipelineViewBinding<Backend>>) {
						if (auto texture = std::get_if<Backend::GLTextureView>(&value.get())) {
							entry.view = texture->impl;
							entry.view_target = texture->target;
							entry.view_format = texture->format;
						}
						else if (auto buffer = std::get_if<Backend::GLBufferView>(&value.get())) {
							entry.view = buffer->impl;
							entry.view_target = GL_TEXTURE_BUFFER;
						}
					}
					else if constexpr (std::same_as<Value, NativePipelineSamplerBinding<Backend>>) {
						entry.sampler = value.get().impl;
					}
					else if constexpr (std::same_as<Value, NativePipelineCombinedBinding<Backend>>) {
						if (auto texture = std::get_if<Backend::GLTextureView>(&value.view.get())) {
							entry.view = texture->impl;
							entry.view_target = texture->target;
							entry.view_format = texture->format;
						}
						entry.sampler = value.sampler.get().impl;
					}
				},
				binding.value
			);
			result.bindings.emplace_back(entry);
		}
		return result;
	}

}

#endif // !defined(__APPLE__)
