// Renders one triangle offscreen and counts the pixels it actually shaded.
//
// HelloTriangle never inspects its rendered output, only that the graph completed, so a
// backend that presents an empty frame still passes there. This probe closes that gap: the
// target is cleared to black, the fragment shader returns pure red, so any red pixel can only
// have come from the draw. The same source builds against the pre-change tree, which makes the
// result directly comparable across revisions.
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

#include <vector>

#include <string>

#include <iostream>

#include <cstdint>

#include <array>

#include <chrono>

#include <atomic>
#include <condition_variable>
#include <mutex>

#include <optional>
#include <string_view>

#include <source_location>
#endif // !defined(__cpp_lib_modules)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_rhi;

namespace {

	using namespace std::chrono_literals;
	using Resources = fyuu_rhi::execution::CommandGraphResources;

	constexpr std::uint32_t TargetWidth = 127u;
	constexpr std::uint32_t TargetHeight = 93u;
	constexpr std::uint32_t RowPitch = 512u;

	/// Backend validation and runtime errors are the first thing worth seeing when a draw comes
	/// back empty, so they are printed as they happen and counted for the exit status. A null sink
	/// here is what once let an OpenGL defect hide behind a passing pixel count.
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

	/// A function-local static rather than a local in Run: the RHI context outlives Run and may log
	/// while it is being torn down, so the sink has to be immortal.
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

	Resources Wait(std::shared_ptr<State> const& state) {
		std::unique_lock lock(state->mutex);
		if (!state->condition.wait_for(
			lock,
			30s,
			[&state]() {
				return state->completed;
			}
		)) {
			throw std::runtime_error("Timed out waiting for the draw graph");
		}
		if (state->error) {
			std::rethrow_exception(state->error);
		}
		if (state->stopped || !state->resources) {
			throw std::runtime_error("The draw graph was stopped");
		}
		return std::move(*state->resources);
	}

