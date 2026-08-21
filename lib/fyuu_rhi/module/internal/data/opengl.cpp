module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <vector>

#include <cstdint>

#include <optional>
#include <variant>


#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#include <boost/scope/unique_resource.hpp>
#include <glad/glad.h>
#include <glad/glad_wgl.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#include <boost/scope/unique_resource.hpp>
#include <glad/glad.h>
#include <glad/glad_egl.h>
#include <glad/glad_glx.h>
#include <X11/Xlib.h>
#elif defined(__ANDROID__)
#include <boost/scope/unique_resource.hpp>
#include <glad/glad.h>
#include <glad/glad_egl.h>
#endif

module fyuu_rhi:opengl_data;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :execution;
import :pipeline;

namespace fyuu_rhi::opengl {

#if defined(__linux__) || defined(__ANDROID__)
	struct EGLDisplayDeleter {
		void operator()(EGLDisplay display) const noexcept;
	};

	struct EGLSurfaceDeleter {
		EGLDisplay display;

		void operator()(EGLSurface surface) const noexcept;
	};

	struct EGLContextDeleter {
		EGLDisplay display;

		void operator()(EGLContext context) const noexcept;
	};

	using ManagedEGLDisplay = boost::scope::unique_resource<
		EGLDisplay,
		EGLDisplayDeleter
	>;
	using ManagedEGLSurface = boost::scope::unique_resource<
		EGLSurface,
		EGLSurfaceDeleter
	>;
	using ManagedEGLContext = boost::scope::unique_resource<
		EGLContext,
		EGLContextDeleter
	>;
#endif // defined(__linux__) || defined(__ANDROID__)

#if defined(_WIN32)
	struct WindowDeleter {
		void operator()(HWND window) const noexcept;
	};

	using Window = boost::scope::unique_resource<HWND, WindowDeleter>;

	struct Instance {
		Window window;
		HDC device_context;
		HGLRC context;
		DWORD owner_thread;
	};
#elif defined(__linux__) && !defined(__ANDROID__)
	struct DisplayDeleter {
		void operator()(Display* display) const noexcept;
	};

	struct ColormapDeleter {
		Display* display;

		void operator()(Colormap colormap) const noexcept;
	};

	struct DrawableDeleter {
		Display* display;

		void operator()(GLXDrawable drawable) const noexcept;
	};

	struct ContextDeleter {
		Display* display;

		void operator()(GLXContext context) const noexcept;
	};

	using ManagedDisplay = boost::scope::unique_resource<Display*, DisplayDeleter>;
	using ManagedColormap = boost::scope::unique_resource<Colormap, ColormapDeleter>;
	using ManagedDrawable = boost::scope::unique_resource<GLXDrawable, DrawableDeleter>;
	using ManagedContext = boost::scope::unique_resource<GLXContext, ContextDeleter>;

	struct GLXInstance {
		ManagedDisplay display;
		GLXFBConfig config;
		ManagedColormap colormap;
		ManagedDrawable drawable;
		ManagedContext context;
	};

	struct EGLInstance {
		ManagedEGLDisplay display;
		EGLConfig config;
		ManagedEGLSurface surface;
		ManagedEGLContext context;
	};
#elif defined(__ANDROID__)
	struct Instance {
		ManagedEGLDisplay display;
		EGLConfig config;
		ManagedEGLSurface surface;
		ManagedEGLContext context;
	};
#endif

	/// The window + GL context an OpenGL logical device is created from. The
	/// instance types differ per platform, so a variant carries the pointer.
#if defined(_WIN32) || defined(__ANDROID__)
	using InstanceReference = std::variant<std::monostate, Instance const*>;
#elif defined(__linux__) && !defined(__ANDROID__)
	using InstanceReference = std::variant<std::monostate, GLXInstance const*, EGLInstance const*>;
#endif

	struct PhysicalDevice {
		InstanceReference instance;
	};

	struct LogicalDevice {
		InstanceReference instance;
	};

	struct CompletionState {
		std::atomic_bool complete = false;
		std::atomic_bool stopped = false;
		std::mutex mutex;
		std::exception_ptr error;
	};

	struct CompletionToken {
		std::shared_ptr<CompletionState> state;
	};

	struct Submission;

	struct CommandSchedulerContext {
		struct PendingSync {
			GLsync sync;
			std::shared_ptr<CompletionState> state;
		};

		struct PresentTarget {
			execution::PlatformHandle handle{};
#if defined(_WIN32)
			HDC device_context = nullptr;
#elif defined(__ANDROID__) || defined(__linux__)
			EGLSurface surface = EGL_NO_SURFACE;
#endif
		};

		InstanceReference instance;
		std::atomic<std::mutex*> submission_mutex = nullptr;
		std::atomic<std::condition_variable*> submission_condition = nullptr;
		std::atomic<std::deque<Submission>*> submissions = nullptr;
		std::deque<PendingSync> pending;
		std::unordered_map<GLuint, GLuint> vertex_arrays;
		std::vector<PresentTarget> present_targets;
		std::exception_ptr fatal_error;
		std::atomic_bool context_ready = false;
		std::jthread thread;

