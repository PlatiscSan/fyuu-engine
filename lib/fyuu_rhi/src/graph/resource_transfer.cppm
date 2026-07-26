module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <vector>
#endif
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
#include <execution>
#endif

export module fyuu_rhi:resource_transfer;
#if defined(__cpp_lib_modules)
import std;
#endif
import :logical_device;
import :resource_mapping;
import :resource_retirement;
import :staging_resource_pool;

namespace fyuu_rhi::execution {
	template <class Backend> struct ResourceTransferPlan {
		CommandGraphDescriptor descriptor;
		ExecutableGraph<Backend> graph;
		GraphResourceID source;
		GraphResourceID destination;
		std::size_t transfer_size = 0u;
	};

	template <class Backend>
	ResourceTransferPlan<Backend> MakeResourceTransferPlan(
		LogicalDevice<Backend>& logical_device,
		std::size_t source_offset,
		std::size_t destination_offset,
		std::size_t size
	) {
		CommandGraphBuilder builder;
		auto source = builder.RegisterResource();
		auto destination = builder.RegisterResource();
		auto node = builder.AddNode(GraphNodeFlagBits::Copy);
		builder.AddAccess(node, {
			.resource = source,
			.flags = GraphAccessFlagBits::Read | GraphAccessFlagBits::CopySource,
			.range = { .offset = source_offset, .size = size }
		});
		builder.AddAccess(node, {
			.resource = destination,
			.flags = GraphAccessFlagBits::Write | GraphAccessFlagBits::CopyDestination,
			.range = { .offset = destination_offset, .size = size }
		});
		builder.AddCommand(node, CopyBufferCommand{
			.source = source,
			.destination = destination,
			.source_offset = source_offset,
			.destination_offset = destination_offset,
			.size = size
		});
		auto descriptor = builder.Build();
		auto graph = logical_device.CreateCommandGraph(descriptor);
		return { descriptor, logical_device.CompileCommandGraph(graph), source, destination, size };
	}

	std::size_t TextureTransferSize(
		TextureDataLayout const& layout,
		TextureRegion const& region,
		ResourceFlags const& flags
	) {
		bool block_compressed =
			flags.Test(ResourceFlagBits::Bc1Unorm) ||
			flags.Test(ResourceFlagBits::Bc1UnormSrgb) ||
			flags.Test(ResourceFlagBits::Bc2Unorm) ||
			flags.Test(ResourceFlagBits::Bc2UnormSrgb) ||
			flags.Test(ResourceFlagBits::Bc3Unorm) ||
			flags.Test(ResourceFlagBits::Bc3UnormSrgb) ||
			flags.Test(ResourceFlagBits::Bc4Unorm) ||
			flags.Test(ResourceFlagBits::Bc4Snorm) ||
			flags.Test(ResourceFlagBits::Bc5Unorm) ||
			flags.Test(ResourceFlagBits::Bc5Snorm) ||
			flags.Test(ResourceFlagBits::Bc6HUfloat) ||
			flags.Test(ResourceFlagBits::Bc6HSfloat) ||
			flags.Test(ResourceFlagBits::Bc7Unorm) ||
			flags.Test(ResourceFlagBits::Bc7UnormSrgb);
		auto block_height = block_compressed ? 4u : 1u;
		auto copied_rows = (region.height + block_height - 1u) / block_height;
		auto image_rows = (layout.rows_per_image + block_height - 1u) / block_height;
		auto images = static_cast<std::size_t>(region.depth) * region.array_layer_count;
		if (images == 0u) {
			return 0u;
		}
		auto maximum = (std::numeric_limits<std::size_t>::max)();
		if (images - 1u > (maximum - copied_rows) / image_rows) {
			throw std::overflow_error("Texture transfer size overflows");
		}
		auto rows = static_cast<std::size_t>(image_rows) * (images - 1u) + copied_rows;
		if (rows > maximum / layout.bytes_per_row) {
			throw std::overflow_error("Texture transfer size overflows");
		}
		return static_cast<std::size_t>(layout.bytes_per_row) * rows;
	}

