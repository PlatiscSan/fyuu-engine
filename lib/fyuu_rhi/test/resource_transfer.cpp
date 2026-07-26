#include <cstddef>
#include <cstdint>
#include <concepts>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
import fyuu_rhi;

namespace mock {
	inline std::vector<fyuu_rhi::execution::CommandGraphDescriptor> graphs;

	struct NativeResource {
		std::size_t size = 0u;
	};

	struct NativeCommandGraph {
		fyuu_rhi::execution::CommandGraphDescriptor descriptor;
	};

	struct DeviceState {
		std::vector<std::pair<std::size_t, fyuu_rhi::ResourceFlags>> buffers;
	};

	struct Backend {
		using LogicalDevice = std::shared_ptr<DeviceState>;
		using Scheduler = std::shared_ptr<int>;
		using Resource = NativeResource;
		using CommandGraph = std::shared_ptr<NativeCommandGraph>;
		using ExecutableGraph = std::shared_ptr<NativeCommandGraph>;
		using View = int;
		using Sampler = int;
		using Pipeline = int;
		using PipelineResourceGroup = int;
		using PresentationTarget = int;

		static Resource CreateBuffer(
			LogicalDevice const& logical_device,
			std::size_t size,
			fyuu_rhi::ResourceFlags const& flags
		) {
			logical_device->buffers.emplace_back(size, flags);
			return { size };
		}

		static Resource CreateTexture(
			LogicalDevice const&,
			std::size_t,
			std::size_t,
			std::size_t,
			std::size_t,
			fyuu_rhi::ResourceFlags const&
		) {
			return {};
		}

		static CommandGraph CreateCommandGraph(
			fyuu_rhi::execution::CommandGraphDescriptor const& descriptor
		) {
			graphs.emplace_back(descriptor);
			return std::make_shared<NativeCommandGraph>(descriptor);
		}

		static ExecutableGraph CompileCommandGraph(CommandGraph const& graph) {
			return graph;
		}
	};

	void StartDeferredDestroy(std::shared_ptr<int> const&, auto const& deferred_destroy) {
		deferred_destroy.Destroy(deferred_destroy.object);
		deferred_destroy.completion.SetValue(deferred_destroy.completion.operation);
	}
}

namespace {
	using Backend = mock::Backend;
	using Resource = fyuu_rhi::Resource<Backend>;
	using Scheduler = fyuu_rhi::execution::Scheduler<Backend>;

	void Require(bool condition, std::string_view message) {
		if (!condition) {
			throw std::runtime_error(message.data());
		}
	}

	template <class Function>
	void RequireInvalidArgument(Function&& function, std::string_view message) {
		try {
			std::forward<Function>(function)();
		}
		catch (std::invalid_argument const&) {
			return;
		}
		throw std::runtime_error(message.data());
	}

	template <class Function>
	void RequireOutOfRange(Function&& function, std::string_view message) {
		try {
			std::forward<Function>(function)();
		}
		catch (std::out_of_range const&) {
			return;
		}
		throw std::runtime_error(message.data());
	}

	fyuu_rhi::ResourceFlags BufferFlags(
		fyuu_rhi::ResourceFlagBits location,
		fyuu_rhi::ResourceFlagBits copy
	) {
		fyuu_rhi::ResourceFlags flags;
		flags.Set(location);
		flags.Set(copy);
		return flags;
	}

	fyuu_rhi::ResourceFlags TextureFlags(fyuu_rhi::ResourceFlagBits copy) {
		auto flags = BufferFlags(fyuu_rhi::ResourceFlagBits::DeviceLocal, copy);
		flags.Set(fyuu_rhi::ResourceFlagBits::R8G8B8A8Unorm);
		return flags;
	}

	void UploadValidation() {
		auto state = std::make_shared<mock::DeviceState>();
		fyuu_rhi::LogicalDevice<Backend> logical_device(state);
		Scheduler scheduler(std::make_shared<int>(1));
		std::byte byte{ 1u };

		RequireInvalidArgument([&]() {
			auto resource = logical_device.CreateBuffer(
				4u,
				BufferFlags(fyuu_rhi::ResourceFlagBits::DeviceLocal, fyuu_rhi::ResourceFlagBits::CopyDST)
			);
			(void)fyuu_rhi::execution::Upload(
				logical_device, scheduler, std::move(resource), 0u, std::span<std::byte const>{}
			);
		}, "Upload accepted empty data");

		RequireInvalidArgument([&]() {
			auto resource = logical_device.CreateBuffer(
				4u,
				BufferFlags(fyuu_rhi::ResourceFlagBits::DeviceLocal, fyuu_rhi::ResourceFlagBits::CopySRC)
			);
			(void)fyuu_rhi::execution::Upload(
				logical_device, scheduler, std::move(resource), 0u,
				std::span<std::byte const>(&byte, 1u)
			);
		}, "Upload accepted a destination without CopyDST");

		RequireOutOfRange([&]() {
			auto resource = logical_device.CreateBuffer(
				4u,
				BufferFlags(fyuu_rhi::ResourceFlagBits::DeviceLocal, fyuu_rhi::ResourceFlagBits::CopyDST)
			);
			(void)fyuu_rhi::execution::Upload(
				logical_device, scheduler, std::move(resource), 4u,
				std::span<std::byte const>(&byte, 1u)
			);
		}, "Upload accepted an invalid destination range");
	}

