module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#include <atomic>

#include <string_view>

#include <source_location>
#include <span>
#endif // !defined(__cpp_lib_modules)
#if defined(__ANDROID__)
#include <android_native_app_glue.h>
#endif // defined(__ANDROID__)
export module fyuu_rhi:core;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_rhi {

	namespace log {

		/// Severity of a log record. Mirrors fyuu_engine::LogLevel.
		enum class Level {
			Debug,
			Info,
			Warning,
			Error,
			Fatal
		};
	
		/**
		 * @brief Application-owned synchronous diagnostic destination.
		 *
		 * The Sink passed to InitializeRHIContext() must outlive every RHI call.
		 * Write() may be invoked from command recording and completion threads.
		 */
		class Sink {
		public:
			virtual ~Sink() noexcept = default;
			virtual void Write(
				Level level, 
				std::string_view msg, 
				std::source_location const& loc = std::source_location::current()
			) noexcept = 0;
		};

	}

	/// Rendering API implemented by an Instance.
	enum class Backend : std::uint8_t {
		Unknown,
		Vulkan,
		OpenGL,
		DirectX12,
		Metal,
		WebGPU,
	};

	/// Four-component application or engine version embedded in backend metadata.
	struct Version {
		std::uint8_t variant;
		std::uint8_t major;
		std::uint8_t minor;
		std::uint8_t patch;
	};

	/**
	 * @brief Initializes process-wide names, versions, platform state, and logging.
	 *
	 * Call once before EnumerateBackends() or RequestInstance(). String data is
	 * copied; @p sink is borrowed and must remain alive until RHI use has ended.
	 */
	void InitializeRHIContext(
		std::string_view app_name, Version const& app_ver, 
		std::string_view engine_name, Version const& engine_ver,
#if defined(__ANDROID__)
		android_app* android_app,
#endif // defined(__ANDROID__)
		log::Sink* sink
	);

	std::string_view ApplicationName() noexcept;

	Version ApplicationVersion() noexcept;

	std::string_view EngineName() noexcept;

	Version EngineVersion() noexcept;

} // namespace fyuu_rhi