	void ValidateTextureTransfer(
		ResourceTextureExtent const& extent,
		ResourceFlags const& flags,
		TextureDataLayout const& layout,
		TextureRegion const& region,
		char const* message
	) {
		if (extent.width == 0u || layout.offset != 0u || layout.bytes_per_row == 0u ||
			layout.rows_per_image == 0u || region.width == 0u ||
			region.height == 0u || region.depth == 0u ||
			region.array_layer_count == 0u || region.mip_level >= extent.mip_levels) {
			throw std::invalid_argument(message);
		}
		auto mip_width = std::max(extent.width >> region.mip_level, 1u);
		auto mip_height = std::max(extent.height >> region.mip_level, 1u);
		if (region.offset_x > mip_width || region.width > mip_width - region.offset_x ||
			region.offset_y > mip_height || region.height > mip_height - region.offset_y) {
			throw std::out_of_range(message);
		}
		if (flags.Test(ResourceFlagBits::Texture3D)) {
			auto mip_depth = std::max(
				extent.depth_or_array_layers >> region.mip_level,
				1u
			);
			if (region.base_array_layer != 0u || region.array_layer_count != 1u ||
				region.offset_z > mip_depth || region.depth > mip_depth - region.offset_z) {
				throw std::out_of_range(message);
			}
		}
		else if (region.offset_z != 0u || region.depth != 1u ||
			region.base_array_layer > extent.depth_or_array_layers ||
			region.array_layer_count >
				extent.depth_or_array_layers - region.base_array_layer) {
			throw std::out_of_range(message);
		}
	}

	template <class Backend>
	ResourceTransferPlan<Backend> MakeTextureUploadPlan(
		LogicalDevice<Backend>& logical_device,
		TextureDataLayout const& layout,
		TextureRegion const& region,
		std::size_t staging_size
	) {
		CommandGraphBuilder builder;
		auto source = builder.RegisterResource();
		auto destination = builder.RegisterResource();
		auto node = builder.AddNode(GraphNodeFlagBits::Copy);
		builder.AddAccess(node, {
			.resource = source,
			.flags = GraphAccessFlagBits::Read | GraphAccessFlagBits::CopySource,
			.range = { .offset = layout.offset, .size = staging_size - layout.offset }
		});
		builder.AddAccess(node, {
			.resource = destination,
			.flags = GraphAccessFlagBits::Write | GraphAccessFlagBits::CopyDestination,
			.range = {
				.base_mip_level = region.mip_level,
				.mip_level_count = 1u,
				.base_array_layer = region.base_array_layer,
				.array_layer_count = region.array_layer_count
			}
		});
		builder.AddCommand(node, CopyBufferToTextureCommand{
			.source = source,
			.destination = destination,
			.source_layout = layout,
			.destination_region = region
		});
		auto descriptor = builder.Build();
		auto graph = logical_device.CreateCommandGraph(descriptor);
		return {
			descriptor,
			logical_device.CompileCommandGraph(graph),
			source,
			destination,
			staging_size - layout.offset
		};
	}

	template <class Backend>
	ResourceTransferPlan<Backend> MakeTextureReadbackPlan(
		LogicalDevice<Backend>& logical_device,
		TextureRegion const& region,
		TextureDataLayout const& layout,
		std::size_t staging_size
	) {
		CommandGraphBuilder builder;
		auto source = builder.RegisterResource();
		auto destination = builder.RegisterResource();
		auto node = builder.AddNode(GraphNodeFlagBits::Copy);
		builder.AddAccess(node, {
			.resource = source,
			.flags = GraphAccessFlagBits::Read | GraphAccessFlagBits::CopySource,
			.range = {
				.base_mip_level = region.mip_level,
				.mip_level_count = 1u,
				.base_array_layer = region.base_array_layer,
				.array_layer_count = region.array_layer_count
			}
		});
		builder.AddAccess(node, {
			.resource = destination,
			.flags = GraphAccessFlagBits::Write | GraphAccessFlagBits::CopyDestination,
			.range = { .offset = layout.offset, .size = staging_size - layout.offset }
		});
		builder.AddCommand(node, CopyTextureToBufferCommand{
			.source = source,
			.destination = destination,
			.source_region = region,
			.destination_layout = layout
		});
		auto descriptor = builder.Build();
		auto graph = logical_device.CreateCommandGraph(descriptor);
		return {
			descriptor,
			logical_device.CompileCommandGraph(graph),
			source,
			destination,
			staging_size - layout.offset
		};
	}

