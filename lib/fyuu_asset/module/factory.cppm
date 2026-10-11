module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>

#include <string>

#include <string_view>

#include <coroutine>
#endif // !defined(__cpp_lib_modules)

export module fyuu_asset:factory;
import :asset;
import :operation;
import :uuid;
import :value;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_asset {
	/** @brief Creates or loads one Asset through a native coroutine awaitable. */
	class Factory final {
	private:
		OperationTask m_task;       ///< Queue node embedded in this awaitable.
		UUID m_uuid;                ///< Requested or generated asset identity.
		std::string m_type;         ///< Runtime type used by create or named load.
		std::string m_name;         ///< Asset name used by create or named load.
		Object m_fields;            ///< Create input or fields loaded by the worker.
		std::exception_ptr m_error; ///< Exception produced by the worker.

		static void ExecuteCreate(OperationTask* submitted) noexcept;
		static void ExecuteLoadByUUID(OperationTask* submitted) noexcept;
		static void ExecuteLoadByName(OperationTask* submitted) noexcept;

	public:
		/** @brief Creates and persists an asset from its complete dynamic fields. */
		Factory(std::string_view type, std::string_view name, Object const& fields);

		/** @brief Loads an asset identified by its catalog UUID. */
		explicit Factory(UUID const& uuid);

		/** @brief Loads an asset identified by its runtime type and catalog name. */
		Factory(std::string_view type, std::string_view name);

		Factory(Factory const&) = delete;
		Factory& operator=(Factory const&) = delete;
		Factory(Factory&&) = delete;
		Factory& operator=(Factory&&) = delete;

		[[nodiscard]] static constexpr bool await_ready() noexcept {
			return false;
		}

		void await_suspend(std::coroutine_handle<> continuation);
		[[nodiscard]] Asset await_resume();
	};
} // namespace fyuu_asset
