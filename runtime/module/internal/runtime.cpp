module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <stdexcept>
#include <string>
#endif // !defined(__cpp_lib_modules)
#include "fyuu_platform.h"
#include "fyuu_runtime.h"

module fyuu_engine:runtime;

import fyuu_engine;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace {

	void ThrowIfFailed(Fyuu_Result const& result) {
		switch (result) {
		case FYUU_RESULT_SUCCESS:
			return;
		case FYUU_RESULT_INVALID_ARGUMENT:
			throw fyuu_engine::Error{ fyuu_engine::Result::InvalidArgument, "Invalid argument" };
		case FYUU_RESULT_INVALID_STATE:
			throw fyuu_engine::Error{ fyuu_engine::Result::InvalidState, "Invalid state" };
		case FYUU_RESULT_PLATFORM_ERROR:
			throw fyuu_engine::Error{ fyuu_engine::Result::PlatformError, "Platform operation failed" };
		case FYUU_RESULT_APPLICATION_ERROR:
			throw fyuu_engine::Error{ fyuu_engine::Result::ApplicationError, "Application operation failed" };
		case FYUU_RESULT_OUT_OF_MEMORY:
			throw fyuu_engine::Error{ fyuu_engine::Result::OutOfMemory, "Out of memory" };
		case FYUU_RESULT_LOG_ERROR:
			throw fyuu_engine::Error{ fyuu_engine::Result::LogError, "Log operation failed" };
		default:
			throw fyuu_engine::Error{ fyuu_engine::Result::UnknownError, "Unknown Runtime error" };
		}
	}

}

namespace fyuu_engine {

	// This partition implements the native module policy layer. Public calls enter here,
	// delegate state changes to the C ABI, and translate failed Result values to Error.
	// Application and Platform callbacks return through the generated :api_glue partition.

	Error::Error(Result const& result, std::string const& message)
		: std::runtime_error(message),
		m_result(result) {
	}

	Result Error::Code() const noexcept {
		return m_result;
	}

	Application::~Application() noexcept = default;

	void Application::Tick(Runtime&) {
	}

	bool Application::CloseRequested(Runtime&) {
		return true;
	}

	Platform::Platform() {
		Fyuu_PlatformDescriptor const descriptor{
			sizeof(Fyuu_PlatformDescriptor),
			FYUU_ABI_VERSION,
			this,
			nullptr,
			PumpEventsThunk,
			nullptr
		};
		auto const result = Fyuu_PlatformCreate(&descriptor, &m_handle);
		ThrowIfFailed(result);
	}

	Platform::~Platform() noexcept {
		Fyuu_PlatformDestroy(m_handle);
	}

	bool Platform::Valid() const noexcept {
		return m_handle;
	}

	Runtime::Runtime(Platform& platform, Application& application)
		: m_application(&application),
		m_platform(&platform) {
		if (!platform.Valid()) {
			throw Error{ Result::InvalidArgument, "Runtime requires a valid Platform" };
		}
		Fyuu_RuntimeDescriptor const descriptor{
			sizeof(Fyuu_RuntimeDescriptor),
			FYUU_ABI_VERSION,
			this,
			nullptr,
			TickThunk,
			CloseRequestedThunk,
			nullptr
		};
		auto result = Fyuu_RuntimeCreate(platform.m_handle, &descriptor, &m_handle);
		if (result == FYUU_RESULT_SUCCESS) {
			result = Fyuu_RuntimeInitialize(m_handle);
		}
		if (result != FYUU_RESULT_SUCCESS) {
			Fyuu_RuntimeDestroy(m_handle);
			m_handle = nullptr;
		}
		ThrowIfFailed(result);
	}

	Runtime::~Runtime() noexcept {
		Fyuu_RuntimeDestroy(m_handle);
	}

	void Runtime::Tick() {
		auto const result = Fyuu_RuntimeTick(m_handle);
		if (m_platform && m_platform->m_pending_exception) {
			auto const exception = m_platform->m_pending_exception;
			m_platform->m_pending_exception = nullptr;
			std::rethrow_exception(exception);
		}
		if (m_pending_exception) {
			auto const exception = m_pending_exception;
			m_pending_exception = nullptr;
			std::rethrow_exception(exception);
		}
		ThrowIfFailed(result);
	}

	void Runtime::RequestStop() noexcept {
		Fyuu_RuntimeRequestStop(m_handle);
	}

	RuntimeState Runtime::GetState() const noexcept {
		return static_cast<RuntimeState>(Fyuu_RuntimeGetState(m_handle));
	}

	void Runtime::Run() {
		// Run is only a convenience driver. Construction and Tick remain the authoritative
		// operations, so manual and blocking execution follow the same C ABI call chain.
		try {
			while (GetState() != RuntimeState::Stopped) {
				Tick();
			}
		}
		catch (...) {
			auto const exception = std::current_exception();
			RequestStop();
			try {
				Tick();
			}
			catch (...) {
			}
			std::rethrow_exception(exception);
		}
	}

	bool Runtime::Valid() const noexcept {
		return m_handle;
	}

}
