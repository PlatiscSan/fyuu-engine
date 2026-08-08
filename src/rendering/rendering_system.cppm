module;

#if !defined(ENGINE_VER_VARIANT)
#define ENGINE_VER_VARIANT 0
#endif // !defined(ENGINE_VER_VARIANT)
#if !defined(ENGINE_VER_MAJOR)
#define ENGINE_VER_MAJOR 1
#endif // !defined(ENGINE_VER_MAJOR)
#if !defined(ENGINE_VER_MINOR)
#define ENGINE_VER_MINOR 0
#endif // !defined(ENGINE_VER_MINOR)
#if !defined(ENGINE_VER_PATCH)
#define ENGINE_VER_PATCH 0
#endif // !defined(ENGINE_VER_PATCH)

#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <format>
#include <optional>
#include <source_location>
#include <mutex>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#include <imgui.h>
#include <SDL3/SDL.h>
#if defined(__linux__)
#include <X11/Xlib.h>
#include <wayland-client-core.h>
#include <wayland-util.h>
#endif // defined(__linux__)

module fyuu_engine:rendering_system;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :application_types;
import :log;
import :platform;
import fyuu_rhi;

namespace {

	using fyuu_engine::ApplicationDescriptor;
	using fyuu_engine::Platform;
	namespace log = fyuu_engine::log;
	constexpr auto kTriFmtPos = fyuu_rhi::ResourceFlagBits::R32G32B32A32Float;
	constexpr auto kTriFmtTarget = fyuu_rhi::ResourceFlagBits::R8G8B8A8Unorm;

	void InitializeRHILogger() noexcept {
		fyuu_rhi::log::Trace = log::Trace;
		fyuu_rhi::log::Info = log::Info;
		fyuu_rhi::log::Warning = log::Warning;
		fyuu_rhi::log::Error = log::Error;
		fyuu_rhi::log::Fatal = log::Fatal;
	}

	void LogPhysicalDevice(
		fyuu_rhi::PhysicalDeviceInfo const& info,
		std::source_location const& location = std::source_location::current()
	) {
		auto TypeName = [](fyuu_rhi::PhysicalDeviceInfo::Type type) -> std::string_view {
			using Type = fyuu_rhi::PhysicalDeviceInfo::Type;
			switch (type) {
			case Type::DiscreteGPU: return "Discrete GPU";
			case Type::IntegratedGPU: return "Integrated GPU";
			case Type::CPU: return "CPU";
			case Type::Virtual: return "Virtual";
			default: return "Unknown";
			}
			};
		auto OptionalHex = [](std::optional<std::uint32_t> value) {
			return value ? std::format("0x{:04X}", *value) : std::string("N/A");
			};
		auto MemorySize = [](std::optional<std::size_t> bytes) {
			if (!bytes) return std::string("N/A");
			return std::format("{:.2f} GiB", *bytes / (1024.0 * 1024.0 * 1024.0));
			};
		log::Info(
			std::format(
				"Physical device: {} ({}, vendor {}, device {}, dedicated memory {})",
				info.name,
				TypeName(info.type),
				OptionalHex(info.vendor_id),
				OptionalHex(info.device_id),
				MemorySize(info.dedicated_memory)
			),
			location
		);
	}

	fyuu_rhi::ResourceFlags StagingBufferFlags() {
		fyuu_rhi::ResourceFlags flags;
		flags.Set(fyuu_rhi::ResourceFlagBits::CopySRC);
		flags.Set(fyuu_rhi::ResourceFlagBits::CopyDST);
		flags.Set(fyuu_rhi::ResourceFlagBits::HostVisible);
		return flags;
	}

	template <class Backend>
	struct GraphState {
		std::mutex mutex;
		std::condition_variable condition;
		std::optional<fyuu_rhi::execution::CommandGraphResources<Backend>> resources;
		std::exception_ptr error;
		bool done = false;
		bool stopped = false;

		fyuu_rhi::execution::CommandGraphResources<Backend> Wait() {
			std::unique_lock lock(mutex);
			condition.wait(lock, [this] { return done; });
			if (error) std::rethrow_exception(error);
			if (stopped || !resources) {
				throw std::runtime_error("Command graph execution was cancelled");
			}
			auto result = std::move(*resources);
			resources.reset();
			return result;
		}
	};

	template <class Backend>
	struct GraphReceiver {
		std::shared_ptr<GraphState<Backend>> state;

		struct Environment {};
		[[nodiscard]] Environment get_env() const noexcept { return {}; }

		void RecoverBindings(fyuu_rhi::execution::CommandGraphResources<Backend>&& resources) noexcept {
			std::lock_guard lock(state->mutex);
			state->resources.emplace(std::move(resources));
		}

		void set_value(fyuu_rhi::execution::CommandGraphResources<Backend>&& resources) && noexcept {
			{
				std::lock_guard lock(state->mutex);
				state->resources.emplace(std::move(resources));
				state->done = true;
			}
			state->condition.notify_one();
		}

		void set_error(std::exception_ptr error) && noexcept {
			{
				std::lock_guard lock(state->mutex);
				state->error = std::move(error);
				state->done = true;
			}
			state->condition.notify_one();
		}

		void set_stopped() && noexcept {
			{
				std::lock_guard lock(state->mutex);
				state->stopped = true;
				state->done = true;
			}
			state->condition.notify_one();
		}
	};

