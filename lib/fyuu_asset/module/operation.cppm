module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <atomic>

#include <coroutine>
#endif // !defined(__cpp_lib_modules)

export module fyuu_asset:operation;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace fyuu_asset {
	/** @brief Intrusive queue node embedded in one asynchronous asset operation. */
	struct OperationTask final {
		std::atomic<OperationTask*> m_next = nullptr;  ///< Next task in the lock-free producer stack.
		void* m_operation = nullptr;                   ///< Concrete operation owning this node.
		std::coroutine_handle<> m_continuation = {};   ///< Suspended operation coroutine.
		void (*Execute)(OperationTask*) noexcept = {}; ///< Background operation entry point.
	};

	/** @brief Submits one embedded operation to the shared asset worker. */
	void ScheduleAssetTask(OperationTask* submitted);
} // namespace fyuu_asset