	template <class Receiver>
	std::stop_token ResourceTransferStopToken(Receiver const& receiver) noexcept {
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		if constexpr (requires {
			std::stop_token{ std::execution::get_stop_token(std::execution::get_env(receiver)) };
		}) {
			return std::stop_token{
				std::execution::get_stop_token(std::execution::get_env(receiver))
			};
		}
		else
#endif
		if constexpr (requires { std::stop_token{ receiver.get_stop_token() }; }) {
			return std::stop_token{ receiver.get_stop_token() };
		}
		else if constexpr (requires { std::stop_token{ receiver.get_env().get_stop_token() }; }) {
			return std::stop_token{ receiver.get_env().get_stop_token() };
		}
		else {
			return {};
		}
	}

	template <class Backend, class Receiver> class ResourceUploadOperationState {
		struct MapReceiver;
		struct UnmapReceiver;
		struct GraphReceiver;

		Scheduler<Backend> m_scheduler;
		std::shared_ptr<StagingResourcePool<Backend>> m_staging_pool;
		std::optional<Resource<Backend>> m_destination;
		std::optional<Resource<Backend>> m_staging;
		ResourceTransferPlan<Backend> m_plan;
		std::vector<std::byte> m_data;
		Receiver m_receiver;
		std::optional<CommandGraphBindings<Backend>> m_bindings;
		std::stop_token m_stop_token;
		std::atomic_bool m_completed = false;
		bool m_started = false;

		struct MapReceiver {
			ResourceUploadOperationState* operation;
			std::stop_token get_stop_token() const noexcept { return operation->m_stop_token; }
			void set_value(MappedResource<Backend> mapped) && noexcept {
				operation->Mapped(std::move(mapped));
			}
			void set_error(std::exception_ptr const& error) && noexcept {
				operation->CompleteError(error);
			}
			void set_stopped() && noexcept { operation->CompleteStopped(); }
		};

		struct UnmapReceiver {
			ResourceUploadOperationState* operation;
			std::stop_token get_stop_token() const noexcept { return operation->m_stop_token; }
			void set_value(Resource<Backend> staging) && noexcept {
				operation->Unmapped(std::move(staging));
			}
			void set_error(std::exception_ptr const& error) && noexcept {
				operation->CompleteError(error);
			}
			void set_stopped() && noexcept { operation->CompleteStopped(); }
		};

		struct GraphReceiver {
			ResourceUploadOperationState* operation;
			std::stop_token get_stop_token() const noexcept { return operation->m_stop_token; }
			void set_value() && noexcept { operation->CompleteValue(); }
			void set_error(std::exception_ptr const& error) && noexcept {
				operation->CompleteError(error);
			}
			void set_stopped() && noexcept { operation->CompleteStopped(); }
		};

		std::optional<ResourceMapOperationState<Backend, MapReceiver>> m_map;
		std::optional<ResourceUnmapOperationState<Backend, UnmapReceiver>> m_unmap;
		std::optional<CommandGraphOperationState<Backend, GraphReceiver>> m_graph;

		void ReleaseStaging(Resource<Backend>&& staging) noexcept {
			try {
				if (m_staging_pool->Release(staging, StagingResourceKind::Upload)) {
					return;
				}
			}
			catch (...) {
			}
			Retire(m_scheduler, std::move(staging));
		}

		void RetireResources() noexcept {
			if (m_staging) {
				auto staging = std::move(*m_staging);
				m_staging.reset();
				Retire(m_scheduler, std::move(staging));
			}
			if (m_destination) {
				auto destination = std::move(*m_destination);
				m_destination.reset();
				Retire(m_scheduler, std::move(destination));
			}
		}

		void Mapped(MappedResource<Backend> mapped) noexcept {
			try {
				auto bytes = mapped.WritableBytes();
				std::memcpy(bytes.data(), m_data.data(), m_data.size());
				m_data.clear();
				m_unmap.emplace(std::move(mapped), UnmapReceiver{ this });
				m_unmap->start();
			}
			catch (...) { CompleteError(std::current_exception()); }
		}

		void Unmapped(Resource<Backend> staging) noexcept {
			try {
				m_staging.emplace(std::move(staging));
				m_bindings.emplace(m_plan.descriptor);
				m_bindings->Bind(m_plan.source, *m_staging);
				m_bindings->Bind(m_plan.destination, *m_destination);
				m_graph.emplace(m_scheduler, m_plan.graph, *m_bindings, GraphReceiver{ this });
				m_graph->start();
			}
			catch (...) { CompleteError(std::current_exception()); }
		}

		void CompleteValue() noexcept {
			if (m_completed.exchange(true, std::memory_order::acq_rel)) return;
			auto destination = std::move(*m_destination);
			m_destination.reset();
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_value(std::move(m_receiver), std::move(destination));
#else
			std::move(m_receiver).set_value(std::move(destination));
#endif
		}

		void CompleteError(std::exception_ptr const& error) noexcept {
			if (m_completed.exchange(true, std::memory_order::acq_rel)) return;
			RetireResources();
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_error(std::move(m_receiver), error);
#else
			std::move(m_receiver).set_error(error);
#endif
		}

		void CompleteStopped() noexcept {
			if (m_completed.exchange(true, std::memory_order::acq_rel)) return;
			RetireResources();
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_stopped(std::move(m_receiver));
#else
			std::move(m_receiver).set_stopped();
#endif
		}

	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using operation_state_concept = std::execution::operation_state_t;
#endif
		template <class R>
		ResourceUploadOperationState(
			Scheduler<Backend> const& scheduler,
			std::shared_ptr<StagingResourcePool<Backend>> const& staging_pool,
			Resource<Backend>&& destination,
			Resource<Backend>&& staging,
			ResourceTransferPlan<Backend>&& plan,
			std::vector<std::byte>&& data,
			R&& receiver
		) : m_scheduler(scheduler), m_staging_pool(staging_pool),
			m_destination(std::move(destination)),
			m_staging(std::move(staging)), m_plan(std::move(plan)), m_data(std::move(data)),
			m_receiver(std::forward<R>(receiver)),
			m_stop_token(ResourceTransferStopToken(m_receiver)) {}

		ResourceUploadOperationState(ResourceUploadOperationState const&) = delete;
		ResourceUploadOperationState(ResourceUploadOperationState&&) = delete;
		ResourceUploadOperationState& operator=(ResourceUploadOperationState const&) = delete;
		ResourceUploadOperationState& operator=(ResourceUploadOperationState&&) = delete;
		~ResourceUploadOperationState() noexcept {
			if (m_staging && (!m_started || m_completed.load(std::memory_order::acquire))) {
				auto staging = std::move(*m_staging);
				m_staging.reset();
				ReleaseStaging(std::move(staging));
			}
			RetireResources();
		}

		void start() & noexcept {
			if (m_started) std::terminate();
			m_started = true;
			try {
				ResourceMapFlags flags;
				flags.Set(ResourceMapFlagBits::Write);
				auto mapped_size = m_staging->Size();
				auto staging = std::move(*m_staging);
				m_staging.reset();
				m_map.emplace(m_scheduler, std::move(staging),
					ResourceDataRange{ 0u, mapped_size }, flags, MapReceiver{ this });
				m_map->start();
			}
			catch (...) { CompleteError(std::current_exception()); }
		}
	};