	int Run(
		char const* backend_name,
		bool two_attribute_layout,
		bool push_constants,
		bool uniform_space_zero,
		bool early_binds,
		bool zero_clip_z
	) {
		using namespace fyuu_rhi;
		using namespace fyuu_rhi::execution;
		using namespace fyuu_rhi::pipeline;
		auto const name = std::string_view{ backend_name };
		auto const backend = name == "d3d12" ? Backend::DirectX12 :
			name == "vulkan" ? Backend::Vulkan :
			name == "webgpu" ? Backend::WebGPU :
			name == "metal" ? Backend::Metal : Backend::OpenGL;

		InitializeRHIContext("Draw probe", {}, "FyuuEngine", {}, &LogSink());
		auto& instance = RequestInstance(backend);
		auto devices = instance.EnumeratePhysicalDevices();
		auto device = BestPerformance(devices).CreateLogicalDevice();
		auto scheduler = device.CreateScheduler();

		// A fragment colour no clear value can produce, so red pixels can only come from the draw.
		// The "simple" layout is one vec4 position at stride 16; "hello" mirrors the HelloTriangle
		// test's two-attribute layout (vec2 position plus vec4 colour at stride 24).
		//
		// The two-attribute shaders return a VertexOutput struct rather than a bare SV_Position,
		// because their fragment stage consumes the interpolated colour: a fragment input with no
		// matching vertex output makes the pipeline invalid, which WebGPU and D3D12 reject outright
		// while Vulkan and OpenGL accept it silently. That mismatch is what once made this probe
		// look like four backends disagreeing when two of them were simply refusing an ill-formed
		// pipeline. Attributing the colour in also means these modes verify that attribute
		// location 1 survives the vertex layout, not just that a triangle was rasterized.
		constexpr char simple_shader[] = R"(
			struct Uniform { float4 color; };
			[[vk::binding(0, 0)]] ConstantBuffer<Uniform> object : register(b0, space0);
			[shader("vertex")]
			float4 vertex_main(float4 position : POSITION) : SV_Position {
				return position;
			}
			[shader("fragment")]
			float4 fragment_main() : SV_Target0 { return object.color; }
		)";
		constexpr char hello_shader[] = R"(
			struct Uniform { float4 color; };
			[[vk::binding(0, 0)]] ConstantBuffer<Uniform> object : register(b0, space0);
			struct VertexOutput {
				float4 position : SV_Position;
				float4 color : COLOR0;
			};
			[shader("vertex")]
			VertexOutput vertex_main(float2 position : POSITION, float4 color : COLOR0) {
				VertexOutput output;
				output.position = float4(position, 0.5, 1.0);
				output.color = color;
				return output;
			}
			[shader("fragment")]
			float4 fragment_main(VertexOutput input) : SV_Target0 {
				return object.color * input.color;
			}
		)";
		// Adds an immediate-constant (push constant) range on top of the two-attribute layout, so
		// SetPipelineConstants is exercised in the same command position HelloTriangle uses it.
		// The uniform buffer sits in space 1, the shape the engine actually uses. The immediate
		// block is deliberately left unpinned here: Slang assigns an unpinned block the next free
		// CBV register in space 0, which only avoids a user binding at (b0, space0) because this
		// shader's user binding is in space 1. The "pushcolor_space0" mode below is the same check
		// with the uniform buffer in space 0.
		// The fragment output is the product of all three colours, which lets the "pushcolor" mode
		// pick values whose product is a pixel no other data path (clear, uniform alone, or the
		// single-colour shaders) can produce.
		constexpr char push_shader[] = R"(
			struct Uniform { float4 color; };
			[[vk::binding(0, 1)]] ConstantBuffer<Uniform> object : register(b0, space1);
			struct Immediate { float4 color; };
			[[vk::push_constant]] ConstantBuffer<Immediate> immediate;
			struct VertexOutput {
				float4 position : SV_Position;
				float4 color : COLOR0;
			};
			[shader("vertex")]
			VertexOutput vertex_main(float2 position : POSITION, float4 color : COLOR0) {
				VertexOutput output;
				output.position = float4(position, 0.5, 1.0);
				output.color = color;
				return output;
			}
			[shader("fragment")]
			float4 fragment_main(VertexOutput input) : SV_Target0 {
				return object.color * immediate.color * input.color;
			}
		)";
		// The same immediate-constant check with the user uniform buffer in space 0. The user's CBV
		// then sits at register (b0, space0), the register the emulated immediate-constant range
		// used to be declared at, and the block is left unpinned exactly like "pushcolor": the
		// backend must follow the register Slang actually assigns the block (here cb1, space0)
		// instead of the shader having to declare it.
		constexpr char push_space0_shader[] = R"(
			struct Uniform { float4 color; };
			[[vk::binding(0, 0)]] ConstantBuffer<Uniform> object : register(b0, space0);
			struct Immediate { float4 color; };
			[[vk::push_constant]] ConstantBuffer<Immediate> immediate;
			struct VertexOutput {
				float4 position : SV_Position;
				float4 color : COLOR0;
			};
			[shader("vertex")]
			VertexOutput vertex_main(float2 position : POSITION, float4 color : COLOR0) {
				VertexOutput output;
				output.position = float4(position, 0.5, 1.0);
				output.color = color;
				return output;
			}
			[shader("fragment")]
			float4 fragment_main(VertexOutput input) : SV_Target0 {
				return object.color * immediate.color * input.color;
			}
		)";
		auto const& shader = push_constants
			? (uniform_space_zero ? push_space0_shader : push_shader)
			: (two_attribute_layout ? hello_shader : simple_shader);
		std::array const modules{ SlangPipelineProgramDescriptor::Module{ "probe", shader } };
		std::array const entries{
			SlangPipelineProgramDescriptor::EntryPoint{ "vertex_main", Stage::Vertex },
			SlangPipelineProgramDescriptor::EntryPoint{ "fragment_main", Stage::Fragment }
		};
		std::array const colors{ ColorTargetState{ .format = ResourceFlagBits::R8G8B8A8Unorm } };
		std::vector<VertexBufferLayout> layouts;
		std::vector<VertexAttribute> attributes;
		if (two_attribute_layout || push_constants) {
			layouts = { VertexBufferLayout{ .slot = 0u, .stride = 6u * sizeof(float) } };
			attributes = {
				VertexAttribute{
					.location = 0u,
					.slot = 0u,
					.offset = 0u,
					.format = ResourceFlagBits::R32G32Float
				},
				VertexAttribute{
					.location = 1u,
					.slot = 0u,
					.offset = 2u * sizeof(float),
					.format = ResourceFlagBits::R32G32B32A32Float
				}
			};
		}
		else {
			layouts = { VertexBufferLayout{ .slot = 0u, .stride = 16u } };
			attributes = {
				VertexAttribute{
					.location = 0u,
					.slot = 0u,
					.offset = 0u,
					.format = ResourceFlagBits::R32G32B32A32Float
				}
			};
		}
		auto pipeline = device.CreateGraphicsPipeline({
			.program = { .modules = modules, .entry_points = entries },
			.vertex = { .buffers = layouts, .attributes = attributes },
			.color_targets = colors
		});

		ResourceFlags target_flags;
		target_flags.Set(ResourceFlagBits::DeviceLocal);
		target_flags.Set(ResourceFlagBits::CopySRC);
		target_flags.Set(ResourceFlagBits::RenderAttachment);
		target_flags.Set(ResourceFlagBits::Texture2D);
		target_flags.Set(ResourceFlagBits::TextureView2D);
		target_flags.Set(ResourceFlagBits::TextureViewAspectAll);
		target_flags.Set(ResourceFlagBits::R8G8B8A8Unorm);
		target_flags.Set(ResourceFlagBits::Sample1);
		auto target = device.CreateTexture(TargetWidth, TargetHeight, 1u, 1u, target_flags);
		auto target_view = target.CreateTextureView(0u, 1u, 0u, 1u, target_flags);

		ResourceFlags uniform_flags;
		uniform_flags.Set(ResourceFlagBits::HostVisible);
		uniform_flags.Set(ResourceFlagBits::UniformBuffer);
		auto uniform = device.CreateBuffer(256u, uniform_flags);

		ResourceFlags vertex_flags;
		vertex_flags.Set(ResourceFlagBits::HostVisible);
		vertex_flags.Set(ResourceFlagBits::VertexBuffer);
		// Stride 16 for one vec4 position, 24 for vec2 position plus vec4 colour.
		std::vector<float> vertex_data;
		if (two_attribute_layout || push_constants) {
			vertex_data = {
				-0.9f, -0.9f, 1.0f, 0.0f, 0.0f, 1.0f,
				0.9f, -0.9f, 1.0f, 0.0f, 0.0f, 1.0f,
				0.0f, 0.9f, 1.0f, 0.0f, 0.0f, 1.0f
			};
		}
		else {
			// The "zeroz" mode writes gl_Position.z = 0, the near plane of the engine's
			// Vulkan-style [0, 1] depth convention that every shader in this RHI is authored
			// in. On OpenGL that must land inside the clip volume; a clip-space fixup that
			// re-maps it to -1 silently clips the whole draw.
			auto const depth = zero_clip_z ? 0.0f : 0.5f;
			vertex_data = {
				-0.9f, -0.9f, depth, 1.0f,
				0.9f, -0.9f, depth, 1.0f,
				0.0f, 0.9f, depth, 1.0f
			};
		}
		auto vertices = device.CreateBuffer(vertex_data.size() * sizeof(float), vertex_flags);

		std::array const bindings{
			ResourceBinding{ .slot = 0u, .value = BindingValue::FromBuffer(uniform) }
		};
		// The push-constant shaders declare their uniform buffer in space 1, matching HelloTriangle,
		// except "pushcolor_space0", which puts it in space 0. The group has to be created in, and
		// bound into, the same space the shader declares.
		auto const group_space = (push_constants && !uniform_space_zero) ? 1u : 0u;
		auto group = pipeline.CreatePipelineResourceGroup(group_space, bindings);

		auto builder = scheduler.schedule();
		auto const target_binding = builder.RegisterResource();
		auto const uniform_binding = builder.RegisterResource();
		auto const vertex_binding = builder.RegisterResource();
		auto const view_binding = builder.RegisterView();
		auto const pipeline_binding = builder.RegisterPipeline();
		auto const group_binding = builder.RegisterResourceGroup();

		auto upload = builder.CreateNode(QueueType::Transfer);
		// "pushcolor" multiplies this uniform colour by the immediate-constant colour, so the two
		// are deliberately different from every other mode's values.
		std::array const fragment_color = push_constants
			? std::array{ 0.8f, 0.4f, 0.0f, 1.0f }
			: std::array{ 1.0f, 0.0f, 0.0f, 1.0f };
		auto const* color_bytes = reinterpret_cast<std::byte const*>(fragment_color.data());
		upload.Record(
			WriteBuffer{
				uniform_binding,
				0u,
				std::vector<std::byte>{ color_bytes, color_bytes + sizeof(fragment_color) }
			}
		);
		auto const* vertex_bytes = reinterpret_cast<std::byte const*>(vertex_data.data());
		upload.Record(
			WriteBuffer{
				vertex_binding,
				0u,
				std::vector<std::byte>{
					vertex_bytes,
					vertex_bytes + vertex_data.size() * sizeof(float)
				}
			}
		);
		auto draw = builder.CreateNode(QueueType::Graphics, upload);
		draw
			.Access({ target_binding, AccessMode::Write, ResourceUsage::ColorAttachment, {} })
			.Access({ uniform_binding, AccessMode::Read, ResourceUsage::Uniform, {} })
			.Access({ vertex_binding, AccessMode::Read, ResourceUsage::VertexBuffer, {} })
			.Record(BindPipeline{ pipeline_binding });
		// HelloTriangle binds the resource group and vertex buffer before BeginRendering rather
		// than inside the render scope, which is the one structural difference left between its
		// graph and this probe's.
		if (early_binds) {
			draw
				.Record(BindResourceGroup{ group_binding, group_space })
				.Record(
					BindVertexBuffer{
						vertex_binding,
						0u,
						(two_attribute_layout || push_constants) ? 24u : 16u,
						0u
					}
				);
		}
		draw
			.Record(
				BeginRendering{
					.area = { 0, 0, TargetWidth, TargetHeight },
					.colors = {
						{
							.resource = target_binding,
							.view = view_binding,
							.load = LoadOperation::Clear,
							// Black, so any red pixel proves the draw ran.
							.clear = { 0.0f, 0.0f, 0.0f, 1.0f }
						}
					}
				}
			)
			.Record(Viewport{ 0.0f, 0.0f, float(TargetWidth), float(TargetHeight) })
			.Record(Scissor{ 0, 0, TargetWidth, TargetHeight });
		if (push_constants) {
			std::array const immediate_color{ 0.5f, 0.5f, 0.5f, 1.0f };
			auto const* immediate_bytes =
				reinterpret_cast<std::byte const*>(immediate_color.data());
			// Both push shaders leave the immediate block unpinned, so it reflects under the same
			// backend-neutral ABI identity in every mode and on every backend.
			draw.Record(SetPipelineConstants{
				.slot = 0u,
				.space = 0u,
				.offset = 0u,
				.data = std::vector<std::byte>{
					immediate_bytes,
					immediate_bytes + sizeof(immediate_color)
				}
			});
		}
		if (!early_binds) {
			draw
				.Record(BindResourceGroup{ group_binding, group_space })
				.Record(
					BindVertexBuffer{
						vertex_binding,
						0u,
						(two_attribute_layout || push_constants) ? 24u : 16u,
						0u
					}
				);
		}
		draw
			.Record(Draw{ 3u })
			.Record(EndRendering{});

		auto upload_state = std::make_shared<State>();
		auto operation = std::move(builder).connect(Receiver{ upload_state });
		operation.BindResource(target_binding, std::move(target));
		operation.BindResource(uniform_binding, std::move(uniform));
		operation.BindResource(vertex_binding, std::move(vertices));
		operation.BindView(view_binding, std::move(target_view));
		operation.BindPipeline(pipeline_binding, std::move(pipeline));
		operation.BindResourceGroup(group_binding, std::move(group));
		operation.start();
		auto resources = Wait(upload_state);
		target = resources.TakeResource(target_binding);
		target_view = resources.TakeView(view_binding);

		ResourceFlags readback_flags;
		readback_flags.Set(ResourceFlagBits::DeviceReadback);
		readback_flags.Set(ResourceFlagBits::CopyDST);
		auto readback = device.CreateBuffer(RowPitch * TargetHeight, readback_flags);

		auto copy_builder = scheduler.schedule();
		auto const source_binding = copy_builder.RegisterResource();
		auto const destination_binding = copy_builder.RegisterResource();
		copy_builder
			.CreateNode(QueueType::Transfer)
			.Access({ source_binding, AccessMode::Read, ResourceUsage::CopySource, {} })
			.Access({ destination_binding, AccessMode::Write, ResourceUsage::CopyDestination, {} })
			.Record(
				CopyTextureToBuffer{
					.source = source_binding,
					.destination = destination_binding,
					.source_region = { .width = TargetWidth, .height = TargetHeight },
					.destination_layout = {
						.offset = 0u,
						.bytes_per_row = RowPitch,
						.rows_per_image = TargetHeight
					}
				}
			);

		auto copy_state = std::make_shared<State>();
		auto copy_operation = std::move(copy_builder).connect(Receiver{ copy_state });
		copy_operation.BindResource(source_binding, std::move(target));
		copy_operation.BindResource(destination_binding, std::move(readback));
		copy_operation.start();
		auto copy_resources = Wait(copy_state);
		readback = copy_resources.TakeResource(destination_binding);

		auto mapping = readback.Map({ 0u, RowPitch * TargetHeight });
		auto const bytes = mapping.Read();
		std::size_t red = 0u;
		std::size_t non_black = 0u;
		// Product of the uniform colour (0.8, 0.4, 0.0), the immediate-constant colour
		// (0.5, 0.5, 0.5) and the vertex colour (1, 0, 0) after the 8-bit UNORM round trip:
		// (0.4, 0.0, 0.0) -> (102, 0, 0). Every prefix of that chain is a different pixel — the
		// uniform alone is (204, 102, 0), uniform times immediate is (102, 51, 0), and uniform
		// times vertex colour is (204, 0, 0) — so a red channel of exactly 102 cannot be produced
		// unless the immediate-constant data actually reached the fragment shader.
		std::size_t immediate = 0u;
		// The first shaded pixel, so a failing mode reports what it actually drew instead of only
		// that it failed to match: the difference between "the immediate constant never arrived"
		// and "it arrived mis-scaled" is one glance at this triple.
		std::array<int, 3> sample{ -1, -1, -1 };
		for (std::uint32_t y = 0u; y < TargetHeight; ++y) {
			for (std::uint32_t x = 0u; x < TargetWidth; ++x) {
				auto const offset = static_cast<std::size_t>(y) * RowPitch + x * 4u;
				auto const r = std::to_integer<int>(bytes[offset]);
				auto const g = std::to_integer<int>(bytes[offset + 1u]);
				auto const b = std::to_integer<int>(bytes[offset + 2u]);
				if (r > 200 && g < 60 && b < 60) {
					++red;
				}
				if (r + g + b > 30) {
					++non_black;
					if (sample[0] < 0) {
						sample = { r, g, b };
					}
				}
				if (r >= 99 && r <= 105 && g <= 6 && b <= 6) {
					++immediate;
				}
			}
		}
		auto const total = static_cast<std::size_t>(TargetWidth) * TargetHeight;
		std::cout << name << " draw: red=" << red << " non-black=" << non_black
			<< " total=" << total
			<< " (" << (100.0 * static_cast<double>(red) / static_cast<double>(total)) << "%)";
		if (push_constants) {
			std::cout << " immediate=" << immediate << " expected=(102, 0, 0)";
		}
		std::cout << " sample=(" << sample[0] << ", " << sample[1] << ", " << sample[2] << ")";
		std::cout << std::endl;
		if (push_constants) {
			if (non_black == 0u) {
				throw std::runtime_error("the draw produced no shaded pixels");
			}
			if (immediate == 0u) {
				throw std::runtime_error(
					"the immediate-constant colour did not reach the fragment shader"
				);
			}
		}
		else if (red == 0u) {
			throw std::runtime_error("the draw produced no shaded pixels");
		}
		// Whatever the pixels say, a backend that logged an error did not do what was asked.
		if (LogSink().HasError()) {
			throw std::runtime_error(
				"the backend reported a validation or runtime error; see the log above"
			);
		}
		return 0;
	}

} // namespace

