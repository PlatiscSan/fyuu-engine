// Draws things other than a triangle, and checks the pixels that come back.
//
// HelloTriangle only proves a graph completed, and DrawProbe shades one triangle with one
// attribute layout. Neither says anything about the primitives, vertex/index paths, attachment
// state, or sampling that a renderer actually uses, so this probe covers them one mode at a time,
// each with a target whose expected contents can be stated without knowing how a backend
// rasterizes:
//
//   lines             LineList, a horizontal 1-pixel line. Only the rows it can cover are allowed
//                     to carry lit pixels, and the endpoints must be reached.
//   strip             TriangleStrip, four vertices forming one quad. The lit region must be the
//                     quad, with a tolerance that absorbs edge rules.
//   indexed           The same quad through an index buffer and DrawIndexed with a non-zero base
//                     vertex, so a backend that ignores the base vertex draws the placeholder
//                     triangle instead and fails.
//   scissor           A full-target quad clipped by Scissor. Nothing outside the scissor may be
//                     touched.
//   depth             Two overlapping quads with a depth attachment and Less test: the nearer one
//                     wins in the overlap, the farther one survives outside it. This is the only
//                     mode that creates a depth target at all.
//   depth_only        The same depth test on a depth-only D32Float attachment instead of the
//                     stencil-capable D24S8 pair. A backend that hands the API a stencil
//                     load/store pair for every depth attachment cannot create this one, which is
//                     how the WebGPU backend used to reject it before it learned to check for a
//                     stencil aspect.
//   blend             A half-transparent red quad over a blue clear, with SourceAlpha blending:
//                     the centre pixel must be the blend, not either input.
//   texture           A 2x2 texture uploaded through a staging buffer and sampled with nearest
//                     filtering. All four texel colours must appear in the four quadrants; which
//                     quadrant holds which colour is deliberately not asserted, because the
//                     vertical texture origin is not part of the RHI's contract. The upload is a
//                     HostVisible CopySrc staging buffer, the pattern the engine's render cache
//                     uses, which is what a WebGPU host-visible buffer created mapped used to make
//                     illegal to submit.
//   texture_separate  The same texture with separately declared texture and sampler bindings, which
//                     is the shape D3D12 and WebGPU use in the engine. The combined shape is the
//                     one Vulkan and Metal use, and it is the mode above.
//
// PointList is deliberately absent: Vulkan leaves gl_PointSize undefined when the shader does not
// write it, so a point's coverage is not something a portable pixel assertion can rely on.
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <source_location>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_rhi;

namespace {

	using namespace std::chrono_literals;
	using Resources = fyuu_rhi::execution::CommandGraphResources;

	constexpr std::uint32_t TargetWidth = 128u;
	constexpr std::uint32_t TargetHeight = 128u;
	/// Copy footprints use 256-byte rows on D3D12 and WebGPU, and the other backends accept it, but
	/// a row also has to hold the copy's own width: 128 texels of R8G8B8A8 are 512 bytes, which is
	/// what OpenGL's glGetTextureSubImage requires of PACK_ROW_LENGTH and what keeps a readback from
	/// running past the end of its buffer.
	constexpr std::uint32_t RowPitch = 512u;
	/// Half a target, in normalised device coordinates.
	constexpr float QuadHalf = 0.5f;

	class ProbeLogSink final : public fyuu_rhi::log::Sink {
	private:
		std::atomic_bool m_has_error = false;

	public:
		void Write(
			fyuu_rhi::log::Level level,
			std::string_view message,
			std::source_location const& location
		) noexcept override {
			if (
				level == fyuu_rhi::log::Level::Error ||
				level == fyuu_rhi::log::Level::Fatal
			) {
				m_has_error.store(true, std::memory_order_relaxed);
			}
			std::clog
				<< '[' << static_cast<int>(level) << "] "
				<< location.file_name() << ':' << location.line() << ": "
				<< message << '\n';
		}

		bool HasError() const noexcept {
			return m_has_error.load(std::memory_order_relaxed);
		}
	};

	/// Immortal, because the RHI context outlives the run and may log while tearing down.
	ProbeLogSink& LogSink() {
		static ProbeLogSink sink;
		return sink;
	}

	struct State {
		std::mutex mutex;
		std::condition_variable condition;
		std::optional<Resources> resources;
		std::exception_ptr error;
		bool completed = false;
		bool stopped = false;
	};

	struct Receiver {
		std::shared_ptr<State> state;

		struct Environment {
		};

		Environment get_env() const noexcept {
			return {};
		}

		void RecoverBindings(Resources&& resources) noexcept {
			std::lock_guard lock(state->mutex);
			state->resources.emplace(std::move(resources));
		}

		void set_value(Resources&& resources) && noexcept {
			{
				std::lock_guard lock(state->mutex);
				state->resources.emplace(std::move(resources));
				state->completed = true;
			}
			state->condition.notify_all();
		}

		void set_error(std::exception_ptr error) && noexcept {
			{
				std::lock_guard lock(state->mutex);
				state->error = std::move(error);
				state->completed = true;
			}
			state->condition.notify_all();
		}

		void set_stopped() && noexcept {
			{
				std::lock_guard lock(state->mutex);
				state->stopped = true;
				state->completed = true;
			}
			state->condition.notify_all();
		}
	};

	Resources Wait(std::shared_ptr<State> const& state, std::string_view mode) {
		std::unique_lock lock(state->mutex);
		if (!state->condition.wait_for(
			lock,
			30s,
			[&state]() {
				return state->completed;
			}
		)) {
			throw std::runtime_error(std::format("{}: timed out waiting for the graph", mode));
		}
		if (state->error) {
			std::rethrow_exception(state->error);
		}
		if (state->stopped || !state->resources) {
			throw std::runtime_error(std::format("{}: the graph was stopped", mode));
		}
		return std::move(*state->resources);
	}

