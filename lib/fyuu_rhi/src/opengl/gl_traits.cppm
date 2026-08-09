/* the opengl pattern
module;
#include <version>
#if !defined(__cpp_lib_modules)

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
#endif // !defined(__APPLE__)
export module fyuu_rhi:;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
namespace fyuu_rhi::opengl {

}
#endif // !defined(__APPLE__)

*/

module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <exception>
#include <memory>
#include <vector>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <optional>
#include <string_view>
#include <variant>

#include <compare>
#include <span>
#include <stop_token>
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
#endif // !defined(__APPLE__)
export module fyuu_rhi:opengl_traits;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :core_types;
import :resource_types;
import :sampler_types;
import :pipeline_types;
import :native_pipeline_binding;
import :execution_types;

namespace fyuu_rhi::opengl {
	
	using namespace fyuu_rhi::pipeline;
	export struct Backend {
#if defined(_WIN32)
		using PlatformHandle = HWND;
#elif defined(__linux__)
		struct X11PlatformHandle {
			Display* display;
			Window window;

			std::strong_ordering operator<=>(X11PlatformHandle const&) const noexcept = default;
		};

		struct WaylandPlatformHandle {
			wl_display* display;
			wl_surface* surface;

			std::strong_ordering operator<=>(WaylandPlatformHandle const&) const noexcept = default;
		};

		using PlatformHandle = std::variant<X11PlatformHandle, WaylandPlatformHandle>;
#elif defined(__ANDROID__)
		using PlatformHandle = ANativeWindow*;
#endif // defined(_WIN32)

		struct Instance {
#if defined(_WIN32)
			HDC dc;
			HGLRC rc;
			HGLRC shared_rc;
#else
#if defined(__linux__)
			struct GLX {
				Display* dpy;
				GLXDrawable drawable;
				GLXContext ctx;
				GLXFBConfig fbconfig;
			};
#endif // defined(__linux__)
			struct EGL {
				EGLDisplay display;
				EGLSurface draw;
				EGLSurface read;
				EGLContext context;
				EGLConfig config;
				void* native_window;
			};
			std::variant<
				std::monostate
#if defined(__linux__)
				, GLX
#endif // defined(__linux__)
				, EGL
			> gl_handle;
#endif // defined(_WIN32)
		};

		using PhysicalDevice = Instance const*;
		struct LogicalDevice {
			Instance const* instance = nullptr;

			explicit operator bool() const noexcept {
				return instance != nullptr;
			}
		};

		struct Resource {
			GLuint impl = 0;
			GLenum target = 0u;
			GLenum format = 0u;
			std::size_t size = 0u;
			std::uint32_t width = 0u;
			std::uint32_t height = 0u;
			enum class Type : std::uint8_t {
				Buffer,
				Texture
			} type = Type::Buffer;

			Resource(GLuint impl_, Type type_, std::size_t size_) noexcept;
			Resource(
				GLuint impl_, GLenum target_, GLenum format_, std::uint32_t width_,
				std::uint32_t height_
			) noexcept;
			Resource(Resource const&) = delete;
			Resource& operator=(Resource const&) = delete;
			Resource(Resource&& other) noexcept;
			Resource& operator=(Resource&& other) noexcept;
			~Resource() noexcept;
		};

		struct GLTextureView {
			GLuint impl = 0;
			GLenum target = 0u;
			GLenum format = 0u;
			bool owned = true;
			GLTextureView(
				GLuint impl_,
				GLenum target_,
				GLenum format_,
				bool owned_ = true
			) noexcept;
			GLTextureView(GLTextureView const&) = delete;
			GLTextureView& operator=(GLTextureView const&) = delete;
			GLTextureView(GLTextureView&& other) noexcept;
			GLTextureView& operator=(GLTextureView&& other) noexcept;
			~GLTextureView() noexcept;
		};

		struct GLBufferView {
			struct Range {
				std::size_t offset;
				std::size_t size;
			};

