module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string_view>
#include <concepts>
#include <vector>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#endif // !defined(__cpp_lib_modules)

export module fyuu_rhi:logical_device;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource_types;
import :resource;
import :view;
import :sampler_types;
import :sampler;
import :scheduler_types;
import :scheduler;
import :command_graph;
import :command_graph_validation;
import :pipeline_types;
import :pipeline;
import :native_pipeline_binding;
import :staging_resource_pool;

namespace fyuu_rhi::execution {
	template <class Backend> class StagingResourcePoolAccess;
}

namespace fyuu_rhi {

	export template <class Backend> class LogicalDevice {
	public:
		using Implementation = typename Backend::LogicalDevice;

	private:
		template <class U> friend class execution::StagingResourcePoolAccess;

		Implementation m_impl;
		std::shared_ptr<execution::StagingResourcePool<Backend>> m_staging_resource_pool;

	public:
		template <std::convertible_to<Implementation> I>
		LogicalDevice(I&& impl)
			: m_impl(std::forward<I>(impl)),
			m_staging_resource_pool(
				std::make_shared<execution::StagingResourcePool<Backend>>()
			) {

		}

		Resource<Backend> CreateBuffer(std::size_t size_in_bytes, ResourceFlags const& flags) {
			using Ret = decltype(Backend::CreateBuffer(m_impl, size_in_bytes, flags));
			static_assert(std::constructible_from<Resource<Backend>, Ret>,
				"Resource<Backend> must be constructible from buffer returned by CreateBuffer()");
			return Resource<Backend>(
				Backend::CreateBuffer(m_impl, size_in_bytes, flags),
				size_in_bytes,
				flags
			);
		}

		Resource<Backend> CreateTexture(std::size_t width, std::size_t height, std::size_t depth_arr_layers, std::size_t mip_lvl_cnt, ResourceFlags const& flags) {
			if (width == 0u || height == 0u || depth_arr_layers == 0u || mip_lvl_cnt == 0u) {
				throw std::invalid_argument("CreateTexture(): dimensions and mip count must be non-zero");
			}
			using Ret = decltype(Backend::CreateTexture(m_impl, width, height, depth_arr_layers, mip_lvl_cnt, flags));
			static_assert(std::constructible_from<Resource<Backend>, Ret>,
				"Resource<Backend> must be constructible from texture returned by CreateTexture()");
			return Resource<Backend>(
				Backend::CreateTexture(
					m_impl,
					width,
					height,
					depth_arr_layers,
					mip_lvl_cnt,
					flags
				),
				0u,
				flags,
				{
					.width = static_cast<std::uint32_t>(width),
					.height = static_cast<std::uint32_t>(height),
					.depth_or_array_layers = static_cast<std::uint32_t>(depth_arr_layers),
					.mip_levels = static_cast<std::uint32_t>(mip_lvl_cnt)
				}
			);
		}

		View<Backend> CreateBufferView(Resource<Backend> const& buf, std::size_t offset, std::size_t range, ResourceFlags const& flags) {
			using Ret = decltype(Backend::CreateBufferView(m_impl, buf.GetLogicalDevicePassKey().GetImplementation(), offset, range, flags));
			static_assert(std::constructible_from<View<Backend>, Ret>,
				"View<Backend> must be constructible from view returned by CreateBufferView()");
			return Backend::CreateBufferView(m_impl, buf.GetLogicalDevicePassKey().GetImplementation(), offset, range, flags);
		}

		View<Backend> CreateTextureView(Resource<Backend> const& tex, std::size_t base_mip_lvl, std::size_t mip_lvl_cnt, std::size_t base_arr_layer, std::size_t arr_layer_cnt, ResourceFlags const& flags) {
			using Ret = decltype(Backend::CreateTextureView(m_impl, tex.GetLogicalDevicePassKey().GetImplementation(), base_mip_lvl, mip_lvl_cnt, base_arr_layer, arr_layer_cnt, flags));
			static_assert(std::constructible_from<View<Backend>, Ret>,
				"View<Backend> must be constructible from view returned by CreateTextureView()");
			return Backend::CreateTextureView(m_impl, tex.GetLogicalDevicePassKey().GetImplementation(), base_mip_lvl, mip_lvl_cnt, base_arr_layer, arr_layer_cnt, flags);
		}

		Sampler<Backend> CreateSampler(SamplerDescriptor const& descriptor) {
			using Ret = decltype(Backend::CreateSampler(m_impl, descriptor));
			static_assert(std::constructible_from<Sampler<Backend>, Ret>,
				"Sampler<Backend> must be constructible from sampler returned by CreateSampler()");
			return Backend::CreateSampler(m_impl, descriptor);
		}

		template <class... Args>
		execution::Scheduler<Backend> CreateScheduler(
			execution::SchedulerDescriptor const& descriptor,
			Args&&... args
		) {
			using Ret = decltype(
				Backend::CreateScheduler(
					m_impl,
					descriptor,
					std::forward<Args>(args)...
				)
			);
			static_assert(
				std::constructible_from<execution::Scheduler<Backend>, Ret>,
				"Scheduler<Backend> must be constructible from scheduler returned by CreateScheduler()"
			);
			return execution::Scheduler<Backend>(
				Backend::CreateScheduler(
					m_impl,
					descriptor,
					std::forward<Args>(args)...
				)
			);
		}

		execution::ExecutableGraph<Backend> CompileCommandGraph(
			execution::CommandGraph<Backend> const& graph
		) {
			auto impl = Backend::CompileCommandGraph(graph.GetImplementation());
			return execution::ExecutableGraph<Backend>(impl);
		}

		execution::CommandGraph<Backend> CreateCommandGraph(
			execution::CommandGraphDescriptor const& descriptor
		) {
			execution::ValidateCommandGraphDescriptor(descriptor);
			auto impl = Backend::CreateCommandGraph(descriptor);
			return execution::CommandGraph<Backend>(impl);
		}

		pipeline::Pipeline<Backend> CreateGraphicsPipeline(
			pipeline::GraphicsPipelineDescriptor const& descriptor
		) {
			using Ret = decltype(Backend::CreateGraphicsPipeline(m_impl, descriptor));
			static_assert(
				std::constructible_from<pipeline::Pipeline<Backend>, Ret>,
				"Pipeline<Backend> must be constructible from pipeline returned by CreateGraphicsPipeline()"
			);
			return Backend::CreateGraphicsPipeline(m_impl, descriptor);
		}

		pipeline::Pipeline<Backend> CreateComputePipeline(
			pipeline::ComputePipelineDescriptor const& descriptor
		) {
			using Ret = decltype(Backend::CreateComputePipeline(m_impl, descriptor));
			static_assert(
				std::constructible_from<pipeline::Pipeline<Backend>, Ret>,
				"Pipeline<Backend> must be constructible from pipeline returned by CreateComputePipeline()"
			);
			return Backend::CreateComputePipeline(m_impl, descriptor);
		}

		pipeline::PipelineResourceGroup<Backend> CreatePipelineResourceGroup(
			pipeline::Pipeline<Backend> const& pipeline_obj,
			std::uint32_t space,
			std::span<pipeline::PipelineResourceBinding<Backend> const> bindings
		) {
			std::vector<pipeline::NativePipelineResourceBinding<Backend>> native_bindings;
			native_bindings.reserve(bindings.size());
			for (auto const& entry : bindings) {
				auto const& value = entry.value;
				auto buffer = value.Buffer();
				auto view = value.BoundView();
				auto sampler = value.BoundSampler();
				pipeline::NativePipelineBindingValue<Backend> native_value;
				if (buffer) {
					native_value = pipeline::NativePipelineBufferBinding<Backend>{
						.impl = std::cref(buffer->GetLogicalDevicePassKey().GetImplementation()),
						.offset = value.Offset(),
						.size = value.Size()
					};
				}
				else if (view && sampler) {
					native_value = pipeline::NativePipelineCombinedBinding<Backend>{
						.view = std::cref(view->GetPassKey().GetImplementation()),
						.sampler = std::cref(sampler->GetPassKey().GetImplementation())
					};
				}
				else if (view) {
					native_value = std::cref(view->GetPassKey().GetImplementation());
				}
				else if (sampler) {
					native_value = std::cref(sampler->GetPassKey().GetImplementation());
				}
				native_bindings.push_back(
					{
						.slot = entry.slot,
						.array_element = entry.array_element,
						.value = std::move(native_value)
					}
				);
			}
			auto impl = Backend::CreatePipelineResourceGroup(
				m_impl,
				pipeline_obj.GetPassKey().GetImplementation(),
				space,
				native_bindings
			);
			return pipeline::PipelineResourceGroup<Backend>(std::move(impl), space);
		}

	};

}

namespace fyuu_rhi::execution {
	template <class Backend> class StagingResourcePoolAccess {
	public:
		[[nodiscard]] static std::shared_ptr<StagingResourcePool<Backend>> const& Get(
			LogicalDevice<Backend> const& logical_device
		) noexcept {
			return logical_device.m_staging_resource_pool;
		}
	};
}