	template <class Backend>
	fyuu_rhi::Resource<Backend> SyncUpload(
		fyuu_rhi::LogicalDevice<Backend>& logical_device,
		fyuu_rhi::execution::CommandScheduler<Backend>& scheduler,
		fyuu_rhi::Resource<Backend>&& dest,
		std::span<std::byte const> data
	) {
		if (data.empty() || data.size() > dest.Size()) {
			throw std::invalid_argument("SyncUpload data exceeds the destination buffer size");
		}
		using namespace fyuu_rhi::execution;
		auto staging = logical_device.CreateBuffer(data.size(), StagingBufferFlags());
		auto builder = scheduler.schedule();
		auto staging_index = builder.RegisterResource();
		auto destination_index = builder.RegisterResource();
		auto transfer = builder.CreateNode(QueueType::Transfer);
		transfer.Access({ staging_index, AccessMode::Read, ResourceUsage::CopySource, {} })
			.Access({ destination_index, AccessMode::Write, ResourceUsage::CopyDestination, {} })
			.Record(WriteBuffer{
				staging_index,
				0u,
				std::vector<std::byte>(data.begin(), data.end())
				})
			.Record(CopyBuffer{ staging_index, destination_index, 0u, 0u, data.size() });
		auto state = std::make_shared<GraphState<Backend>>();
		auto operation = std::move(builder).connect(GraphReceiver<Backend>{ state });
		operation.BindResource(staging_index, std::move(staging));
		operation.BindResource(destination_index, std::move(dest));
		operation.start();
		auto resources = state->Wait();
		return resources.TakeResource(destination_index);
	}

	template <class Backend>
	fyuu_rhi::Resource<Backend> SyncUploadTexture(
		fyuu_rhi::LogicalDevice<Backend>& logical_device,
		fyuu_rhi::execution::CommandScheduler<Backend>& scheduler,
		fyuu_rhi::Resource<Backend>&& destination,
		fyuu_rhi::TextureDataLayout const& layout,
		fyuu_rhi::TextureRegion const& region,
		std::span<std::byte const> data
	) {
		if (data.empty()) {
			throw std::invalid_argument("SyncUploadTexture data must not be empty");
		}
		using namespace fyuu_rhi::execution;
		auto staging_size = layout.offset + data.size();
		auto staging = logical_device.CreateBuffer(staging_size, StagingBufferFlags());
		auto builder = scheduler.schedule();
		auto staging_index = builder.RegisterResource();
		auto destination_index = builder.RegisterResource();
		auto transfer = builder.CreateNode(QueueType::Transfer);
		transfer.Access({ staging_index, AccessMode::Read, ResourceUsage::CopySource, {} })
			.Access({ destination_index, AccessMode::Write, ResourceUsage::CopyDestination, {} })
			.Record(WriteBuffer{
				staging_index,
				layout.offset,
				std::vector<std::byte>(data.begin(), data.end())
				})
			.Record(CopyBufferToTexture{
				staging_index,
				destination_index,
				{ layout.offset, layout.bytes_per_row, layout.rows_per_image },
				region
				});
		auto state = std::make_shared<GraphState<Backend>>();
		auto operation = std::move(builder).connect(GraphReceiver<Backend>{ state });
		operation.BindResource(staging_index, std::move(staging));
		operation.BindResource(destination_index, std::move(destination));
		operation.start();
		auto resources = state->Wait();
		return resources.TakeResource(destination_index);
	}

	template <class Resource> struct ResourceBackend;
	template <class Backend>
	struct ResourceBackend<fyuu_rhi::Resource<Backend>> {
		using Type = Backend;
	};

	template <class Instance, class PhysicalDevice, class LogicalDevice>
	struct RenderingBackend {
		using Scheduler = decltype(std::declval<LogicalDevice&>().CreateScheduler());
		using Resource = decltype(std::declval<LogicalDevice&>().CreateTexture(
			std::size_t{},
			std::size_t{},
			std::size_t{},
			std::size_t{},
			std::declval<fyuu_rhi::ResourceFlags const&>()
		));
		using Backend = typename ResourceBackend<Resource>::Type;
		using View = decltype(std::declval<LogicalDevice&>().CreateTextureView(
			std::declval<Resource const&>(),
			std::size_t{},
			std::size_t{},
			std::size_t{},
			std::size_t{},
			std::declval<fyuu_rhi::ResourceFlags const&>()
		));
		using Pipeline = decltype(std::declval<LogicalDevice&>().CreateGraphicsPipeline(
			std::declval<fyuu_rhi::pipeline::GraphicsPipelineDescriptor const&>()
		));
		using Sampler = decltype(std::declval<LogicalDevice&>().CreateSampler(
			std::declval<fyuu_rhi::SamplerDescriptor const&>()
		));
		using ResourceGroup = fyuu_rhi::pipeline::PipelineResourceGroup<Backend>;

		struct FrameResources {
			struct GuiVertex {
				float position[2];
				float uv[2];
				std::uint32_t color;
			};
			struct GuiDraw {
				std::int32_t x;
				std::int32_t y;
				std::uint32_t width;
				std::uint32_t height;
				std::uint32_t index_count;
				std::uint32_t first_index;
				std::int32_t vertex_offset;
			};
			struct GuiData {
				std::vector<GuiVertex> vertices;
				std::vector<ImDrawIdx> indices;
				std::vector<GuiDraw> draws;
			};

			Resource target;
			View view;
			std::uint32_t width;
			std::uint32_t height;
			Pipeline triangle_pipeline;
			Pipeline gui_pipeline;
			Resource triangle_vertices;
			Resource font_texture;
			View font_view;
			Sampler font_sampler;
			ResourceGroup font_group;
			std::optional<Resource> gui_vertices;
			std::optional<Resource> gui_indices;
			GuiData gui_data;
			/// Font atlas generation the font texture was uploaded from; when the
			/// display DPI changes the atlas is rebuilt and the whole frame (and
			/// thus the font texture) must be recreated to match it.
			std::uint64_t font_generation = 0u;

			static fyuu_rhi::ResourceFlags TargetFlags() {
				fyuu_rhi::ResourceFlags flags;
				flags.Set(fyuu_rhi::ResourceFlagBits::DeviceLocal);
				flags.Set(fyuu_rhi::ResourceFlagBits::Texture2D);
				flags.Set(fyuu_rhi::ResourceFlagBits::RenderAttachment);
				flags.Set(fyuu_rhi::ResourceFlagBits::CopySRC);
				flags.Set(fyuu_rhi::ResourceFlagBits::R8G8B8A8Unorm);
				return flags;
			}

