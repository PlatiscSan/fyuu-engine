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
		 * The Sink passed to InitializeRHIContext() must remain alive until
		 * RebindRHISink() or a later InitializeRHIContext() returns after replacing it.
		 * Rebinding does not stop RHI work.
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
	 * @brief Updates context metadata, cache configuration, platform state, and the log Sink.
	 *
	 * Call before EnumerateBackends() or RequestInstance(). Repeated calls are allowed.
	 * Each successful call copies names and versions, recomputes the cache root, and
	 * refreshes platform state. The Vulkan Instance is retired and created lazily
	 * on its next request; other native backend instances are retained.
	 * Slang compilation sessions are local to each compile miss. Initialization retires
	 * their global owner after active compilation completes; reconstruction stays lazy.
	 * Every call replaces the Sink after its admitted Write() calls return; nullptr
	 * disables logging. The new Sink is borrowed until another initialization or rebind.
	 * This function does not acquire a reference-counted Runtime lifetime.
	 * @note The host must serialize initialization with other RHI operations, except
	 * diagnostic emission, and release
	 * borrowed metadata string views before calling; those views may be invalidated.
	 * All Vulkan-derived objects must be destroyed before reinitialization.
	 * @warning Do not call from Write() or while holding a lock needed by Write().
	 */
	void InitializeRHIContext(
		std::string_view app_name, Version const& app_ver, 
		std::string_view engine_name, Version const& engine_ver,
#if defined(__ANDROID__)
		android_app* android_app,
#endif // defined(__ANDROID__)
		log::Sink* sink
	);

	/**
	 * @brief Replaces the diagnostic Sink after all admitted old Write() calls return.
	 * @param sink Borrowed new destination; nullptr disables logging.
	 * @note Call after InitializeRHIContext() has returned. The replaced Sink can be
	 * destroyed after return; keep the new Sink alive until a later rebind replaces it.
	 * Concurrent rebinds are serialized; the final destination follows their admission order.
	 * The host must coordinate destruction with other rebinders that may reuse the same Sink.
	 * @throws std::logic_error if initialization has not completed.
	 * @warning Do not call from Write(), or while holding a lock needed by Write().
	 */
	void RebindRHISink(log::Sink* sink);

	std::string_view ApplicationName() noexcept;

	Version ApplicationVersion() noexcept;

	std::string_view EngineName() noexcept;

	Version EngineVersion() noexcept;

} // namespace fyuu_rhi
