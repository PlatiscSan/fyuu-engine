module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

#include <algorithm>
#include <iterator>

#include <cstring>
#include <string>
#include <limits>

#include <cstdint>
#include <type_traits>

#include <array>

#include <optional>
#include <variant>

#include <string_view>

#include <filesystem>

#include <concepts>
#include <ranges>
#include <span>

#include <format>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#include <boost/hash2/xxhash.hpp>
#include <glad/glad.h>
#include <slang.h>
#include <spirv_glsl.hpp>
#endif // !defined(__APPLE__)

module fyuu_rhi:opengl_logical_device;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :cache;
import :instance_dispatch;
import :logical_device_dispatch;
import :opengl_data;
import :opengl_utility;
import :pipeline;
import :pipeline_factory;
import :resource_factory;
import :sampler;
import :sampler_factory;
import :slang;
#if defined(_WIN32)
import :opengl_instance_wgl;
#elif defined(__linux__) && !defined(__ANDROID__)
import :opengl_instance_egl;
import :opengl_instance_glx;
#elif defined(__ANDROID__)
import :opengl_instance_egl;
#endif // defined(_WIN32)

namespace {

	using Bits = fyuu_rhi::ResourceFlagBits;

	GLbitfield BufferStorageFlags(fyuu_rhi::ResourceFlags const& flags) noexcept {
		GLbitfield result = 0u;
		if (flags.Test(Bits::HostVisible)) {
			result |= GL_MAP_WRITE_BIT | GL_DYNAMIC_STORAGE_BIT;
		}
		if (flags.Test(Bits::DeviceReadback)) {
			result |= GL_MAP_READ_BIT;
		}
		return result;
	}

	GLsizei SampleCount(fyuu_rhi::ResourceFlags const& flags) {
		if (flags.TestMultipleInRange(Bits::Sample1, Bits::Sample64)) {
			throw std::invalid_argument("An OpenGL texture requires at most one sample count");
		}
		if (flags.Test(Bits::Sample2)) {
			return 2;
		}
		if (flags.Test(Bits::Sample4)) {
			return 4;
		}
		if (flags.Test(Bits::Sample8)) {
			return 8;
		}
		if (flags.Test(Bits::Sample16)) {
			return 16;
		}
		if (flags.Test(Bits::Sample32)) {
			return 32;
		}
		if (flags.Test(Bits::Sample64)) {
			return 64;
		}
		return 1;
	}

	GLenum TextureTarget(fyuu_rhi::ResourceFlags const& flags, std::size_t depth_or_array_layers, GLsizei sample_count) {
		if (flags.TestMultipleInRange(Bits::Texture1D, Bits::Texture3D)) {
			throw std::invalid_argument("An OpenGL texture requires one dimension");
		}
		if (flags.Test(Bits::Texture1D)) {
			if (sample_count > 1) {
				throw std::invalid_argument("OpenGL 1D textures cannot be multisampled");
			}
			return depth_or_array_layers > 1u ? GL_TEXTURE_1D_ARRAY : GL_TEXTURE_1D;
		}
		if (flags.Test(Bits::Texture3D)) {
			if (sample_count > 1) {
				throw std::invalid_argument("OpenGL 3D textures cannot be multisampled");
			}
			if (depth_or_array_layers <= 1u) {
				throw std::invalid_argument("An OpenGL 3D texture requires depth greater than one");
			}
			return GL_TEXTURE_3D;
		}
		if (sample_count > 1) {
			return depth_or_array_layers > 1u ? GL_TEXTURE_2D_MULTISAMPLE_ARRAY : GL_TEXTURE_2D_MULTISAMPLE;
		}
		return depth_or_array_layers > 1u ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
	}

	GLsizei Dimension(std::size_t value) {
		if (value > static_cast<std::size_t>((std::numeric_limits<GLsizei>::max)())) {
			throw std::invalid_argument("An OpenGL texture dimension exceeds GLsizei");
		}
		return static_cast<GLsizei>(value);
	}

