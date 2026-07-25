/* the opengl pattern
module;
#include <boost/smart_ptr/intrusive_ptr.hpp>
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
#include <type_traits>
#include <thread>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <variant>
#include <optional>
#include <vector>
#include <span>
#include <cstdint>
#include <utility>
#include <deque>
#include <condition_variable>
#include <stop_token>
#include <exception>
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
import :scheduler_types;
import :pipeline_types;
import :native_pipeline_binding;
import :native_command_graph;
import :presentation_cache;
import :completion_service;
import :execution_pool;

namespace fyuu_rhi::opengl {
	
	using namespace fyuu_rhi::pipeline;
	using namespace fyuu_rhi::execution;

	export struct Backend {

#if defined(_WIN32)
		using PresentationTarget = HWND;
#elif defined(__linux__)
		struct X11PresentationTarget {
			Display* display;
			Window window;

			friend auto operator<=>(X11PresentationTarget const&, X11PresentationTarget const&) noexcept = default;
		};

		struct WaylandPresentationTarget {
			wl_display* display;
			wl_surface* surface;

			friend auto operator<=>(WaylandPresentationTarget const&, WaylandPresentationTarget const&) noexcept = default;
		};

		using PresentationTarget = std::variant<X11PresentationTarget, WaylandPresentationTarget>;
#elif defined(__ANDROID__)
		using PresentationTarget = ANativeWindow*;
#endif
		struct PresentationTargetHash {
#if defined(_WIN32) || defined(__ANDROID__)
			std::size_t operator()(PresentationTarget target) const noexcept {
				return execution::HashNativePointer(target);
			}
#elif defined(__linux__)
			struct HashTarget {
				std::size_t operator()(X11PresentationTarget const& target) const noexcept {
					auto display = execution::HashNativePointer(target.display);
					auto window = std::hash<Window>{}(target.window);
					return execution::CombineHashes(display, window);
				}

				std::size_t operator()(WaylandPresentationTarget const& target) const noexcept {
					auto display = execution::HashNativePointer(target.display);
					auto surface = execution::HashNativePointer(target.surface);
					return execution::CombineHashes(display, surface);
				}
			};

			std::size_t operator()(PresentationTarget const& target) const noexcept {
				return std::visit(HashTarget{}, target);
			}
#endif
		};

		struct Instance {
#if defined(_WIN32)
			HDC dc;
			HGLRC rc;
#else
#if defined(__linux__)
			struct GLX {
				Display* dpy;
				GLXDrawable drawable;
				GLXContext ctx;
				GLXFBConfig fbconfig;   // added for shared context creation
			};
#endif // defined(__linux__)
			struct EGL {
				EGLDisplay display;
				EGLSurface draw;
				EGLSurface read;
				EGLContext context;
				EGLConfig config;       // added for shared context creation
				void* native_window;   	// wl_egl_window* for Wayland, ANativeWindow* for Android
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
		struct PresentationEntry {
			PresentationTarget target;
#if defined(_WIN32)
			HDC dc = nullptr;

			PresentationEntry() noexcept = default;
			PresentationEntry(PresentationEntry const&) = delete;
			PresentationEntry& operator=(PresentationEntry const&) = delete;
			PresentationEntry(PresentationEntry&& other) noexcept
				: target(std::exchange(other.target, nullptr)),
				dc(std::exchange(other.dc, nullptr)) {

			}
			PresentationEntry& operator=(PresentationEntry&& other) noexcept {
				std::swap(target, other.target);
				std::swap(dc, other.dc);
				return *this;
			}
			~PresentationEntry() noexcept {
				if (target && dc) {
					ReleaseDC(target, dc);
				}
			}
#elif defined(__linux__)
			std::variant<std::monostate, GLXDrawable, EGLSurface> drawable;
#elif defined(__ANDROID__)
			EGLSurface drawable = EGL_NO_SURFACE;
#endif
		};

		using PresentationCache = execution::PresentationCache<
			PresentationTarget,
			PresentationEntry,
			PresentationTargetHash
		>;

		struct LogicalDevice {
			Instance const* instance = nullptr;
			std::shared_ptr<PresentationCache> presentation_cache;
			boost::intrusive_ptr<execution::CompletionService> completion_service;

			explicit operator bool() const noexcept {
				return instance != nullptr;
			}
		};

		struct GLScheduler {
			using CommandPool = execution::ExecutionPool<std::vector<GraphCommand>>;

			struct QueueState {
				struct Submission {
					void* operation_state = nullptr;
					void (*Execute)(void*) = nullptr;
					execution::GraphCompletion completion;
					std::shared_ptr<QueueState> keep_alive;
				};

				LogicalDevice impl;
				std::shared_ptr<CommandPool> command_pool;
				std::deque<Submission> submissions;
				std::mutex mutex;
				std::condition_variable condition;
				bool ready = false;
				std::exception_ptr startup_error;
				std::jthread worker;

				QueueState(
					LogicalDevice const& logical_device_,
					std::shared_ptr<CommandPool> const& command_pool_
				);
				void Enqueue(Submission const& submission);

			private:
				static void Run(std::stop_token stop_token, QueueState* self) noexcept;
			};

			struct QueueCollection {
				std::shared_ptr<QueueState> graphics;
				std::shared_ptr<QueueState> compute;
				std::shared_ptr<QueueState> copy;

				[[nodiscard]] std::shared_ptr<QueueState> const& Select(
					execution::GraphNodeFlagBits capability
				) const;
			};

			QueueCollection queues;
		};

		using Scheduler = std::shared_ptr<GLScheduler>;

		struct GLResource {
			GLuint impl = 0;
			GLenum target = 0u;
			GLenum format = 0u;
			std::size_t size = 0u;
			std::uint32_t width = 0u;
			std::uint32_t height = 0u;
			std::uint32_t depth_or_layers = 0u;
			std::uint32_t mip_level_count = 0u;
			std::uint32_t sample_count = 1u;
			enum class Type : std::uint8_t {
				Buffer,
				Texture
			} type = Type::Buffer;

			GLResource(GLuint impl_, Type type_, std::size_t size_) noexcept
				: impl(impl_), size(size_), type(type_) {}
			GLResource(
				GLuint impl_, GLenum target_, GLenum format_, std::uint32_t width_,
				std::uint32_t height_, std::uint32_t depth_or_layers_,
				std::uint32_t mip_level_count_, std::uint32_t sample_count_
			) noexcept : impl(impl_), target(target_), format(format_), width(width_),
				height(height_), depth_or_layers(depth_or_layers_),
				mip_level_count(mip_level_count_), sample_count(sample_count_),
				type(Type::Texture) {}
			GLResource(GLResource const&) = delete;
			GLResource& operator=(GLResource const&) = delete;
			GLResource(GLResource&& other) noexcept
				: impl(std::exchange(other.impl, 0)), target(other.target), format(other.format),
				size(other.size), width(other.width), height(other.height),
				depth_or_layers(other.depth_or_layers), mip_level_count(other.mip_level_count),
				sample_count(other.sample_count), type(other.type) {}
			GLResource& operator=(GLResource&& other) noexcept {
				std::swap(impl, other.impl);
				std::swap(target, other.target);
				std::swap(format, other.format);
				std::swap(size, other.size);
				std::swap(width, other.width);
				std::swap(height, other.height);
				std::swap(depth_or_layers, other.depth_or_layers);
				std::swap(mip_level_count, other.mip_level_count);
				std::swap(sample_count, other.sample_count);
				type = other.type;
				return *this;
			}
			~GLResource() noexcept {
				switch (type) {
				case fyuu_rhi::opengl::Backend::GLResource::Type::Buffer:
					glDeleteBuffers(1u, &impl);
					break;
				case fyuu_rhi::opengl::Backend::GLResource::Type::Texture:
					glDeleteTextures(1u, &impl);
					break;
				default:
					break;
				}
			}
		};

		using Resource = GLResource;

		struct GLTextureView {
			GLuint impl = 0;
			GLenum target = 0u;
			GLenum format = 0u;
			std::uint32_t base_mip_level = 0u;
			std::uint32_t mip_level_count = 0u;
			std::uint32_t base_array_layer = 0u;
			std::uint32_t array_layer_count = 0u;
			GLTextureView(
				GLuint impl_, GLenum target_, GLenum format_, std::uint32_t base_mip_level_,
				std::uint32_t mip_level_count_, std::uint32_t base_array_layer_,
				std::uint32_t array_layer_count_
			) noexcept : impl(impl_), target(target_), format(format_),
				base_mip_level(base_mip_level_), mip_level_count(mip_level_count_),
				base_array_layer(base_array_layer_), array_layer_count(array_layer_count_) {}
			GLTextureView(GLTextureView const&) = delete;
			GLTextureView& operator=(GLTextureView const&) = delete;
			GLTextureView(GLTextureView&& other) noexcept
				: impl(std::exchange(other.impl, 0)), target(other.target), format(other.format),
				base_mip_level(other.base_mip_level), mip_level_count(other.mip_level_count),
				base_array_layer(other.base_array_layer), array_layer_count(other.array_layer_count) {}
			GLTextureView& operator=(GLTextureView&& other) noexcept {
				std::swap(impl, other.impl);
				std::swap(target, other.target);
				std::swap(format, other.format);
				std::swap(base_mip_level, other.base_mip_level);
				std::swap(mip_level_count, other.mip_level_count);
				std::swap(base_array_layer, other.base_array_layer);
				std::swap(array_layer_count, other.array_layer_count);
				return *this;
			}
			~GLTextureView() noexcept {
				glDeleteTextures(1u, &impl);
			}
		};

		struct GLBufferView {
			struct Range {
				std::size_t offset;
				std::size_t size;
			};

			GLuint impl = 0;
			std::optional<Range> range; // fallback for no GLAD_GL_ARB_texture_buffer_range
			GLBufferView(GLuint impl_, std::optional<Range> range_) noexcept
				: impl(impl_), range(std::move(range_)) {}
			GLBufferView(GLBufferView const&) = delete;
			GLBufferView& operator=(GLBufferView const&) = delete;
			GLBufferView(GLBufferView&& other) noexcept
				: impl(std::exchange(other.impl, 0)), range(std::move(other.range)) {}
			GLBufferView& operator=(GLBufferView&& other) noexcept {
				std::swap(impl, other.impl);
				range = std::move(other.range);
				return *this;
			}
			~GLBufferView() noexcept {
				glDeleteTextures(1u, &impl);
			}
		};

		struct View {
			std::variant<std::monostate, GLTextureView, GLBufferView> impl;
		};

		struct GLSampler {
			GLuint impl = 0;
			explicit GLSampler(GLuint impl_) noexcept : impl(impl_) {}
			GLSampler(GLSampler const&) = delete;
			GLSampler& operator=(GLSampler const&) = delete;
			GLSampler(GLSampler&& other) noexcept : impl(std::exchange(other.impl, 0)) {}
			GLSampler& operator=(GLSampler&& other) noexcept {
				std::swap(impl, other.impl);
				return *this;
			}
			~GLSampler() noexcept {
				if (impl) {
					glDeleteSamplers(1u, &impl);
				}
			}
		};

		using Sampler = GLSampler;

		struct GLPipeline {
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

			GLPipeline(
				GLuint impl_,
				std::vector<VertexBufferLayout> vertex_buffers_,
				std::vector<VertexAttribute> vertex_attributes_,
				PrimitiveState primitive_,
				RasterizationState rasterization_,
				MultisampleState multisample_,
				std::optional<DepthStencilState> depth_stencil_,
				std::vector<ColorTargetState> color_targets_,
				std::vector<PipelineBindingMetadata> bindings_
			) : impl(impl_),
				vertex_buffers(std::move(vertex_buffers_)),
				vertex_attributes(std::move(vertex_attributes_)),
				primitive(primitive_),
				rasterization(rasterization_),
				multisample(multisample_),
				depth_stencil(std::move(depth_stencil_)),
				color_targets(std::move(color_targets_)),
				bindings(std::move(bindings_)) {}

			GLPipeline(GLuint impl_, std::vector<PipelineBindingMetadata> bindings_)
				: impl(impl_), compute(true), bindings(std::move(bindings_)) {

			}

			GLPipeline(GLPipeline const&) = delete;
			GLPipeline& operator=(GLPipeline const&) = delete;
			GLPipeline(GLPipeline&& other) noexcept
				: impl(std::exchange(other.impl, 0)), compute(other.compute),
				vertex_buffers(std::move(other.vertex_buffers)),
				vertex_attributes(std::move(other.vertex_attributes)),
				primitive(other.primitive),
				rasterization(other.rasterization),
				multisample(other.multisample),
				depth_stencil(std::move(other.depth_stencil)),
				color_targets(std::move(other.color_targets)),
				bindings(std::move(other.bindings)) {}
			GLPipeline& operator=(GLPipeline&& other) noexcept {
				std::swap(impl, other.impl);
				std::swap(compute, other.compute);
				vertex_buffers = std::move(other.vertex_buffers);
				vertex_attributes = std::move(other.vertex_attributes);
				primitive = other.primitive;
				rasterization = other.rasterization;
				multisample = other.multisample;
				depth_stencil = std::move(other.depth_stencil);
				color_targets = std::move(other.color_targets);
				bindings = std::move(other.bindings);
				return *this;
			}

			~GLPipeline() noexcept {
				if (impl) {
					glDeleteProgram(impl);
				}
			}
		};

		using Pipeline = GLPipeline;

		using PipelineResourceGroup = NativePipelineResourceGroup<Backend>;
		using CommandGraph = std::shared_ptr<execution::NativeCommandGraph<Backend>>;
		using ExecutableGraph = std::shared_ptr<execution::NativeExecutableGraph<Backend>>;
		struct GraphExecution {
			struct Batch {
				struct BarrierPoint {
					std::size_t command_offset = 0u;
					GLbitfield bits = 0u;
				};

				std::shared_ptr<GLScheduler::QueueState> queue;
				GLScheduler::CommandPool::Lease commands;
				std::vector<BarrierPoint> barriers;
			};

			Scheduler scheduler;
			ExecutableGraph graph;
			std::vector<Batch> batches;
		};

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

		static Scheduler CreateScheduler(LogicalDevice const& ld, SchedulerDescriptor const& descriptor);
		static CommandGraph CreateCommandGraph(
			execution::CommandGraphDescriptor const& descriptor,
			execution::NativeCommandGraphBindings<Backend> const& bindings
		);

		static Resource CreateBuffer(LogicalDevice const& ld, std::size_t size_in_bytes, ResourceFlags const& flags);

		static Resource CreateTexture(LogicalDevice const& ld, std::size_t width, std::size_t height, std::size_t depth_arr_layers, std::size_t mip_lvl_cnt, ResourceFlags const& flags);

		static View CreateTextureView(LogicalDevice const& ld, Resource const& res, std::size_t base_mip_lvl, std::size_t mip_lvl_cnt, std::size_t base_arr_layer, std::size_t arr_layer_cnt, ResourceFlags const& flags);

		static View CreateBufferView(LogicalDevice const& ld, Resource const& res, std::size_t offset, std::size_t range, ResourceFlags const& flags);

		static Sampler CreateSampler(LogicalDevice const& ld, SamplerDescriptor const& descriptor);

		static Pipeline CreateGraphicsPipeline(LogicalDevice const& ld, GraphicsPipelineDescriptor const& descriptor);
		static Pipeline CreateComputePipeline(LogicalDevice const& ld, ComputePipelineDescriptor const& descriptor);

		static PipelineResourceGroup CreatePipelineResourceGroup(
			LogicalDevice const& ld,
			Pipeline const& pipeline,
			std::uint32_t space,
			std::span<NativePipelineResourceBinding<Backend> const> bindings
		);

	};

	Backend::GraphExecution CreateGraphExecution(
		Backend::Scheduler const& scheduler,
		Backend::ExecutableGraph const& graph
	);
	Backend::ExecutableGraph CompileCommandGraph(Backend::CommandGraph const& graph);
	void StartGraphExecution(
		Backend::GraphExecution& graph_execution,
		execution::GraphCompletion const& completion
	);
}
#endif // !defined(__APPLE__)