		explicit CommandSchedulerContext(InstanceReference const& instance) noexcept
			: instance(instance) {
		}

		CommandSchedulerContext(CommandSchedulerContext const&) = delete;
		CommandSchedulerContext& operator=(CommandSchedulerContext const&) = delete;
		CommandSchedulerContext(CommandSchedulerContext&& other) noexcept;
		~CommandSchedulerContext() noexcept;

		void Run(std::stop_token stop_token);
		void ReapSignaled();
		void DrainPendingOnShutdown();
	};

	enum class ResourceType : std::uint8_t {
		Buffer,
		Texture
	};

	struct ResourceDeleter {
		ResourceType type;
		void operator()(GLuint impl) const noexcept;
	};

	struct Resource {
		boost::scope::unique_resource<GLuint, ResourceDeleter> impl;
		GLenum target;
		GLenum format;
		ResourceType type;

		explicit Resource(GLuint buffer) noexcept;

		Resource(GLuint texture, GLenum target_, GLenum format_) noexcept;

		Resource(Resource&&) noexcept = default;
		Resource& operator=(Resource&&) noexcept = default;
	};

	struct ViewDeleter {
		bool owned;
		void operator()(GLuint impl) const noexcept;
	};

	struct View {
		struct BufferRange {
			std::size_t offset;
			std::size_t size;
		};

		boost::scope::unique_resource<GLuint, ViewDeleter> impl;
		std::optional<BufferRange> buffer_range;
		GLenum target;
		GLenum format;

		View(
			GLuint impl_,
			std::optional<BufferRange> buffer_range_,
			GLenum target_,
			GLenum format_,
			bool owned_
		) noexcept;

		View(View&&) noexcept = default;
		View& operator=(View&&) noexcept = default;
	};

	struct SamplerDeleter {
		void operator()(GLuint impl) const noexcept;
	};

	struct Sampler {
		boost::scope::unique_resource<GLuint, SamplerDeleter> impl;

		explicit Sampler(GLuint impl_) noexcept;

		Sampler(Sampler&&) noexcept = default;
		Sampler& operator=(Sampler&&) noexcept = default;
	};

	struct PipelineDeleter {
		void operator()(GLuint impl) const noexcept;
	};

	using ManagedPipeline = boost::scope::unique_resource<GLuint, PipelineDeleter>;

	struct Pipeline {
		ManagedPipeline impl;
		bool compute;
		std::vector<pipeline::VertexBufferLayout> vertex_buffers;
		std::vector<pipeline::VertexAttribute> vertex_attributes;
		pipeline::PrimitiveState primitive;
		pipeline::RasterizationState rasterization;
		pipeline::MultisampleState multisample;
		std::optional<pipeline::DepthStencilState> depth_stencil;
		std::vector<pipeline::ColorTargetState> color_targets;
		std::vector<pipeline::BindingMetadata> bindings;
	};

	struct PipelineResourceGroup {
		struct Binding {
			std::uint32_t slot;
			std::uint32_t array_element;
			GLuint buffer;
			std::size_t buffer_offset;
			std::size_t buffer_size;
			GLuint view;
			GLenum view_target;
			GLenum view_format;
			GLuint sampler;
		};

		std::uint32_t space;
		std::vector<Binding> bindings;
	};

	struct Submission {
		struct ResourceSnapshot {
			GLuint impl;
			GLenum target;
			GLenum format;
			std::size_t size;
			std::uint32_t width;
			std::uint32_t height;
			enum class Type : std::uint8_t {
				Buffer,
				Texture
			} type;
		};

		struct ViewSnapshot {
			GLuint impl;
			GLenum target;
			GLenum format;
			bool texture;
		};

		struct SamplerSnapshot {
			GLuint impl;
		};

		struct PipelineSnapshot {
			GLuint impl;
			bool compute;
			std::vector<pipeline::VertexBufferLayout> vertex_buffers;
			std::vector<pipeline::VertexAttribute> vertex_attributes;
			pipeline::PrimitiveState primitive;
			pipeline::RasterizationState rasterization;
			std::optional<pipeline::DepthStencilState> depth_stencil;
			std::optional<pipeline::BlendState> blend;
			pipeline::ColorWriteMask write_mask;
			std::vector<pipeline::BindingMetadata> bindings;
		};

		struct GroupBindingSnapshot {
			std::uint32_t slot;
			std::uint32_t array_element;
			GLuint buffer;
			std::size_t buffer_offset;
			std::size_t buffer_size;
			GLuint view;
			GLenum view_target;
			GLenum view_format;
			GLuint sampler;
		};

		struct GroupSnapshot {
			std::vector<GroupBindingSnapshot> bindings;
		};

		execution::ExecutionPlan plan;
		std::vector<ResourceSnapshot> resources;
		std::vector<ViewSnapshot> views;
		std::vector<SamplerSnapshot> samplers;
		std::vector<PipelineSnapshot> pipelines;
		std::vector<GroupSnapshot> groups;
		std::vector<execution::PlatformHandle> presentation_targets;
		std::shared_ptr<CompletionState> state;
	};

} // namespace fyuu_rhi::opengl::data