			static fyuu_rhi::ResourceFlags BufferFlags(fyuu_rhi::ResourceFlagBits usage) {
				fyuu_rhi::ResourceFlags flags;
				flags.Set(fyuu_rhi::ResourceFlagBits::DeviceLocal);
				flags.Set(fyuu_rhi::ResourceFlagBits::CopyDST);
				flags.Set(usage);
				return flags;
			}

			static fyuu_rhi::ResourceFlags FontFlags() {
				fyuu_rhi::ResourceFlags flags;
				flags.Set(fyuu_rhi::ResourceFlagBits::DeviceLocal);
				flags.Set(fyuu_rhi::ResourceFlagBits::Texture2D);
				flags.Set(fyuu_rhi::ResourceFlagBits::TextureView2D);
				flags.Set(fyuu_rhi::ResourceFlagBits::TextureBinding);
				flags.Set(fyuu_rhi::ResourceFlagBits::CopyDST);
				flags.Set(fyuu_rhi::ResourceFlagBits::R8G8B8A8Unorm);
				return flags;
			}

			static GuiData CollectGuiData(ImDrawData const* draw_data) {
				GuiData result;
				if (!draw_data || draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f) {
					return result;
				}
				result.vertices.reserve(static_cast<std::size_t>(draw_data->TotalVtxCount));
				result.indices.reserve(static_cast<std::size_t>(draw_data->TotalIdxCount));
				std::uint32_t vertex_base = 0u;
				std::uint32_t index_base = 0u;
				for (int list_index = 0; list_index < draw_data->CmdListsCount; ++list_index) {
					auto const* list = draw_data->CmdLists[list_index];
					for (auto const& source : list->VtxBuffer) {
						result.vertices.push_back({
							{
								(source.pos.x - draw_data->DisplayPos.x) / draw_data->DisplaySize.x * 2.0f - 1.0f,
								1.0f - (source.pos.y - draw_data->DisplayPos.y) / draw_data->DisplaySize.y * 2.0f
							},
							{ source.uv.x, source.uv.y },
							source.col
							});
					}
					result.indices.insert(result.indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
					for (auto const& command : list->CmdBuffer) {
						if (command.UserCallback || command.GetTexID() != 1u) continue;
						float left = (command.ClipRect.x - draw_data->DisplayPos.x) * draw_data->FramebufferScale.x;
						float top = (command.ClipRect.y - draw_data->DisplayPos.y) * draw_data->FramebufferScale.y;
						float right = (command.ClipRect.z - draw_data->DisplayPos.x) * draw_data->FramebufferScale.x;
						float bottom = (command.ClipRect.w - draw_data->DisplayPos.y) * draw_data->FramebufferScale.y;
						left = std::max(left, 0.0f);
						top = std::max(top, 0.0f);
						right = std::min(right, draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
						bottom = std::min(bottom, draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
						if (right <= left || bottom <= top) continue;
						result.draws.push_back({
							static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
							static_cast<std::uint32_t>(right - left), static_cast<std::uint32_t>(bottom - top),
							command.ElemCount, index_base + command.IdxOffset,
							static_cast<std::int32_t>(vertex_base + command.VtxOffset)
							});
					}
					vertex_base += static_cast<std::uint32_t>(list->VtxBuffer.Size);
					index_base += static_cast<std::uint32_t>(list->IdxBuffer.Size);
				}
				return result;
			}

			void SubmitRender(
				Scheduler& scheduler,
				typename Backend::PlatformHandle const& presentation_target,
				ApplicationDescriptor const& application
			) {
				using namespace fyuu_rhi::execution;
				bool gui = !gui_data.draws.empty();
				auto builder = scheduler.schedule();
				auto target_index = builder.RegisterResource();
				auto vertex_index = builder.RegisterResource();
				auto view_index = builder.RegisterView();
				auto triangle_pipeline_index = builder.RegisterPipeline();
				std::optional<std::size_t> gui_vertex_index;
				std::optional<std::size_t> gui_index_index;
				std::optional<std::size_t> font_index;
				std::optional<std::size_t> gui_pipeline_index;
				std::optional<std::size_t> font_group_index;
				if (gui) {
					gui_vertex_index = builder.RegisterResource();
					gui_index_index = builder.RegisterResource();
					font_index = builder.RegisterResource();
					gui_pipeline_index = builder.RegisterPipeline();
					font_group_index = builder.RegisterResourceGroup();
				}

				auto render = builder.CreateNode(QueueType::Graphics);
				render.Access({ target_index, AccessMode::Write, ResourceUsage::ColorAttachment, {} })
					.Access({ vertex_index, AccessMode::Read, ResourceUsage::VertexBuffer, {} });
				if (gui) {
					render.Access({ *gui_vertex_index, AccessMode::Read, ResourceUsage::VertexBuffer, {} })
						.Access({ *gui_index_index, AccessMode::Read, ResourceUsage::IndexBuffer, {} })
						.Access({ *font_index, AccessMode::Read, ResourceUsage::Sampled, {} });
				}
				render.Record(BindPipeline{ triangle_pipeline_index })
					.Record(BindVertexBuffer{ vertex_index, 0u, 32u, 0u })
					.Record(BeginRendering{
						{ 0, 0, width, height },
						{{
							ColorAttachment{
								target_index,
								view_index,
								LoadOperation::Clear,
								StoreOperation::Store,
								application.clear_color,
								std::nullopt,
								std::nullopt
							}
						}},
						{}
						})
					.Record(Viewport{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f, application.clip_space })
					.Record(Scissor{ 0, 0, width, height })
					.Record(Draw{ 3u, 1u, 0u, 0u });
				if (gui) {
					render.Record(BindPipeline{ *gui_pipeline_index })
						.Record(BindResourceGroup{ *font_group_index, 0u })
						.Record(BindVertexBuffer{
							*gui_vertex_index, 0u, static_cast<std::uint32_t>(sizeof(GuiVertex)), 0u })
							.Record(BindIndexBuffer{
								*gui_index_index,
								sizeof(ImDrawIdx) == 4u ? IndexType::Uint32 : IndexType::Uint16,
								0u
								});
					for (auto const& draw : gui_data.draws) {
						render.Record(Scissor{ draw.x, draw.y, draw.width, draw.height })
							.Record(DrawIndexed{
								draw.index_count, 1u, draw.first_index, draw.vertex_offset, 0u
								});
					}
				}
				render.Record(EndRendering{});

				auto present = builder.CreateNode(QueueType::Present, render);
				present.Access({ target_index, AccessMode::Read, ResourceUsage::PresentationSource, {} })
					.Record(Present{
						target_index, 0u, application.frames_in_flight, application.vertical_sync
						});

				auto state = std::make_shared<GraphState<Backend>>();
				auto operation = std::move(builder).connect(GraphReceiver<Backend>{ state });
				operation.BindResource(target_index, std::move(target));
				operation.BindResource(vertex_index, std::move(triangle_vertices));
				operation.BindView(view_index, std::move(view));
				operation.BindPipeline(triangle_pipeline_index, std::move(triangle_pipeline));
				if (gui) {
					operation.BindResource(*gui_vertex_index, std::move(*gui_vertices));
					operation.BindResource(*gui_index_index, std::move(*gui_indices));
					operation.BindResource(*font_index, std::move(font_texture));
					operation.BindPipeline(*gui_pipeline_index, std::move(gui_pipeline));
					operation.BindResourceGroup(*font_group_index, std::move(font_group));
				}
				operation.SetPresentationTarget(presentation_target);
				operation.start();

				auto resources = state->Wait();
				target = resources.TakeResource(target_index);
				triangle_vertices = resources.TakeResource(vertex_index);
				view = resources.TakeView(view_index);
				triangle_pipeline = resources.TakePipeline(triangle_pipeline_index);
				if (gui) {
					gui_vertices.emplace(resources.TakeResource(*gui_vertex_index));
					gui_indices.emplace(resources.TakeResource(*gui_index_index));
					font_texture = resources.TakeResource(*font_index);
					gui_pipeline = resources.TakePipeline(*gui_pipeline_index);
					font_group = resources.TakeResourceGroup(*font_group_index);
				}
			}

			FrameResources(
				LogicalDevice& logical_device,
				Scheduler& scheduler,
				ApplicationDescriptor const& application,
				ImDrawData const* draw_data
			) : target(logical_device.CreateTexture(
				application.surface_width,
				application.surface_height,
				1u,
				1u,
				TargetFlags()
			)),
				view(logical_device.CreateTextureView(
					target,
					0u,
					1u,
					0u,
					1u,
					TargetFlags()
				)),
				width(application.surface_width),
				height(application.surface_height),
				triangle_pipeline(CreateTrianglePipeline(logical_device)),
				gui_pipeline(CreateGuiPipeline(logical_device)),
				triangle_vertices(logical_device.CreateBuffer(
					sizeof(kTriangleVertices), BufferFlags(fyuu_rhi::ResourceFlagBits::VertexBuffer))),
				font_texture(CreateFontTexture(logical_device, scheduler)),
				font_view(logical_device.CreateTextureView(
					font_texture, 0u, 1u, 0u, 1u, FontFlags())),
				font_sampler(logical_device.CreateSampler({
					.address_mode_u = fyuu_rhi::AddressMode::ClampToEdge,
					.address_mode_v = fyuu_rhi::AddressMode::ClampToEdge,
					.address_mode_w = fyuu_rhi::AddressMode::ClampToEdge,
					.mag_filter = fyuu_rhi::FilterMode::Linear,
					.min_filter = fyuu_rhi::FilterMode::Linear,
					.mipmap_filter = fyuu_rhi::MipmapFilterMode::Linear,
					.max_lod = 0.0f
					})),
				font_group(CreateFontGroup(logical_device, gui_pipeline, font_view, font_sampler)) {
				triangle_vertices = SyncUpload(logical_device, scheduler,
					std::move(triangle_vertices),
					std::span<std::byte const>{
					reinterpret_cast<std::byte const*>(kTriangleVertices),
						sizeof(kTriangleVertices)
				}
				);
				Rebuild(logical_device, scheduler, application, draw_data);
			}

			void Rebuild(
				LogicalDevice& logical_device,
				Scheduler& scheduler,
				ApplicationDescriptor const& application,
				ImDrawData const* draw_data
			) {
				auto gui = CollectGuiData(draw_data);
				auto vertex_bytes = std::as_bytes(std::span(gui.vertices));
				auto index_bytes = std::as_bytes(std::span(gui.indices));
				std::vector<std::byte> aligned_index_bytes;
				if constexpr (std::same_as<LogicalDevice, fyuu_rhi::WebGPULogicalDevice>) {
					if (index_bytes.size() % 4u != 0u) {
						aligned_index_bytes.resize((index_bytes.size() + 3u) & ~std::size_t{ 3u });
						std::ranges::copy(index_bytes, aligned_index_bytes.begin());
						index_bytes = aligned_index_bytes;
					}
				}
				if (!gui_vertices || gui_vertices->Size() < vertex_bytes.size()) {
					gui_vertices.emplace(logical_device.CreateBuffer(
						std::max<std::size_t>(vertex_bytes.size(), 1u),
						BufferFlags(fyuu_rhi::ResourceFlagBits::VertexBuffer)));
				}
				if (!gui_indices || gui_indices->Size() < index_bytes.size()) {
					gui_indices.emplace(logical_device.CreateBuffer(
						std::max<std::size_t>(index_bytes.size(), 1u),
						BufferFlags(fyuu_rhi::ResourceFlagBits::IndexBuffer)));
				}
				if (!vertex_bytes.empty()) {
					*gui_vertices = SyncUpload(logical_device, scheduler, std::move(*gui_vertices), vertex_bytes);
				}
				if (!index_bytes.empty()) {
					*gui_indices = SyncUpload(logical_device, scheduler, std::move(*gui_indices), index_bytes);
				}
				gui_data = std::move(gui);
			}

		private:
			static constexpr float kTriangleVertices[] = {
				// position            color
				 0.0f,  0.5f, 0.0f, 1.0f,   1.0f, 0.0f, 0.0f, 1.0f,
				 0.5f, -0.5f, 0.0f, 1.0f,   0.0f, 1.0f, 0.0f, 1.0f,
				-0.5f, -0.5f, 0.0f, 1.0f,   0.0f, 0.0f, 1.0f, 1.0f,
			};

			static Pipeline CreateTrianglePipeline(LogicalDevice& ld) {
				using namespace fyuu_rhi::pipeline;
				static constexpr char const kShaderSource[] = R"(
						struct VSOut { float4 pos : SV_Position; float3 color : COLOR; };
						[shader("vertex")]
						VSOut vs_main(float3 pos : POSITION, float3 color : COLOR) {
							VSOut o;
							o.pos = float4(pos, 1.0);
							o.color = color;
							return o;
						}
						[shader("fragment")]
						float4 fs_main(VSOut input) : SV_Target0 {
							return float4(input.color, 1.0);
						}
					)";
				return ld.CreateGraphicsPipeline({
					.program = {
						.modules = {{{
							SlangPipelineProgramDescriptor::Module{
								.name = "tri",
								.source = kShaderSource
							}
						}}},
						.entry_points = {{
							{.name = "vs_main", .stage = PipelineStage::Vertex},
							{.name = "fs_main", .stage = PipelineStage::Fragment}
						}}
					},
					.vertex = {
						.buffers = {{ VertexBufferLayout{.slot = 0, .stride = 32 } }},
						.attributes = {{
							{.location = 0, .slot = 0, .offset = 0,
								.format = kTriFmtPos},
							{.location = 1, .slot = 0, .offset = 16,
								.format = kTriFmtPos}
						}}
					},
					.color_targets = {{ ColorTargetState{.format = kTriFmtTarget } }}
					});
			}

			static Resource CreateFontTexture(LogicalDevice& logical_device, Scheduler& scheduler) {
				unsigned char* pixels = nullptr;
				int width = 0;
				int height = 0;
				ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
				if (!pixels || width <= 0 || height <= 0) {
					throw std::runtime_error("Dear ImGui did not produce a valid font atlas");
				}
				auto texture = logical_device.CreateTexture(
					static_cast<std::size_t>(width), static_cast<std::size_t>(height), 1u, 1u, FontFlags());
				fyuu_rhi::TextureDataLayout layout{
					.offset = 0u,
					.bytes_per_row = static_cast<std::uint32_t>(width * 4),
					.rows_per_image = static_cast<std::uint32_t>(height)
				};
				fyuu_rhi::TextureRegion region{
					.array_layer_count = 1u,
					.width = static_cast<std::uint32_t>(width),
					.height = static_cast<std::uint32_t>(height),
					.depth = 1u
				};
				ImGui::GetIO().Fonts->SetTexID(1u);
				return SyncUploadTexture(
					logical_device, scheduler, std::move(texture), layout, region,
					std::span<std::byte const>{
					reinterpret_cast<std::byte const*>(pixels),
						static_cast<std::size_t>(width)* static_cast<std::size_t>(height) * 4u
				}
				);
			}

			static Pipeline CreateGuiPipeline(LogicalDevice& logical_device) {
				using namespace fyuu_rhi::pipeline;
				static constexpr char const webgpu_shader[] = R"(
						[[vk::binding(0, 0)]] Texture2D<float4> font_texture : register(t0, space0);
						[[vk::binding(1, 0)]] SamplerState font_sampler : register(s1, space0);
						struct VSOut {
							float4 position : SV_Position;
							float2 uv : TEXCOORD0;
							float4 color : COLOR0;
						};
						[shader("vertex")]
						VSOut vs_main(float2 position : POSITION, float2 uv : TEXCOORD0, float4 color : COLOR0) {
							VSOut output;
							output.position = float4(position, 0.0, 1.0);
							output.uv = uv;
							output.color = color;
							return output;
						}
						[shader("fragment")]
						float4 fs_main(VSOut input) : SV_Target0 {
							return input.color * font_texture.Sample(font_sampler, input.uv);
						}
					)";
				static constexpr char const combined_shader[] = R"(
						Sampler2D font_texture : register(t0, space0);
						struct VSOut {
							float4 position : SV_Position;
							float2 uv : TEXCOORD0;
							float4 color : COLOR0;
						};
						[shader("vertex")]
						VSOut vs_main(float2 position : POSITION, float2 uv : TEXCOORD0, float4 color : COLOR0) {
							VSOut output;
							output.position = float4(position, 0.0, 1.0);
							output.uv = uv;
							output.color = color;
							return output;
						}
						[shader("fragment")]
						float4 fs_main(VSOut input) : SV_Target0 {
							return input.color * font_texture.Sample(input.uv);
						}
					)";
				// WebGPU requires distinct texture and sampler bind-group entries.
				// Other backends retain Sampler2D because NVIDIA's desktop GLSL
				// compiler rejects Slang's separable texture/sampler output.
				auto shader = std::same_as<LogicalDevice, fyuu_rhi::WebGPULogicalDevice>
					? webgpu_shader
					: combined_shader;
				return logical_device.CreateGraphicsPipeline({
					.program = {
						.modules = {{{ SlangPipelineProgramDescriptor::Module{.name = "imgui", .source = shader } }}},
						.entry_points = {{
							{.name = "vs_main", .stage = PipelineStage::Vertex },
							{.name = "fs_main", .stage = PipelineStage::Fragment }
						}}
					},
					.vertex = {
						.buffers = {{ VertexBufferLayout{.slot = 0u, .stride = sizeof(GuiVertex) } }},
						.attributes = {{
							{.location = 0u, .slot = 0u, .offset = 0u,
								.format = fyuu_rhi::ResourceFlagBits::R32G32Float },
							{.location = 1u, .slot = 0u, .offset = 8u,
								.format = fyuu_rhi::ResourceFlagBits::R32G32Float },
							{.location = 2u, .slot = 0u, .offset = 16u,
								.format = fyuu_rhi::ResourceFlagBits::R8G8B8A8Unorm }
						}}
					},
					.color_targets = {{ ColorTargetState{
						.format = kTriFmtTarget,
						.blend = BlendState{
							.color = { BlendFactor::SourceAlpha, BlendFactor::OneMinusSourceAlpha, BlendOperation::Add },
							.alpha = { BlendFactor::One, BlendFactor::OneMinusSourceAlpha, BlendOperation::Add }
						}
					} }}
					});
			}

