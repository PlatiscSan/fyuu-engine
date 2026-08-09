module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <compare>
#endif // !defined(__cpp_lib_modules)
#include <boost/uuid.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

export module fyuu_asset:uuid;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_asset {

	class UUID {
	private:
		boost::uuids::uuid m_impl{};
		friend struct std::hash<UUID>;

	public:
		UUID() noexcept = default;

		[[nodiscard]] static UUID Generate() {
			UUID result;
			result.m_impl = boost::uuids::random_generator{}();
			return result;
		}

		[[nodiscard]] static UUID Parse(std::string_view value) {
			UUID result;
			result.m_impl = boost::uuids::string_generator{}(value.begin(), value.end());
			return result;
		}

		[[nodiscard]] static UUID FromBytes(std::uint8_t const (&source)[16]) noexcept {
			UUID result;
			std::memcpy(result.m_impl.data, source, 16u);
			return result;
		}

		[[nodiscard]] static UUID FromBytes(std::byte const (&source)[16]) noexcept {
			UUID result;
			std::memcpy(result.m_impl.data, source, 16u);
			return result;
		}

		[[nodiscard]] bool IsNil() const noexcept {
			return m_impl.is_nil();
		}

		[[nodiscard]] std::string ToString() const {
			return boost::uuids::to_string(m_impl);
		}

		void ToBytes(std::uint8_t (&output)[16]) const noexcept {
			std::memcpy(output, m_impl.data, 16u);
		}

		void ToBytes(std::byte (&output)[16]) const noexcept {
			std::memcpy(output, m_impl.data, 16u);
		}

		[[nodiscard]] std::strong_ordering operator<=>(UUID const& other) const noexcept {
			return std::memcmp(m_impl.data, other.m_impl.data, 16u) <=> 0;
		}
	};

}

export template <> struct std::hash<fyuu_asset::UUID> {
	[[nodiscard]] std::size_t operator()(fyuu_asset::UUID const& value) const noexcept {
		return boost::uuids::hash_value(value.m_impl);
	}
};

export template <> struct std::equal_to<fyuu_asset::UUID> {
	[[nodiscard]] bool operator()(fyuu_asset::UUID const& left, fyuu_asset::UUID const& right) const noexcept {
		return std::is_eq(left <=> right);
	}
};
