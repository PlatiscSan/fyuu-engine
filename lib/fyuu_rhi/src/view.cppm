module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <concepts>
#endif // !defined(__cpp_lib_modules)

export module fyuu_rhi:view;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
namespace fyuu_rhi {

	export template <class Backend> class View {
	public:
		using Implementation = typename Backend::View;
	private:
		Implementation m_impl;
	public:
		template <class ViewType>
		class PassKey {
			static_assert(
				std::same_as<ViewType, View> || std::same_as<ViewType, View const>,
				"ViewType must be View or const View"
			);

			template <class U> friend class LogicalDevice;

			ViewType* m_view;

			template <class Self>
			decltype(auto) GetImplementation(this Self&& self) noexcept {
				return self.m_view->m_impl;
			}

		public:
			explicit PassKey(ViewType* view) noexcept
				: m_view(view) {

			}
		};

		template <std::convertible_to<Implementation> I>
		View(I&& impl)
			: m_impl(std::forward<I>(impl)) {

		}

		View(View const&) = delete;
		View& operator=(View const&) = delete;
		View(View&&) noexcept = default;
		View& operator=(View&&) noexcept = default;
		~View() noexcept = default;

		template <class Self>
		auto GetPassKey(this Self&& self) noexcept {
			using ViewType = std::remove_reference_t<Self>;
			return PassKey<ViewType>{ &self };
		}
	};

}