			static ResourceGroup CreateFontGroup(
				LogicalDevice& logical_device,
				Pipeline const& pipeline,
				View const& view,
				Sampler const& sampler
			) {
				using Binding = fyuu_rhi::pipeline::PipelineResourceBinding<Backend>;
				using Value = fyuu_rhi::pipeline::PipelineBindingValue<Backend>;
				if constexpr (std::same_as<LogicalDevice, fyuu_rhi::WebGPULogicalDevice>) {
					std::array bindings{
						Binding{.slot = 0u, .value = Value::FromView(view) },
						Binding{.slot = 1u, .value = Value::FromSampler(sampler) }
					};
					return logical_device.CreatePipelineResourceGroup(pipeline, 0u, bindings);
				}
				else {
					std::array bindings{
						Binding{.slot = 0u, .value = Value::FromCombined(view, sampler) }
					};
					return logical_device.CreatePipelineResourceGroup(pipeline, 0u, bindings);
				}
			}
		};

		Instance& instance;
		PhysicalDevice physical_device;
		LogicalDevice logical_device;
		Scheduler scheduler;
		std::optional<FrameResources> frame;

		static PhysicalDevice SelectPhysicalDevice(Instance const& instance) {
			auto physical_devices = instance.EnumeratePhysicalDevices();
			return fyuu_rhi::BestPerformance(physical_devices);
		}

