module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>

#include <stdexcept>

#include <algorithm>

#include <vector>

#include <functional>

#include <cstdint>

#include <variant>

#include <span>

#include <ranges>

#include <format>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:native_pipeline_binding;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :pipeline_types;
import :resource_types;
import :slang_pipeline_interface;

namespace fyuu_rhi::pipeline {

	template <class Backend>
	struct NativePipelineBufferBinding {
		std::reference_wrapper<typename Backend::Resource const> impl;
		std::size_t offset = 0;
		std::size_t size = PipelineWholeBuffer;
	};

	template <class Backend>
	using NativePipelineViewBinding = std::reference_wrapper<typename Backend::View const>;

	template <class Backend>
	using NativePipelineSamplerBinding = std::reference_wrapper<typename Backend::Sampler const>;

	template <class Backend>
	struct NativePipelineCombinedBinding {
		std::reference_wrapper<typename Backend::View const> view;
		std::reference_wrapper<typename Backend::Sampler const> sampler;
	};

	template <class Backend>
	using NativePipelineBindingValue = std::variant<
		std::monostate,
		NativePipelineBufferBinding<Backend>,
		NativePipelineViewBinding<Backend>,
		NativePipelineSamplerBinding<Backend>,
		NativePipelineCombinedBinding<Backend>
	>;

	template <class Backend>
	struct NativePipelineResourceBinding {
		std::uint32_t slot = 0;
		std::uint32_t array_element = 0;
		NativePipelineBindingValue<Backend> value;
	};

	struct PipelineBindingMetadata {
		ResourceFlags flags;
		std::uint32_t slot = 0;
		std::uint32_t space = 0;
		std::uint32_t count = 1;
	};

	std::vector<PipelineBindingMetadata> MakePipelineBindingMetadata(
		SlangPipelineInterface const& pipeline_interface
	) {
		std::vector<PipelineBindingMetadata> result;
		result.reserve(pipeline_interface.bindings.size());
		for (auto const& entry : pipeline_interface.bindings) {
			result.push_back(
				{
					.flags = entry.flags,
					.slot = entry.slot,
					.space = entry.space,
					.count = entry.count
				}
			);
		}
		return result;
	}

	template <class Backend>
	struct NativePipelineResourceGroup {
		struct Binding {
			std::uint32_t slot = 0;
			std::uint32_t array_element = 0;
			NativePipelineBindingValue<Backend> value;
		};

		std::vector<Binding> bindings;
		std::vector<PipelineBindingMetadata> layout;
	};

	template <class Backend>
	NativePipelineResourceGroup<Backend> MakePipelineResourceGroup(
		std::span<PipelineBindingMetadata const> metadata,
		std::uint32_t space,
		std::span<NativePipelineResourceBinding<Backend> const> bindings
	) {
		NativePipelineResourceGroup<Backend> result;
		for (auto const& reflected : metadata) {
			if (reflected.space == space) {
				result.layout.push_back(reflected);
			}
		}
		result.bindings.reserve(bindings.size());
		for (auto const& entry : bindings) {
			auto MatchesLocation = [space, &entry](PipelineBindingMetadata const& value) {
				return value.space == space && value.slot == entry.slot;
			};
			auto reflected = std::ranges::find_if(metadata, MatchesLocation);
			if (reflected == metadata.end()) {
				throw std::invalid_argument(
					std::format("Pipeline space {} has no slot {}", space, entry.slot)
				);
			}
			if (entry.array_element >= reflected->count) {
				throw std::out_of_range(
					std::format(
						"Pipeline binding {} array element {} exceeds count {}",
						entry.slot,
						entry.array_element,
						reflected->count
					)
				);
			}
			auto MatchesExisting = [&entry](typename NativePipelineResourceGroup<Backend>::Binding const& value) {
				return value.slot == entry.slot && value.array_element == entry.array_element;
			};
			if (std::ranges::find_if(result.bindings, MatchesExisting) != result.bindings.end()) {
				throw std::invalid_argument(
					std::format(
						"Pipeline binding {} array element {} is specified more than once",
						entry.slot,
						entry.array_element
					)
				);
			}

			bool expects_buffer =
				reflected->flags.Test(ResourceFlagBits::UniformBuffer) ||
				reflected->flags.Test(ResourceFlagBits::StorageBuffer);
			bool expects_sampler = reflected->flags.Test(ResourceFlagBits::SamplerBinding);
			bool expects_view =
				reflected->flags.Test(ResourceFlagBits::TextureBinding) ||
				reflected->flags.Test(ResourceFlagBits::StorageBinding);
			bool matches = false;
			if (expects_buffer && !expects_sampler) {
				matches = std::holds_alternative<NativePipelineBufferBinding<Backend>>(entry.value);
			}
			else if (!expects_buffer && expects_view && !expects_sampler) {
				matches = std::holds_alternative<NativePipelineViewBinding<Backend>>(entry.value);
			}
			else if (!expects_buffer && !expects_view && expects_sampler) {
				matches = std::holds_alternative<NativePipelineSamplerBinding<Backend>>(entry.value);
			}
			else if (!expects_buffer && expects_view && expects_sampler) {
				matches = std::holds_alternative<NativePipelineCombinedBinding<Backend>>(entry.value);
			}
			if (!matches) {
				throw std::invalid_argument(
					std::format("Resources do not match pipeline slot {}", entry.slot)
				);
			}
			result.bindings.push_back(
				{
					.slot = entry.slot,
					.array_element = entry.array_element,
					.value = entry.value
				}
			);
		}
		for (auto const& reflected : metadata) {
			if (reflected.space != space) {
				continue;
			}
			for (std::uint32_t array_element = 0; array_element < reflected.count; ++array_element) {
				auto MatchesRequired = [&reflected, array_element](
					typename NativePipelineResourceGroup<Backend>::Binding const& value
				) {
					return value.slot == reflected.slot && value.array_element == array_element;
				};
				if (std::ranges::find_if(result.bindings, MatchesRequired) == result.bindings.end()) {
					throw std::invalid_argument(
						std::format(
							"Pipeline binding {} array element {} is missing",
							reflected.slot,
							array_element
						)
					);
				}
			}
		}
		return result;
	}

}