	void AllocateTextureStorage(
		GLuint texture,
		GLenum target,
		GLenum format,
		GLsizei width,
		GLsizei height,
		GLsizei depth_or_array_layers,
		GLsizei mip_levels,
		GLsizei sample_count,
		bool direct_state_access
	) {
		if (direct_state_access) {
			switch (target) {
			case GL_TEXTURE_1D:
				glTextureStorage1D(texture, mip_levels, format, width);
				return;
			case GL_TEXTURE_1D_ARRAY:
				glTextureStorage2D(texture, mip_levels, format, width, depth_or_array_layers);
				return;
			case GL_TEXTURE_2D:
				glTextureStorage2D(texture, mip_levels, format, width, height);
				return;
			case GL_TEXTURE_2D_ARRAY:
			case GL_TEXTURE_3D:
				glTextureStorage3D(texture,	mip_levels, format, width, height, depth_or_array_layers);
				return;
			case GL_TEXTURE_2D_MULTISAMPLE:
				glTextureStorage2DMultisample(texture, sample_count, format, width, height,	GL_TRUE);
				return;
			case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
				glTextureStorage3DMultisample(texture, sample_count, format, width, height, depth_or_array_layers, GL_TRUE);
				return;
			default:
				throw std::invalid_argument("Unsupported OpenGL texture target");
			}
		}

		glBindTexture(target, texture);
		switch (target) {
		case GL_TEXTURE_1D:
			glTexStorage1D(target, mip_levels, format, width);
			break;
		case GL_TEXTURE_1D_ARRAY:
			glTexStorage2D(target, mip_levels, format, width, depth_or_array_layers);
			break;
		case GL_TEXTURE_2D:
			glTexStorage2D(target, mip_levels, format, width, height);
			break;
		case GL_TEXTURE_2D_ARRAY:
		case GL_TEXTURE_3D:
			glTexStorage3D(target, mip_levels, format, width, height, depth_or_array_layers);
			break;
		case GL_TEXTURE_2D_MULTISAMPLE:
			glTexStorage2DMultisample(target, sample_count, format, width, height, GL_TRUE);
			break;
		case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
			glTexStorage3DMultisample(target, sample_count, format, width, height, depth_or_array_layers, GL_TRUE);
			break;
		default:
			glBindTexture(target, 0u);
			throw std::invalid_argument("Unsupported OpenGL texture target");
		}
		glBindTexture(target, 0u);
	}

	using namespace fyuu_rhi;
	using namespace fyuu_rhi::pipeline;

	void ShareContext(opengl::LogicalDevice const* logical_device) {
		std::visit(
			[]<class Instance>(Instance instance) {
				if constexpr (!std::same_as<Instance, std::monostate>) {
					ShareContextOnCurrentThread<std::remove_pointer_t<Instance>>{
						instance
					}();
				}
			},
			logical_device->instance
		);
	}

	bool SupportsProgramBinary() noexcept {
		return GLAD_GL_VERSION_4_1 ||
			GLAD_GL_ES_VERSION_3_0 ||
			GLAD_GL_ARB_get_program_binary;
	}

	struct ShaderTarget {
		SlangCompileTarget format;
		std::string_view profile;
		std::string_view cache_name;
		std::uint32_t essl_version = 0u;
	};

