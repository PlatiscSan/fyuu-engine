module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <vector>

#include <span>
#endif // !defined(__cpp_lib_modules)
#if defined(__ANDROID__)
#include <android/native_window.h>
#endif // defined(__ANDROID__)
export module fyuu_rhi:instance;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :core;
import :physical_device;

export namespace fyuu_rhi {

	/**
	 * @brief Process-level entry point for one rendering backend.
	 *
	 * An Instance is a non-owning handle to backend state retained by FyuuRHI.
	 * Request it with RequestInstance(), keep it valid while enumerating adapters,
	 * and do not combine objects obtained from different backends.
	 */
	class Instance {
	private:
		/// @brief Identifies the internal static backend slot without borrowing a wrapper pointer.
		Backend m_backend;

	public:
		explicit Instance(Backend backend) noexcept
			: m_backend(backend) {
		}
		
		explicit operator bool() const noexcept {
			return m_backend != Backend::Unknown;
		}

		/**
		 * @brief Makes the backend's shared OpenGL context current on this thread.
		 *
		 * This is required before issuing OpenGL work from an additional thread.
		 * Other backends implement it as a no-op.
		 */
		void ShareContextOnThisThread();

		/// Enumerates physical adapters exposed by this backend.
		std::vector<PhysicalDevice> EnumeratePhysicalDevices() const;
	};

	/// Returns the backends compiled into the current FyuuRHI build.
	std::span<Backend const> EnumerateBackends() noexcept;

	/**
	 * @brief Requests the singleton Instance for @p backend.
	 * @param backend A value returned by EnumerateBackends().
	 * @return A process-lifetime non-owning handle for the requested backend.
	 */
	Instance& RequestInstance(Backend backend);

} // namespace fyuu_rhi