			GLuint impl = 0;
			std::optional<Range> range; // fallback for no GLAD_GL_ARB_texture_buffer_range
			GLBufferView(GLuint impl_, std::optional<Range> range_) noexcept;
			GLBufferView(GLBufferView const&) = delete;
			GLBufferView& operator=(GLBufferView const&) = delete;
			GLBufferView(GLBufferView&& other) noexcept;
			GLBufferView& operator=(GLBufferView&& other) noexcept;
			~GLBufferView() noexcept;
		};

		using View = std::variant<std::monostate, GLTextureView, GLBufferView>;

		struct Sampler {
			GLuint impl = 0;
			explicit Sampler(GLuint impl_) noexcept;
			Sampler(Sampler const&) = delete;
			Sampler& operator=(Sampler const&) = delete;
			Sampler(Sampler&& other) noexcept;
			Sampler& operator=(Sampler&& other) noexcept;
			~Sampler() noexcept;
		};

		struct Pipeline {
			GLuint impl = 0;
			bool compute = false;
			std::vector<VertexBufferLayout> vertex_buffers;
			std::vector<VertexAttribute> vertex_attributes;
			PrimitiveState primitive;
			RasterizationState rasterization;
			MultisampleState multisample;
			std::optional<DepthStencilState> depth_stencil;
			std::vector<ColorTargetState> color_targets;
			std::vector<PipelineBindingMetadata> bindings;

			Pipeline(
				GLuint impl_,
				std::vector<VertexBufferLayout> vertex_buffers_,
				std::vector<VertexAttribute> vertex_attributes_,
				PrimitiveState primitive_,
				RasterizationState rasterization_,
				MultisampleState multisample_,
				std::optional<DepthStencilState> depth_stencil_,
				std::vector<ColorTargetState> color_targets_,
				std::vector<PipelineBindingMetadata> bindings_
			);
			Pipeline(GLuint impl_, std::vector<PipelineBindingMetadata> bindings_);

			Pipeline(Pipeline const&) = delete;
			Pipeline& operator=(Pipeline const&) = delete;
			Pipeline(Pipeline&& other) noexcept;
			Pipeline& operator=(Pipeline&& other) noexcept;
			~Pipeline() noexcept;
		};

		struct PipelineResourceGroup {
			struct Binding {
				std::uint32_t slot = 0u;
				std::uint32_t array_element = 0u;
				GLuint buffer = 0u;
				GLuint view = 0u;
				GLenum view_target = 0u;
				GLenum view_format = 0u;
				GLuint sampler = 0u;
			};

			std::vector<Binding> bindings;
		};


		class CompletionToken {
		public:
			/// Defined in the execution partition, where SchedulerContext::Implementation
			/// is complete. Unlike the Vulkan token, the completion state is written by
			/// the GL thread and read by the polling thread, so Deleter must hand the
			/// state back to the scheduler's pending list before destroying it:
			///   struct Implementation {
			///       std::shared_ptr<SchedulerContext::Implementation> scheduler;
			///       std::atomic<bool> complete = false;
			///       std::atomic<bool> stopped = false;
			///       std::exception_ptr error;
			///   };
			/// Public because Submission and the scheduler reference it by pointer.
			struct Implementation;
		private:
			struct Deleter {
				void operator()(Implementation* impl) const noexcept;
			};
			std::unique_ptr<Implementation, Deleter> impl;

			explicit CompletionToken(std::unique_ptr<Implementation, Deleter> impl_) noexcept
				: impl(std::move(impl_)) {}
			friend struct Backend;

		public:
			CompletionToken() noexcept = default;
			CompletionToken(CompletionToken const&) = delete;
			CompletionToken& operator=(CompletionToken const&) = delete;
			CompletionToken(CompletionToken&&) noexcept = default;
			CompletionToken& operator=(CompletionToken&&) noexcept = default;
			~CompletionToken() noexcept;

			[[nodiscard]] bool Poll() noexcept;
			[[nodiscard]] std::exception_ptr Error() const noexcept;
			[[nodiscard]] bool IsStopped() const noexcept;
		};

