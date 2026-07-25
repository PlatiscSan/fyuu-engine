module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <span>
#include <string>
#include <vector>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:vulkan_utility;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace fyuu_rhi::vulkan {

	std::vector<std::string> ToStrings(std::span<char const* const> strings) {
		std::vector<std::string> result;
		result.reserve(strings.size());
		for (auto string : strings) {
			result.emplace_back(string);
		}
		return result;
	}

}
