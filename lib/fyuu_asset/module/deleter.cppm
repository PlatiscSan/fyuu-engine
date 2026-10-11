module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>

#include <string>

#include <string_view>

#include <coroutine>
#endif // !defined(__cpp_lib_modules)

export module fyuu_asset:deleter;
import :operation;
import :uuid;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_asset {
	/** @brief Asynchronously deletes one persistent asset and its catalog entry. */
	class Deleter final {
	private:
		OperationTask m_task;       ///< Queue node embedded in this awaitable.
		UUID m_uuid;                ///< Requested or resolved asset identity.
		std::string m_type;         ///< Runtime type used by named deletion.
		std::string m_name;         ///< Asset name used by named deletion.
		std::exception_ptr m_error; ///< Exception produced by the worker.

		static void ExecuteByUUID(OperationTask* submitted) noexcept;
		static void ExecuteByName(OperationTask* submitted) noexcept;

	public:
		/** @brief Deletes the asset identified by a catalog UUID. */
		explicit Deleter(UUID const& uuid) noexcept;

		/** @brief Deletes the asset identified by runtime type and catalog name. */
		Deleter(std::string_view type, std::string_view name);

		Deleter(Deleter const&) = delete;
		Deleter& operator=(Deleter const&) = delete;
		Deleter(Deleter&&) = delete;
		Deleter& operator=(Deleter&&) = delete;

		/** @brief Always suspends so deletion executes on the asset worker. */
		[[nodiscard]] static constexpr bool await_ready() noexcept {
			return false;
		}

		/** @brief Submits this deletion and stores its suspended continuation. */
		void await_suspend(std::coroutine_handle<> continuation);

		/** @brief Returns the deleted UUID or rethrows the worker exception. */
		[[nodiscard]] UUID await_resume();
	};
} // namespace fyuu_asset
