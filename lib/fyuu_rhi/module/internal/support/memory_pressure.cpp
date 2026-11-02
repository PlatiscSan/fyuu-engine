module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>

#include <string>

#include <limits>

#include <fstream>

#include <cstdint>

#include <chrono>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <string_view>

#include <format>

#include <stop_token>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <unistd.h>
#elif defined(__linux__) || defined(__ANDROID__)
#include <unistd.h>
#endif // platform

module fyuu_rhi:memory_pressure;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :log;

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
    /** @brief Admits at most one diagnostic every 30 seconds without locks, allocation or retry loops. */
    struct MemoryDiagnosticLimiter {
#if !defined(NDEBUG)
        std::atomic_llong next_sample = 0;
#endif // !defined(NDEBUG)

        bool TryAcquire() noexcept {
#if !defined(NDEBUG)
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
            auto expected = next_sample.load(std::memory_order_relaxed);
            return now >= expected && next_sample.compare_exchange_strong(
                expected,
                now + 30000,
                std::memory_order_relaxed
            );
#else
            return false;
#endif // !defined(NDEBUG)
        }
    };

#if !defined(NDEBUG)
    /** @brief Queries process counters only on the diagnostic thread, never on rendering paths. */
    bool SampleProcessMemory(
        std::uint64_t& bytes,
        std::uint64_t* working_set = nullptr,
        std::uint64_t* peak_working_set = nullptr
    ) noexcept {
        try {
#if defined(_WIN32)
            PROCESS_MEMORY_COUNTERS_EX counters{};
            if (GetProcessMemoryInfo(
                GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                sizeof(counters)
            )) {
                bytes = counters.PrivateUsage;
                if (working_set) {
                    *working_set = counters.WorkingSetSize;
                }
                if (peak_working_set) {
                    *peak_working_set = counters.PeakWorkingSetSize;
                }
                return true;
            }
#elif defined(__linux__) || defined(__ANDROID__)
            std::ifstream input("/proc/self/smaps_rollup");
            /** @brief Older kernels may provide smaps but not its aggregate rollup. */
            if (!input.is_open()) {
                input.clear();
                input.open("/proc/self/smaps");
            }
            std::string key;
            std::string remainder;
            std::uint64_t total = 0u;
            std::uint64_t resident = 0u;
            bool found = false;
            bool resident_found = false;
            while (input >> key) {
                if (key == "Private_Clean:" || key == "Private_Dirty:" || key == "Private_Hugetlb:") {
                    std::uint64_t value = 0u;
                    if (!(input >> value)) {
                        return false;
                    }
                    total += value * 1024u;
                    found = true;
                } else if (key == "Rss:") {
                    std::uint64_t value = 0u;
                    if (!(input >> value)) {
                        return false;
                    }
                    resident += value * 1024u;
                    resident_found = true;
                }
                std::getline(input, remainder);
            }
            bytes = total;
            if (working_set) {
                *working_set = resident;
            }
            std::ifstream status("/proc/self/status");
            bool peak_found = false;
            while (status >> key) {
                if (key == "VmHWM:") {
                    std::uint64_t value = 0u;
                    if (!(status >> value)) {
                        return false;
                    }
                    if (peak_working_set) {
                        *peak_working_set = value * 1024u;
                    }
                    peak_found = true;
                    break;
                }
                std::getline(status, remainder);
            }
            return found && resident_found && peak_found;
#elif defined(__APPLE__)
            task_vm_info_data_t information{};
            mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
            if (task_info(
                mach_task_self(),
                TASK_VM_INFO,
                reinterpret_cast<task_info_t>(&information),
                &count
            ) == KERN_SUCCESS && count >= TASK_VM_INFO_REV1_COUNT) {
                bytes = information.phys_footprint;
                if (working_set) {
                    *working_set = information.resident_size;
                }
                if (peak_working_set) {
                    *peak_working_set = information.resident_size_peak;
                }
                return true;
            }
#endif // platform
        } catch (...) {
        }
        return false;
    }

    /** @brief Emits exactly one Debug record per process-memory sample; failures do not escape. */
    void LogProcessMemorySummary() noexcept {
        try {
            std::uint64_t bytes = 0u;
            std::uint64_t resident = 0u;
            std::uint64_t peak_resident = 0u;
            bool valid = SampleProcessMemory(bytes, &resident, &peak_resident);
#if defined(_WIN32)
            auto pid = GetCurrentProcessId();
            constexpr auto primary = "private";
#elif defined(__linux__) || defined(__ANDROID__)
            auto pid = getpid();
            constexpr auto primary = "private_resident";
#elif defined(__APPLE__)
            auto pid = getpid();
            constexpr auto primary = "physical_footprint";
#else
            constexpr auto pid = 0u;
            constexpr auto primary = "unavailable";
#endif // platform
            log::Debug(
                std::format(
                    "RHI process memory summary: pid={} utc_ms={} available={} {}_bytes={} {}_mib={:.3f} resident_bytes={} resident_mib={:.3f} peak_resident_bytes={}",
                    pid,
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()
                    ).count(),
                    valid,
                    primary,
                    bytes,
                    primary,
                    static_cast<double>(bytes) / (1024.0 * 1024.0),
                    resident,
                    static_cast<double>(resident) / (1024.0 * 1024.0),
                    peak_resident
                )
            );
        } catch (...) {
            /// @note A failed memory query or sink must not terminate the diagnostic worker.
        }
    }
#endif // !defined(NDEBUG)

    /**
     * @brief Starts one process-wide Debug memory-summary worker after the RHI sink is installed.
     * @note The worker waits 30 seconds before each sample, even without GPU work.
     * Stop-aware waiting permits prompt shutdown; it owns no backend objects.
     * Release builds do not create a thread or query process memory.
     */
    void StartProcessMemorySummaries() noexcept {
#if !defined(NDEBUG) && !defined(__EMSCRIPTEN__)
        try {
            static std::jthread worker([](std::stop_token stop) {
                std::mutex mutex;
                std::condition_variable_any condition;
                std::unique_lock lock(mutex);
                while (!stop.stop_requested()) {
                    condition.wait_for(
                        lock,
                        stop,
                        std::chrono::seconds(30),
                        []() {
                            return false;
                        }
                    );
                    if (stop.stop_requested()) {
                        break;
                    }
                    lock.unlock();
                    LogProcessMemorySummary();
                    lock.lock();
                }
            }
            );
            (void)worker;
        } catch (...) {
            log::Warning("Cannot start the RHI process-memory summary worker");
        }
#endif // !defined(NDEBUG) && !defined(__EMSCRIPTEN__)
    }

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

    /** @brief Retains one recent working set plus pressure-scaled spare objects.
     * @note Prevents small recurring workloads from rebuilding their native pools
     * every frame. Only completed objects are eligible for this bounded budget.
     */
    std::size_t WorkingSetRetentionLimit(std::size_t recent_demand) noexcept {
        auto spare = IdleRetentionLimit(recent_demand);
        auto maximum = (std::numeric_limits<std::size_t>::max)();
        return spare > maximum - recent_demand ? maximum : recent_demand + spare;
    }
} // namespace fyuu_rhi::details