	export template <class Backend> class ResourceUploadSender {
		Scheduler<Backend> m_scheduler;
		std::shared_ptr<StagingResourcePool<Backend>> m_staging_pool;
		Resource<Backend> m_destination;
		Resource<Backend> m_staging;
		ResourceTransferPlan<Backend> m_plan;
		std::vector<std::byte> m_data;

	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using sender_concept = std::execution::sender_t;
#endif
		ResourceUploadSender(Scheduler<Backend> const& scheduler,
			std::shared_ptr<StagingResourcePool<Backend>> const& staging_pool,
			Resource<Backend>&& destination, Resource<Backend>&& staging,
			ResourceTransferPlan<Backend>&& plan, std::vector<std::byte>&& data)
			: m_scheduler(scheduler), m_staging_pool(staging_pool),
			m_destination(std::move(destination)),
			m_staging(std::move(staging)), m_plan(std::move(plan)), m_data(std::move(data)) {}
		ResourceUploadSender(ResourceUploadSender const&) = delete;
		ResourceUploadSender& operator=(ResourceUploadSender const&) = delete;
		ResourceUploadSender(ResourceUploadSender&&) noexcept = default;
		ResourceUploadSender& operator=(ResourceUploadSender&&) = delete;
		~ResourceUploadSender() noexcept {
			if (m_staging.ID() != 0u) {
				try {
					if (!m_staging_pool->Release(m_staging, StagingResourceKind::Upload)) {
						Retire(m_scheduler, std::move(m_staging));
					}
				}
				catch (...) {
					Retire(m_scheduler, std::move(m_staging));
				}
			}
			if (m_destination.ID() != 0u) {
				Retire(m_scheduler, std::move(m_destination));
			}
		}
		ScheduleEnvironment<Backend> get_env() const noexcept { return ScheduleEnvironment<Backend>(m_scheduler); }
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		template <class... Env> auto get_completion_signatures(Env&&...) const noexcept
			-> std::execution::completion_signatures<
				std::execution::set_value_t(Resource<Backend>),
				std::execution::set_error_t(std::exception_ptr), std::execution::set_stopped_t()> { return {}; }
#endif
		template <class R> auto connect(R&& receiver) && {
			return ResourceUploadOperationState<Backend, std::remove_cvref_t<R>>(
				m_scheduler, m_staging_pool, std::move(m_destination), std::move(m_staging),
				std::move(m_plan), std::move(m_data), std::forward<R>(receiver));
		}
	};

