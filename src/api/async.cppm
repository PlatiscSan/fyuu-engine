module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <utility>
#include <string>
#endif // !defined(__cpp_lib_modules)
#include "fyuu_async.h"

module fyuu_engine:async_api;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import plastic.serial_task;

struct Fyuu_AsyncOperation_T {
	plastic::concurrency::SerialTask<void> task;
	Fyuu_Result result = FYUU_PENDING;
	std::string error;

	explicit Fyuu_AsyncOperation_T(plastic::concurrency::SerialTask<void>&& operation) noexcept
		: task(std::move(operation)) {
	}

	Fyuu_Result Resolve() noexcept {
		if (result != FYUU_PENDING) {
			return result;
		}
		try {
			task.Wait();
			result = FYUU_SUCCESS;
		}
		catch (std::exception const& exception) {
			try {
				error = exception.what();
			}
			catch (...) {
			}
			result = FYUU_ERROR_OPERATION_FAILED;
		}
		catch (...) {
			try {
				error = "Unknown asynchronous operation error";
			}
			catch (...) {
			}
			result = FYUU_ERROR_OPERATION_FAILED;
		}
		return result;
	}
};

namespace fyuu_engine::api {

	Fyuu_AsyncOperation CreateAsyncOperation(plastic::concurrency::SerialTask<void>&& operation) {
		return new Fyuu_AsyncOperation_T{ std::move(operation) };
	}

}

extern "C" {

	LIB_API Fyuu_Bool LIB_CALL Fyuu_AsyncOperationIsDone(Fyuu_AsyncOperation operation) NOEXCEPT {
		return operation && operation->task.IsDone();
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_AsyncOperationGetResult(Fyuu_AsyncOperation operation) NOEXCEPT {
		if (!operation) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		if (!operation->task.IsDone()) {
			return FYUU_PENDING;
		}
		return operation->Resolve();
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_AsyncOperationWait(Fyuu_AsyncOperation operation) NOEXCEPT {
		return operation
			? operation->Resolve()
			: FYUU_ERROR_INVALID_ARGUMENT;
	}

	LIB_API Fyuu_StringView LIB_CALL Fyuu_AsyncOperationGetError(Fyuu_AsyncOperation operation) NOEXCEPT {
		if (!operation) {
			return {};
		}
		if (operation->task.IsDone()) {
			operation->Resolve();
		}
		return {
			.data = operation->error.data(),
			.size = operation->error.size()
		};
	}

	LIB_API void LIB_CALL Fyuu_AsyncOperationRelease(Fyuu_AsyncOperation operation) NOEXCEPT {
		if (!operation) {
			return;
		}
		operation->Resolve();
		delete operation;
	}

}