		explicit RenderingBackend(Instance& instance_)
			: instance(instance_),
			physical_device(SelectPhysicalDevice(instance_)),
			logical_device(physical_device.CreateLogicalDevice()),
			scheduler(logical_device.CreateScheduler()) {
			LogPhysicalDevice(physical_device.GetInfo());
		}

		void Render(
			typename Backend::PlatformHandle const& presentation_target,
			ApplicationDescriptor const& application,
			ImDrawData const* draw_data,
			std::uint64_t font_generation
		) {
			if (!frame || frame->width != application.surface_width ||
				frame->height != application.surface_height ||
				frame->font_generation != font_generation) {
				frame.emplace(logical_device, scheduler, application, draw_data);
				frame->font_generation = font_generation;
			}
			else {
				frame->Rebuild(logical_device, scheduler, application, draw_data);
			}
			frame->SubmitRender(scheduler, presentation_target, application);
		}
	};

	fyuu_rhi::Version RHIApplicationVersion(ApplicationDescriptor const& application) noexcept {
		return {
			.variant = application.version.variant,
			.major = application.version.major,
			.minor = application.version.minor,
			.patch = application.version.patch
		};
	}

	constexpr fyuu_rhi::Version EngineVersion{
		.variant = ENGINE_VER_VARIANT,
		.major = ENGINE_VER_MAJOR,
		.minor = ENGINE_VER_MINOR,
		.patch = ENGINE_VER_PATCH
	};

#if defined(__linux__)
	class NotX11 : public std::runtime_error {
	public:
		NotX11() : std::runtime_error("SDL window is not using X11") {

		}
	};

