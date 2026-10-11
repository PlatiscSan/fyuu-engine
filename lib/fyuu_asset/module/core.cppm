module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string_view>

#include <filesystem>

#include <source_location>
#endif // !defined(__cpp_lib_modules)
export module fyuu_asset:core;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_asset {
	namespace log {

		/// @brief Severity assigned to an asset-system diagnostic record.
		enum class Level { Debug, Info, Warning, Error, Fatal };

		/**
		 * @brief Application-owned synchronous destination for asset diagnostics.
		 *
		 * The sink passed to InitializeAssetContext() is borrowed and must outlive
		 * every asset-system call. Write() may be called concurrently from any
		 * thread used by the caller or by later asset operations.
		 */
		class Sink {
		public:
			virtual ~Sink() noexcept = default;

			/**
			 * @brief Receives one diagnostic record.
			 * @param level Severity of the record.
			 * @param message Diagnostic text valid for the duration of this call.
			 * @param location Source location that emitted the record.
			 */
			virtual void Write(
			    Level level,
			    std::string_view message,
			    std::source_location const& location = std::source_location::current()
			) noexcept = 0;
		};

	} // namespace log

	/**
	 * @brief Initializes the process-wide asset context.
	 *
	 * The asset root is copied, converted to an absolute path and normalized.
	 * The function may be called repeatedly, but only the first call establishes
	 * the context. The borrowed log sink must remain alive until all asset-system
	 * use has ended.
	 *
	 * @param asset_root Root directory containing all persisted assets.
	 * @param sink Optional synchronous diagnostic destination.
	 * @throws std::invalid_argument if @p asset_root is empty.
	 * @throws std::filesystem::filesystem_error if the absolute path cannot be formed.
	 */
	void InitializeAssetContext(std::filesystem::path const& asset_root, log::Sink* sink = nullptr);

} // namespace fyuu_asset
