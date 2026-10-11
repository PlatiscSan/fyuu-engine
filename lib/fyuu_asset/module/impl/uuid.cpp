module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>

#include <string>

#include <bit>

#include <string_view>
#endif // !defined(__cpp_lib_modules)
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

module fyuu_asset;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :uuid;

/// @brief Both identity representations occupy the same 16 bytes without padding.
static_assert(sizeof(boost::uuids::uuid) == sizeof(fyuu_asset::UUID));

namespace fyuu_asset {

	UUID ParseUUID(std::string_view text) {
		if (text.empty()) {
			throw std::invalid_argument{"Asset UUID text must not be empty"};
		}
		if (text.contains('\0')) {
			throw std::invalid_argument{"Asset UUID text must not contain null characters"};
		}

		boost::uuids::uuid parsed{};
		try {
			parsed = boost::uuids::string_generator{}(text.begin(), text.end());
		} catch (std::exception const&) {
			throw std::invalid_argument{"Asset UUID text is not a well-formed UUID"};
		}
		return std::bit_cast<UUID>(parsed);
	}

	std::string FormatUUID(UUID const& uuid) {
		return boost::uuids::to_string(std::bit_cast<boost::uuids::uuid>(uuid));
	}

} // namespace fyuu_asset
