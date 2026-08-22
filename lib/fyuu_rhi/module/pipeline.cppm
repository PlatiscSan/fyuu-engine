module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <memory>
#include <utility>
#include <string>
#include <limits>

#include <cstdint>
#include <type_traits>

#include <optional>
#include <variant>

#include <filesystem>

#include <span>
#endif // !defined(__cpp_lib_modules)
export module fyuu_rhi:pipeline;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource;
import :view;
import :sampler;

export namespace fyuu_rhi::pipeline {

	// Pipeline stages are part of a pipeline program description. They do not
	// represent independently creatable RHI objects.
	enum class Stage : std::uint8_t {
		Vertex,
		Fragment,
		TessellationControl,
		Hull = TessellationControl,
		TessellationEvaluation,
		Domain = TessellationEvaluation,
		Geometry,
		Compute,
		Task,
		Amplification = Task,
		Mesh
	};

	struct SlangPipelineProgramDescriptor {
		struct Module {
			std::string name;
			std::string source;
		};

		struct EntryPoint {
			std::string name;
			Stage stage;
		};

		struct Macro {
			std::string name;
			std::string value;
		};

		enum class Optimization : std::uint8_t {
			None,
			Default,
			High,
			Max
		};

		enum class MatrixLayout : std::uint8_t {
			RowMajor,
			ColumnMajor
		};

		std::span<Module const> modules{};
		std::span<EntryPoint const> entry_points{};
		std::span<Macro const> macros{};
		std::span<std::filesystem::path const> include_paths{};
		Optimization optimization = Optimization::Default;
		MatrixLayout matrix_layout = MatrixLayout::RowMajor;
		bool enable_debug_info = false;
	};

	enum class VertexInputRate : std::uint8_t {
		Vertex,
		Instance
	};

	struct VertexBufferLayout {
		std::uint32_t slot = 0;
		std::uint32_t stride = 0;
		VertexInputRate input_rate = VertexInputRate::Vertex;
	};

	struct VertexAttribute {
		std::uint32_t location = 0;
		std::uint32_t slot = 0;
		std::uint32_t offset = 0;

		ResourceFlagBits format = ResourceFlagBits::Count;
	};

	struct VertexState {
		std::span<VertexBufferLayout const> buffers{};
		std::span<VertexAttribute const> attributes{};
	};

	enum class PrimitiveTopology : std::uint8_t {
		PointList,
		LineList,
		LineStrip,
		TriangleList,
		TriangleStrip
	};

	enum class IndexFormat : std::uint8_t {
		Uint16,
		Uint32
	};

	struct PrimitiveState {
		PrimitiveTopology topology = PrimitiveTopology::TriangleList;

		// Required by D3D12 and WebGPU when primitive restart is used with a
		// strip topology. Must be empty for non-strip topologies.
		std::optional<IndexFormat> strip_index_format;
	};

	enum class FrontFace : std::uint8_t {
		CounterClockwise,
		Clockwise
	};

	enum class CullMode : std::uint8_t {
		None,
		Front,
		Back
	};

	struct DepthBiasState {
		std::int32_t constant = 0;
		float slope_scale = 0.0f;
		float clamp = 0.0f;
	};

	struct RasterizationState {
		FrontFace front_face = FrontFace::CounterClockwise;
		CullMode cull_mode = CullMode::None;
		DepthBiasState depth_bias{};
	};

	enum class CompareOperation : std::uint8_t {
		Never,
		Less,
		Equal,
		LessEqual,
		Greater,
		NotEqual,
		GreaterEqual,
		Always
	};

	enum class StencilOperation : std::uint8_t {
		Keep,
		Zero,
		Replace,
		Invert,
		IncrementClamp,
		DecrementClamp,
		IncrementWrap,
		DecrementWrap
	};

	struct StencilFaceState {
		CompareOperation compare = CompareOperation::Always;
		StencilOperation fail_operation = StencilOperation::Keep;
		StencilOperation depth_fail_operation = StencilOperation::Keep;
		StencilOperation pass_operation = StencilOperation::Keep;
	};