	void ReadbackValidation() {
		auto state = std::make_shared<mock::DeviceState>();
		fyuu_rhi::LogicalDevice<Backend> logical_device(state);
		Scheduler scheduler(std::make_shared<int>(1));

		RequireInvalidArgument([&]() {
			auto resource = logical_device.CreateBuffer(
				4u,
				BufferFlags(fyuu_rhi::ResourceFlagBits::DeviceLocal, fyuu_rhi::ResourceFlagBits::CopyDST)
			);
			(void)fyuu_rhi::execution::Readback(
				logical_device, scheduler, std::move(resource), { 0u, 4u }
			);
		}, "Readback accepted a source without CopySRC");

		RequireOutOfRange([&]() {
			auto resource = logical_device.CreateBuffer(
				4u,
				BufferFlags(fyuu_rhi::ResourceFlagBits::DeviceLocal, fyuu_rhi::ResourceFlagBits::CopySRC)
			);
			(void)fyuu_rhi::execution::Readback(
				logical_device, scheduler, std::move(resource), { 3u, 2u }
			);
		}, "Readback accepted an invalid source range");
	}

	void UploadBuildsStagingAndCopyGraph() {
		mock::graphs.clear();
		auto state = std::make_shared<mock::DeviceState>();
		fyuu_rhi::LogicalDevice<Backend> logical_device(state);
		Scheduler scheduler(std::make_shared<int>(1));
		auto resource = logical_device.CreateBuffer(
			16u,
			BufferFlags(fyuu_rhi::ResourceFlagBits::DeviceLocal, fyuu_rhi::ResourceFlagBits::CopyDST)
		);
		std::byte data[]{ std::byte{ 1u }, std::byte{ 2u }, std::byte{ 3u } };
		auto sender = fyuu_rhi::execution::Upload(
			logical_device, scheduler, std::move(resource), 5u, data
		);
		static_assert(!std::copy_constructible<decltype(sender)>);
		Require(state->buffers.size() == 2u, "Upload did not create one staging buffer");
		auto const& staging = state->buffers.back();
		Require(staging.first >= 3u, "Upload staging is smaller than the payload");
		Require(staging.second.Test(fyuu_rhi::ResourceFlagBits::HostVisible),
			"Upload staging is not host visible");
		Require(staging.second.Test(fyuu_rhi::ResourceFlagBits::CopySRC),
			"Upload staging is not a copy source");
		Require(mock::graphs.size() == 1u, "Upload did not create one copy graph");
		auto const& command = std::get<fyuu_rhi::execution::CopyBufferCommand>(
			mock::graphs.front().nodes.front().commands.front()
		);
		Require(command.source_offset == 0u && command.destination_offset == 5u &&
			command.size == 3u, "Upload copy graph has an invalid range");
		(void)sender;
	}

	void ReadbackBuildsStaging() {
		mock::graphs.clear();
		auto state = std::make_shared<mock::DeviceState>();
		fyuu_rhi::LogicalDevice<Backend> logical_device(state);
		Scheduler scheduler(std::make_shared<int>(1));
		auto resource = logical_device.CreateBuffer(
			16u,
			BufferFlags(fyuu_rhi::ResourceFlagBits::DeviceLocal, fyuu_rhi::ResourceFlagBits::CopySRC)
		);
		auto sender = fyuu_rhi::execution::Readback(
			logical_device, scheduler, std::move(resource), { 4u, 7u }
		);
		static_assert(!std::copy_constructible<decltype(sender)>);
		Require(state->buffers.size() == 2u, "Readback did not create one staging buffer");
		auto const& staging = state->buffers.back();
		Require(staging.first >= 7u, "Readback staging is smaller than the requested range");
		Require(staging.second.Test(fyuu_rhi::ResourceFlagBits::DeviceReadback),
			"Readback staging is not device readback memory");
		Require(staging.second.Test(fyuu_rhi::ResourceFlagBits::CopyDST),
			"Readback staging is not a copy destination");
		Require(mock::graphs.size() == 1u, "Readback did not create one copy graph");
		auto const& command = std::get<fyuu_rhi::execution::CopyBufferCommand>(
			mock::graphs.front().nodes.front().commands.front()
		);
		Require(command.source_offset == 4u && command.destination_offset == 0u &&
			command.size == 7u, "Readback copy graph has an invalid range");
		(void)sender;
	}

