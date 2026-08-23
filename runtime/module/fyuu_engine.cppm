module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#endif // !defined(__cpp_lib_modules)
#include "fyuu_log.h"
#include "fyuu_platform.h"
#include "fyuu_runtime.h"

export module fyuu_engine;

export import :api_types;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_engine {

	class Platform;
	class Runtime;
	class Logger;

	/// Reports a failed C++ Runtime operation while preserving its stable ABI result code.
	class Error final : public std::runtime_error {
	private:
		Result m_result;

	public:
		Error(Result result, std::string_view message);
		Result Code() const noexcept;
	};

	/// Borrowed log data passed synchronously to one LogSink invocation.
	struct LogRecord {
		LogLevel level = LogLevel::Info;
		std::string_view category;
		std::string_view message;
	};

	/// Receives records from Logger. The sink must outlive every Logger borrowing it.
	struct LogSink {
		virtual ~LogSink() noexcept;
		virtual void Write(LogRecord const& record) = 0;
	};

	/// Writes synchronous human-readable records to the process standard output stream.
	struct ConsoleLogSink final : LogSink {
		void Write(LogRecord const& record) override;
	};

	/// Owns an opaque ABI logger and synchronously forwards records to a borrowed LogSink.
	/// Call chain: Logger::Write -> C ABI -> generated thunk -> LogSink::Write.
	class Logger {
	private:
		Fyuu_Logger* m_handle = nullptr;
		LogSink* m_sink = nullptr;
		std::exception_ptr m_pending_exception;
		friend class Runtime;

		static Fyuu_Result LIB_CALL WriteThunk(
			Fyuu_Logger* logger,
			void* user_data,
			Fyuu_LogRecord const* record
		) noexcept;

	public:
		explicit Logger(LogSink& sink);
		~Logger() noexcept;

		Logger() = delete;
		Logger(Logger const&) = delete;
		Logger& operator=(Logger const&) = delete;
		Logger(Logger&&) = delete;
		Logger& operator=(Logger&&) = delete;

		void Write(LogRecord const& record);
		void Write(
			LogLevel level,
			std::string_view category,
			std::string_view message
		);
		bool Valid() const noexcept;
	};

	/// Receives the application side of the Runtime lifecycle.
	/// Call chain: Runtime -> C ABI runtime callback -> generated thunk -> Application.
	class Application {
	public:
		/// Destroys the application after every Runtime using it has been destroyed.
		virtual ~Application() noexcept;

		/// Advances application state after the platform event pump completes.
		virtual void Tick(Runtime& runtime);
		/// Decides whether an operating-system close request may stop Runtime.
		virtual bool CloseRequested(Runtime& runtime);
	};

	/// Owns the opaque C ABI platform object and defines the native event-pump contract.
	/// Call chain: Runtime::Tick -> C ABI -> generated thunk -> PumpEvents.
	class Platform {
	private:
		Fyuu_Platform* m_handle = nullptr;
		std::exception_ptr m_pending_exception;
		friend class Runtime;

		// Installed in Fyuu_PlatformDescriptor. Definition is generated in :api_glue.
		static Fyuu_Result LIB_CALL PumpEventsThunk(
			Fyuu_Platform* platform,
			void* user_data,
			bool* close_requested
		) noexcept;

	protected:
		/// Pumps one batch of native events and reports whether the OS requested closure.
		virtual void PumpEvents(bool& close_requested) = 0;

	public:
		/// Creates the ABI Platform; concrete adapters acquire native resources in their constructors.
		Platform();
		/// Destroys the ABI Platform after the concrete adapter has released native resources.
		virtual ~Platform() noexcept;

		Platform(Platform const&) = delete;
		Platform& operator=(Platform const&) = delete;
		Platform(Platform&&) = delete;
		Platform& operator=(Platform&&) = delete;

		/// Returns true when this wrapper owns a C ABI platform object.
		bool Valid() const noexcept;
	};

	/// Owns the opaque C ABI runtime object and borrows Platform, Logger, and Application.
	/// Every borrowed object must outlive this object.
	class Runtime {
	private:
		Fyuu_Runtime* m_handle = nullptr;
		Application* m_application = nullptr;
		Logger* m_logger = nullptr;
		Platform* m_platform = nullptr;
		std::exception_ptr m_pending_exception;

		// Installed in Fyuu_RuntimeDescriptor. Definitions are generated in :api_glue.
		static void LIB_CALL TickThunk(Fyuu_Runtime* runtime, void* user_data) noexcept;
		static bool LIB_CALL CloseRequestedThunk(Fyuu_Runtime* runtime, void* user_data) noexcept;

	public:
		/// Creates and initializes the ABI Runtime; throws Error on failure.
		Runtime(
			Platform& platform,
			Logger& logger,
			Application& application
		);
		/// Stops and destroys the ABI Runtime without deleting borrowed objects.
		~Runtime() noexcept;

		Runtime() = delete;
		Runtime(Runtime const&) = delete;
		Runtime& operator=(Runtime const&) = delete;
		Runtime(Runtime&&) = delete;
		Runtime& operator=(Runtime&&) = delete;

		/// Pumps platform events, handles close requests, and advances Application once; throws on failure.
		void Tick();
		/// Requests an orderly transition from Running to StopRequested.
		void RequestStop() noexcept;
		/// Returns the state maintained by the C ABI runtime core.
		RuntimeState GetState() const noexcept;
		/// Returns the Logger borrowed at construction for application and subsystem use.
		Logger& GetLogger() const noexcept;
		/// Calls Tick until Stopped and rethrows any operation failure.
		void Run();
		/// Returns true when this wrapper owns a C ABI runtime object.
		bool Valid() const noexcept;
	};

}
