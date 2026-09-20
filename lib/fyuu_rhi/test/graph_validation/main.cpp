// Exercises the command graph compiler without a device, a window, or a backend.
//
// Every graph in this file is built from the public builder and handed to connect(), which is
// where CompileExecutionPlan validates and plans it. Nothing is started, so the compilation is
// pure CPU work: the test runs on any machine, in a fraction of a second, and does not need
// InitializeRHIContext(). That makes the validation rules below the cheapest part of the suite
// to keep honest, and they are otherwise only reachable through a backend that happens to make
// the same mistake.
//
// The rules come from execution.cppm's CommandValidator and CompileExecutionPlan. Each case
// names the contract it pins, so a failure reads as "the compiler accepted X" instead of only
// reporting a count.
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_rhi;

namespace {

	using namespace fyuu_rhi::execution;

	using Resources = fyuu_rhi::execution::CommandGraphResources;
	using Builder = fyuu_rhi::execution::CommandGraphBuilder;

	/// Minimal receiver: the compiler runs inside connect(), so nothing here is ever signalled.
	/// It only has to satisfy CommandGraphReceiver.
	struct Receiver {
		struct Environment {
		};

		Environment get_env() const noexcept {
			return {};
		}

		void RecoverBindings(Resources&&) noexcept {
		}

		void set_value(Resources&&) && noexcept {
		}

		void set_error(std::exception_ptr) && noexcept {
		}

		void set_stopped() && noexcept {
		}
	};

	/// A fresh scheduler is enough: the context is only needed once a graph is executed, and this
	/// test never starts one.
	CommandGraphBuilder MakeBuilder() {
		return CommandScheduler{}.schedule();
	}

	std::exception_ptr TryConnect(CommandGraphBuilder&& builder) {
		try {
			auto operation = std::move(builder).connect(Receiver{});
			(void)operation;
		} catch (...) {
			return std::current_exception();
		}
		return {};
	}

	std::size_t g_cases = 0u;

	/// Pins that the compiler rejects a graph, and reports the case name when it does not.
	template <class Build>
	void Rejected(std::string_view name, Build&& build) {
		++g_cases;
		auto builder = MakeBuilder();
		build(builder);
		auto error = TryConnect(std::move(builder));
		if (!error) {
			throw std::runtime_error(std::format("the graph compiler accepted {}", name));
		}
	}

	/// Pins that a well-formed graph compiles. A rejection here is a false negative in the
	/// compiler, which is otherwise invisible: nothing else in the suite compiles a graph whose
	/// every stage is populated.
	template <class Build>
	void Accepted(std::string_view name, Build&& build) {
		++g_cases;
		auto builder = MakeBuilder();
		build(builder);
		auto error = TryConnect(std::move(builder));
		if (error) {
			try {
				std::rethrow_exception(error);
			} catch (std::exception const& failure) {
				throw std::runtime_error(
					std::format("the graph compiler rejected {}: {}", name, failure.what())
				);
			}
		}
	}