	struct DepthStencilState {
		ResourceFlagBits format = ResourceFlagBits::Count;

		bool depth_test_enabled = false;
		bool depth_write_enabled = false;
		CompareOperation depth_compare = CompareOperation::Always;

		bool stencil_enabled = false;
		StencilFaceState stencil_front{};
		StencilFaceState stencil_back{};
		std::uint32_t stencil_read_mask = 0xFFFFFFFFu;
		std::uint32_t stencil_write_mask = 0xFFFFFFFFu;
	};

	enum class BlendFactor : std::uint8_t {
		Zero,
		One,
		SourceColor,
		OneMinusSourceColor,
		SourceAlpha,
		OneMinusSourceAlpha,
		DestinationColor,
		OneMinusDestinationColor,
		DestinationAlpha,
		OneMinusDestinationAlpha,
		SourceAlphaSaturated,
		Constant,
		OneMinusConstant
	};

	enum class BlendOperation : std::uint8_t {
		Add,
		Subtract,
		ReverseSubtract,
		Min,
		Max
	};

	struct BlendComponent {
		BlendFactor source_factor = BlendFactor::One;
		BlendFactor destination_factor = BlendFactor::Zero;
		BlendOperation operation = BlendOperation::Add;
	};

	struct BlendState {
		BlendComponent color{};
		BlendComponent alpha{};
	};

	enum class ColorWriteMask : std::uint8_t {
		None = 0,
		Red = 1u << 0,
		Green = 1u << 1,
		Blue = 1u << 2,
		Alpha = 1u << 3,
		All = 0x0Fu
	};

	constexpr ColorWriteMask operator|(ColorWriteMask lhs, ColorWriteMask rhs) noexcept {
		return static_cast<ColorWriteMask>(
			static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs)
		);
	}

	constexpr ColorWriteMask operator&(ColorWriteMask lhs, ColorWriteMask rhs) noexcept {
		return static_cast<ColorWriteMask>(
			static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs)
		);
	}

	struct ColorTargetState {
		// Required when creating D3D12, Vulkan and WebGPU graphics pipelines.
		// OpenGL retains it for framebuffer compatibility validation.
		ResourceFlagBits format = ResourceFlagBits::Count;
		std::optional<BlendState> blend;
		ColorWriteMask write_mask = ColorWriteMask::All;
	};

	struct MultisampleState {
		ResourceFlagBits sample_count = ResourceFlagBits::Sample1;
		std::uint32_t mask = 0xFFFFFFFFu;
		bool alpha_to_coverage_enabled = false;
	};

	struct GraphicsPipelineDescriptor {
		SlangPipelineProgramDescriptor program{};
		VertexState vertex{};
		PrimitiveState primitive{};
		RasterizationState rasterization{};
		MultisampleState multisample{};
		std::optional<DepthStencilState> depth_stencil;
		std::span<ColorTargetState const> color_targets{};
	};

	struct ComputePipelineDescriptor {
		SlangPipelineProgramDescriptor program{};
	};

	inline constexpr std::size_t PipelineWholeBuffer = std::numeric_limits<std::size_t>::max();

	class BindingValue {
	private:
		struct BufferBinding {
			Resource const* buffer;
			std::size_t offset = 0;
			std::size_t size = PipelineWholeBuffer;
		};

		using ViewBinding = View const*;
		using SamplerBinding = Sampler const*;

		struct CombinedBinding {
			View const* view;
			Sampler const* sampler;
		};

		using Value = std::variant<BufferBinding, ViewBinding, SamplerBinding, CombinedBinding>;
		Value m_value;

		template <class T>
		explicit BindingValue(T&& value) noexcept
			: m_value(std::forward<T>(value)) {

		}

	public:
		static BindingValue FromBuffer(
			Resource const& buffer,
			std::size_t offset = 0,
			std::size_t size = PipelineWholeBuffer
		) noexcept {
			return BindingValue(BufferBinding{ &buffer, offset, size });
		}

		static BindingValue FromView(View const& view) noexcept {
			return BindingValue(ViewBinding{ &view });
		}

		/// Binds a sampler declared independently from its sampled texture.
		static BindingValue FromSampler(Sampler const& sampler) noexcept {
			return BindingValue(SamplerBinding{ &sampler });
		}

		/// Binds one logical combined texture/sampler declaration. Backends whose
		/// native ABI separates them materialize two adjacent native bindings.
		static BindingValue FromCombined(
			View const& view,
			Sampler const& sampler
		) noexcept {
			return BindingValue(CombinedBinding{ &view, &sampler });
		}

		Resource const* Buffer() const noexcept {
			if (auto const* binding = std::get_if<BufferBinding>(&m_value)) {
				return binding->buffer;
			}
			return nullptr;
		}

		View const* BoundView() const noexcept {
			if (auto const* binding = std::get_if<ViewBinding>(&m_value)) {
				return *binding;
			}
			if (auto const* binding = std::get_if<CombinedBinding>(&m_value)) {
				return binding->view;
			}
			return nullptr;
		}

		Sampler const* BoundSampler() const noexcept {
			if (auto const* binding = std::get_if<SamplerBinding>(&m_value)) {
				return *binding;
			}
			if (auto const* binding = std::get_if<CombinedBinding>(&m_value)) {
				return binding->sampler;
			}
			return nullptr;
		}

		std::size_t Offset() const noexcept {
			auto binding = std::get_if<BufferBinding>(&m_value);
			return binding ? binding->offset : 0;
		}

		std::size_t Size() const noexcept {
			auto binding = std::get_if<BufferBinding>(&m_value);
			return binding ? binding->size : PipelineWholeBuffer;
		}
	};

	struct ResourceBinding {
		std::uint32_t slot = 0;
		std::uint32_t array_element = 0;
		BindingValue value;
	};

	struct BindingMetadata {
		ResourceFlags flags;
		std::uint32_t slot = 0;
		std::uint32_t space = 0;
		std::uint32_t count = 1;
	};

} // namespace fyuu_rhi::pipeline

