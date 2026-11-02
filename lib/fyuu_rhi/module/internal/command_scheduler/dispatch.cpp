module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>

#include <variant>

#include <span>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:command_scheduler_dispatch;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :execution;

namespace fyuu_rhi::execution {
	/** @brief Identifies attachment defaults that must be resolved before native recording. */
	bool UsesDefaultClear(BeginRendering const& value) noexcept {
		for (auto const& color : value.colors) {
			if (color.load == LoadOperation::ClearDefault) {
				return true;
			}
		}
		return value.depth_stencil &&
			(value.depth_stencil->depth_load == LoadOperation::ClearDefault ||
			value.depth_stencil->stencil_load == LoadOperation::ClearDefault);
	}

	/** @brief Resolves only default-clear aspects; Load, Discard and explicit Clear remain unchanged. */
	template <class Lookup>
	void ResolveAttachmentClears(BeginRendering& value, Lookup const& GetDefault) {
		for (auto& color : value.colors) {
			if (color.load == LoadOperation::ClearDefault) {
				auto clear = std::get_if<ColorClearValue>(&GetDefault(color.resource));
				if (!clear) {
					throw std::invalid_argument("Color clear default requires a color attachment texture");
				}
				color.clear = *clear;
				color.load = LoadOperation::Clear;
			}
		}
		if (value.depth_stencil) {
			auto depth = &*value.depth_stencil;
			if (depth->depth_load == LoadOperation::ClearDefault || depth->stencil_load == LoadOperation::ClearDefault) {
				auto clear = std::get_if<DepthStencilClearValue>(&GetDefault(depth->resource));
				if (!clear) {
					throw std::invalid_argument("Depth/stencil clear default requires a depth attachment texture");
				}
				if (depth->depth_load == LoadOperation::ClearDefault) {
					depth->clear_depth = clear->depth;
					depth->depth_load = LoadOperation::Clear;
				}
				if (depth->stencil_load == LoadOperation::ClearDefault) {
					depth->clear_stencil = clear->stencil;
					depth->stencil_load = LoadOperation::Clear;
				}
			}
		}
	}

	template <class NativeCommandSchedulerContext>
	struct ExecuteCommands {
		NativeCommandSchedulerContext* context;

		CompletionToken operator()(
			ExecutionPlan const&,
			std::span<PlatformHandle const>,
			std::span<Resource const>,
			std::span<View const>,
			std::span<Sampler const>,
			std::span<Pipeline const>,
			std::span<PipelineResourceGroup const>,
			StopTokenView
		) const {
			throw std::runtime_error(
				"Command execution is not implemented for this backend"
			);
		}
	};

} // namespace fyuu_rhi::execution
