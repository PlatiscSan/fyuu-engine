module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <utility>

#include <cstdint>
#endif // !defined(__cpp_lib_modules)
export module fyuu_rhi:view;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_rhi {
	namespace execution {
		template <class NativeCommandSchedulerContext>
		struct ExecuteCommands;
	}

	class View {
	public:
		using UniqueHandle = std::unique_ptr<
			struct ViewImplementation,
			void(*)(struct ViewImplementation*)
		>;

	private:
		template <class Native>
		friend struct CreatePipelineResourceGroup;
		template <class Native>
		friend struct execution::ExecuteCommands;

		UniqueHandle m_impl;

	public:
		View() noexcept
			: m_impl(nullptr, nullptr) {
		}

		explicit View(UniqueHandle&& impl) noexcept
			: m_impl(std::move(impl)) {
		}

		explicit operator bool() const noexcept {
			return static_cast<bool>(m_impl);
		}

	};

} // namespace fyuu_rhi