	ShaderTarget SelectShaderTarget() {
		if (GLAD_GL_ES_VERSION_3_2) {
			return { SLANG_SPIRV, "spirv_1_0", "essl_320", 320u };
		}
		if (GLAD_GL_ES_VERSION_3_1) {
			return { SLANG_SPIRV, "spirv_1_0", "essl_310", 310u };
		}
		if (GLAD_GL_ES_VERSION_3_0) {
			return { SLANG_SPIRV, "spirv_1_0", "essl_300", 300u };
		}
		if (GLAD_GL_ES_VERSION_2_0) {
			throw std::runtime_error("Graphics pipelines require OpenGL ES 3.0 or newer");
		}
		if (GLAD_GL_VERSION_4_6) {
			return { SLANG_GLSL, "glsl_460", "glsl_460" };
		}
		if (GLAD_GL_VERSION_4_5) {
			return { SLANG_GLSL, "glsl_450", "glsl_450" };
		}
		if (GLAD_GL_VERSION_4_4) {
			return { SLANG_GLSL, "glsl_440", "glsl_440" };
		}
		if (GLAD_GL_VERSION_4_3) {
			return { SLANG_GLSL, "glsl_430", "glsl_430" };
		}
		if (GLAD_GL_VERSION_4_2) {
			return { SLANG_GLSL, "glsl_420", "glsl_420" };
		}
		if (GLAD_GL_VERSION_4_1) {
			return { SLANG_GLSL, "glsl_410", "glsl_410" };
		}
		if (GLAD_GL_VERSION_4_0) {
			return { SLANG_GLSL, "glsl_400", "glsl_400" };
		}
		if (GLAD_GL_VERSION_3_3) {
			return { SLANG_GLSL, "glsl_330", "glsl_330" };
		}
		throw std::runtime_error("OpenGL graphics pipelines require OpenGL 3.3 or newer");
	}

	GLenum ShaderStage(Stage stage) {
		switch (stage) {
		case Stage::Vertex: return GL_VERTEX_SHADER;
		case Stage::Fragment: return GL_FRAGMENT_SHADER;
		case Stage::TessellationControl: return GL_TESS_CONTROL_SHADER;
		case Stage::TessellationEvaluation: return GL_TESS_EVALUATION_SHADER;
		case Stage::Geometry: return GL_GEOMETRY_SHADER;
		case Stage::Compute: return GL_COMPUTE_SHADER;
		case Stage::Task:
		case Stage::Mesh:
			throw std::invalid_argument("OpenGL mesh shading pipelines are not implemented");
		default:
			throw std::invalid_argument("Unsupported OpenGL pipeline stage");
		}
	}

	std::string ShaderInfoLog(GLuint shader) {
		GLint length = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
		if (length <= 1) {
			return {};
		}
		std::string result(static_cast<std::size_t>(length), '\0');
		glGetShaderInfoLog(shader, length, nullptr, result.data());
		return result;
	}