	std::pair<Display*, Window> X11Window(SDL_Window* window) {
		auto properties = SDL_GetWindowProperties(window);
		if (!properties) {
			throw std::runtime_error(std::format(
				"Calling SDL_GetWindowProperties(), SDL reports {}",
				SDL_GetError()
			));
		}
		auto display = static_cast<Display*>(SDL_GetPointerProperty(
			properties,
			SDL_PROP_WINDOW_X11_DISPLAY_POINTER,
			nullptr
		));
		if (!display) {
			throw NotX11{};
		}
		auto window_id = SDL_GetNumberProperty(
			properties,
			SDL_PROP_WINDOW_X11_WINDOW_NUMBER,
			0
		);
		if (window_id == 0) {
			throw std::runtime_error("SDL did not provide an X11 window handle");
		}
		return { display, static_cast<Window>(window_id) };
	}

	std::pair<wl_display*, wl_surface*> WaylandWindow(SDL_Window* window) {
		auto properties = SDL_GetWindowProperties(window);
		if (!properties) {
			throw std::runtime_error(std::format(
				"Calling SDL_GetWindowProperties(), SDL reports {}",
				SDL_GetError()
			));
		}
		auto display = static_cast<wl_display*>(SDL_GetPointerProperty(
			properties,
			SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER,
			nullptr
		));
		auto surface = static_cast<wl_surface*>(SDL_GetPointerProperty(
			properties,
			SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER,
			nullptr
		));
		if (!display || !surface) {
			throw std::runtime_error("SDL did not provide Wayland display and surface handles");
		}
		return { display, surface };
	}
#endif // defined(__linux__)