	// ---------------------------------------------------------------------------------------
	// Pixels
	// ---------------------------------------------------------------------------------------

	std::array<int, 3> ReadPixel(
		std::span<std::byte const> bytes,
		std::uint32_t column,
		std::uint32_t row
	) {
		auto const offset =
			static_cast<std::size_t>(row) * RowPitch + static_cast<std::size_t>(column) * 4u;
		return {
			std::to_integer<int>(bytes[offset]),
			std::to_integer<int>(bytes[offset + 1u]),
			std::to_integer<int>(bytes[offset + 2u])
		};
	}

	bool IsLit(std::array<int, 3> pixel) {
		return pixel[0] + pixel[1] + pixel[2] > 30;
	}

	/// Lit pixels inside [column_begin, column_end) x [row_begin, row_end).
	std::size_t CountLit(
		std::span<std::byte const> bytes,
		std::uint32_t column_begin,
		std::uint32_t row_begin,
		std::uint32_t column_end,
		std::uint32_t row_end
	) {
		std::size_t count = 0u;
		for (auto row = row_begin; row < row_end; ++row) {
			for (auto column = column_begin; column < column_end; ++column) {
				if (IsLit(ReadPixel(bytes, column, row))) {
					++count;
				}
			}
		}
		return count;
	}

	/// Rows holding at least one lit pixel, as [first, last]; last < first when none is lit.
	std::pair<std::uint32_t, std::uint32_t> LitRowRange(std::span<std::byte const> bytes) {
		std::uint32_t first = TargetHeight;
		std::uint32_t last = 0u;
		for (std::uint32_t row = 0u; row < TargetHeight; ++row) {
			for (std::uint32_t column = 0u; column < TargetWidth; ++column) {
				if (IsLit(ReadPixel(bytes, column, row))) {
					first = (std::min)(first, row);
					last = (std::max)(last, row);
					break;
				}
			}
		}
		return { first, last };
	}

	bool IsNear(std::array<int, 3> pixel, std::array<int, 3> expected, int tolerance) {
		for (std::size_t channel = 0u; channel < 3u; ++channel) {
			if (std::abs(pixel[channel] - expected[channel]) > tolerance) {
				return false;
			}
		}
		return true;
	}

	/// Prints the pixel a failure is about, so the report says what was drawn instead.
	std::string Describe(std::string_view mode, std::string_view what, std::array<int, 3> pixel) {
		return std::format(
			"{}: {} was ({}, {}, {})",
			mode,
			what,
			pixel[0],
			pixel[1],
			pixel[2]
		);
	}

	// ---------------------------------------------------------------------------------------
	// Geometry
	// ---------------------------------------------------------------------------------------

	constexpr std::array<float, 4> Red{ 1.0f, 0.0f, 0.0f, 1.0f };
	constexpr std::array<float, 4> Green{ 0.0f, 1.0f, 0.0f, 1.0f };
	constexpr std::array<float, 4> HalfRed{ 1.0f, 0.0f, 0.0f, 0.5f };

	/// The same colours after the 8-bit unorm round trip, for the pixel comparisons.
	constexpr std::array<int, 3> Red8{ 255, 0, 0 };
	constexpr std::array<int, 3> Green8{ 0, 255, 0 };
	constexpr std::array<int, 3> Blue8{ 0, 0, 255 };
	constexpr std::array<int, 3> White8{ 255, 255, 255 };
	/// Half-transparent red over an opaque blue clear: 0.5 * red + 0.5 * blue per channel.
	constexpr std::array<int, 3> Blend8{ 128, 0, 128 };

	/// One vertex: a vec4 position, then either colour.rgba or the uv pair plus padding.
	constexpr std::uint32_t VertexStride = 8u * sizeof(float);

	/// Well outside the clip volume, so a full-target quad covers every pixel as an interior
	/// sample instead of depending on how a backend resolves a triangle edge lying exactly on the
	/// clip boundary. A quad that stops at +/-1 has its edge on the boundary pixel's own edge.
	constexpr float FullExtent = 1.5f;

	void PushVertex(std::vector<float>& vertices, float x, float y, float z, std::array<float, 4> attribute) {
		vertices.insert(
			vertices.end(),
			{ x, y, z, 1.0f, attribute[0], attribute[1], attribute[2], attribute[3] }
		);
	}

	/// Two triangles covering [centre +/- half] with one attribute for every vertex.
	void PushQuad(
		std::vector<float>& vertices,
		float centre_x,
		float centre_y,
		float half_x,
		float half_y,
		float z,
		std::array<float, 4> attribute
	) {
		PushVertex(vertices, centre_x - half_x, centre_y - half_y, z, attribute);
		PushVertex(vertices, centre_x + half_x, centre_y - half_y, z, attribute);
		PushVertex(vertices, centre_x - half_x, centre_y + half_y, z, attribute);
		PushVertex(vertices, centre_x + half_x, centre_y - half_y, z, attribute);
		PushVertex(vertices, centre_x + half_x, centre_y + half_y, z, attribute);
		PushVertex(vertices, centre_x - half_x, centre_y + half_y, z, attribute);
	}

	/// The full-target quad of the texture modes, with uv (0, 0) at the first corner.
	std::vector<float> FullTargetQuad() {
		std::vector<float> vertices;
		auto const push = [&vertices](float x, float y, float u, float v) {
			PushVertex(vertices, x, y, 0.0f, { u, v, 0.0f, 0.0f });
		};
		push(-FullExtent, -FullExtent, 0.0f, 0.0f);
		push(FullExtent, -FullExtent, 1.0f, 0.0f);
		push(-FullExtent, FullExtent, 0.0f, 1.0f);
		push(FullExtent, -FullExtent, 1.0f, 0.0f);
		push(FullExtent, FullExtent, 1.0f, 1.0f);
		push(-FullExtent, FullExtent, 0.0f, 1.0f);
		return vertices;
	}