	std::string ProgramInfoLog(GLuint program) {
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
		if (entry.code.empty() || entry.code.size() % sizeof(std::uint32_t) != 0u) {
			throw std::runtime_error(
				std::format(
					"Slang produced invalid SPIR-V for OpenGL ES entry point '{}'",
					entry.name
				)
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
		options.fragment.default_float_precision =
			spirv_cross::CompilerGLSL::Options::Mediump;
		options.fragment.default_int_precision =
			spirv_cross::CompilerGLSL::Options::Highp;
		compiler.set_common_options(options);
		return compiler.compile();
	}

	GLuint CompileShader(
		shader::SlangCompiledEntryPoint const& entry,
		std::uint32_t essl_version
	) {
		auto shader_type = ShaderStage(entry.stage);
		GLuint shader = glCreateShader(shader_type);
		if (shader == 0u) {
			throw std::runtime_error("Failed to create an OpenGL pipeline stage");
		}
		std::string converted_source;
		GLchar const* source = nullptr;
		GLint length = 0;
		if (essl_version != 0u) {
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
			auto diagnostics = ShaderInfoLog(shader);
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

	void HashDriver(boost::hash2::xxhash_64& hash) {
		constexpr std::array values{
			GL_VENDOR,
			GL_RENDERER,
			GL_VERSION,
			GL_SHADING_LANGUAGE_VERSION
		};
		std::ranges::for_each(
			values,
			[&hash](GLenum value) {
				auto text = reinterpret_cast<char const*>(glGetString(value));
				HashString(hash, text ? std::string_view(text) : std::string_view{});
			}
		);
	}

	std::filesystem::path PipelineCachePath(
		shader::SlangProgram const& program,
		std::string_view profile
	) {
		boost::hash2::xxhash_64 hash;
		HashString(hash, profile);
		HashDriver(hash);
		std::ranges::for_each(
			program.GetEntryPoints(),
			[&hash](auto const& entry) {
				auto stage = static_cast<std::uint8_t>(entry.stage);
				hash.update(&stage, sizeof(stage));
				HashString(hash, entry.name);
				hash.update(entry.code.data(), entry.code.size());
			}
		);
		return cache::GetCacheFilePath(
			std::format("opengl-pipeline-{:016x}.bin", hash.result())
		);
	}

	std::string CacheTag(std::string_view profile) {
		boost::hash2::xxhash_64 hash;
		HashString(hash, profile);
		HashDriver(hash);
		return std::format("opengl-{}-{:016x}", profile, hash.result());
	}

	GLuint LoadProgramBinary(std::filesystem::path const& path) {
		if (!SupportsProgramBinary()) {
			return 0u;
		}
		auto cached = cache::ReadFile(path);
		if (cached.size() <= sizeof(GLenum)) {
			return 0u;
		}
		GLenum format = 0u;
		std::memcpy(&format, cached.data(), sizeof(format));
		auto bytes = std::span(cached).subspan(sizeof(format));
		GLuint program = glCreateProgram();
		if (program == 0u) {
			throw std::runtime_error("Failed to create an OpenGL pipeline program");
		}
		glProgramBinary(
			program,
			format,
			bytes.data(),
			static_cast<GLsizei>(bytes.size())
		);
		GLint linked = GL_FALSE;
		glGetProgramiv(program, GL_LINK_STATUS, &linked);
		if (linked == GL_FALSE) {
			glDeleteProgram(program);
			return 0u;
		}
		return program;
	}

	void SaveProgramBinary(GLuint program, std::filesystem::path const& path) {
		GLint length = 0;
		glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &length);
		if (length <= 0) {
			return;
		}
		GLenum format = 0u;
		std::vector<std::byte> bytes(static_cast<std::size_t>(length));
		GLsizei written = 0;
		glGetProgramBinary(program, length, &written, &format, bytes.data());
		if (written <= 0) {
			return;
		}
		std::vector<std::byte> serialized(
			sizeof(format) + static_cast<std::size_t>(written)
		);
		std::memcpy(serialized.data(), &format, sizeof(format));
		std::memcpy(
			serialized.data() + sizeof(format),
			bytes.data(),
			static_cast<std::size_t>(written)
		);
		cache::WriteFileAtomically(path, serialized);
	}

	GLuint CreateProgram(
		shader::SlangProgram const& program,
		ShaderTarget const& target,
		bool compute
	) {
		auto cache_path = PipelineCachePath(program, target.cache_name);
		GLuint result = LoadProgramBinary(cache_path);
		if (result != 0u) {
			return result;
		}
		result = glCreateProgram();
		if (result == 0u) {
			throw std::runtime_error("Failed to create an OpenGL pipeline program");
		}
		if (SupportsProgramBinary()) {
			glProgramParameteri(result, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
		}
		std::vector<GLuint> shaders;
		shaders.reserve(program.GetEntryPoints().size());
		try {
			std::ranges::transform(
				program.GetEntryPoints(),
				std::back_inserter(shaders),
				[&target](auto const& entry) {
					return CompileShader(entry, target.essl_version);
				}
			);
			std::ranges::for_each(
				shaders,
				[result](GLuint shader) {
					glAttachShader(result, shader);
				}
			);
			glLinkProgram(result);
			GLint linked = GL_FALSE;
			glGetProgramiv(result, GL_LINK_STATUS, &linked);
			if (linked == GL_FALSE) {
				throw std::runtime_error(
					std::format(
						"Failed to link OpenGL {} pipeline: {}",
						compute ? "compute" : "graphics",
						ProgramInfoLog(result)
					)
				);
			}
		}
		catch (...) {
			std::ranges::for_each(
				shaders,
				[result](GLuint shader) {
					glDetachShader(result, shader);
					glDeleteShader(shader);
				}
			);
			glDeleteProgram(result);
			throw;
		}
		std::ranges::for_each(
			shaders,
			[result](GLuint shader) {
				glDetachShader(result, shader);
				glDeleteShader(shader);
			}
		);
		if (SupportsProgramBinary()) {
			SaveProgramBinary(result, cache_path);
		}
		return result;
	}

} // namespace

namespace fyuu_rhi::opengl {

	void PipelineDeleter::operator()(GLuint impl) const noexcept {
		if (impl != 0u) {
			glDeleteProgram(impl);
		}
	}

	void SamplerDeleter::operator()(GLuint impl) const noexcept {
		if (impl != 0u) {
			glDeleteSamplers(1u, &impl);
		}
	}

	Sampler::Sampler(GLuint impl_) noexcept
		: impl(impl_, SamplerDeleter{}) {
	}

} // namespace fyuu_rhi::opengl

namespace fyuu_rhi {

	template <>
	struct CreateBuffer<opengl::LogicalDevice> {
		opengl::LogicalDevice* logical_device;

		Resource operator()(std::size_t size_in_bytes, ResourceFlags const& flags) const {
			GLuint buffer = 0u;
			auto storage_flags = BufferStorageFlags(flags);
			if (GLAD_GL_ARB_direct_state_access) {
				glCreateBuffers(1, &buffer);
				if (buffer == 0u) {
					throw std::runtime_error("Failed to create an OpenGL buffer");
				}
				glNamedBufferStorage(buffer, static_cast<GLsizeiptr>(size_in_bytes), nullptr, storage_flags);
			}
			else {
				glGenBuffers(1, &buffer);
				if (buffer == 0u) {
					throw std::runtime_error("Failed to create an OpenGL buffer");
				}
				glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
				if (GLAD_GL_ARB_buffer_storage) {
					glBufferStorage(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(size_in_bytes), nullptr, storage_flags);
				}
				else {
					glBufferData(
						GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(size_in_bytes), nullptr, 
						storage_flags & GL_DYNAMIC_STORAGE_BIT ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW
					);
				}
				glBindBuffer(GL_COPY_WRITE_BUFFER, 0u);
			}
			glFlush();
			return MakeResource(opengl::Resource(buffer), size_in_bytes, flags);
		}
	};

	template <>
	struct CreateTexture<opengl::LogicalDevice> {
		opengl::LogicalDevice* logical_device;

		Resource operator()(
			std::size_t width,
			std::size_t height,
			std::size_t depth_or_array_layers,
			std::size_t mip_levels,
			ResourceFlags const& flags
		) const {

			auto sample_count = SampleCount(flags);
			if (sample_count > 1 && mip_levels != 1u) {
				throw std::invalid_argument("An OpenGL multisample texture requires one mip level");
			}
			auto target = TextureTarget(
				flags,
				depth_or_array_layers,
				sample_count
			);
			auto format = opengl::InternalFormat(flags);
			GLuint texture = 0u;
			auto direct_state_access = GLAD_GL_ARB_direct_state_access != 0;
			if (direct_state_access) {
				glCreateTextures(target, 1, &texture);
			}
			else {
				glGenTextures(1, &texture);
			}
			if (texture == 0u) {
				throw std::runtime_error("Failed to create an OpenGL texture");
			}
			try {
				AllocateTextureStorage(
					texture,
					target,
					format,
					Dimension(width),
					Dimension(height),
					Dimension(depth_or_array_layers),
					Dimension(mip_levels),
					sample_count,
					direct_state_access
				);
			}
			catch (...) {
				glDeleteTextures(1, &texture);
				throw;
			}
			glFlush();
			return MakeResource(
				opengl::Resource(texture, target, format),
				ResourceTextureExtent{
					.width = static_cast<std::uint32_t>(width),
					.height = static_cast<std::uint32_t>(height),
					.depth_or_array_layers = static_cast<std::uint32_t>(depth_or_array_layers),
					.mip_levels = static_cast<std::uint32_t>(mip_levels)
				},
				flags
			);
		}
	};

	template <>
	struct CreateSampler<opengl::LogicalDevice> {
		opengl::LogicalDevice* logical_device;

		Sampler operator()(SamplerDescriptor const& descriptor) const {
			if (!GLAD_GL_ARB_sampler_objects) {
				throw std::runtime_error(
					"An OpenGL sampler requires GL_ARB_sampler_objects"
				);
			}
			GLuint sampler = 0u;
			if (GLAD_GL_ARB_direct_state_access) {
				glCreateSamplers(1u, &sampler);
			}
			else {
				glGenSamplers(1u, &sampler);
			}
			if (sampler == 0u) {
				throw std::runtime_error("Failed to create an OpenGL sampler");
			}
			glSamplerParameteri(
				sampler,
				GL_TEXTURE_WRAP_S,
				opengl::SamplerAddressMode(descriptor.address_mode_u)
			);
			glSamplerParameteri(
				sampler,
				GL_TEXTURE_WRAP_T,
				opengl::SamplerAddressMode(descriptor.address_mode_v)
			);
			glSamplerParameteri(
				sampler,
				GL_TEXTURE_WRAP_R,
				opengl::SamplerAddressMode(descriptor.address_mode_w)
			);
			glSamplerParameteri(
				sampler,
				GL_TEXTURE_MAG_FILTER,
				opengl::Filter(descriptor.mag_filter)
			);

			GLenum min_filter = opengl::Filter(descriptor.min_filter);
			if (descriptor.max_lod > descriptor.min_lod) {
				switch (descriptor.min_filter) {
				case FilterMode::Nearest:
					min_filter = descriptor.mipmap_filter == MipmapFilterMode::Nearest ?
						GL_NEAREST_MIPMAP_NEAREST :
						GL_NEAREST_MIPMAP_LINEAR;
					break;
				case FilterMode::Linear:
					min_filter = descriptor.mipmap_filter == MipmapFilterMode::Nearest ?
						GL_LINEAR_MIPMAP_NEAREST :
						GL_LINEAR_MIPMAP_LINEAR;
					break;
				default:
					min_filter = GL_LINEAR_MIPMAP_LINEAR;
				}
			}
			glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, min_filter);

			if (descriptor.max_anisotropy > 1u && GLAD_GL_ARB_texture_filter_anisotropic) {
				glSamplerParameterf(
					sampler,
					GL_TEXTURE_MAX_ANISOTROPY,
					static_cast<GLfloat>(descriptor.max_anisotropy)
				);
			}

			GLenum compare_func = opengl::ComparisonFunction(descriptor.compare_function);
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

			return MakeSampler(opengl::Sampler(sampler));
		}
	};

	template <>
	struct CreateGraphicsPipeline<opengl::LogicalDevice> {
		opengl::LogicalDevice* logical_device;

		Pipeline operator()(pipeline::GraphicsPipelineDescriptor const& descriptor) const {
			ShareContext(logical_device);
			auto shader_target = SelectShaderTarget();
			slang::TargetDesc target{
				.format = shader_target.format,
				.profile = shader::SlangGlobalSession()->findProfile(
					shader_target.profile.data()
				)
			};
			shader::SlangProgram program(
				target,
				descriptor.program,
				CacheTag(shader_target.cache_name)
			);
			if (std::ranges::any_of(
				program.GetInterface().bindings,
				[](auto const& binding) {
					return binding.space != 0u;
				}
			)) {
				throw std::invalid_argument(
					"OpenGL pipeline resource bindings must use space 0"
				);
			}
			if (!program.GetInterface().push_constants.empty()) {
				throw std::invalid_argument(
					"OpenGL pipelines do not support push constants"
				);
			}

			bool has_vertex_stage = false;
			std::uint32_t stage_mask = 0u;
			std::ranges::for_each(
				program.GetEntryPoints(),
				[&](auto const& entry) {
					if (entry.stage == pipeline::Stage::Compute) {
						throw std::invalid_argument(
							"A compute entry point cannot be used by an OpenGL graphics pipeline"
						);
					}
					(void)ShaderStage(entry.stage);
					if (
						shader_target.essl_version != 0u &&
						shader_target.essl_version < 320u &&
						entry.stage != pipeline::Stage::Vertex &&
						entry.stage != pipeline::Stage::Fragment
					) {
						throw std::invalid_argument(
							"OpenGL ES 3.0 and 3.1 graphics pipelines support only vertex and fragment stages"
						);
					}
					auto stage_bit = 1u << static_cast<std::uint32_t>(entry.stage);
					if (stage_mask & stage_bit) {
						throw std::invalid_argument(
							"An OpenGL graphics pipeline cannot contain duplicate stages"
						);
					}
					stage_mask |= stage_bit;
					has_vertex_stage |= entry.stage == pipeline::Stage::Vertex;
				}
			);
			if (!has_vertex_stage) {
				throw std::invalid_argument(
					"OpenGL graphics pipeline has no vertex entry point"
				);
			}

			auto native_program = CreateProgram(program, shader_target, false);
			return MakePipeline(
				opengl::Pipeline{
					opengl::ManagedPipeline(
						native_program,
						opengl::PipelineDeleter{}
					),
					false,
					std::vector<pipeline::VertexBufferLayout>(
						descriptor.vertex.buffers.begin(),
						descriptor.vertex.buffers.end()
					),
					std::vector<pipeline::VertexAttribute>(
						descriptor.vertex.attributes.begin(),
						descriptor.vertex.attributes.end()
					),
					descriptor.primitive,
					descriptor.rasterization,
					descriptor.multisample,
					descriptor.depth_stencil,
					std::vector<pipeline::ColorTargetState>(
						descriptor.color_targets.begin(),
						descriptor.color_targets.end()
					),
					pipeline::MakePipelineBindingMetadata(program.GetInterface())
				}
			);
		}
	};

	template <>
	struct CreateComputePipeline<opengl::LogicalDevice> {
		opengl::LogicalDevice* logical_device;

		Pipeline operator()(pipeline::ComputePipelineDescriptor const& descriptor) const {
			ShareContext(logical_device);
			if (
				!GLAD_GL_VERSION_4_3 &&
				!GLAD_GL_ES_VERSION_3_1 &&
				!GLAD_GL_ARB_compute_shader
			) {
				throw std::runtime_error(
					"Compute pipelines require OpenGL 4.3 or OpenGL ES 3.1"
				);
			}
			auto shader_target = SelectShaderTarget();
			slang::TargetDesc target{
				.format = shader_target.format,
				.profile = shader::SlangGlobalSession()->findProfile(
					shader_target.profile.data()
				)
			};
			shader::SlangProgram program(
				target,
				descriptor.program,
				CacheTag(shader_target.cache_name)
			);
			if (std::ranges::any_of(
				program.GetInterface().bindings,
				[](auto const& binding) {
					return binding.space != 0u;
				}
			)) {
				throw std::invalid_argument(
					"OpenGL compute resource bindings must use space 0"
				);
			}
			if (!program.GetInterface().push_constants.empty()) {
				throw std::invalid_argument(
					"OpenGL compute pipelines do not support push constants"
				);
			}
			if (
				program.GetEntryPoints().size() != 1u ||
				program.GetEntryPoints().front().stage != pipeline::Stage::Compute
			) {
				throw std::invalid_argument(
					"OpenGL compute pipelines require exactly one compute entry point"
				);
			}

			auto native_program = CreateProgram(program, shader_target, true);
			return MakePipeline(
				opengl::Pipeline{
					opengl::ManagedPipeline(
						native_program,
						opengl::PipelineDeleter{}
					),
					true,
					{},
					{},
					{},
					{},
					{},
					std::nullopt,
					{},
					pipeline::MakePipelineBindingMetadata(program.GetInterface())
				}
			);
		}
	};

} // namespace fyuu_rhi
#endif // !defined(__APPLE__)
