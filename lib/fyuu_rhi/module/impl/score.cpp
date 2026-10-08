module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>

#include <optional>

#include <span>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :physical_device;

namespace {

	using Type = fyuu_rhi::PhysicalDevice::Info::Type;

	std::size_t PhysicalDeviceTypeScore(Type type) noexcept {
		switch (type) {
		case Type::DiscreteGPU:
			return 4u;
		case Type::IntegratedGPU:
			return 3u;
		case Type::CPU:
			return 2u;
		case Type::Virtual:
			return 1u;
		default:
			return 0u;
		}
	}
}

namespace fyuu_rhi {

	PhysicalDevice const& BestPerformance(std::span<PhysicalDevice const> physical_devices) {
		if (physical_devices.empty()) {
			throw std::invalid_argument("BestPerformance(): no available physical devices");
		}

		std::size_t best_index = 0u;
		auto best_info = physical_devices[best_index].GetInfo();
		std::size_t best_score = PhysicalDeviceTypeScore(best_info.type);
		std::size_t best_memory = best_info.dedicated_memory.value_or(0u);

		for (std::size_t i = 1u; i < physical_devices.size(); ++i) {
			auto info = physical_devices[i].GetInfo();
			std::size_t score = PhysicalDeviceTypeScore(info.type);
			std::size_t memory = info.dedicated_memory.value_or(0u);

			if (score > best_score || (score == best_score && memory > best_memory)) {
				best_index = i;
				best_score = score;
				best_memory = memory;
			}
		}

		return physical_devices[best_index];
	}

} // namespace fyuu_rhi
