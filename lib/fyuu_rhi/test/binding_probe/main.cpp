// Pins the binding contract of Pipeline::CreatePipelineResourceGroup.
//
// Nothing is drawn and no window is opened: this probe only creates the objects a resource group
// is built from and checks which BindingValue kinds each reflected slot accepts. That is the one
// place the five backends have to agree, and the place where a mismatched value used to be written
// into a native descriptor instead of being rejected. The Vulkan backend only gained that check
// recently; the other four already had it, so this probe holds all five to the same rule.
//
// The sampler half covers the other recent Vulkan change: a descriptor that asks for anisotropy
// must not produce a sampler on a device that never enabled samplerAnisotropy (VUID
// VkSamplerCreateInfo-anisotropyEnable-01070), and maxAnisotropy must stay within the device limit
// (01071). The run fails if the backend logs an error, which is exactly how the validation layer
// reports both.
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <exception>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <array>
#include <atomic>
#include <source_location>
#endif // !defined(__cpp_lib_modules)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_rhi;

namespace {

	/// Counts the backend's own errors, so a group that was created only because the backend
	/// silently accepted nonsense is still a failure.
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

	/// A function-local static: the RHI context may log while being torn down, after main returns.
	ProbeLogSink& LogSink() {
		static ProbeLogSink sink;
		return sink;
	}

	std::size_t g_cases = 0u;

	/// Pins that the pipeline refuses the binding. A backend that accepts it would go on to write a
	/// descriptor whose type does not match the value, which is what the validation layer, if there
	/// is one, then reports.
	template <class Call>
	void Rejected(std::string_view name, Call&& call) {
		++g_cases;
		try {
			call();
		} catch (std::exception const&) {
			return;
		}
		throw std::runtime_error(
			std::format("the pipeline accepted {}", name)
		);
	}

	/// Pins that a value matching the reflected slot produces a group.
	template <class Call>
	void Accepted(std::string_view name, Call&& call) {
		++g_cases;
		try {
			call();
		} catch (std::exception const& failure) {
			throw std::runtime_error(
				std::format("the pipeline rejected {}: {}", name, failure.what())
			);
		}
	}