	template <class Backend, class Receiver> class ResourceReadbackOperationState {
		struct GraphReceiver;
		struct MapReceiver;
		struct UnmapReceiver;
		Scheduler<Backend> m_scheduler;
		std::shared_ptr<StagingResourcePool<Backend>> m_staging_pool;
		std::optional<Resource<Backend>> m_source;
		std::optional<Resource<Backend>> m_staging;
		ResourceTransferPlan<Backend> m_plan;
		Receiver m_receiver;
		CommandGraphBindings<Backend> m_bindings;
		std::vector<std::byte> m_data;
		std::stop_token m_stop_token;
		std::atomic_bool m_completed = false;
		bool m_started = false;

		struct GraphReceiver {
			ResourceReadbackOperationState* operation;
			std::stop_token get_stop_token() const noexcept { return operation->m_stop_token; }
			void set_value() && noexcept { operation->GraphCompleted(); }
			void set_error(std::exception_ptr const& error) && noexcept { operation->CompleteError(error); }
			void set_stopped() && noexcept { operation->CompleteStopped(); }
		};
		struct MapReceiver {
			ResourceReadbackOperationState* operation;
			std::stop_token get_stop_token() const noexcept { return operation->m_stop_token; }
			void set_value(MappedResource<Backend> mapped) && noexcept { operation->Mapped(std::move(mapped)); }
			void set_error(std::exception_ptr const& error) && noexcept { operation->CompleteError(error); }
			void set_stopped() && noexcept { operation->CompleteStopped(); }
		};
		struct UnmapReceiver {
			ResourceReadbackOperationState* operation;
			std::stop_token get_stop_token() const noexcept { return operation->m_stop_token; }
			void set_value(Resource<Backend> staging) && noexcept { operation->CompleteValue(std::move(staging)); }
			void set_error(std::exception_ptr const& error) && noexcept { operation->CompleteError(error); }
			void set_stopped() && noexcept { operation->CompleteStopped(); }
		};

		std::optional<CommandGraphOperationState<Backend, GraphReceiver>> m_graph;
		std::optional<ResourceMapOperationState<Backend, MapReceiver>> m_map;
		std::optional<ResourceUnmapOperationState<Backend, UnmapReceiver>> m_unmap;

		void ReleaseStaging(Resource<Backend>&& staging) noexcept {
			try {
				if (m_staging_pool->Release(staging, StagingResourceKind::Readback)) {
					return;
				}
			}
			catch (...) {
			}
			Retire(m_scheduler, std::move(staging));
		}

		void RetireResources() noexcept {
			if (m_staging) {
				auto staging = std::move(*m_staging); m_staging.reset();
				Retire(m_scheduler, std::move(staging));
			}
			if (m_source) {
				auto source = std::move(*m_source); m_source.reset();
				Retire(m_scheduler, std::move(source));
			}
		}
		void GraphCompleted() noexcept {
			try {
				ResourceMapFlags flags; flags.Set(ResourceMapFlagBits::Read);
				auto mapped_size = m_staging->Size();
				auto staging = std::move(*m_staging); m_staging.reset();
				m_map.emplace(m_scheduler, std::move(staging),
					ResourceDataRange{ 0u, mapped_size }, flags, MapReceiver{ this });
				m_map->start();
			}
			catch (...) { CompleteError(std::current_exception()); }
		}
		void Mapped(MappedResource<Backend> mapped) noexcept {
			try {
				auto bytes = mapped.Bytes();
				std::memcpy(m_data.data(), bytes.data(), m_data.size());
				m_unmap.emplace(std::move(mapped), UnmapReceiver{ this });
				m_unmap->start();
			}
			catch (...) { CompleteError(std::current_exception()); }
		}
		void CompleteValue(Resource<Backend> staging) noexcept {
			if (m_completed.exchange(true, std::memory_order::acq_rel)) return;
			m_staging.emplace(std::move(staging));
			auto source = std::move(*m_source); m_source.reset();
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_value(std::move(m_receiver), std::move(source), std::move(m_data));
#else
			std::move(m_receiver).set_value(std::move(source), std::move(m_data));
#endif
		}
		void CompleteError(std::exception_ptr const& error) noexcept {
			if (m_completed.exchange(true, std::memory_order::acq_rel)) return;
			RetireResources();
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_error(std::move(m_receiver), error);
#else
			std::move(m_receiver).set_error(error);
#endif
		}
		void CompleteStopped() noexcept {
			if (m_completed.exchange(true, std::memory_order::acq_rel)) return;
			RetireResources();
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_stopped(std::move(m_receiver));
#else
			std::move(m_receiver).set_stopped();
#endif
		}

	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using operation_state_concept = std::execution::operation_state_t;
#endif
		template <class R> ResourceReadbackOperationState(Scheduler<Backend> const& scheduler,
			std::shared_ptr<StagingResourcePool<Backend>> const& staging_pool,
			Resource<Backend>&& source, Resource<Backend>&& staging,
			ResourceTransferPlan<Backend>&& plan, R&& receiver)
			: m_scheduler(scheduler), m_staging_pool(staging_pool),
			m_source(std::move(source)), m_staging(std::move(staging)),
			m_plan(std::move(plan)), m_receiver(std::forward<R>(receiver)),
			m_bindings(m_plan.descriptor), m_data(m_plan.transfer_size),
			m_stop_token(ResourceTransferStopToken(m_receiver)) {
			m_bindings.Bind(m_plan.source, *m_source);
			m_bindings.Bind(m_plan.destination, *m_staging);
		}
		ResourceReadbackOperationState(ResourceReadbackOperationState const&) = delete;
		ResourceReadbackOperationState(ResourceReadbackOperationState&&) = delete;
		ResourceReadbackOperationState& operator=(ResourceReadbackOperationState const&) = delete;
		ResourceReadbackOperationState& operator=(ResourceReadbackOperationState&&) = delete;
		~ResourceReadbackOperationState() noexcept {
			if (m_staging && (!m_started || m_completed.load(std::memory_order::acquire))) {
				auto staging = std::move(*m_staging);
				m_staging.reset();
				ReleaseStaging(std::move(staging));
			}
			RetireResources();
		}
		void start() & noexcept {
			if (m_started) std::terminate();
			m_started = true;
			try {
				m_graph.emplace(m_scheduler, m_plan.graph, m_bindings, GraphReceiver{ this });
				m_graph->start();
			}
			catch (...) { CompleteError(std::current_exception()); }
		}
	};