	// ---------------------------------------------------------------------------------------
	// Shaders
	// ---------------------------------------------------------------------------------------

	/// Vertex colour straight to the target: the geometry modes only differ in their primitives,
	/// their attachment state, and what the vertex data carries as the second attribute.
	constexpr char SolidShader[] = R"(
		struct VertexOutput {
			float4 position : SV_Position;
			float4 color : COLOR0;
		};
		[shader("vertex")]
		VertexOutput vertex_main(float4 position : POSITION, float4 color : COLOR0) {
			VertexOutput output;
			output.position = position;
			output.color = color;
			return output;
		}
		[shader("fragment")]
		float4 fragment_main(VertexOutput input) : SV_Target0 { return input.color; }
	)";

	// The uv pair rides in the second attribute's first two components rather than in a TEXCOORD
	// semantic of its own, so every mode declares the same two vec4 attributes. POSITION plus
	// COLOR0 is the location pair the other probes already prove survives Slang translation, and
	// the texture modes reuse it instead of depending on where an unproven semantic lands.
	constexpr char TexturedShader[] = R"(
		[[vk::binding(1, 1)]] Sampler2D color_texture : register(t1, space1);
		struct VertexOutput {
			float4 position : SV_Position;
			float2 uv : COLOR0;
		};
		[shader("vertex")]
		VertexOutput vertex_main(float4 position : POSITION, float4 attribute : COLOR0) {
			VertexOutput output;
			output.position = position;
			output.uv = attribute.xy;
			return output;
		}
		[shader("fragment")]
		float4 fragment_main(VertexOutput input) : SV_Target0 {
			return color_texture.Sample(input.uv);
		}
	)";

	constexpr char TexturedSeparateShader[] = R"(
		[[vk::binding(1, 1)]] Texture2D color_texture : register(t1, space1);
		[[vk::binding(2, 1)]] SamplerState color_sampler : register(s2, space1);
		struct VertexOutput {
			float4 position : SV_Position;
			float2 uv : COLOR0;
		};
		[shader("vertex")]
		VertexOutput vertex_main(float4 position : POSITION, float4 attribute : COLOR0) {
			VertexOutput output;
			output.position = position;
			output.uv = attribute.xy;
			return output;
		}
		[shader("fragment")]
		float4 fragment_main(VertexOutput input) : SV_Target0 {
			return color_texture.Sample(color_sampler, input.uv);
		}
	)";

	// ---------------------------------------------------------------------------------------
	// Resource flags
	// ---------------------------------------------------------------------------------------

	fyuu_rhi::ResourceFlags ColorTargetFlags() {
		using Bits = fyuu_rhi::ResourceFlagBits;
		fyuu_rhi::ResourceFlags flags;
		flags.Set(Bits::DeviceLocal);
		flags.Set(Bits::CopySRC);
		flags.Set(Bits::RenderAttachment);
		flags.Set(Bits::Texture2D);
		flags.Set(Bits::TextureView2D);
		flags.Set(Bits::TextureViewAspectAll);
		flags.Set(Bits::R8G8B8A8Unorm);
		flags.Set(Bits::Sample1);
		return flags;
	}

	/// @param stencil_aspect Whether to request the stencil-capable format the pipeline and the
	///        engine use, or a depth-only one. The depth-only format is the sharper test: Dawn
	///        rejects a stencil load/store pair on an attachment with no stencil aspect, so a
	///        backend that sets those unconditionally fails only on this one.
	fyuu_rhi::ResourceFlags DepthTargetFlags(bool stencil_aspect) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		fyuu_rhi::ResourceFlags flags;
		flags.Set(Bits::DeviceLocal);
		flags.Set(Bits::RenderAttachment);
		flags.Set(Bits::Texture2D);
		flags.Set(Bits::TextureView2D);
		flags.Set(Bits::TextureViewAspectAll);
		flags.Set(stencil_aspect ? Bits::D24UnormS8Uint : Bits::D32Float);
		flags.Set(Bits::Sample1);
		return flags;
	}

	fyuu_rhi::ResourceFlags SampledTextureFlags() {
		using Bits = fyuu_rhi::ResourceFlagBits;
		fyuu_rhi::ResourceFlags flags;
		flags.Set(Bits::DeviceLocal);
		flags.Set(Bits::CopyDST);
		flags.Set(Bits::TextureBinding);
		flags.Set(Bits::Texture2D);
		flags.Set(Bits::TextureView2D);
		flags.Set(Bits::TextureViewAspectAll);
		flags.Set(Bits::R8G8B8A8Unorm);
		flags.Set(Bits::Sample1);
		return flags;
	}

	fyuu_rhi::ResourceFlags VertexBufferFlags() {
		using Bits = fyuu_rhi::ResourceFlagBits;
		fyuu_rhi::ResourceFlags flags;
		flags.Set(Bits::HostVisible);
		flags.Set(Bits::VertexBuffer);
		return flags;
	}

	fyuu_rhi::ResourceFlags IndexBufferFlags() {
		using Bits = fyuu_rhi::ResourceFlagBits;
		fyuu_rhi::ResourceFlags flags;
		flags.Set(Bits::HostVisible);
		flags.Set(Bits::IndexBuffer);
		return flags;
	}

	fyuu_rhi::ResourceFlags StagingFlags() {
		using Bits = fyuu_rhi::ResourceFlagBits;
		fyuu_rhi::ResourceFlags flags;
		flags.Set(Bits::HostVisible);
		flags.Set(Bits::CopySRC);
		return flags;
	}

	fyuu_rhi::ResourceFlags ReadbackFlags() {
		using Bits = fyuu_rhi::ResourceFlagBits;
		fyuu_rhi::ResourceFlags flags;
		flags.Set(Bits::DeviceReadback);
		flags.Set(Bits::CopyDST);
		return flags;
	}

	// ---------------------------------------------------------------------------------------
	// Scenarios
	// ---------------------------------------------------------------------------------------

	/// One non-indexed draw: how many vertices, and where they start in the vertex buffer.
	struct DrawCall {
		std::uint32_t vertex_count = 0u;
		std::uint32_t first_vertex = 0u;
	};

	/// Everything that differs between modes, so the run itself stays one linear sequence.
	struct Scenario {
		fyuu_rhi::pipeline::PrimitiveTopology topology =
			fyuu_rhi::pipeline::PrimitiveTopology::TriangleList;
		std::vector<float> vertices;
		std::vector<std::uint16_t> indices;
		std::vector<DrawCall> draws;
		bool indexed = false;
		std::int32_t vertex_offset = 0;
		bool textured = false;
		bool separate_texture = false;
		bool needs_depth = false;
		/// Depth-only attachment and pipeline, as opposed to the stencil-capable pair.
		bool depth_only = false;
		std::optional<fyuu_rhi::pipeline::BlendState> blend;
		/// Black everywhere except the blend mode, which needs a destination colour to mix with.
		std::array<float, 4> clear{ 0.0f, 0.0f, 0.0f, 1.0f };
		std::uint32_t scissor_width = TargetWidth;
	};

	/// The 2x2 upload source: one texel per quadrant, on a 256-byte row pitch, which is the
	/// alignment D3D12 and WebGPU require and the other backends accept. Sampling is nearest and
	/// the quad maps the whole target, so each texel owns a 64x64 quadrant and nothing is filtered.
	std::vector<std::byte> TexelData() {
		std::vector<std::byte> texels(RowPitch * 2u, std::byte{ 0u });
		auto const put = [&texels](std::size_t column, std::size_t row, std::array<int, 4> rgba) {
			auto const offset = row * RowPitch + column * 4u;
			for (std::size_t channel = 0u; channel < 4u; ++channel) {
				texels[offset + channel] = static_cast<std::byte>(rgba[channel]);
			}
		};
		put(0u, 0u, { 255, 0, 0, 255 });
		put(1u, 0u, { 0, 255, 0, 255 });
		put(0u, 1u, { 0, 0, 255, 255 });
		put(1u, 1u, { 255, 255, 255, 255 });
		return texels;
	}

	Scenario MakeScenario(std::string_view mode) {
		Scenario scenario;
		if (mode == "lines") {
			// The centre of row 64, in normalised device coordinates: for row r it is
			// 1 - (2r + 1) / height. A line drawn through a row centre covers that row.
			constexpr auto RowCentre =
				1.0f - (2.0f * 64.0f + 1.0f) / static_cast<float>(TargetHeight);
			PushVertex(scenario.vertices, -0.5f, RowCentre, 0.5f, Red);
			PushVertex(scenario.vertices, 0.5f, RowCentre, 0.5f, Red);
			scenario.topology = fyuu_rhi::pipeline::PrimitiveTopology::LineList;
			scenario.draws.push_back({ 2u, 0u });
		}
		else if (mode == "strip") {
			// Strip order rather than the two-triangle order PushQuad emits: a six-vertex strip
			// would be four triangles, not one quad.
			PushVertex(scenario.vertices, -QuadHalf, -QuadHalf, 0.5f, Red);
			PushVertex(scenario.vertices, QuadHalf, -QuadHalf, 0.5f, Red);
			PushVertex(scenario.vertices, -QuadHalf, QuadHalf, 0.5f, Red);
			PushVertex(scenario.vertices, QuadHalf, QuadHalf, 0.5f, Red);
			scenario.topology = fyuu_rhi::pipeline::PrimitiveTopology::TriangleStrip;
			scenario.draws.push_back({ 4u, 0u });
		}
		else if (mode == "indexed") {
			// Four placeholder vertices first, all outside the clip volume, then the quad. The
			// indices address the quad through a base vertex of 4, so a backend that drops
			// DrawIndexed's vertex_offset rasterizes the placeholders and lights nothing.
			for (auto index = 0; index < 4; ++index) {
				PushVertex(scenario.vertices, 4.0f, 4.0f, 0.5f, Red);
			}
			PushVertex(scenario.vertices, -QuadHalf, -QuadHalf, 0.5f, Red);
			PushVertex(scenario.vertices, QuadHalf, -QuadHalf, 0.5f, Red);
			PushVertex(scenario.vertices, -QuadHalf, QuadHalf, 0.5f, Red);
			PushVertex(scenario.vertices, QuadHalf, QuadHalf, 0.5f, Red);
			// Indexed off the quad's four corners, the second triangle has to reuse the corner the
			// first one left out: (0, 1, 2) is the half below the diagonal from the bottom-left
			// corner to the top-right one, (1, 2, 3) the half above it, and together they tile the
			// quad. Indexing {0, 2, 3} instead leaves the third corner's triangle uncovered, which
			// is a quad-shaped hole on the diagonal and was this probe's own first bug.
			scenario.indices = { 0u, 1u, 2u, 1u, 2u, 3u };
			scenario.indexed = true;
			scenario.vertex_offset = 4;
			scenario.draws.push_back({ 6u, 0u });
		}
		else if (mode == "scissor") {
			PushQuad(scenario.vertices, 0.0f, 0.0f, FullExtent, FullExtent, 0.5f, Red);
			// Half the target: the left columns must be lit and the right ones untouched.
			scenario.scissor_width = TargetWidth / 2u;
			scenario.draws.push_back({ 6u, 0u });
		}
		else if (mode == "depth" || mode == "depth_only") {
			// The near quad is drawn first, so the result only stays red if the test rejects the
			// farther green quad in the overlap. A backend that ignores the compare function, the
			// test, or depth writes overwrites red with green and fails here.
			PushQuad(scenario.vertices, 0.0f, 0.0f, 0.25f, 0.25f, 0.3f, Red);
			PushQuad(scenario.vertices, 0.0f, 0.0f, 0.75f, 0.75f, 0.9f, Green);
			scenario.needs_depth = true;
			scenario.depth_only = mode == "depth_only";
			scenario.draws.push_back({ 6u, 0u });
			scenario.draws.push_back({ 6u, 6u });
		}
		else if (mode == "blend") {
			PushQuad(scenario.vertices, 0.0f, 0.0f, FullExtent, FullExtent, 0.5f, HalfRed);
			scenario.blend = fyuu_rhi::pipeline::BlendState{
				.color = {
					.source_factor = fyuu_rhi::pipeline::BlendFactor::SourceAlpha,
					.destination_factor = fyuu_rhi::pipeline::BlendFactor::OneMinusSourceAlpha,
					.operation = fyuu_rhi::pipeline::BlendOperation::Add
				},
				.alpha = {
					.source_factor = fyuu_rhi::pipeline::BlendFactor::One,
					.destination_factor = fyuu_rhi::pipeline::BlendFactor::OneMinusSourceAlpha,
					.operation = fyuu_rhi::pipeline::BlendOperation::Add
				}
			};
			// Blue, so the blend has a destination no other mode produces.
			scenario.clear = { 0.0f, 0.0f, 1.0f, 1.0f };
			scenario.draws.push_back({ 6u, 0u });
		}
		else if (mode == "texture" || mode == "texture_separate") {
			scenario.vertices = FullTargetQuad();
			scenario.textured = true;
			scenario.separate_texture = mode == "texture_separate";
			scenario.draws.push_back({ 6u, 0u });
		}
		else {
			throw std::invalid_argument(std::format("unknown mode '{}'", mode));
		}
		return scenario;
	}

	// ---------------------------------------------------------------------------------------
	// One mode
	// ---------------------------------------------------------------------------------------

	fyuu_rhi::Backend BackendOf(std::string_view name) {
		return name == "d3d12" ? fyuu_rhi::Backend::DirectX12 :
			name == "vulkan" ? fyuu_rhi::Backend::Vulkan :
			name == "webgpu" ? fyuu_rhi::Backend::WebGPU :
			name == "metal" ? fyuu_rhi::Backend::Metal : fyuu_rhi::Backend::OpenGL;
	}

	/// Opens the backend once and hands the device and scheduler to the caller, which is what lets a
	/// bare run cover several modes: a WebGPU adapter may only ever create one device, so a mode
	/// cannot open its own.
	template <class Body>
	int WithDevice(char const* backend_name, Body&& body) {
		using namespace fyuu_rhi;
		InitializeRHIContext("Draw shapes", {}, "FyuuEngine", {}, &LogSink());
		auto& instance = RequestInstance(BackendOf(backend_name));
		auto devices = instance.EnumeratePhysicalDevices();
		if (devices.empty()) {
			throw std::runtime_error("the backend did not expose a physical device");
		}
		auto device = BestPerformance(devices).CreateLogicalDevice();
		auto scheduler = device.CreateScheduler();
		return body(device, scheduler);
	}

	template <class Device, class Scheduler>
	int RunMode(
		char const* backend_name,
		Device& device,
		Scheduler& scheduler,
		std::string_view mode
	) {
		using namespace fyuu_rhi;
		using namespace fyuu_rhi::execution;
		using namespace fyuu_rhi::pipeline;
		auto const name = std::string_view{ backend_name };

		auto scenario = MakeScenario(mode);

		auto const* shader = scenario.textured
			? (scenario.separate_texture ? TexturedSeparateShader : TexturedShader)
			: SolidShader;
		// Each program is named after its own source. A shared name is not just a label: with the
		// same target settings several programs share one Slang session, and a module loaded from a
		// source string is cached in that session by name and path, so a second program under the
		// same name silently receives the first one's code and reflected interface - while the
		// on-disk cache key, which does include the source, stores that result under the second
		// program's own key. This probe used one name for all three shaders and poisoned its own
		// shader cache that way; every mode that runs alone was unaffected, which is why only the
		// bare run noticed.
		auto const* module_name = scenario.textured
			? (scenario.separate_texture ? "draw_shapes_texture_separate" : "draw_shapes_texture")
			: "draw_shapes_solid";
		std::array const modules{ SlangPipelineProgramDescriptor::Module{ module_name, shader } };
		std::array const entries{
			SlangPipelineProgramDescriptor::EntryPoint{ "vertex_main", Stage::Vertex },
			SlangPipelineProgramDescriptor::EntryPoint{ "fragment_main", Stage::Fragment }
		};
		// One vertex layout for every mode: a vec4 position, then a vec4 the colour modes
		// interpolate as COLOR0 and the texture modes read uv out of.
		std::array const layouts{ VertexBufferLayout{ .slot = 0u, .stride = VertexStride } };
		std::array const attributes{
			VertexAttribute{
				.location = 0u,
				.slot = 0u,
				.offset = 0u,
				.format = ResourceFlagBits::R32G32B32A32Float
			},
			VertexAttribute{
				.location = 1u,
				.slot = 0u,
				.offset = 16u,
				.format = ResourceFlagBits::R32G32B32A32Float
			}
		};
		std::array const color_targets{
			ColorTargetState{
				.format = ResourceFlagBits::R8G8B8A8Unorm,
				.blend = scenario.blend
			}
		};
		std::optional<DepthStencilState> depth_state;
		if (scenario.needs_depth) {
			// Less, so the second draw's farther quad fails in the overlap. The default compare
			// is Always, which would make the depth test indistinguishable from no test at all.
			depth_state = DepthStencilState{
				.format = scenario.depth_only
					? ResourceFlagBits::D32Float
					: ResourceFlagBits::D24UnormS8Uint,
				.depth_test_enabled = true,
				.depth_write_enabled = true,
				.depth_compare = CompareOperation::Less
			};
		}
		auto pipeline = device.CreateGraphicsPipeline({
			.program = { .modules = modules, .entry_points = entries },
			.vertex = { .buffers = layouts, .attributes = attributes },
			.primitive = { .topology = scenario.topology },
			.depth_stencil = depth_state,
			.color_targets = color_targets
		});

		auto target = device.CreateTexture(TargetWidth, TargetHeight, 1u, 1u, ColorTargetFlags());
		auto target_view = target.CreateTextureView(0u, 1u, 0u, 1u, ColorTargetFlags());
		Resource depth;
		View depth_view;
		if (scenario.needs_depth) {
			depth = device.CreateTexture(
				TargetWidth,
				TargetHeight,
				1u,
				1u,
				DepthTargetFlags(!scenario.depth_only)
			);
			depth_view = depth.CreateTextureView(
				0u,
				1u,
				0u,
				1u,
				DepthTargetFlags(!scenario.depth_only)
			);
		}
		auto vertices =
			device.CreateBuffer(scenario.vertices.size() * sizeof(float), VertexBufferFlags());
		Resource indices;
		if (scenario.indexed) {
			indices = device.CreateBuffer(
				scenario.indices.size() * sizeof(std::uint16_t),
				IndexBufferFlags()
			);
		}

		// The sampled texture is uploaded through a host-visible staging buffer, so this mode also
		// covers the write-copy-sample chain rather than only the descriptor that names it.
		Resource staging;
		Resource texture;
		View texture_view;
		Sampler sampler;
		PipelineResourceGroup group;
		auto const group_space = 1u;
		std::vector<std::byte> texels;
		if (scenario.textured) {
			texture = device.CreateTexture(2u, 2u, 1u, 1u, SampledTextureFlags());
			texture_view = texture.CreateTextureView(0u, 1u, 0u, 1u, SampledTextureFlags());
			SamplerDescriptor descriptor;
			descriptor.address_mode_u = AddressMode::ClampToEdge;
			descriptor.address_mode_v = AddressMode::ClampToEdge;
			descriptor.address_mode_w = AddressMode::ClampToEdge;
			descriptor.mag_filter = FilterMode::Nearest;
			descriptor.min_filter = FilterMode::Nearest;
			descriptor.mipmap_filter = MipmapFilterMode::Nearest;
			sampler = device.CreateSampler(descriptor);
			texels = TexelData();
			staging = device.CreateBuffer(texels.size(), StagingFlags());
			// The shaders declare their texture in space 1, at slot 1 (combined) or slots 1 and 2
			// (separate), so the group carries those slots and nothing else.
			std::vector<ResourceBinding> bindings;
			bindings.reserve(2u);
			if (scenario.separate_texture) {
				bindings.push_back(
					ResourceBinding{ .slot = 1u, .value = BindingValue::FromView(texture_view) }
				);
				bindings.push_back(
					ResourceBinding{ .slot = 2u, .value = BindingValue::FromSampler(sampler) }
				);
			}
			else {
				bindings.push_back(ResourceBinding{
					.slot = 1u,
					.value = BindingValue::FromCombined(texture_view, sampler)
				});
			}
			group = pipeline.CreatePipelineResourceGroup(group_space, bindings);
		}

		auto builder = scheduler.schedule();
		auto const target_binding = builder.RegisterResource();
		auto const view_binding = builder.RegisterView();
		auto const vertex_binding = builder.RegisterResource();
		auto const pipeline_binding = builder.RegisterPipeline();
		auto const readback_binding = builder.RegisterResource();
		// Only what the mode uses is registered: the graph refuses to run with a registered but
		// unbound slot, so an unused one cannot simply be left empty.
		std::optional<std::size_t> index_binding;
		if (scenario.indexed) {
			index_binding = builder.RegisterResource();
		}
		std::optional<std::size_t> depth_binding;
		std::optional<std::size_t> depth_view_binding;
		if (scenario.needs_depth) {
			depth_binding = builder.RegisterResource();
			depth_view_binding = builder.RegisterView();
		}
		std::optional<std::size_t> group_binding;
		std::optional<std::size_t> staging_binding;
		std::optional<std::size_t> texture_binding;
		std::optional<std::size_t> texture_view_binding;
		std::optional<std::size_t> sampler_binding;
		if (scenario.textured) {
			group_binding = builder.RegisterResourceGroup();
			staging_binding = builder.RegisterResource();
			texture_binding = builder.RegisterResource();
			texture_view_binding = builder.RegisterView();
			sampler_binding = builder.RegisterSampler();
		}

		auto upload = builder.CreateNode(QueueType::Transfer);
		auto const* vertex_bytes = reinterpret_cast<std::byte const*>(scenario.vertices.data());
		upload.Record(WriteBuffer{
			vertex_binding,
			0u,
			std::vector<std::byte>{
				vertex_bytes,
				vertex_bytes + scenario.vertices.size() * sizeof(float)
			}
		});
		if (scenario.indexed) {
			auto const* index_bytes = reinterpret_cast<std::byte const*>(scenario.indices.data());
			upload.Record(WriteBuffer{
				*index_binding,
				0u,
				std::vector<std::byte>{
					index_bytes,
					index_bytes + scenario.indices.size() * sizeof(std::uint16_t)
				}
			});
		}
		if (scenario.textured) {
			upload
				.Record(WriteBuffer{ *staging_binding, 0u, texels })
				.Access({ *staging_binding, AccessMode::Read, ResourceUsage::CopySource, {} })
				.Access({ *texture_binding, AccessMode::Write, ResourceUsage::CopyDestination, {} })
				.Record(
					CopyBufferToTexture{
						.source = *staging_binding,
						.destination = *texture_binding,
						.source_layout = {
							.offset = 0u,
							.bytes_per_row = RowPitch,
							.rows_per_image = 2u
						},
						.destination_region = { .width = 2u, .height = 2u, .depth = 1u }
					}
				);
		}

		BeginRendering begin{
			.area = { 0, 0, TargetWidth, TargetHeight },
			.colors = {
				ColorAttachment{
					.resource = target_binding,
					.view = view_binding,
					.load = LoadOperation::Clear,
					.clear = {
						scenario.clear[0],
						scenario.clear[1],
						scenario.clear[2],
						scenario.clear[3]
					}
				}
			}
		};
		if (scenario.needs_depth) {
			begin.depth_stencil = DepthStencilAttachment{
				.resource = *depth_binding,
				.view = *depth_view_binding
			};
		}

		auto draw = builder.CreateNode(QueueType::Graphics, upload);
		draw
			.Access({ target_binding, AccessMode::Write, ResourceUsage::ColorAttachment, {} })
			.Access({ vertex_binding, AccessMode::Read, ResourceUsage::VertexBuffer, {} })
			.Record(BindPipeline{ pipeline_binding })
			.Record(begin)
			.Record(Viewport{ 0.0f, 0.0f, float(TargetWidth), float(TargetHeight) })
			.Record(Scissor{ 0, 0, scenario.scissor_width, TargetHeight })
			.Record(BindVertexBuffer{ vertex_binding, 0u, VertexStride, 0u });
		if (scenario.indexed) {
			// DrawIndexed needs the index buffer bound and declared, not only registered.
			draw
				.Access({ *index_binding, AccessMode::Read, ResourceUsage::IndexBuffer, {} })
				.Record(BindIndexBuffer{ *index_binding, IndexType::Uint16, 0u });
		}
		if (scenario.needs_depth) {
			draw.Access({
				*depth_binding,
				AccessMode::Write,
				ResourceUsage::DepthStencilAttachment,
				{}
			});
		}
		if (scenario.textured) {
			draw
				.Access({ *texture_binding, AccessMode::Read, ResourceUsage::Sampled, {} })
				.Record(BindResourceGroup{ *group_binding, group_space });
		}
		for (auto const call : scenario.draws) {
			if (scenario.indexed) {
				draw.Record(
					DrawIndexed{
						.index_count = static_cast<std::uint32_t>(scenario.indices.size()),
						.vertex_offset = scenario.vertex_offset
					}
				);
				continue;
			}
			draw.Record(Draw{ .vertex_count = call.vertex_count, .first_vertex = call.first_vertex });
		}
		draw.Record(EndRendering{});

		// Readback is a third node in the same graph rather than a second graph, so the copy is
		// ordered after the render scope by the plan's own barriers.
		auto readback_node = builder.CreateNode(QueueType::Transfer, draw);
		readback_node
			.Access({ target_binding, AccessMode::Read, ResourceUsage::CopySource, {} })
			.Access({ readback_binding, AccessMode::Write, ResourceUsage::CopyDestination, {} })
			.Record(
				CopyTextureToBuffer{
					.source = target_binding,
					.destination = readback_binding,
					.source_region = { .width = TargetWidth, .height = TargetHeight },
					.destination_layout = {
						.offset = 0u,
						.bytes_per_row = RowPitch,
						.rows_per_image = TargetHeight
					}
				}
			);

		auto readback = device.CreateBuffer(RowPitch * TargetHeight, ReadbackFlags());

		auto state = std::make_shared<State>();
		auto operation = std::move(builder).connect(Receiver{ state });
		operation.BindResource(target_binding, std::move(target));
		operation.BindView(view_binding, std::move(target_view));
		operation.BindResource(vertex_binding, std::move(vertices));
		operation.BindPipeline(pipeline_binding, std::move(pipeline));
		operation.BindResource(readback_binding, std::move(readback));
		if (scenario.indexed) {
			operation.BindResource(*index_binding, std::move(indices));
		}
		if (scenario.needs_depth) {
			operation.BindResource(*depth_binding, std::move(depth));
			operation.BindView(*depth_view_binding, std::move(depth_view));
		}
		if (scenario.textured) {
			operation.BindResource(*staging_binding, std::move(staging));
			operation.BindResource(*texture_binding, std::move(texture));
			operation.BindView(*texture_view_binding, std::move(texture_view));
			operation.BindSampler(*sampler_binding, std::move(sampler));
			operation.BindResourceGroup(*group_binding, std::move(group));
		}
		operation.start();
		auto resources = Wait(state, mode);
		readback = resources.TakeResource(readback_binding);

		auto mapping = readback.Map({ 0u, static_cast<std::size_t>(RowPitch) * TargetHeight });
		auto const pixels = mapping.Read();

		auto const fail = [mode](std::string message) {
			throw std::runtime_error(std::format("{}: {}", mode, std::move(message)));
		};
		auto const describe = [mode](std::string_view what, std::array<int, 3> pixel) {
			return Describe(mode, what, pixel);
		};

		std::cout << name << ' ' << mode << ':';
		if (mode == "lines") {
			auto const [first, last] = LitRowRange(pixels);
			auto const lit = CountLit(pixels, 0u, 0u, TargetWidth, TargetHeight);
			std::cout << " lit=" << lit << " rows=[" << first << ", " << last << ']';
			if (lit == 0u) {
				fail("the line drew nothing");
			}
			// Two pixels inside the near end, on the row the line actually landed on, so a line
			// that only covers its middle columns is still a failure.
			auto const left_end = ReadPixel(pixels, 34u, first);
			std::cout << ' ' << describe("the left end", left_end);
			if (!IsLit(left_end)) {
				fail(describe("the line's left end", left_end) + ", which is not lit");
			}
			// A one-pixel horizontal line through a row centre covers that row; the band absorbs
			// backends whose coverage rule straddles the centre between two rows.
			if (first < 62u || last > 66u) {
				fail(std::format(
					"the line covered rows {} to {}, well off the single row it spans",
					first,
					last
				));
			}
			if (lit < 50u) {
				fail(std::format("a line spanning 64 columns covered only {} pixels", lit));
			}
		}
		else if (mode == "strip" || mode == "indexed") {
			auto const total = CountLit(pixels, 0u, 0u, TargetWidth, TargetHeight);
			auto const inside = CountLit(pixels, 30u, 30u, 98u, 98u);
			auto const centre = ReadPixel(pixels, 64u, 64u);
			std::cout << " lit=" << total << " inside=" << inside << ' '
				<< describe("the centre", centre);
			if (total != inside) {
				fail(std::format(
					"{} lit pixels fall outside the quad's own area plus a 2-pixel margin",
					total - inside
				));
			}
			// 64x64 = 4096 when the quad's edges land on pixel edges; the range absorbs the
			// per-backend edge rule (a 62x62 result is 3844, a 65x65 one is 4225) without
			// accepting a quad that is a whole row or column too large.
			if (total < 3800u || total > 4250u) {
				fail(std::format("the 64x64 quad covered {} pixels", total));
			}
			if (!IsNear(centre, Red8, 8)) {
				fail(describe("the quad's centre", centre));
			}
		}
		else if (mode == "scissor") {
			auto const left = CountLit(pixels, 0u, 0u, 62u, TargetHeight);
			auto const right = CountLit(pixels, 66u, 0u, TargetWidth, TargetHeight);
			auto const clipped = ReadPixel(pixels, 100u, 64u);
			std::cout << " left=" << left << " right=" << right << ' '
				<< describe("past the scissor", clipped);
			if (IsLit(clipped)) {
				fail(describe("the pixel past the scissor", clipped) + ", which the scissor should have clipped");
			}
			if (left < 7000u) {
				fail(std::format("only {} pixels are lit inside the scissor", left));
			}
			if (right != 0u) {
				fail(std::format("the scissor let {} lit pixels through past column 64", right));
			}
		}
		else if (mode == "depth" || mode == "depth_only") {
			auto const overlap = ReadPixel(pixels, 64u, 64u);
			auto const beside = ReadPixel(pixels, 32u, 64u);
			auto const above = ReadPixel(pixels, 64u, 32u);
			auto const outside = CountLit(pixels, 0u, 0u, 14u, TargetHeight) +
				CountLit(pixels, 0u, 0u, TargetWidth, 14u);
			std::cout << ' ' << describe("the overlap", overlap) << ' '
				<< describe("the far quad beside it", beside) << ' '
				<< describe("the far quad past it", above);
			if (outside != 0u) {
				fail(std::format("{} lit pixels are outside both quads", outside));
			}
			if (!IsNear(overlap, Red8, 8)) {
				fail(describe("where the near quad is in front", overlap));
			}
			if (!IsNear(beside, Green8, 8)) {
				fail(describe("the far quad beside the overlap", beside));
			}
			if (!IsNear(above, Green8, 8)) {
				fail(describe("the far quad past the overlap", above));
			}
		}
		else if (mode == "blend") {
			auto const centre = ReadPixel(pixels, 64u, 64u);
			auto const corner = ReadPixel(pixels, 1u, 1u);
			std::cout << ' ' << describe("the centre", centre) << ' '
				<< describe("the corner", corner);
			// Half-transparent red over an opaque blue clear. Rounding may land on 127 or 128.
			if (!IsNear(centre, Blend8, 4)) {
				fail(describe("the blended centre", centre));
			}
			if (!IsNear(corner, Blend8, 4)) {
				fail(describe("the blended corner", corner));
			}
		}
		else if (mode == "texture" || mode == "texture_separate") {
			// Which texel lands in which quadrant depends on the texture origin convention, which
			// the RHI does not pin, so the four sampled colours are compared as a set: every
			// quadrant must hold a different one of the four texels. A sampler that returns
			// nothing (or the clear colour) matches none of them.
			std::array const expected{ Red8, Green8, Blue8, White8 };
			std::array const quadrants{
				std::pair{ 32u, 32u },
				std::pair{ 96u, 32u },
				std::pair{ 32u, 96u },
				std::pair{ 96u, 96u }
			};
			std::array<bool, 4> matched{};
			for (auto const& [column, row] : quadrants) {
				auto const pixel = ReadPixel(pixels, column, row);
				std::cout << " (" << column << ", " << row << ")=(" << pixel[0] << ", "
					<< pixel[1] << ", " << pixel[2] << ')';
				auto found = false;
				for (std::size_t index = 0u; index < expected.size(); ++index) {
					if (!matched[index] && IsNear(pixel, expected[index], 8)) {
						matched[index] = true;
						found = true;
						break;
					}
				}
				if (!found) {
					fail(describe(
						std::format("the quadrant at ({}, {})", column, row),
						pixel
					) + ", which matches none of the four texels");
				}
			}
		}
		std::cout << std::endl;

		if (LogSink().HasError()) {
			throw std::runtime_error(
				"the backend reported a validation or runtime error; see the log above"
			);
		}
		return 0;
	}

	/// The modes the bare run covers, in the order it covers them.
	constexpr std::array<std::string_view, 9> Modes{
		"lines",
		"strip",
		"indexed",
		"scissor",
		"depth",
		"depth_only",
		"blend",
		"texture",
		"texture_separate"
	};

	int RunAll(char const* backend_name) {
		return WithDevice(backend_name, [backend_name](auto& device, auto& scheduler) {
			for (auto const mode : Modes) {
				RunMode(backend_name, device, scheduler, mode);
			}
			return 0;
		});
	}

} // namespace

int main(int argc, char** argv) try {
	auto const name = std::string{
		argc > 1 ? std::string_view{ argv[1] } : std::string_view{ "opengl" }
	};
	auto const mode = argc > 2 ? std::string_view{ argv[2] } : std::string_view{ "all" };
	return mode == "all"
		? RunAll(name.c_str())
		: WithDevice(name.c_str(), [&](auto& device, auto& scheduler) {
			return RunMode(name.c_str(), device, scheduler, mode);
		});
}
catch (std::exception const& error) {
	std::cerr << "Draw shapes failed: " << error.what() << std::endl;
	return 1;
}
