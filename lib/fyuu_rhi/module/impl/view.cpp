module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :view_factory;
import :view;

namespace fyuu_rhi {

	View::View() noexcept = default;
	View::View(ImplementationStorage&& impl) noexcept : m_impl(std::move(impl)) {
	}
	View::View(View&& other) noexcept = default;
	View& View::operator=(View&& other) noexcept = default;
	View::~View() noexcept = default;

} // namespace fyuu_rhi