	export template <class Backend> class ResourceReadbackSender {
		Scheduler<Backend> m_scheduler;
		std::shared_ptr<StagingResourcePool<Backend>> m_staging_pool;
		Resource<Backend> m_source;
		Resource<Backend> m_staging;
		ResourceTransferPlan<Backend> m_plan;
	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using sender_concept = std::execution::sender_t;
#endif
		ResourceReadbackSender(Scheduler<Backend> const& scheduler,
			std::shared_ptr<StagingResourcePool<Backend>> const& staging_pool,
			Resource<Backend>&& source,
			Resource<Backend>&& staging, ResourceTransferPlan<Backend>&& plan)
			: m_scheduler(scheduler), m_staging_pool(staging_pool),
			m_source(std::move(source)), m_staging(std::move(staging)),
			m_plan(std::move(plan)) {}
		ResourceReadbackSender(ResourceReadbackSender const&) = delete;
		ResourceReadbackSender& operator=(ResourceReadbackSender const&) = delete;
		ResourceReadbackSender(ResourceReadbackSender&&) noexcept = default;
		ResourceReadbackSender& operator=(ResourceReadbackSender&&) = delete;
		~ResourceReadbackSender() noexcept {
			if (m_staging.ID() != 0u) {
				try {
					if (!m_staging_pool->Release(m_staging, StagingResourceKind::Readback)) {
						Retire(m_scheduler, std::move(m_staging));
					}
				}
				catch (...) {
					Retire(m_scheduler, std::move(m_staging));
				}
			}
			if (m_source.ID() != 0u) {
				Retire(m_scheduler, std::move(m_source));
			}
		}
		ScheduleEnvironment<Backend> get_env() const noexcept { return ScheduleEnvironment<Backend>(m_scheduler); }
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		template <class... Env> auto get_completion_signatures(Env&&...) const noexcept
			-> std::execution::completion_signatures<
				std::execution::set_value_t(Resource<Backend>, std::vector<std::byte>),
				std::execution::set_error_t(std::exception_ptr), std::execution::set_stopped_t()> { return {}; }
#endif
		template <class R> auto connect(R&& receiver) && {
			return ResourceReadbackOperationState<Backend, std::remove_cvref_t<R>>(
				m_scheduler, m_staging_pool, std::move(m_source), std::move(m_staging),
				std::move(m_plan), std::forward<R>(receiver));
		}
	};

