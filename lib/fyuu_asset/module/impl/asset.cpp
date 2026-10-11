module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>

#include <string>

#include <string_view>
#endif // !defined(__cpp_lib_modules)

module fyuu_asset;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :asset;
import :asset_storage;
import :uuid;
import :value;

namespace fyuu_asset {
	Asset::Asset(Asset const& other) :
	    m_uuid(other.m_uuid), m_type(other.m_type), m_name(other.m_name), m_fields(other.m_fields),
	    m_owns_persistence(other.m_owns_persistence) {
	}

	Asset& Asset::operator=(Asset const& other) {
		if (this == &other) {
			return *this;
		}
		Asset replacement{other};
		return *this = std::move(replacement);
	}

	Asset::Asset(Asset&& other) noexcept :
	    m_uuid(other.m_uuid), m_type(std::move(other.m_type)), m_name(std::move(other.m_name)),
	    m_fields(std::move(other.m_fields)),
	    m_owns_persistence(std::exchange(other.m_owns_persistence, false)) {
	}

	Asset& Asset::operator=(Asset&& other) noexcept {
		if (this == &other) {
			return *this;
		}
		QueuePersistence();
		m_uuid = other.m_uuid;
		m_type = std::move(other.m_type);
		m_name = std::move(other.m_name);
		m_fields = std::move(other.m_fields);
		m_owns_persistence = std::exchange(other.m_owns_persistence, false);
		return *this;
	}

	Asset::~Asset() noexcept {
		QueuePersistence();
	}

	Asset::Asset(
	    UUID const& uuid,
	    std::string_view type,
	    std::string_view name,
	    Object const& fields
	) : m_uuid(uuid), m_type(type), m_name(name), m_fields(fields) {
		ValidateAssetIdentity(m_uuid, m_type, m_name);
	}

	Asset::Asset(UUID const& uuid, std::string_view type, std::string_view name, Object&& fields) :
	    m_uuid(uuid), m_type(type), m_name(name), m_fields(std::move(fields)) {
		ValidateAssetIdentity(m_uuid, m_type, m_name);
	}

	Asset::Asset(std::string_view type, std::string_view name, Object const& fields) :
	    Asset(GenerateAssetUUID(), type, name, fields) {
	}

	Asset::Asset(std::string_view type, std::string_view name, Object&& fields) :
	    m_uuid(GenerateAssetUUID()), m_type(type), m_name(name), m_fields(std::move(fields)) {
		ValidateAssetIdentity(m_uuid, m_type, m_name);
	}

	UUID Asset::GetUUID() const noexcept {
		return m_uuid;
	}

	std::string_view Asset::GetType() const noexcept {
		return m_type;
	}

	std::string_view Asset::GetName() const noexcept {
		return m_name;
	}
} // namespace fyuu_asset