		/// One submission replayed by the scheduler thread. The plan is snapshotted:
		/// ExecuteCommands returns before replay, so caller-owned spans are invalid.
		struct Submission {
			/// GL object names plus metadata; Backend::Resource is not copyable, so
			/// only the fields replay needs are mirrored here.
			struct ResourceSnapshot {
				GLuint impl;
				GLenum target;
				GLenum format;
				std::size_t size;
				std::uint32_t width;
				std::uint32_t height;
				enum class Type : std::uint8_t { Buffer, Texture } type;
			};

			struct ViewSnapshot {
				GLuint impl = 0;
				GLenum target = 0u;
				GLenum format = 0u;
				bool texture = false;         // else buffer view (texture buffer)
			};

			struct SamplerSnapshot {
				GLuint impl = 0;
			};

			struct PipelineSnapshot {
				GLuint impl = 0;
				bool compute = false;
				std::vector<VertexBufferLayout> vertex_buffers;
				std::vector<VertexAttribute> vertex_attributes;
				PrimitiveState primitive;
				RasterizationState rasterization;
				std::optional<DepthStencilState> depth_stencil;
				/// First color target's blend state; OpenGL applies it as fixed-function
				/// blending at pipeline bind time.
				std::optional<BlendState> blend;
				ColorWriteMask write_mask = ColorWriteMask::All;
				std::vector<PipelineBindingMetadata> bindings;
			};

			struct GroupBindingSnapshot {
				std::uint32_t slot = 0u;
				std::uint32_t array_element = 0u;
				GLuint buffer = 0;            // nonzero → glBindBufferBase
				GLuint view = 0;              // nonzero → texture/image bind
				GLenum view_target = 0u;      // texture target or GL_TEXTURE_BUFFER
				GLenum view_format = 0u;      // internal format, for glBindImageTexture
				GLuint sampler = 0;           // nonzero → glBindSampler
			};

			struct GroupSnapshot {
				std::vector<GroupBindingSnapshot> bindings;
			};

			execution::ExecutionPlan plan;                   // deep copy
			std::vector<ResourceSnapshot> resources;
			std::vector<ViewSnapshot> views;
			std::vector<SamplerSnapshot> samplers;
			std::vector<PipelineSnapshot> pipelines;
			std::vector<GroupSnapshot> groups;
			std::vector<PlatformHandle> presentation_targets;
			/// Exclusively owned by the token; the GL thread writes completion only
			/// through this raw pointer, retired by CompletionToken::Deleter.
			CompletionToken::Implementation* token_state = nullptr;
		};

		struct SchedulerContext {
			/// Owns the dedicated GL thread and render context. GL executes
			/// synchronously and context-affine, so the thread is the backend's
			/// only "queue".
			/// The submission queue lives on the GL thread's stack and is published
			/// through the atomics below (run the D3D12 CompletionService pattern),
			/// so its lifetime is exactly the thread's. The pending list stays a
			/// member because CompletionToken::Deleter may run after the thread ends.
			struct Implementation {
				/// Published by Run() from the GL thread stack; cleared to nullptr
				/// before the thread exits so no one touches freed stack objects.
				std::atomic<std::mutex*> mutex = nullptr;
				std::atomic<std::condition_variable*> condition = nullptr;
				std::atomic<std::deque<Submission>*> submissions = nullptr;

				/// One submitted batch awaiting GPU completion. GLsync is
				/// context-bound, so only the GL thread may wait on or delete it.
				struct PendingSync {
					GLsync sync;
					CompletionToken::Implementation* state;   // raw; token owns it
				};
				/// Guards pending: the GL thread's complete store and the token's
				/// Deleter removal share this lock so destruction never races a write.
				std::mutex pending_mutex;
				std::deque<PendingSync> pending;

				/// GL-thread-only cache of vertex arrays, keyed by program. Vertex
				/// arrays are context-specific, so they live per scheduler.
				std::unordered_map<GLuint, GLuint> vertex_arrays;