int main(int argc, char** argv) try {
	auto const name = argc > 1 ? std::string_view{ argv[1] } : std::string_view{ "opengl" };
	auto const mode = argc > 2 ? std::string_view{ argv[2] } : std::string_view{ "simple" };
	// "pushcolor" is the immediate-constant check: it feeds a meaningful push-constant colour and
	// requires the multiply to reach the fragment shader, so a backend that silently drops
	// SetPipelineConstants fails instead of reporting the uniform colour.
	//
	// There is deliberately no mode that feeds nonsense constants and asserts the frame is black:
	// such a mode passes exactly when the constants are dropped, which makes it a trap rather than
	// a test. Its diagnostic value ("the frame shows the uniform colour, so the constants never
	// arrived") is reported by "pushcolor" anyway, together with the actual pixel.
	// "pushcolor_space0" is the same immediate-constant check with the user uniform buffer in
	// space 0, where the emulated immediate-constant range used to be declared on top of the
	// user's CBV register (b0, space0) and failed root signature serialization on D3D12. It is
	// the regression test for declaring the range at the register Slang actually assigns.
	auto const uniform_space_zero = mode == "pushcolor_space0";
	auto const push_constants = mode == "pushcolor" || uniform_space_zero;
	return Run(
		std::string{ name }.c_str(),
		mode == "hello" || mode == "early" || push_constants,
		push_constants,
		uniform_space_zero,
		mode == "early",
		mode == "zeroz"
	);
}
catch (std::exception const& error) {
	std::cerr << "Draw probe failed: " << error.what() << std::endl;
	return 1;
}