	void TextureUploadBuildsStagingAndCopyGraph() {
		mock::graphs.clear();
		auto state = std::make_shared<mock::DeviceState>();
		fyuu_rhi::LogicalDevice<Backend> logical_device(state);
		Scheduler scheduler(std::make_shared<int>(1));
		auto texture = logical_device.CreateTexture(
			8u, 4u, 1u, 1u, TextureFlags(fyuu_rhi::ResourceFlagBits::CopyDST)
		);
		fyuu_rhi::TextureDataLayout layout{
			.bytes_per_row = 32u,
			.rows_per_image = 4u
		};
		fyuu_rhi::TextureRegion region{
			.width = 8u,
			.height = 4u
		};
		std::vector<std::byte> data(128u);
		auto sender = fyuu_rhi::execution::Upload(
			logical_device, scheduler, std::move(texture), layout, region, data
		);
		Require(state->buffers.back().first >= 128u,
			"Texture upload staging is smaller than the layout");
		auto const& command = std::get<fyuu_rhi::execution::CopyBufferToTextureCommand>(
			mock::graphs.front().nodes.front().commands.front()
		);
		Require(command.source_layout.offset == 0u &&
			command.source_layout.bytes_per_row == 32u &&
			command.destination_region.width == 8u,
			"Texture upload graph does not preserve its layout and region");
		(void)sender;
	}

	void TextureReadbackBuildsStagingAndCopyGraph() {
		mock::graphs.clear();
		auto state = std::make_shared<mock::DeviceState>();
		fyuu_rhi::LogicalDevice<Backend> logical_device(state);
		Scheduler scheduler(std::make_shared<int>(1));
		auto texture = logical_device.CreateTexture(
			8u, 4u, 1u, 1u, TextureFlags(fyuu_rhi::ResourceFlagBits::CopySRC)
		);
		fyuu_rhi::TextureDataLayout layout{
			.bytes_per_row = 40u,
			.rows_per_image = 4u
		};
		fyuu_rhi::TextureRegion region{
			.width = 8u,
			.height = 4u
		};
		auto sender = fyuu_rhi::execution::Readback(
			logical_device, scheduler, std::move(texture), region, layout
		);
		Require(state->buffers.back().first >= 160u,
			"Texture readback staging does not preserve row pitch");
		auto const& command = std::get<fyuu_rhi::execution::CopyTextureToBufferCommand>(
			mock::graphs.front().nodes.front().commands.front()
		);
		Require(command.destination_layout.offset == 0u &&
			command.destination_layout.bytes_per_row == 40u &&
			command.source_region.height == 4u,
			"Texture readback graph does not preserve its layout and region");
		(void)sender;
	}

	void UploadStagingIsReused() {
		auto state = std::make_shared<mock::DeviceState>();
		fyuu_rhi::LogicalDevice<Backend> logical_device(state);
		Scheduler scheduler(std::make_shared<int>(1));
		std::byte data[]{ std::byte{ 1u }, std::byte{ 2u }, std::byte{ 3u } };
		{
			auto resource = logical_device.CreateBuffer(
				16u,
				BufferFlags(
					fyuu_rhi::ResourceFlagBits::DeviceLocal,
					fyuu_rhi::ResourceFlagBits::CopyDST
				)
			);
			auto sender = fyuu_rhi::execution::Upload(
				logical_device, scheduler, std::move(resource), 0u, data
			);
		}
		auto allocation_count = state->buffers.size();
		auto resource = logical_device.CreateBuffer(
			16u,
			BufferFlags(
				fyuu_rhi::ResourceFlagBits::DeviceLocal,
				fyuu_rhi::ResourceFlagBits::CopyDST
			)
		);
		auto sender = fyuu_rhi::execution::Upload(
			logical_device, scheduler, std::move(resource), 0u, data
		);
		Require(
			state->buffers.size() == allocation_count + 1u,
			"Upload did not reuse its staging allocation"
		);
	}

	struct TestCase {
		std::string_view name;
		void (*Run)();
	};
}

int main() {
	TestCase tests[]{
		{ "upload validation", UploadValidation },
		{ "readback validation", ReadbackValidation },
		{ "upload staging", UploadBuildsStagingAndCopyGraph },
		{ "readback staging", ReadbackBuildsStaging },
		{ "texture upload staging", TextureUploadBuildsStagingAndCopyGraph },
		{ "texture readback staging", TextureReadbackBuildsStagingAndCopyGraph },
		{ "upload staging reuse", UploadStagingIsReused }
	};
	for (auto const& test : tests) {
		try {
			test.Run();
		}
		catch (std::exception const& error) {
			std::cerr << test.name << ": " << error.what() << '\n';
			return 1;
		}
	}
	return 0;
}
