module;
#include <version>
#if !defined(__cpp_lib_modules)
#if defined(__cpp_lib_reflection)
#include <meta>
#endif // defined(__cpp_lib_reflection)
#endif // !defined(__cpp_lib_modules)
#include <boost/uuid.hpp>
export module fyuu_asset:base_asset;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace fyuu_asset {

	export enum class AssetType : std::uint8_t {
		Unknown
	};

	export enum class AssetState : std::uint8_t {
		Unloaded,
		Loading,
		Loaded,
		Modified,
		Unloading
	};

	export class BaseAsset {
	private:
		boost::uuids::uuid m_id;
		AssetType m_type;
		AssetState m_state;

	public:
		BaseAsset(AssetType type) noexcept
			: m_id(boost::uuids::random_generator()()),
			m_type(type),
			m_state(AssetState::Unloaded) {
		}

		boost::uuids::uuid GetID() const noexcept {
			return m_id;
		}

		AssetType GetType() const noexcept {
			return m_type;
		}

		AssetState GetState() const noexcept {
			return m_state;
		}
	};

}

