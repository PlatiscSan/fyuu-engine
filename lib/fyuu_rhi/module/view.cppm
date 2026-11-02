module;
#include <version>
#if !defined(__cpp_lib_modules)

#include <cstdint>
#endif // !defined(__cpp_lib_modules)
export module fyuu_rhi:view;
import :fast_pimpl;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_rhi {
	namespace execution {
		template <class NativeCommandSchedulerContext>
		struct ExecuteCommands;
	}

	/**
	 * @brief Move-only interpretation of a range within a Resource.
	 *
	 * A View does not replace Resource ownership. Bind both the Resource and View
	 * to a graph when a command needs state transitions as well as a native view.
	 */
	class View {
	public:
		using ImplementationStorage = details::FastPimpl<struct ViewImplementation, 224u>;

	private:
		template <class Native>
		friend struct CreatePipelineResourceGroup;
		template <class Native>
		friend struct execution::ExecuteCommands;

		ImplementationStorage m_impl;

	public:
		View() noexcept;

		explicit View(ImplementationStorage&& impl) noexcept;

		View(View const&) = delete;
		View& operator=(View const&) = delete;
		View(View&& other) noexcept;
		View& operator=(View&& other) noexcept;
		~View() noexcept;

		explicit operator bool() const noexcept {
			return static_cast<bool>(m_impl);
		}

	};

} // namespace fyuu_rhi
