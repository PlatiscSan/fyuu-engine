module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
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

	using UUID = boost::uuids::uuid;

	struct UUIDHash {
		[[nodiscard]] std::size_t operator()(UUID const& value) const noexcept {
			return boost::uuids::hash_value(value);
		}
	};

	struct UUIDEquality {
		[[nodiscard]] bool operator()(UUID const& left, UUID const& right) const noexcept {
			return left == right;
		}
	};

	[[nodiscard]] bool UUIDEqual(UUID const& left, UUID const& right) noexcept {
		return left == right;
	}

	[[nodiscard]] bool UUIDIsNil(UUID const& value) noexcept {
		return value.is_nil();
	}

	[[nodiscard]] UUID GenerateUUID() {
		return boost::uuids::random_generator{}();
	}

	[[nodiscard]] UUID ParseUUID(std::string_view const& value) {
		return boost::uuids::string_generator{}(value.begin(), value.end());
	}

	[[nodiscard]] std::string UUIDToString(UUID const& value) {
		return boost::uuids::to_string(value);
	}

	void UUIDFromBytes(std::uint8_t const (&source)[16], UUID& output) noexcept {
		for (std::size_t index = 0u; index < 16u; ++index) {
			output.data[index] = source[index];
		}
	}

	void UUIDToBytes(UUID const& source, std::uint8_t (&output)[16]) noexcept {
		for (std::size_t index = 0u; index < 16u; ++index) {
			output[index] = source.data[index];
		}
	}

}
