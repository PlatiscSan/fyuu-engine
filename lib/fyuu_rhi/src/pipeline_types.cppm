module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <limits>
#endif // !defined(__cpp_lib_modules)

export module fyuu_rhi:pipeline_types;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource_types;

namespace fyuu_rhi::pipeline {

	// Pipeline stages are part of a pipeline program description. They do not
	// represent independently creatable RHI objects.
	export enum class PipelineStage : std::uint8_t {
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

	export struct SlangPipelineProgramDescriptor {
		struct Module {
			std::string name;
			std::string source;
		};

		struct EntryPoint {
			std::string name;
			PipelineStage stage;
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

	export enum class VertexInputRate : std::uint8_t {
		Vertex,
		Instance
	};

	export struct VertexBufferLayout {
		std::uint32_t slot = 0;
		std::uint32_t stride = 0;
		VertexInputRate input_rate = VertexInputRate::Vertex;
	};

	export struct VertexAttribute {
		std::uint32_t location = 0;
		std::uint32_t slot = 0;
		std::uint32_t offset = 0;

		ResourceFlagBits format = ResourceFlagBits::Count;
	};

	export struct VertexState {
		std::span<VertexBufferLayout const> buffers{};
		std::span<VertexAttribute const> attributes{};
	};

	export enum class PrimitiveTopology : std::uint8_t {
		PointList,
		LineList,
		LineStrip,
		TriangleList,
		TriangleStrip
	};

	export enum class IndexFormat : std::uint8_t {
		Uint16,
		Uint32
	};

	export struct PrimitiveState {
		PrimitiveTopology topology = PrimitiveTopology::TriangleList;

		// Required by D3D12 and WebGPU when primitive restart is used with a
		// strip topology. Must be empty for non-strip topologies.
		std::optional<IndexFormat> strip_index_format;
	};

	export enum class FrontFace : std::uint8_t {
		CounterClockwise,
		Clockwise
	};

	export enum class CullMode : std::uint8_t {
		None,
		Front,
		Back
	};

	export struct DepthBiasState {
		std::int32_t constant = 0;
		float slope_scale = 0.0f;
		float clamp = 0.0f;
	};

	export struct RasterizationState {
		FrontFace front_face = FrontFace::CounterClockwise;
		CullMode cull_mode = CullMode::None;
		DepthBiasState depth_bias{};
	};

	export enum class CompareOperation : std::uint8_t {
		Never,
		Less,
		Equal,
		LessEqual,
		Greater,
		NotEqual,
		GreaterEqual,
		Always
	};

	export enum class StencilOperation : std::uint8_t {
		Keep,
		Zero,
		Replace,
		Invert,
		IncrementClamp,
		DecrementClamp,
		IncrementWrap,
		DecrementWrap
	};

	export struct StencilFaceState {
		CompareOperation compare = CompareOperation::Always;
		StencilOperation fail_operation = StencilOperation::Keep;
		StencilOperation depth_fail_operation = StencilOperation::Keep;
		StencilOperation pass_operation = StencilOperation::Keep;
	};

	export struct DepthStencilState {
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

	export enum class BlendFactor : std::uint8_t {
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

	export enum class BlendOperation : std::uint8_t {
		Add,
		Subtract,
		ReverseSubtract,
		Min,
		Max
	};

	export struct BlendComponent {
		BlendFactor source_factor = BlendFactor::One;
		BlendFactor destination_factor = BlendFactor::Zero;
		BlendOperation operation = BlendOperation::Add;
	};

	export struct BlendState {
		BlendComponent color{};
		BlendComponent alpha{};
	};

	export enum class ColorWriteMask : std::uint8_t {
		None = 0,
		Red = 1u << 0,
		Green = 1u << 1,
		Blue = 1u << 2,
		Alpha = 1u << 3,
		All = 0x0Fu
	};

	export constexpr ColorWriteMask operator|(ColorWriteMask lhs, ColorWriteMask rhs) noexcept {
		return static_cast<ColorWriteMask>(
			static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs)
		);
	}

	export constexpr ColorWriteMask operator&(ColorWriteMask lhs, ColorWriteMask rhs) noexcept {
		return static_cast<ColorWriteMask>(
			static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs)
		);
	}

	export struct ColorTargetState {
		// Required when creating D3D12, Vulkan and WebGPU graphics pipelines.
		// OpenGL retains it for framebuffer compatibility validation.
		ResourceFlagBits format = ResourceFlagBits::Count;
		std::optional<BlendState> blend;
		ColorWriteMask write_mask = ColorWriteMask::All;
	};

	export struct MultisampleState {
		ResourceFlagBits sample_count = ResourceFlagBits::Sample1;
		std::uint32_t mask = 0xFFFFFFFFu;
		bool alpha_to_coverage_enabled = false;
	};

	export struct GraphicsPipelineDescriptor {
		SlangPipelineProgramDescriptor program{};
		VertexState vertex{};
		PrimitiveState primitive{};
		RasterizationState rasterization{};
		MultisampleState multisample{};
		std::optional<DepthStencilState> depth_stencil;
		std::span<ColorTargetState const> color_targets{};
	};

	export struct ComputePipelineDescriptor {
		SlangPipelineProgramDescriptor program{};
	};

	export inline constexpr std::size_t PipelineWholeBuffer = std::numeric_limits<std::size_t>::max();

}
