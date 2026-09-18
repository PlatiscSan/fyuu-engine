module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <exception>
#include <memory>
#include <stdexcept>

#include <deque>
#include <vector>

#include <algorithm>

#include <cstdint>
#include <unordered_map>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <optional>
#include <variant>

#include <span>
#include <stop_token>
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
		/// Wakes the completion wait. The GL thread owns completion, so a waiting thread
		/// cannot poll the fence itself and must be notified instead.
		std::condition_variable condition;
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
		/// Read framebuffer reused by Present for its blit source, created on first use and
		/// released with the rest of the scheduler objects.
		GLuint present_read_framebuffer = 0u;
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

	struct SyncDeleter {
		void operator()(GLsync sync) const noexcept;
	};

	using ManagedSync = boost::scope::unique_resource<GLsync, SyncDeleter>;

	struct Resource {
		boost::scope::unique_resource<GLuint, ResourceDeleter> impl;
		ManagedSync creation_sync;
		GLenum target;
		GLenum format;
		ResourceType type;

		Resource(GLuint buffer, GLsync creation_sync) noexcept;

		Resource(
			GLuint texture,
			GLsync creation_sync,
			GLenum target,
			GLenum format
		) noexcept;

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

	/// One logical binding of the pipeline ABI: the pair (space, slot), plus the
	/// number of consecutive GL units a resource array occupies.
	struct BindingKey {
		std::uint32_t space;
		std::uint32_t slot;
		std::uint32_t count;
	};

	/// The GL binding unit one logical binding is addressed through.
	struct BindingUnit {
		std::uint32_t space;
		std::uint32_t slot;
		std::uint32_t unit;
	};

	/// The complete GL binding-unit table of one pipeline.
	struct BindingUnits {
		/// One entry per reflected resource binding, in reflection order.
		std::vector<BindingUnit> bindings;
		/// One entry per pipeline-constant range, in reflection order.
		std::vector<BindingUnit> constants;
	};

	/// Assigns every logical binding a unique GL binding unit.
	///
	/// OpenGL numbers uniform buffers, storage buffers, sampled textures, images,
	/// samplers and pipeline-constant blocks in one flat namespace of binding
	/// points, and the generated GLSL can only name that number
	/// (`layout(binding = N)`, or the matching texture/image unit). The pipeline
	/// ABI identifies a binding by (space, slot), which GLSL cannot express, so
	/// using the raw slot as the unit aliases every binding that shares a slot
	/// across spaces - including a pipeline-constant block, which owns a slot
	/// in every space it may be declared against.
	///
	/// This is the single place the (space, slot) -> unit mapping is computed.
	/// Every key takes the next free unit in order and reserves `count`
	/// consecutive units (one per array element), so array bindings keep their
	/// GLSL array-of-units layout and no two logical bindings collide, whatever
	/// their kind. Pipeline-constant ranges are allocated after the resource
	/// bindings so they can never land on a resource binding's unit.
	BindingUnits AssignBindingUnits(
		std::span<BindingKey const> bindings,
		std::span<BindingKey const> constants
	) {
		BindingUnits result;
		result.bindings.reserve(bindings.size());
		result.constants.reserve(constants.size());
		std::uint32_t next = 0u;
		for (auto const& key : bindings) {
			if (key.count == 0u) {
				throw std::invalid_argument(
					"An OpenGL binding must reserve at least one unit"
				);
			}
			result.bindings.push_back(BindingUnit{ key.space, key.slot, next });
			next += key.count;
		}
		for (auto const& key : constants) {
			result.constants.push_back(BindingUnit{ key.space, key.slot, next });
			++next;
		}
		return result;
	}

	/// Looks up the unit assigned to one logical binding, or null when the
	/// pipeline never assigned one (e.g. the separate halves of a combined image
	/// sampler, which GLSL never emits as resources of their own).
	BindingUnit const* FindBindingUnit(
		std::span<BindingUnit const> units,
		std::uint32_t space,
		std::uint32_t slot
	) noexcept {
		auto found = std::ranges::find_if(
			units,
			[space, slot](auto const& candidate) {
				return candidate.space == space && candidate.slot == slot;
			}
		);
		return found == units.end() ? nullptr : &*found;
	}

	/// Resolves one logical binding to the GL unit assigned to it; throws when
	/// the pipeline assigned none, since every binding the pipeline reflects and
	/// the resource groups are built from must have one.
	std::uint32_t BindingUnitOf(
		std::span<BindingUnit const> units,
		std::uint32_t space,
		std::uint32_t slot
	) {
		auto found = FindBindingUnit(units, space, slot);
		if (!found) {
			throw std::invalid_argument(
				"An OpenGL pipeline has no binding unit for the requested space and slot"
			);
		}
		return found->unit;
	}

	/// Records that one logical binding is addressed through another's unit.
	/// GLSL combines a texture and a separately declared sampler into one sampled
	/// image, so the shader's binding, the bound texture unit and the bound
	/// sampler unit are all the same number and both halves must resolve to it.
	void ShareBindingUnit(
		std::vector<BindingUnit>& units,
		std::uint32_t space,
		std::uint32_t slot,
		std::uint32_t source_space,
		std::uint32_t source_slot
	) {
		auto unit = BindingUnitOf(units, source_space, source_slot);
		auto existing = std::ranges::find_if(
			units,
			[space, slot](auto const& candidate) {
				return candidate.space == space && candidate.slot == slot;
			}
		);
		if (existing != units.end()) {
			existing->unit = unit;
			return;
		}
		units.push_back(BindingUnit{ space, slot, unit });
	}

	struct Pipeline {
		/// One texture/sampler pair the GLSL combines into a single sampled image,
		/// in the pipeline's logical (space, slot) coordinates. Only used while the
		/// pipeline's binding units are being assigned.
		struct CombinedSampler {
			std::uint32_t texture_slot;
			std::uint32_t texture_space;
			std::uint32_t sampler_slot;
			std::uint32_t sampler_space;
		};

		struct ConstantRange {
			std::uint32_t abi_slot;
			std::uint32_t abi_space;
			/// GL binding unit emulating the range as a uniform buffer.
			std::uint32_t unit;
			std::uint32_t offset;
			std::uint32_t size;
			GLenum target;
		};

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
		/// Logical (space, slot) -> GL unit map for `bindings`; the generated
		/// GLSL and the scheduler both resolve through it.
		std::vector<BindingUnit> binding_units;
		std::vector<ConstantRange> constant_ranges;
	};

	struct PipelineResourceGroup {
		struct Binding {
			std::uint32_t slot;
			std::uint32_t array_element;
			GLuint buffer;
			std::size_t buffer_offset;
			std::size_t buffer_size;
			std::size_t buffer_capacity;
			bool dynamic_buffer;
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
			GLsync creation_sync;
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
			std::vector<BindingUnit> binding_units;
			std::vector<Pipeline::ConstantRange> constant_ranges;
		};

		struct GroupBindingSnapshot {
			std::uint32_t slot;
			std::uint32_t array_element;
			GLuint buffer;
			std::size_t buffer_offset;
			std::size_t buffer_size;
			std::size_t buffer_capacity;
			bool dynamic_buffer;
			GLuint view;
			GLenum view_target;
			GLenum view_format;
			GLuint sampler;
		};

		struct GroupSnapshot {
			std::uint32_t space;
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
