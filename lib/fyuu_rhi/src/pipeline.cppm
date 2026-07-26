module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <variant>
#include <vector>
#endif // !defined(__cpp_lib_modules)

export module fyuu_rhi:pipeline;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :pipeline_types;
import :resource;
import :view;
import :sampler;

namespace fyuu_rhi {
	template <class Backend> class LogicalDevice;
}

namespace fyuu_rhi::execution {
	template <class Backend> class CommandGraphBindings;
}

namespace fyuu_rhi::pipeline {

	export template <class Backend> class Pipeline {
	public:
		using Implementation = typename Backend::Pipeline;

	private:
		Implementation m_impl;

	public:
		template <class PipelineType>
		class PassKey {
			static_assert(
				std::same_as<PipelineType, Pipeline> || std::same_as<PipelineType, Pipeline const>,
				"PipelineType must be Pipeline or const Pipeline"
			);

			template <class U> friend class fyuu_rhi::LogicalDevice;
			template <class U> friend class PipelineResourceGroup;
			template <class U> friend class fyuu_rhi::execution::CommandGraphBindings;

			PipelineType* m_pipeline;

			template <class Self>
			decltype(auto) GetImplementation(this Self&& self) noexcept {
				return (self.m_pipeline->m_impl);
			}

		public:
			explicit PassKey(PipelineType* pipeline) noexcept
				: m_pipeline(pipeline) {

			}
		};

		template <std::convertible_to<Implementation> I>
		Pipeline(I&& impl)
			: m_impl(std::forward<I>(impl)) {

		}

		Pipeline(Pipeline const&) = delete;
		Pipeline& operator=(Pipeline const&) = delete;
		Pipeline(Pipeline&&) noexcept = default;
		Pipeline& operator=(Pipeline&&) noexcept = default;
		~Pipeline() noexcept = default;

		template <class Self>
		auto GetPassKey(this Self&& self) noexcept {
			using PipelineType = std::remove_reference_t<Self>;
			return PassKey<PipelineType>{ &self };
		}
	};

	export template <class Backend> class PipelineBindingValue {
	private:
		struct BufferBinding {
			std::reference_wrapper<Resource<Backend> const> buffer;
			std::size_t offset = 0;
			std::size_t size = PipelineWholeBuffer;
		};

		using ViewBinding = std::reference_wrapper<View<Backend> const>;
		using SamplerBinding = std::reference_wrapper<Sampler<Backend> const>;

		struct CombinedBinding {
			std::reference_wrapper<View<Backend> const> view;
			std::reference_wrapper<Sampler<Backend> const> sampler;
		};

		using Value = std::variant<BufferBinding, ViewBinding, SamplerBinding, CombinedBinding>;
		Value m_value;

		template <class T>
		explicit PipelineBindingValue(T&& value) noexcept
			: m_value(std::forward<T>(value)) {

		}

	public:
		static PipelineBindingValue FromBuffer(
			Resource<Backend> const& buffer,
			std::size_t offset = 0,
			std::size_t size = PipelineWholeBuffer
		) noexcept {
			return PipelineBindingValue(BufferBinding{ buffer, offset, size });
		}

		static PipelineBindingValue FromView(View<Backend> const& view) noexcept {
			return PipelineBindingValue(ViewBinding{ view });
		}

		static PipelineBindingValue FromSampler(Sampler<Backend> const& sampler) noexcept {
			return PipelineBindingValue(SamplerBinding{ sampler });
		}

		static PipelineBindingValue FromCombined(
			View<Backend> const& view,
			Sampler<Backend> const& sampler
		) noexcept {
			return PipelineBindingValue(CombinedBinding{ view, sampler });
		}

		Resource<Backend> const* Buffer() const noexcept {
			auto binding = std::get_if<BufferBinding>(&m_value);
			return binding ? &binding->impl.get() : nullptr;
		}

		View<Backend> const* BoundView() const noexcept {
			if (auto binding = std::get_if<ViewBinding>(&m_value)) {
				return &binding->get();
			}
			if (auto binding = std::get_if<CombinedBinding>(&m_value)) {
				return &binding->view.get();
			}
			return nullptr;
		}

		Sampler<Backend> const* BoundSampler() const noexcept {
			if (auto binding = std::get_if<SamplerBinding>(&m_value)) {
				return &binding->get();
			}
			if (auto binding = std::get_if<CombinedBinding>(&m_value)) {
				return &binding->sampler.get();
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

	export template <class Backend> struct PipelineResourceBinding {
		std::uint32_t slot = 0;
		std::uint32_t array_element = 0;
		PipelineBindingValue<Backend> value;
	};

	// A resource group is created for one pipeline space and is immutable after
	// creation. Bound resources are non-owning and must remain alive and unmoved
	// while the group can be used to materialize a
	// Vulkan descriptor set, WebGPU bind group, D3D12 descriptor tables, or
	// OpenGL bindings when command encoding is introduced.
	export template <class Backend> class PipelineResourceGroup {
	public:
		using Implementation = typename Backend::PipelineResourceGroup;

		template <class ResourceGroupType> class PassKey {
			static_assert(
				std::same_as<ResourceGroupType, PipelineResourceGroup> ||
				std::same_as<ResourceGroupType, PipelineResourceGroup const>
			);

			template <class U> friend class fyuu_rhi::LogicalDevice;
			template <class U> friend class fyuu_rhi::execution::CommandGraphBindings;

			ResourceGroupType* m_resource_group;

			template <class Self>
			decltype(auto) GetImplementation(this Self&& self) noexcept {
				return (self.m_resource_group->m_impl);
			}

		public:
			explicit PassKey(ResourceGroupType* resource_group) noexcept
				: m_resource_group(resource_group) {

			}
		};

	private:
		Implementation m_impl;
		std::uint32_t m_space = 0;

	public:
		template <std::convertible_to<Implementation> I>
		PipelineResourceGroup(I&& impl, std::uint32_t space)
			: m_impl(std::forward<I>(impl)),
			m_space(space) {

		}

		std::uint32_t Space() const noexcept {
			return m_space;
		}

		template <class Self>
		auto GetPassKey(this Self&& self) noexcept {
			using ResourceGroupType = std::remove_reference_t<Self>;
			return PassKey<ResourceGroupType>{ &self };
		}
	};

}
