module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <memory>
#include <concepts>
#if defined(__cpp_lib_reflection)
#include <meta>
#endif // defined(__cpp_lib_reflection)
#endif // !defined(__cpp_lib_modules)
#include <boost/uuid.hpp>
#include <boost/intrusive_ptr.hpp>
export module fyuu_asset:managed_asset;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :base_asset;
import :registry;

namespace fyuu_asset {

	export template <std::derived_from<BaseAsset> T> class ManagedAsset {
	private:
		std::shared_ptr<T> m_impl;

	public:
		ManagedAsset(std::shared_ptr<T> const& impl) noexcept
			: m_impl(impl) {
		}

		template <class... Args>
		static ManagedAsset Create(Args&&... args) {
			auto impl = std::make_shared<T>(std::forward<Args>(args)...);
			if (!registry::RegisterAsset(impl)) {
				// Handle registration failure (e.g., throw an exception or return an error)
				throw std::runtime_error("Failed to register asset, the asset may already exist in the registry.");
			}
			return ManagedAsset(impl);
		}
		
	};

}