	template <class Backend>
	typename Backend::PlatformHandle PresentationTarget(Platform const& platform) {
#if defined(_WIN32)
		return reinterpret_cast<typename Backend::PlatformHandle>(
			platform.NativeWindow()
			);
#elif defined(__linux__)
		try {
			auto [display, window] = X11Window(platform.MainWindow());
			return typename Backend::X11PlatformHandle{ display, window };
		}
		catch (NotX11 const&) {
			auto [display, surface] = WaylandWindow(platform.MainWindow());
			return typename Backend::WaylandPlatformHandle{ display, surface };
		}
#else
		throw std::runtime_error("Presentation target is not connected on this platform");
#endif // defined(_WIN32)
	}

}

namespace fyuu_engine {

	class RenderingSystem {
	private:
#if defined(_WIN32)
		using D3D12Backend = RenderingBackend<
			fyuu_rhi::D3D12Instance,
			fyuu_rhi::D3D12PhysicalDevice,
			fyuu_rhi::D3D12LogicalDevice
		>;
#endif // defined(_WIN32)
#if !defined(__APPLE__)
		using VulkanBackend = RenderingBackend<
			fyuu_rhi::VulkanInstance,
			fyuu_rhi::VulkanPhysicalDevice,
			fyuu_rhi::VulkanLogicalDevice
		>;
		using OpenGLBackend = RenderingBackend<
			fyuu_rhi::OpenGLInstance,
			fyuu_rhi::OpenGLPhysicalDevice,
			fyuu_rhi::OpenGLLogicalDevice
		>;
#endif // !defined(__APPLE__)
		using WebGPUBackend = RenderingBackend<
			fyuu_rhi::WebGPUInstance,
			fyuu_rhi::WebGPUPhysicalDevice,
			fyuu_rhi::WebGPULogicalDevice
		>;

		using Backend = std::variant<
			std::monostate,
#if defined(_WIN32)
			D3D12Backend,
#endif // defined(_WIN32)
#if !defined(__APPLE__)
			VulkanBackend,
			OpenGLBackend,
#endif // !defined(__APPLE__)
			WebGPUBackend
		>;

		Backend m_backend;
		std::chrono::steady_clock::time_point m_last_frame_time{};
		bool m_imgui_initialized = false;
		/// Font atlas state: base size from the application descriptor, the DPI
		/// scale the atlas was rasterized at, and a generation counter bumped
		/// whenever the atlas is rebuilt so the font texture is re-uploaded.
		float m_font_size = 13.0f;
		float m_font_scale = 1.0f;
		std::uint64_t m_font_generation = 0u;

		void InitializeD3D12(ApplicationDescriptor const& application) {
#if defined(_WIN32)
			auto version = RHIApplicationVersion(application);
			fyuu_rhi::D3D12Instance::Initialize(
				application.name,
				version,
				"FyuuEngine",
				EngineVersion
			);
			m_backend.emplace<D3D12Backend>(*fyuu_rhi::D3D12Instance::Get());
#else
			throw std::invalid_argument("D3D12 is unavailable on this platform");
#endif // defined(_WIN32)
		}

		void InitializeVulkan(ApplicationDescriptor const& application) {
#if !defined(__APPLE__)
			auto version = RHIApplicationVersion(application);
			fyuu_rhi::VulkanInstance::Initialize(
				application.name,
				version,
				"FyuuEngine",
				EngineVersion
			);
			m_backend.emplace<VulkanBackend>(*fyuu_rhi::VulkanInstance::Get());
#else
			throw std::invalid_argument("Vulkan is unavailable on this platform");
#endif // !defined(__APPLE__)
		}

		void InitializeOpenGL(
			Platform const& platform,
			ApplicationDescriptor const& application
		) {
#if defined(_WIN32)
			auto version = RHIApplicationVersion(application);
			fyuu_rhi::OpenGLInstance::Initialize(
				application.name,
				version,
				"FyuuEngine",
				EngineVersion,
				PresentationTarget<OpenGLBackend::Backend>(platform)
			);
			m_backend.emplace<OpenGLBackend>(*fyuu_rhi::OpenGLInstance::Get());
#elif defined(__linux__)
			auto version = RHIApplicationVersion(application);
			try {
				auto [display, window] = X11Window(platform.MainWindow());
				fyuu_rhi::OpenGLInstance::Initialize(
					application.name,
					version,
					"FyuuEngine",
					EngineVersion,
					display,
					window
				);
			}
			catch (NotX11 const&) {
				auto [display, surface] = WaylandWindow(platform.MainWindow());
				fyuu_rhi::OpenGLInstance::Initialize(
					application.name,
					version,
					"FyuuEngine",
					EngineVersion,
					display,
					surface,
					application.surface_width,
					application.surface_height
				);
			}
			m_backend.emplace<OpenGLBackend>(*fyuu_rhi::OpenGLInstance::Get());
#else
			throw std::runtime_error("OpenGL Android platform context is not connected yet");
#endif // defined(_WIN32)
		}

		void InitializeWebGPU(ApplicationDescriptor const& application) {
#if defined(__ANDROID__)
			throw std::runtime_error("WebGPU Android application state is not connected yet");
#else
			auto version = RHIApplicationVersion(application);
			fyuu_rhi::WebGPUInstance::Initialize(
				application.name,
				version,
				"FyuuEngine",
				EngineVersion
			);
			m_backend.emplace<WebGPUBackend>(*fyuu_rhi::WebGPUInstance::Get());
#endif // defined(__ANDROID__)
		}

