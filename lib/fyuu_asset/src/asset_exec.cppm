module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>

#include <concepts>
#include <compare>
#if defined(__cpp_lib_reflection)
#include <meta>
#endif // defined(__cpp_lib_reflection)
#endif // !defined(__cpp_lib_modules)
export module fyuu_asset:asset_manager;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import plastic.serial_task;
import :managed_asset;
import :base_asset;

namespace fyuu_asset::execution {
	
	/// Background worker pool + task queue shared by the scheduler and the
	/// senders it produces.
	export class SchedulerContext;

	/// Operation state returned by AssetLoader::connect(). start() runs
	/// the load on a worker thread and completes the receiver with set_value /
	/// set_error / set_stopped.
	export template <class Receiver> class AssetLoadOperation {
	private:
		friend class AssetLoader;
		std::shared_ptr<SchedulerContext> m_context;
		Receiver m_receiver;	

	public:
		AssetLoadOperation(std::shared_ptr<SchedulerContext> const& context, Receiver&& receiver) noexcept
			: m_context(context),
			m_receiver(std::move(receiver)) {
		}

		void start() noexcept {
			// TODO: enqueue the load on a worker thread; on completion call
			//   std::execution::set_value(std::move(m_receiver), AssetLoadResult{});
			//   std::execution::set_error(std::move(m_receiver), error);
			//   std::execution::set_stopped(std::move(m_receiver));
		}


	};

	/// Sender produced by AssetScheduler::schedule().
	export class AssetLoader {
	private:
		friend class AssetScheduler;
		std::shared_ptr<SchedulerContext> m_context;		
	public:
		explicit AssetLoader(std::shared_ptr<SchedulerContext> const& context) noexcept
			: m_context(std::move(context)) {
		}

#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using sender_concept = std::execution::sender_t;
		using completion_signatures = std::execution::completion_signatures<
			std::execution::set_value_t(ManagedAsset<BaseAsset>),
			std::execution::set_error_t(std::exception_ptr),
			std::execution::set_stopped_t()
		>;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		template <class Receiver>
		[[nodiscard]] AssetLoadOperation<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) && noexcept {
			return { m_context, std::forward<Receiver>(receiver) };
		}
	};

	/// P2300 std::execution::scheduler for asset loads: copyable,
	/// equality-comparable, schedule() returns a sender that runs the load
	/// on a background worker thread.
	export class AssetScheduler {	
	private:
		std::shared_ptr<SchedulerContext> m_context;

	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using scheduler_concept = std::execution::scheduler_t;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		AssetScheduler();

		[[nodiscard]] AssetLoader schedule() const noexcept {
			return AssetLoader{ m_context };
		}

		std::strong_ordering operator<=>(AssetScheduler const&) const noexcept = default;


	};


} // namespace fyuu_asset::execution