	export template <class Backend> ResourceUploadSender<Backend> Upload(
		LogicalDevice<Backend>& logical_device, Scheduler<Backend> const& scheduler,
		Resource<Backend>&& destination, std::size_t destination_offset,
		std::span<std::byte const> data
	) {
		if (data.empty()) {
			throw std::invalid_argument("Upload(): data must not be empty");
		}
		if (destination.ID() == 0u) {
			throw std::invalid_argument("Upload(): destination is empty");
		}
		if (!destination.Flags().Test(ResourceFlagBits::CopyDST)) {
			throw std::invalid_argument("Upload(): destination requires CopyDST");
		}
		if (destination_offset > destination.Size() ||
			data.size() > destination.Size() - destination_offset) {
			throw std::out_of_range("Upload(): destination range is invalid");
		}
		auto staging_pool = StagingResourcePoolAccess<Backend>::Get(logical_device);
		auto CreateStaging = [&logical_device](
			std::size_t size,
			ResourceFlags const& flags
		) {
			return logical_device.CreateBuffer(size, flags);
		};
		auto staging = staging_pool->Acquire(
			data.size(),
			StagingResourceKind::Upload,
			CreateStaging
		);
		auto plan = MakeResourceTransferPlan(logical_device, 0u, destination_offset, data.size());
		return ResourceUploadSender<Backend>(scheduler, staging_pool, std::move(destination),
			std::move(staging), std::move(plan), std::vector<std::byte>(data.begin(), data.end()));
	}