	void TestAccessDeclarations() {
		Rejected("an access whose usage is None", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			builder.CreateNode(QueueType::Graphics)
				.Access({ resource, AccessMode::Read, ResourceUsage::None, {} });
		});
		Rejected("an access naming a resource that was never registered", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			builder.CreateNode(QueueType::Graphics)
				.Access({ resource, AccessMode::Read, ResourceUsage::Uniform, {} })
				.Access({ resource + 4u, AccessMode::Read, ResourceUsage::Uniform, {} });
		});
		Rejected("an empty buffer range", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			builder.CreateNode(QueueType::Graphics)
				.Access(
					{
						resource,
						AccessMode::Read,
						ResourceUsage::Uniform,
						BufferRange{ 0u, 0u }
					}
				);
		});
		Rejected("a buffer range that overflows size_t", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			builder.CreateNode(QueueType::Graphics)
				.Access(
					{
						resource,
						AccessMode::Read,
						ResourceUsage::Uniform,
						BufferRange{ (std::numeric_limits<std::size_t>::max)(), 8u }
					}
				);
		});
		Rejected("a texture range with no mip levels", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			builder.CreateNode(QueueType::Graphics)
				.Access(
					{
						resource,
						AccessMode::Read,
						ResourceUsage::Sampled,
						TextureRange{ 0u, 0u, 0u, 1u }
					}
				);
		});
		Rejected("a texture range with no array layers", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			builder.CreateNode(QueueType::Graphics)
				.Access(
					{
						resource,
						AccessMode::Read,
						ResourceUsage::Sampled,
						TextureRange{ 0u, 1u, 0u, 0u }
					}
				);
		});
	}

	void TestDependencies() {
		Rejected("a node that depends on itself", [](Builder& builder) {
			auto node = builder.CreateNode(QueueType::Graphics);
			node.DependsOn(node);
		});
		Rejected("a duplicated dependency", [](Builder& builder) {
			auto const first = builder.CreateNode(QueueType::Transfer);
			auto second = builder.CreateNode(QueueType::Graphics);
			second.DependsOn(first).DependsOn(first);
		});
		Rejected("a dependency cycle", [](Builder& builder) {
			auto first = builder.CreateNode(QueueType::Graphics);
			auto second = builder.CreateNode(QueueType::Graphics, first);
			first.DependsOn(second);
		});
	}

	void TestRenderingScopes() {
		Rejected("EndRendering with no matching BeginRendering", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics).Record(EndRendering{});
		});
		Rejected("a nested BeginRendering", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics)
				.Record(BeginRendering{ .area = { 0, 0, 4u, 4u } })
				.Record(BeginRendering{ .area = { 0, 0, 4u, 4u } });
		});
		Rejected("a rendering scope that never ends", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics)
				.Record(BeginRendering{ .area = { 0, 0, 4u, 4u } });
		});
		Rejected("a Draw outside a rendering scope", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics).Record(Draw{ 3u });
		});
		Rejected("a DrawIndexed outside a rendering scope", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics).Record(DrawIndexed{ 3u });
		});
		Rejected("a rendering area with no width", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics)
				.Record(BeginRendering{ .area = { 0, 0, 0u, 4u } });
		});
		Rejected("a rendering area with a negative origin", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics)
				.Record(BeginRendering{ .area = { -1, 0, 4u, 4u } });
		});
		Rejected("BeginRendering on a transfer node", [](Builder& builder) {
			builder.CreateNode(QueueType::Transfer)
				.Record(BeginRendering{ .area = { 0, 0, 4u, 4u } });
		});
	}

	void TestAttachments() {
		Rejected("a colour attachment that was never declared", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			auto const view = builder.RegisterView();
			builder.CreateNode(QueueType::Graphics)
				.Record(
					BeginRendering{
						.area = { 0, 0, 4u, 4u },
						.colors = {
							{
								.resource = resource,
								.view = view,
								.load = LoadOperation::Clear,
								.clear = { 0.0f, 0.0f, 0.0f, 1.0f }
							}
						}
					}
				);
		});
		Rejected("a colour attachment naming an unregistered resource", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			auto const view = builder.RegisterView();
			builder.CreateNode(QueueType::Graphics)
				.Access(
					{
						resource,
						AccessMode::Write,
						ResourceUsage::ColorAttachment,
						{}
					}
				)
				.Record(
					BeginRendering{
						.area = { 0, 0, 4u, 4u },
						.colors = {
							{
								.resource = resource + 2u,
								.view = view,
								.load = LoadOperation::Clear,
								.clear = { 0.0f, 0.0f, 0.0f, 1.0f }
							}
						}
					}
				);
		});
		Rejected("a colour attachment naming an unregistered view", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			auto const view = builder.RegisterView();
			builder.CreateNode(QueueType::Graphics)
				.Access(
					{
						resource,
						AccessMode::Write,
						ResourceUsage::ColorAttachment,
						{}
					}
				)
				.Record(
					BeginRendering{
						.area = { 0, 0, 4u, 4u },
						.colors = {
							{
								.resource = resource,
								.view = view + 3u,
								.load = LoadOperation::Clear,
								.clear = { 0.0f, 0.0f, 0.0f, 1.0f }
							}
						}
					}
				);
		});
		Rejected("a resolve attachment with only one of resource and view", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			auto const view = builder.RegisterView();
			builder.CreateNode(QueueType::Graphics)
				.Access(
					{
						resource,
						AccessMode::Write,
						ResourceUsage::ColorAttachment,
						{}
					}
				)
				.Record(
					BeginRendering{
						.area = { 0, 0, 4u, 4u },
						.colors = {
							{
								.resource = resource,
								.view = view,
								.load = LoadOperation::Clear,
								.clear = { 0.0f, 0.0f, 0.0f, 1.0f },
								.resolve_resource = resource
							}
						}
					}
				);
		});
		Rejected("a depth attachment that was never declared", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			auto const view = builder.RegisterView();
			builder.CreateNode(QueueType::Graphics)
				.Record(
					BeginRendering{
						.area = { 0, 0, 4u, 4u },
						.depth_stencil = DepthStencilAttachment{
							.resource = resource,
							.view = view
						}
					}
				);
		});
	}

	void TestPipelineCommands() {
		Rejected("BindPipeline naming an unregistered pipeline", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics).Record(BindPipeline{ 3u });
		});
		Rejected("BindPipeline on a transfer node", [](Builder& builder) {
			auto const pipeline = builder.RegisterPipeline();
			builder.CreateNode(QueueType::Transfer).Record(BindPipeline{ pipeline });
		});
		Rejected("BindResourceGroup naming an unregistered group", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics).Record(BindResourceGroup{ 2u });
		});
		Rejected("BindResourceGroup on a present node", [](Builder& builder) {
			auto const group = builder.RegisterResourceGroup();
			builder.CreateNode(QueueType::Present).Record(BindResourceGroup{ group });
		});
		Rejected("SetPipelineConstants with empty data", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics)
				.Record(SetPipelineConstants{ .slot = 0u, .space = 0u, .offset = 0u, .data = {} });
		});
		Rejected("SetPipelineConstants with an unaligned offset", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics)
				.Record(
					SetPipelineConstants{
						.slot = 0u,
						.space = 0u,
						.offset = 2u,
						.data = std::vector<std::byte>(4u)
					}
				);
		});
		Rejected("SetPipelineConstants with data that is not a multiple of four bytes", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics)
				.Record(
					SetPipelineConstants{
						.slot = 0u,
						.space = 0u,
						.offset = 0u,
						.data = std::vector<std::byte>(6u)
					}
				);
		});
	}

	void TestVertexAndIndexBuffers() {
		Rejected("BindVertexBuffer with a zero stride", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			builder.CreateNode(QueueType::Graphics)
				.Record(BindVertexBuffer{ .resource = resource, .slot = 0u, .stride = 0u });
		});
		Rejected("BindVertexBuffer naming an unregistered resource", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics)
				.Record(BindVertexBuffer{ .resource = 1u, .slot = 0u, .stride = 16u });
		});
		Rejected("BindVertexBuffer without a vertex-buffer access", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			builder.CreateNode(QueueType::Graphics)
				.Record(BindVertexBuffer{ .resource = resource, .slot = 0u, .stride = 16u });
		});
		Rejected("BindIndexBuffer without an index-buffer access", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			builder.CreateNode(QueueType::Graphics)
				.Record(BindIndexBuffer{ .resource = resource, .type = IndexType::Uint16 });
		});
		Rejected("BindVertexBuffer on a compute node", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			builder.CreateNode(QueueType::Compute)
				.Access({ resource, AccessMode::Read, ResourceUsage::VertexBuffer, {} })
				.Record(BindVertexBuffer{ .resource = resource, .slot = 0u, .stride = 16u });
		});
	}

	void TestViewportAndDispatch() {
		Rejected("a viewport with a negative width", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics)
				.Record(Viewport{ 0.0f, 0.0f, -1.0f, 4.0f });
		});
		Rejected("a viewport whose depth range is inverted", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics)
				.Record(Viewport{ 0.0f, 0.0f, 4.0f, 4.0f, 1.0f, 0.0f });
		});
		Rejected("a viewport whose maximum depth exceeds one", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics)
				.Record(Viewport{ 0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.5f });
		});
		Rejected("a scissor with a negative offset", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics).Record(Scissor{ -1, 0, 4u, 4u });
		});
		Rejected("a Dispatch with a zero group count", [](Builder& builder) {
			builder.CreateNode(QueueType::Compute).Record(Dispatch{ 1u, 0u, 1u });
		});
		Rejected("a Dispatch on a graphics node", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics).Record(Dispatch{ 1u, 1u, 1u });
		});
		Rejected("a Dispatch inside a rendering scope", [](Builder& builder) {
			builder.CreateNode(QueueType::Graphics)
				.Record(BeginRendering{ .area = { 0, 0, 4u, 4u } })
				.Record(Dispatch{ 1u, 1u, 1u });
		});
	}

	void TestCopyAndWriteCommands() {
		Rejected("a CopyBuffer on a graphics node", [](Builder& builder) {
			auto const source = builder.RegisterResource();
			auto const destination = builder.RegisterResource();
			builder.CreateNode(QueueType::Graphics)
				.Access({ source, AccessMode::Read, ResourceUsage::CopySource, {} })
				.Access({ destination, AccessMode::Write, ResourceUsage::CopyDestination, {} })
				.Record(
					CopyBuffer{
						.source = source,
						.destination = destination,
						.size = 16u
					}
				);
		});
		Rejected("a CopyBuffer with a zero size", [](Builder& builder) {
			auto const source = builder.RegisterResource();
			auto const destination = builder.RegisterResource();
			builder.CreateNode(QueueType::Transfer)
				.Access({ source, AccessMode::Read, ResourceUsage::CopySource, {} })
				.Access({ destination, AccessMode::Write, ResourceUsage::CopyDestination, {} })
				.Record(
					CopyBuffer{
						.source = source,
						.destination = destination,
						.size = 0u
					}
				);
		});
		Rejected("a copy whose source access is missing", [](Builder& builder) {
			auto const source = builder.RegisterResource();
			auto const destination = builder.RegisterResource();
			builder.CreateNode(QueueType::Transfer)
				.Access({ destination, AccessMode::Write, ResourceUsage::CopyDestination, {} })
				.Record(
					CopyBuffer{
						.source = source,
						.destination = destination,
						.size = 16u
					}
				);
		});
		Rejected("a copy whose destination access is a read", [](Builder& builder) {
			auto const source = builder.RegisterResource();
			auto const destination = builder.RegisterResource();
			builder.CreateNode(QueueType::Transfer)
				.Access({ source, AccessMode::Read, ResourceUsage::CopySource, {} })
				.Access({ destination, AccessMode::Read, ResourceUsage::CopyDestination, {} })
				.Record(
					CopyBuffer{
						.source = source,
						.destination = destination,
						.size = 16u
					}
				);
		});
		Rejected("a buffer-to-texture copy with a zero row pitch", [](Builder& builder) {
			auto const source = builder.RegisterResource();
			auto const destination = builder.RegisterResource();
			builder.CreateNode(QueueType::Transfer)
				.Access({ source, AccessMode::Read, ResourceUsage::CopySource, {} })
				.Access({ destination, AccessMode::Write, ResourceUsage::CopyDestination, {} })
				.Record(
					CopyBufferToTexture{
						.source = source,
						.destination = destination,
						.source_layout = { .offset = 0u, .bytes_per_row = 0u, .rows_per_image = 1u },
						.destination_region = { .width = 4u, .height = 4u, .depth = 1u }
					}
				);
		});
		Rejected("a texture copy whose extents differ", [](Builder& builder) {
			auto const source = builder.RegisterResource();
			auto const destination = builder.RegisterResource();
			builder.CreateNode(QueueType::Transfer)
				.Access({ source, AccessMode::Read, ResourceUsage::CopySource, {} })
				.Access({ destination, AccessMode::Write, ResourceUsage::CopyDestination, {} })
				.Record(
					CopyTexture{
						.source = source,
						.destination = destination,
						.source_region = { .width = 4u, .height = 4u, .depth = 1u },
						.destination_region = { .width = 8u, .height = 4u, .depth = 1u }
					}
				);
		});
		Rejected("a WriteBuffer with no data", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			builder.CreateNode(QueueType::Transfer)
				.Record(WriteBuffer{ .resource = resource, .offset = 0u, .data = {} });
		});
		Rejected("a WriteBuffer on a graphics node", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			builder.CreateNode(QueueType::Graphics)
				.Record(
					WriteBuffer{
						.resource = resource,
						.offset = 0u,
						.data = std::vector<std::byte>(4u)
					}
				);
		});
		Rejected("a WriteBuffer naming an unregistered resource", [](Builder& builder) {
			builder.CreateNode(QueueType::Transfer)
				.Record(
					WriteBuffer{
						.resource = 7u,
						.offset = 0u,
						.data = std::vector<std::byte>(4u)
					}
				);
		});
	}

	void TestPresentation() {
		Rejected("a Present with no buffers", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			builder.CreateNode(QueueType::Present)
				.Access({ resource, AccessMode::Read, ResourceUsage::PresentationSource, {} })
				.Record(
					Present{
						.source = resource,
						.target = 0u,
						.buffer_count = 0u,
						.vertical_sync = true
					}
				);
		});
		Rejected("a Present without a presentation-source access", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			builder.CreateNode(QueueType::Present)
				.Record(
					Present{
						.source = resource,
						.target = 0u,
						.buffer_count = 3u,
						.vertical_sync = true
					}
				);
		});
		Rejected("a Present on a graphics node", [](Builder& builder) {
			auto const resource = builder.RegisterResource();
			builder.CreateNode(QueueType::Graphics)
				.Access({ resource, AccessMode::Read, ResourceUsage::PresentationSource, {} })
				.Record(
					Present{
						.source = resource,
						.target = 0u,
						.buffer_count = 3u,
						.vertical_sync = true
					}
				);
		});
		Rejected("a Present naming an unregistered resource", [](Builder& builder) {
			builder.CreateNode(QueueType::Present)
				.Record(
					Present{
						.source = 2u,
						.target = 0u,
						.buffer_count = 3u,
						.vertical_sync = true
					}
				);
		});
	}

	/// The graphs every other case is a variation of: an upload on the transfer queue, a draw on
	/// the graphics queue that depends on it, and a present that depends on the draw. Compiling it
	/// also exercises batching and barrier planning across three queue types.
	void TestWellFormedGraphs() {
		Accepted("an upload, a draw, and a present", [](Builder& builder) {
			auto const target = builder.RegisterResource();
			auto const vertex_buffer = builder.RegisterResource();
			auto const uniform = builder.RegisterResource();
			auto const target_view = builder.RegisterView();
			auto const pipeline = builder.RegisterPipeline();
			auto const group = builder.RegisterResourceGroup();

			auto const upload = builder.CreateNode(QueueType::Transfer);
			upload
				.Access({ uniform, AccessMode::Write, ResourceUsage::CopyDestination, {} })
				.Record(
					WriteBuffer{
						.resource = uniform,
						.offset = 0u,
						.data = std::vector<std::byte>(16u)
					}
				);

			auto const draw = builder.CreateNode(QueueType::Graphics, upload);
			draw
				.Access({ target, AccessMode::Write, ResourceUsage::ColorAttachment, {} })
				.Access({ vertex_buffer, AccessMode::Read, ResourceUsage::VertexBuffer, {} })
				.Access({ uniform, AccessMode::Read, ResourceUsage::Uniform, {} })
				.Record(BindPipeline{ pipeline })
				.Record(BindResourceGroup{ .group = group, .space = 0u })
				.Record(
					BindVertexBuffer{
						.resource = vertex_buffer,
						.slot = 0u,
						.stride = 16u,
						.offset = 0u
					}
				)
				.Record(
					BeginRendering{
						.area = { 0, 0, 8u, 8u },
						.colors = {
							{
								.resource = target,
								.view = target_view,
								.load = LoadOperation::Clear,
								.clear = { 0.0f, 0.0f, 0.0f, 1.0f }
							}
						}
					}
				)
				.Record(Viewport{ 0.0f, 0.0f, 8.0f, 8.0f })
				.Record(Scissor{ 0, 0, 8u, 8u })
				.Record(Draw{ 3u })
				.Record(EndRendering{});

			builder.CreateNode(QueueType::Present, draw)
				.Access({ target, AccessMode::Read, ResourceUsage::PresentationSource, {} })
				.Record(
					Present{
						.source = target,
						.target = 0u,
						.buffer_count = 3u,
						.vertical_sync = true
					}
				);
		});

		Accepted("a copy between buffers", [](Builder& builder) {
			auto const source = builder.RegisterResource();
			auto const destination = builder.RegisterResource();
			builder.CreateNode(QueueType::Transfer)
				.Access({ source, AccessMode::Read, ResourceUsage::CopySource, {} })
				.Access({ destination, AccessMode::Write, ResourceUsage::CopyDestination, {} })
				.Record(
					CopyBuffer{
						.source = source,
						.destination = destination,
						.size = 16u
					}
				);
		});

		Accepted("two sequential rendering scopes in one node", [](Builder& builder) {
			// The compiler tracks one open scope per node, so a second scope after the first ends
			// is well-formed rather than a nesting error.
			builder.CreateNode(QueueType::Graphics)
				.Record(BeginRendering{ .area = { 0, 0, 4u, 4u } })
				.Record(EndRendering{})
				.Record(BeginRendering{ .area = { 0, 0, 4u, 4u } })
				.Record(EndRendering{});
		});

		Accepted("a compute dispatch", [](Builder& builder) {
			auto const storage = builder.RegisterResource();
			auto const pipeline = builder.RegisterPipeline();
			auto const group = builder.RegisterResourceGroup();
			builder.CreateNode(QueueType::Compute)
				.Access({ storage, AccessMode::ReadWrite, ResourceUsage::Storage, {} })
				.Record(BindPipeline{ pipeline })
				.Record(BindResourceGroup{ .group = group, .space = 0u })
				.Record(
					SetPipelineConstants{
						.slot = 0u,
						.space = 0u,
						.offset = 0u,
						.data = std::vector<std::byte>(4u)
					}
				)
				.Record(Dispatch{ 4u, 1u, 1u });
		});

		Accepted("a node whose accesses are ranges rather than whole resources", [](Builder& builder) {
			auto const source = builder.RegisterResource();
			auto const destination = builder.RegisterResource();
			builder.CreateNode(QueueType::Transfer)
				.Access(
					{
						source,
						AccessMode::Read,
						ResourceUsage::CopySource,
						BufferRange{ 0u, 16u }
					}
				)
				.Access(
					{
						destination,
						AccessMode::Write,
						ResourceUsage::CopyDestination,
						BufferRange{ 32u, 16u }
					}
				)
				.Record(
					CopyBuffer{
						.source = source,
						.destination = destination,
						.size = 16u
					}
				);
		});
	}

} // namespace

int main() try {
	TestAccessDeclarations();
	TestDependencies();
	TestRenderingScopes();
	TestAttachments();
	TestPipelineCommands();
	TestVertexAndIndexBuffers();
	TestViewportAndDispatch();
	TestCopyAndWriteCommands();
	TestPresentation();
	TestWellFormedGraphs();
	std::cout << "Graph validation: " << g_cases << " cases passed" << std::endl;
	return 0;
}
catch (std::exception const& error) {
	std::cerr << "Graph validation failed: " << error.what() << std::endl;
	return 1;
}