				/// Per-window present state, touched only by the GL thread. A context
				/// may be made current on any drawable sharing its pixel format, so
				/// each presentation target owns an HDC/surface for its window.
				struct PresentTarget {
					PlatformHandle handle{};
#if defined(_WIN32)
					HDC dc = nullptr;
#elif defined(__ANDROID__) || defined(__linux__)
					/// EGL path only; GLX presents on the window drawable directly.
					EGLSurface surface = EGL_NO_SURFACE;
#endif
				};
				std::vector<PresentTarget> present_targets;

				std::exception_ptr fatal_error;
				/// Set by Run() after the render context binds successfully; checked by
				/// ExecuteCommands so submissions never post into a dead thread.
				std::atomic<bool> context_ready = false;
				Instance const* instance = nullptr;          // rc / shared_rc / gl_handle

				/// Must be the LAST member: its destructor (join) runs first during
				/// destruction, so Run() still sees every other member alive.
				std::jthread thread;

				Implementation() = default;
				Implementation(Implementation const&) = delete;
				Implementation& operator=(Implementation const&) = delete;

				void Run(std::stop_token stop);
				void ReapSignaled();
				void DrainPendingOnShutdown();
			};

			std::shared_ptr<Implementation> impl;

			std::strong_ordering operator<=>(SchedulerContext const&) const noexcept = default;
		};

		static CompletionToken ExecuteCommands(
			SchedulerContext const& scheduler,
			execution::ExecutionPlan const& plan,
			std::span<PlatformHandle const> presentation_targets,
			std::span<std::reference_wrapper<Resource> const> resources,
			std::span<std::reference_wrapper<View> const> views,
			std::span<std::reference_wrapper<Sampler> const> samplers,
			std::span<std::reference_wrapper<Pipeline> const> pipelines,
			std::span<std::reference_wrapper<PipelineResourceGroup> const> resource_groups,
			execution::StopTokenView stop_token
		);
#if defined(_WIN32)
		static Instance CreateInstance(std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver, HWND window_handle);
#elif defined(__linux__)
		static Instance CreateInstance(std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver, Display* x11_dpy, Window x11_window);
		static Instance CreateInstance(std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver, wl_display* display, wl_surface* surface, std::uint32_t width, std::uint32_t height);
#elif defined(__ANDROID__)
		static Instance CreateInstance(std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver, android_app* app);
#endif // defined(_WIN32)

		static PhysicalDevice EnumeratePhysicalDevices(Instance const& instance) noexcept;

		static void ShareContextOnThisThread(Instance const& instance);

		static void DestroyInstance(Instance const& instance) noexcept;

		static PhysicalDeviceInfo GetPhysicalDeviceInfo(PhysicalDevice const& phys_dev);

		static LogicalDevice CreateLogicalDevice(PhysicalDevice const& phys_dev) noexcept;

		static Resource CreateBuffer(LogicalDevice const& ld, std::size_t size_in_bytes, ResourceFlags const& flags);

		static Resource CreateTexture(LogicalDevice const& ld, std::size_t width, std::size_t height, std::size_t depth_arr_layers, std::size_t mip_lvl_cnt, ResourceFlags const& flags);

		static View CreateTextureView(LogicalDevice const& ld, Resource const& res, std::size_t base_mip_lvl, std::size_t mip_lvl_cnt, std::size_t base_arr_layer, std::size_t arr_layer_cnt, ResourceFlags const& flags);

		static View CreateBufferView(LogicalDevice const& ld, Resource const& res, std::size_t offset, std::size_t range, ResourceFlags const& flags);

		static Sampler CreateSampler(LogicalDevice const& ld, SamplerDescriptor const& descriptor);

		static Pipeline CreateGraphicsPipeline(LogicalDevice const& ld, GraphicsPipelineDescriptor const& descriptor);
		static Pipeline CreateComputePipeline(LogicalDevice const& ld, ComputePipelineDescriptor const& descriptor);

		static SchedulerContext CreateScheduler(LogicalDevice const& ld);

		static PipelineResourceGroup CreatePipelineResourceGroup(
			LogicalDevice const& ld,
			Pipeline const& pipeline,
			std::uint32_t space,
			std::span<NativePipelineResourceBinding<Backend> const> bindings
		);

	};

}
#endif // !defined(__APPLE__)