	export template <class Backend> ResourceReadbackSender<Backend> Readback(
		LogicalDevice<Backend>& logical_device, Scheduler<Backend> const& scheduler,
		Resource<Backend>&& source, ResourceDataRange const& range
	) {
		if (source.ID() == 0u) {
			throw std::invalid_argument("Readback(): source is empty");
		}
		if (!source.Flags().Test(ResourceFlagBits::CopySRC)) {
			throw std::invalid_argument("Readback(): source requires CopySRC");
		}
		if (range.size == 0u || range.offset > source.Size() ||
			range.size > source.Size() - range.offset) {
			throw std::out_of_range("Readback(): source range is invalid");
		}
		auto staging_pool = StagingResourcePoolAccess<Backend>::Get(logical_device);
		auto CreateStaging = [&logical_device](
			std::size_t size,
			ResourceFlags const& flags
		) {
			return logical_device.CreateBuffer(size, flags);
		};
		auto staging = staging_pool->Acquire(
			range.size,
			StagingResourceKind::Readback,
			CreateStaging
		);
		auto plan = MakeResourceTransferPlan(logical_device, range.offset, 0u, range.size);
		return ResourceReadbackSender<Backend>(scheduler, staging_pool, std::move(source),
			std::move(staging), std::move(plan));
	}

	export template <class Backend> ResourceUploadSender<Backend> Upload(
		LogicalDevice<Backend>& logical_device,
		Scheduler<Backend> const& scheduler,
		Resource<Backend>&& destination,
		TextureDataLayout const& layout,
		TextureRegion const& region,
		std::span<std::byte const> data
	) {
		if (destination.ID() == 0u || destination.TextureExtent().width == 0u) {
			throw std::invalid_argument("Upload(): destination must be a texture");
		}
		if (!destination.Flags().Test(ResourceFlagBits::CopyDST)) {
			throw std::invalid_argument("Upload(): destination requires CopyDST");
		}
		ValidateTextureTransfer(destination.TextureExtent(), destination.Flags(), layout, region,
			"Upload(): texture transfer layout or region is invalid");
		auto transfer_size = TextureTransferSize(layout, region, destination.Flags());
		if (data.size() != transfer_size) {
			throw std::invalid_argument("Upload(): texture data size does not match its layout");
		}
		auto staging_pool = StagingResourcePoolAccess<Backend>::Get(logical_device);
		auto CreateStaging = [&logical_device](
			std::size_t size,
			ResourceFlags const& flags
		) {
			return logical_device.CreateBuffer(size, flags);
		};
		auto staging = staging_pool->Acquire(
			transfer_size,
			StagingResourceKind::Upload,
			CreateStaging
		);
		auto plan = MakeTextureUploadPlan(logical_device, layout, region, transfer_size);
		std::vector<std::byte> staging_data(transfer_size);
		std::memcpy(staging_data.data(), data.data(), data.size());
		return ResourceUploadSender<Backend>(scheduler, staging_pool, std::move(destination),
			std::move(staging), std::move(plan), std::move(staging_data));
	}

	export template <class Backend> ResourceReadbackSender<Backend> Readback(
		LogicalDevice<Backend>& logical_device,
		Scheduler<Backend> const& scheduler,
		Resource<Backend>&& source,
		TextureRegion const& region,
		TextureDataLayout const& layout
	) {
		if (source.ID() == 0u || source.TextureExtent().width == 0u) {
			throw std::invalid_argument("Readback(): source must be a texture");
		}
		if (!source.Flags().Test(ResourceFlagBits::CopySRC)) {
			throw std::invalid_argument("Readback(): source requires CopySRC");
		}
		ValidateTextureTransfer(source.TextureExtent(), source.Flags(), layout, region,
			"Readback(): texture transfer layout or region is invalid");
		auto transfer_size = TextureTransferSize(layout, region, source.Flags());
		auto staging_pool = StagingResourcePoolAccess<Backend>::Get(logical_device);
		auto CreateStaging = [&logical_device](
			std::size_t size,
			ResourceFlags const& flags
		) {
			return logical_device.CreateBuffer(size, flags);
		};
		auto staging = staging_pool->Acquire(
			transfer_size,
			StagingResourceKind::Readback,
			CreateStaging
		);
		auto plan = MakeTextureReadbackPlan(logical_device, region, layout, transfer_size);
		return ResourceReadbackSender<Backend>(scheduler, staging_pool, std::move(source),
			std::move(staging), std::move(plan));
	}
}
