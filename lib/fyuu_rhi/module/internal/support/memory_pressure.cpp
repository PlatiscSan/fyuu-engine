module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>

#include <string>

#include <fstream>

#include <cstdint>

#include <chrono>

#include <mutex>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif // platform

module fyuu_rhi:memory_pressure;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace {
    /** @brief Converts measured memory headroom into an idle-object budget. */
    constexpr std::size_t RetentionLimit(std::size_t demand, double available) noexcept {
        if (available < 0.05) {
            return 0u;
        }
        if (available >= 0.25) {
            return demand;
        }
        return static_cast<std::size_t>(demand * available / 0.25);
    }

    static_assert(RetentionLimit(100u, 1.0) == 100u);
    static_assert(RetentionLimit(100u, 0.25) == 100u);
    static_assert(RetentionLimit(100u, 0.125) == 50u);
    static_assert(RetentionLimit(100u, 0.05) == 20u);
    static_assert(RetentionLimit(100u, 0.049) == 0u);
    static_assert(RetentionLimit(1u, 0.125) == 0u);
    static_assert(RetentionLimit(0u, 1.0) == 0u);
} // namespace

namespace fyuu_rhi::details {
    /** @brief Reads host-memory headroom, not GPU allocation usage.
     * @return Available fraction; failed queries conservatively preserve reuse.
     */
    double AvailableMemoryFraction() noexcept {
#if defined(_WIN32)
        MEMORYSTATUSEX status{.dwLength = sizeof(MEMORYSTATUSEX)};
        if (GlobalMemoryStatusEx(&status) && status.ullTotalPhys != 0u) {
            return static_cast<double>(status.ullAvailPhys) / status.ullTotalPhys;
        }
#elif defined(__linux__) || defined(__ANDROID__)
        try {
            std::ifstream input("/proc/meminfo");
            std::string key;
            std::string remainder;
            unsigned long long total = 0u;
            unsigned long long available = 0u;
            bool has_available = false;
            while (input >> key) {
                if (key == "MemTotal:") {
                    input >> total;
                } else if (key == "MemAvailable:") {
                    input >> available;
                    has_available = static_cast<bool>(input);
                }
                std::getline(input, remainder);
                if (total != 0u && has_available) {
                    return static_cast<double>(available) / total;
                }
            }
        } catch (...) {
        }
#elif defined(__APPLE__)
        std::uint64_t total = 0u;
        std::size_t size = sizeof(total);
        vm_statistics64_data_t statistics{};
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        auto host = mach_host_self();
        auto result = host_statistics64(
            host,
            HOST_VM_INFO64,
            reinterpret_cast<host_info64_t>(&statistics),
            &count
        );
        mach_port_deallocate(mach_task_self(), host);
        if (result == KERN_SUCCESS &&
            sysctlbyname("hw.memsize", &total, &size, nullptr, 0u) == 0 && total != 0u) {
            auto pages = static_cast<double>(statistics.free_count) + statistics.inactive_count;
            return pages * vm_page_size / total;
        }
#endif // platform
        return 1.0;
    }

    /** @brief Scales idle retention by recent demand and current host-memory pressure.
     * @note Below 25% available memory retention decreases proportionally; below
     * 5% it is disabled. These are pressure thresholds, not fixed object limits.
     * Native in-flight work is never counted, throttled or destroyed here.
     */
    std::size_t IdleRetentionLimit(std::size_t recent_demand) noexcept {
        static std::mutex mutex;
        static auto sampled = std::chrono::steady_clock::time_point{};
        static double available = 1.0;
        std::unique_lock lock(mutex);
        auto now = std::chrono::steady_clock::now();
        if (now - sampled >= std::chrono::milliseconds(100u)) {
            available = AvailableMemoryFraction();
            sampled = now;
        }
        return RetentionLimit(recent_demand, available);
    }
} // namespace fyuu_rhi::details