export namespace fyuu_rhi {
	namespace execution {
		template <class NativeCommandSchedulerContext>
		struct ExecuteCommands;
	}

	// A resource group is created for one pipeline space and is immutable after
	// creation. BindingValue borrows the uniquely owned objects only while the
	// group is being created. Each backend implementation must retain whatever
	// native ownership its materialized descriptors require.
	class PipelineResourceGroup {
	public:
		using UniqueHandle = std::unique_ptr<
			struct PipelineResourceGroupImplementation,
			void(*)(struct PipelineResourceGroupImplementation*)
		>;

	private:
		template <class Native>
		friend struct execution::ExecuteCommands;
		UniqueHandle m_impl;

	public:
		PipelineResourceGroup() noexcept
			: m_impl(nullptr, nullptr) {
		}

		explicit PipelineResourceGroup(UniqueHandle&& impl) noexcept
			: m_impl(std::move(impl)) {
		}

		explicit operator bool() const noexcept {
			return static_cast<bool>(m_impl);
		}

		std::uint32_t Space() const noexcept;

	};

	class Pipeline {
	public:
		using UniqueHandle = std::unique_ptr<
			struct PipelineImplementation,
			void(*)(struct PipelineImplementation*)
		>;

	private:
		template <class Native>
		friend struct execution::ExecuteCommands;
		UniqueHandle m_impl;

	public:
		Pipeline() noexcept
			: m_impl(nullptr, nullptr) {
		}

		explicit Pipeline(UniqueHandle&& impl) noexcept
			: m_impl(std::move(impl)) {
		}

		explicit operator bool() const noexcept {
			return static_cast<bool>(m_impl);
		}

		PipelineResourceGroup CreatePipelineResourceGroup(std::uint32_t space, std::span<pipeline::ResourceBinding const> bindings);
	};

}