	int Run(char const* backend_name, bool separate_declarations) {
		using namespace fyuu_rhi;
		using namespace fyuu_rhi::pipeline;
		auto const name = std::string_view{ backend_name };
		auto const backend = name == "d3d12" ? Backend::DirectX12 :
			name == "vulkan" ? Backend::Vulkan :
			name == "webgpu" ? Backend::WebGPU :
			name == "metal" ? Backend::Metal : Backend::OpenGL;

		InitializeRHIContext("Binding probe", {}, "FyuuEngine", {}, &LogSink());
		auto& instance = RequestInstance(backend);
		auto devices = instance.EnumeratePhysicalDevices();
		if (devices.empty()) {
			throw std::runtime_error("the backend did not expose a physical device");
		}
		auto device = BestPerformance(devices).CreateLogicalDevice();

		// A strictly one-colour shader would let a backend that samples nothing pass, so the
		// fragment output is the uniform colour multiplied by the sampled texel. Neither value is
		// ever read back here; the pipeline exists to declare the slots being bound below.
		constexpr char combined_shader[] = R"(
			struct Uniform { float4 color; };
			[[vk::binding(0, 0)]] ConstantBuffer<Uniform> object : register(b0, space0);
			[[vk::binding(1, 1)]] Sampler2D color_texture : register(t1, space1);
			[shader("vertex")]
			float4 vertex_main(float4 position : POSITION) : SV_Position { return position; }
			[shader("fragment")]
			float4 fragment_main() : SV_Target0 {
				return object.color * color_texture.Sample(float2(0.5, 0.5));
			}
		)";
		constexpr char separate_shader[] = R"(
			struct Uniform { float4 color; };
			[[vk::binding(0, 0)]] ConstantBuffer<Uniform> object : register(b0, space0);
			[[vk::binding(1, 1)]] Texture2D color_texture : register(t1, space1);
			[[vk::binding(2, 1)]] SamplerState color_sampler : register(s2, space1);
			[shader("vertex")]
			float4 vertex_main(float4 position : POSITION) : SV_Position { return position; }
			[shader("fragment")]
			float4 fragment_main() : SV_Target0 {
				return object.color * color_texture.Sample(color_sampler, float2(0.5, 0.5));
			}
		)";
		auto const* shader = separate_declarations ? separate_shader : combined_shader;
		std::array const modules{ SlangPipelineProgramDescriptor::Module{ "binding_probe", shader } };
		std::array const entries{
			SlangPipelineProgramDescriptor::EntryPoint{ "vertex_main", Stage::Vertex },
			SlangPipelineProgramDescriptor::EntryPoint{ "fragment_main", Stage::Fragment }
		};
		std::array const colors{ ColorTargetState{ .format = ResourceFlagBits::R8G8B8A8Unorm } };
		std::array const layouts{ VertexBufferLayout{ .slot = 0u, .stride = 16u } };
		std::array const attributes{
			VertexAttribute{
				.location = 0u,
				.slot = 0u,
				.offset = 0u,
				.format = ResourceFlagBits::R32G32B32A32Float
			}
		};
		// Every step that creates an object goes through Accepted(), so a failure names the step.
		// Without that, a bare error out of the setup below cannot say whether the pipeline, the
		// texture, the view, or the sampler is the one the backend refused.
		Pipeline pipeline;
		Accepted("a graphics pipeline that declares a texture binding", [&]() {
			pipeline = device.CreateGraphicsPipeline({
				.program = { .modules = modules, .entry_points = entries },
				.vertex = { .buffers = layouts, .attributes = attributes },
				.color_targets = colors
			});
		});

		ResourceFlags uniform_flags;
		uniform_flags.Set(ResourceFlagBits::HostVisible);
		uniform_flags.Set(ResourceFlagBits::UniformBuffer);
		Resource uniform;
		Accepted("a host-visible uniform buffer", [&]() {
			uniform = device.CreateBuffer(256u, uniform_flags);
		});

		ResourceFlags texture_flags;
		texture_flags.Set(ResourceFlagBits::DeviceLocal);
		texture_flags.Set(ResourceFlagBits::TextureBinding);
		texture_flags.Set(ResourceFlagBits::Texture2D);
		texture_flags.Set(ResourceFlagBits::TextureView2D);
		texture_flags.Set(ResourceFlagBits::TextureViewAspectAll);
		texture_flags.Set(ResourceFlagBits::R8G8B8A8Unorm);
		texture_flags.Set(ResourceFlagBits::Sample1);
		Resource texture;
		View texture_view;
		Accepted("a sampled 2D texture and its view", [&]() {
			texture = device.CreateTexture(4u, 4u, 1u, 1u, texture_flags);
			texture_view = texture.CreateTextureView(0u, 1u, 0u, 1u, texture_flags);
		});

		SamplerDescriptor sampler_descriptor;
		sampler_descriptor.address_mode_u = AddressMode::Repeat;
		sampler_descriptor.address_mode_v = AddressMode::Repeat;
		sampler_descriptor.address_mode_w = AddressMode::Repeat;
		sampler_descriptor.mag_filter = FilterMode::Linear;
		sampler_descriptor.min_filter = FilterMode::Linear;
		sampler_descriptor.mipmap_filter = MipmapFilterMode::Nearest;
		Sampler sampler;
		Accepted("a linear sampler", [&]() {
			sampler = device.CreateSampler(sampler_descriptor);
		});

		// Anisotropy: 8 is within every device's limit, so asking for it must produce a sampler
		// rather than an error on any backend. On Vulkan this is the case that used to set
		// anisotropyEnable on a device created without samplerAnisotropy (01070).
		//
		// Every filter is linear in this descriptor. WebGPU rejects a sampler whose maxAnisotropy
		// exceeds 1 unless magFilter, minFilter *and* mipmapFilter are all linear, while Vulkan and
		// D3D12 accept any combination and let the anisotropic filter cover the min/mag filters.
		// The base sampler keeps a nearest mip filter, which is what makes that difference visible.
		auto anisotropic_descriptor = sampler_descriptor;
		anisotropic_descriptor.mipmap_filter = MipmapFilterMode::Linear;
		anisotropic_descriptor.max_anisotropy = 8u;
		Accepted("a sampler that asks for anisotropy", [&device, &anisotropic_descriptor]() {
			auto anisotropic = device.CreateSampler(anisotropic_descriptor);
			if (!anisotropic) {
				throw std::runtime_error("sampler creation returned an empty sampler");
			}
		});
		if (backend == Backend::Vulkan) {
			// Vulkan additionally has to keep maxAnisotropy inside
			// VkPhysicalDeviceLimits::maxSamplerAnisotropy (01071), so a value above the limit is
			// clamped instead of being passed through. Only Vulkan is checked here: the other
			// backends either clamp in the driver (D3D12) or report an over-limit request through
			// their own error channel (WebGPU), which would fail this probe for the wrong reason.
			auto over_limit_descriptor = anisotropic_descriptor;
			over_limit_descriptor.max_anisotropy = 255u;
			Accepted("a Vulkan sampler above the anisotropy limit", [&device, &over_limit_descriptor]() {
				auto clamped = device.CreateSampler(over_limit_descriptor);
				if (!clamped) {
					throw std::runtime_error("sampler creation returned an empty sampler");
				}
			});
		}

		// The texture lives in space 1 while the uniform stays in space 0, so a group for space 1
		// carries the texture slots and nothing else. A group has to supply every slot its space
		// reflects, and only those, which keeps each negative case below attributable to the one
		// rule it names instead of to an unrelated missing binding.
		auto const group_space = 1u;
		if (!separate_declarations) {
			// One combined slot at (slot 1, space 1): the layout declares eCombinedImageSampler, so
			// the value has to carry both halves.
			Accepted("a combined view and sampler", [&]() {
				std::array const bindings{
					ResourceBinding{
						.slot = 1u,
						.value = BindingValue::FromCombined(texture_view, sampler)
					}
				};
				(void)pipeline.CreatePipelineResourceGroup(group_space, bindings);
			});
			Rejected("a buffer in a combined texture slot", [&]() {
				std::array const bindings{
					ResourceBinding{ .slot = 1u, .value = BindingValue::FromBuffer(uniform) }
				};
				(void)pipeline.CreatePipelineResourceGroup(group_space, bindings);
			});
			Rejected("a view without its sampler in a combined slot", [&]() {
				std::array const bindings{
					ResourceBinding{ .slot = 1u, .value = BindingValue::FromView(texture_view) }
				};
				(void)pipeline.CreatePipelineResourceGroup(group_space, bindings);
			});
			Rejected("a sampler without its view in a combined slot", [&]() {
				std::array const bindings{
					ResourceBinding{ .slot = 1u, .value = BindingValue::FromSampler(sampler) }
				};
				(void)pipeline.CreatePipelineResourceGroup(group_space, bindings);
			});
			Rejected("a binding for a slot the pipeline does not declare", [&]() {
				std::array const bindings{
					ResourceBinding{
						.slot = 9u,
						.value = BindingValue::FromCombined(texture_view, sampler)
					}
				};
				(void)pipeline.CreatePipelineResourceGroup(group_space, bindings);
			});
			Rejected("a binding declared twice", [&]() {
				std::array const bindings{
					ResourceBinding{
						.slot = 1u,
						.value = BindingValue::FromCombined(texture_view, sampler)
					},
					ResourceBinding{
						.slot = 1u,
						.value = BindingValue::FromCombined(texture_view, sampler)
					}
				};
				(void)pipeline.CreatePipelineResourceGroup(group_space, bindings);
			});
			Rejected("a group that leaves a declared slot unbound", [&]() {
				std::array<ResourceBinding, 0u> bindings{};
				(void)pipeline.CreatePipelineResourceGroup(group_space, bindings);
			});
			Rejected("a group in a space the pipeline does not declare", [&]() {
				std::array const bindings{
					ResourceBinding{
						.slot = 1u,
						.value = BindingValue::FromCombined(texture_view, sampler)
					}
				};
				(void)pipeline.CreatePipelineResourceGroup(2u, bindings);
			});
		}
		else {
			// Separate declarations: the reflection decides whether the target keeps two slots
			// (texture at 1, sampler at 2) or folds them into one combined binding. A target that
			// folds them is not a failure, so the pair is tried first and the mismatch cases only
			// run where the pair is accepted.
			std::array const separate_bindings{
				ResourceBinding{ .slot = 1u, .value = BindingValue::FromView(texture_view) },
				ResourceBinding{ .slot = 2u, .value = BindingValue::FromSampler(sampler) }
			};
			bool separate_slots = true;
			try {
				(void)pipeline.CreatePipelineResourceGroup(group_space, separate_bindings);
				++g_cases;
			} catch (std::exception const& failure) {
				separate_slots = false;
				std::cout << name << ": the target folds the separate declarations into one binding ("
					<< failure.what() << ")" << std::endl;
			}
			if (separate_slots) {
				Rejected("a combined value in a plain image slot", [&]() {
					std::array const bindings{
						ResourceBinding{
							.slot = 1u,
							.value = BindingValue::FromCombined(texture_view, sampler)
						},
						ResourceBinding{ .slot = 2u, .value = BindingValue::FromSampler(sampler) }
					};
					(void)pipeline.CreatePipelineResourceGroup(group_space, bindings);
				});
				Rejected("a view in a sampler slot", [&]() {
					std::array const bindings{
						ResourceBinding{ .slot = 1u, .value = BindingValue::FromView(texture_view) },
						ResourceBinding{ .slot = 2u, .value = BindingValue::FromView(texture_view) }
					};
					(void)pipeline.CreatePipelineResourceGroup(group_space, bindings);
				});
				Rejected("a buffer in a plain image slot", [&]() {
					std::array const bindings{
						ResourceBinding{ .slot = 1u, .value = BindingValue::FromBuffer(uniform) },
						ResourceBinding{ .slot = 2u, .value = BindingValue::FromSampler(sampler) }
					};
					(void)pipeline.CreatePipelineResourceGroup(group_space, bindings);
				});
				Rejected("a separate pair with the sampler slot left unbound", [&]() {
					std::array const bindings{
						ResourceBinding{ .slot = 1u, .value = BindingValue::FromView(texture_view) }
					};
					(void)pipeline.CreatePipelineResourceGroup(group_space, bindings);
				});
			}
		}

		std::cout << name << " binding: " << g_cases << " cases passed ("
			<< (separate_declarations ? "separate" : "combined") << " declarations)" << std::endl;
		if (LogSink().HasError()) {
			throw std::runtime_error(
				"the backend reported a validation or runtime error; see the log above"
			);
		}
		return 0;
	}

} // namespace

int main(int argc, char** argv) try {
	auto const name = argc > 1 ? std::string_view{ argv[1] } : std::string_view{ "vulkan" };
	auto const mode = argc > 2 ? std::string_view{ argv[2] } : std::string_view{ "combined" };
	if (mode != "combined" && mode != "separate") {
		throw std::invalid_argument("usage: BindingProbe <backend> [combined|separate]");
	}
	return Run(std::string{ name }.c_str(), mode == "separate");
}
catch (std::exception const& error) {
	std::cerr << "Binding probe failed: " << error.what() << std::endl;
	return 1;
}
