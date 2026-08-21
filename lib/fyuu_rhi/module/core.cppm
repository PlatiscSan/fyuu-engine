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
	
		/// Receives records synchronously.
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

	enum class Backend : std::uint8_t {
		Unknown,
		Vulkan,
		OpenGL,
		DirectX12,
		Metal,
		WebGPU,
	};

	struct Version {
		std::uint8_t variant;
		std::uint8_t major;
		std::uint8_t minor;
		std::uint8_t patch;
	};

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