	public:
		RenderingSystem() noexcept = default;
		RenderingSystem(RenderingSystem const&) = delete;
		RenderingSystem& operator=(RenderingSystem const&) = delete;
		RenderingSystem(RenderingSystem&&) = delete;
		RenderingSystem& operator=(RenderingSystem&&) = delete;
		~RenderingSystem() noexcept {
			Shutdown();
		}

		void Initialize(
			Platform const& platform,
			ApplicationDescriptor const& application
		) {
			if (!std::holds_alternative<std::monostate>(m_backend)) {
				throw std::logic_error("RenderingSystem can only be initialized once");
			}
			InitializeRHILogger();
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			m_imgui_initialized = true;
			auto& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
			io.BackendPlatformName = "fyuu_sdl3";
			io.BackendRendererName = "fyuu_rhi";
			m_font_size = application.font_size;
			int logical_width = 0;
			int logical_height = 0;
			int pixel_width = 0;
			int pixel_height = 0;
			SDL_GetWindowSize(platform.MainWindow(), &logical_width, &logical_height);
			SDL_GetWindowSizeInPixels(platform.MainWindow(), &pixel_width, &pixel_height);
			m_font_scale = std::max(
				logical_width > 0 ? static_cast<float>(pixel_width) / logical_width : 1.0f,
				logical_height > 0 ? static_cast<float>(pixel_height) / logical_height : 1.0f
			);
			ImFontConfig font_config;
			font_config.SizePixels = m_font_size * m_font_scale;
			io.Fonts->AddFontDefault(&font_config);
			io.FontGlobalScale = 1.0f / m_font_scale;
			ImGui::StyleColorsDark();
			auto const& api = application.graphics_api;
			if (api == "platformdefault") {
#if defined(_WIN32)
				InitializeD3D12(application);
#elif defined(__APPLE__)
				throw std::runtime_error("Metal RHI backend is not implemented");
#else
				InitializeVulkan(application);
#endif // defined(_WIN32)
			}
			else if (api == "d3d12") {
				InitializeD3D12(application);
			}
			else if (api == "vulkan") {
				InitializeVulkan(application);
			}
			else if (api == "opengl") {
				InitializeOpenGL(platform, application);
			}
			else if (api == "webgpu") {
				InitializeWebGPU(application);
			}
			else {
				throw std::invalid_argument("Unknown graphics API: " + api);
			}
		}

		void Shutdown() noexcept {
			m_backend.emplace<std::monostate>();
			if (m_imgui_initialized) {
				ImGui::DestroyContext();
				m_imgui_initialized = false;
			}
		}

		void BeginFrame(Platform const& platform) {
			if (!m_imgui_initialized) return;
			auto& io = ImGui::GetIO();
			unsigned char* font_pixels = nullptr;
			int font_width = 0;
			int font_height = 0;
			io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
			if (!font_pixels || font_width <= 0 || font_height <= 0) {
				throw std::runtime_error("Dear ImGui did not produce a valid font atlas");
			}
			io.Fonts->SetTexID(1u);
			int logical_width = 0;
			int logical_height = 0;
			int pixel_width = 0;
			int pixel_height = 0;
			SDL_GetWindowSize(platform.MainWindow(), &logical_width, &logical_height);
			SDL_GetWindowSizeInPixels(platform.MainWindow(), &pixel_width, &pixel_height);
			io.DisplaySize = ImVec2(static_cast<float>(logical_width), static_cast<float>(logical_height));
			io.DisplayFramebufferScale = ImVec2(
				logical_width > 0 ? static_cast<float>(pixel_width) / logical_width : 1.0f,
				logical_height > 0 ? static_cast<float>(pixel_height) / logical_height : 1.0f
			);
			// Rebuild the font atlas when the display DPI changes so text stays
			// crisp at the new scale; the render backend re-uploads the font
			// texture because the generation counter changed.
			float font_scale = std::max(io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
			if (font_scale > m_font_scale + 0.01f || font_scale < m_font_scale - 0.01f) {
				io.Fonts->Clear();
				ImFontConfig font_config;
				font_config.SizePixels = m_font_size * font_scale;
				io.Fonts->AddFontDefault(&font_config);
				io.FontGlobalScale = 1.0f / font_scale;
				io.Fonts->Build();
				m_font_scale = font_scale;
				++m_font_generation;
			}
			auto now = std::chrono::steady_clock::now();
			io.DeltaTime = m_last_frame_time.time_since_epoch().count() == 0
				? 1.0f / 60.0f
				: std::max(std::chrono::duration<float>(now - m_last_frame_time).count(), 0.000001f);
			m_last_frame_time = now;
			float mouse_x = 0.0f;
			float mouse_y = 0.0f;
			auto mouse = SDL_GetMouseState(&mouse_x, &mouse_y);
			io.AddMousePosEvent(mouse_x, mouse_y);
			io.AddMouseButtonEvent(0, (mouse & SDL_BUTTON_LMASK) != 0u);
			io.AddMouseButtonEvent(1, (mouse & SDL_BUTTON_RMASK) != 0u);
			io.AddMouseButtonEvent(2, (mouse & SDL_BUTTON_MMASK) != 0u);
			ImGui::NewFrame();
		}

		void Render(
			Platform const& platform,
			ApplicationDescriptor const& application
		) {
			if (!m_imgui_initialized) return;
			ImGui::Render();
			auto* draw_data = ImGui::GetDrawData();
			auto RenderBackend = [&platform, &application, draw_data, font_generation = m_font_generation](auto& backend) {
				using State = std::remove_cvref_t<decltype(backend)>;
				if constexpr (!std::same_as<State, std::monostate>) {
					backend.Render(
						PresentationTarget<typename State::Backend>(platform),
						application,
						draw_data,
						font_generation
					);
				}
				};
			std::visit(RenderBackend, m_backend);
		}

		[[nodiscard]] bool Initialized() const noexcept {
			return !m_backend.valueless_by_exception() &&
				!std::holds_alternative<std::monostate>(m_backend);
		}
	};

}
