// Completion-path stress tests.
//
// The HelloTriangle test only ever keeps one graph in flight and always waits for it, so it
// cannot observe signal loss or completion latency. These cases drive the completion path
// directly and report measurements, so a regression shows up as a number rather than as a hang.
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

#include <vector>

#include <algorithm>

#include <string>

#include <limits>

#include <iostream>

#include <cstdint>

#include <array>

#include <chrono>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <optional>
#include <string_view>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#include <tlhelp32.h>
#endif // defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_rhi;

namespace {

	using namespace std::chrono_literals;
	using Resources = fyuu_rhi::execution::CommandGraphResources;

	/// Records exactly how one graph reached its terminal state.
	struct Counts {
		std::atomic<int> value{ 0 };
		std::atomic<int> error{ 0 };
		std::atomic<int> stopped{ 0 };
		std::atomic<long long> latency_nanoseconds{ -1 };
		/// Simulates a receiver doing slow application work on the completion thread.
		std::chrono::milliseconds receiver_work{ 0 };
		std::chrono::steady_clock::time_point started{};

		std::mutex mutex;
		std::optional<Resources> resources;
		std::vector<std::exception_ptr> errors;

		int Signals() const noexcept {
			return value.load(std::memory_order_relaxed) +
				error.load(std::memory_order_relaxed) +
				stopped.load(std::memory_order_relaxed);
		}
	};

	struct Receiver {
		std::shared_ptr<Counts> state;

		struct Environment {
		};

		Environment get_env() const noexcept {
			return {};
		}

		void RecoverBindings(Resources&& resources) noexcept {
			// Bindings must never be dropped, including on the error and stopped paths.
			std::lock_guard lock(state->mutex);
			state->resources.emplace(std::move(resources));
		}

		void set_value(Resources&& resources) && noexcept {
			if (state->receiver_work > 0ms) {
				std::this_thread::sleep_for(state->receiver_work);
			}
			state->latency_nanoseconds.store(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - state->started
				).count(),
				std::memory_order_relaxed
			);
			{
				std::lock_guard lock(state->mutex);
				state->resources.emplace(std::move(resources));
			}
			state->value.fetch_add(1, std::memory_order_relaxed);
		}

		void set_error(std::exception_ptr error) && noexcept {
			{
				std::lock_guard lock(state->mutex);
				state->errors.emplace_back(error);
			}
			state->error.fetch_add(1, std::memory_order_relaxed);
		}

		void set_stopped() && noexcept {
			state->stopped.fetch_add(1, std::memory_order_relaxed);
		}
	};

	/// Submits one minimal graph: a host-visible upload with no GPU-side dependencies.
	void Submit(
		fyuu_rhi::LogicalDevice& device,
		fyuu_rhi::execution::CommandScheduler const& scheduler,
		std::shared_ptr<Counts> const& counts
	) {
		using namespace fyuu_rhi;
		using namespace fyuu_rhi::execution;

		ResourceFlags flags;
		flags.Set(ResourceFlagBits::HostVisible);
		flags.Set(ResourceFlagBits::CopySRC);
		auto buffer = device.CreateBuffer(256u, flags);

		auto builder = scheduler.schedule();
		auto const binding = builder.RegisterResource();
		std::array const payload{ std::byte{ 0x11 }, std::byte{ 0x22 } };
		auto upload = builder.CreateNode(QueueType::Transfer);
		upload.Record(
			WriteBuffer{
				.resource = binding,
				.offset = 0u,
				.data = std::vector<std::byte>(payload.begin(), payload.end())
			}
		);
		counts->started = std::chrono::steady_clock::now();
		auto operation = std::move(builder).connect(Receiver{ counts });
		operation.BindResource(binding, std::move(buffer));
		operation.start();
	}

	/// Waits until every tracked graph reported a signal, or the budget expires.
	bool WaitForSignals(
		std::vector<std::shared_ptr<Counts>> const& tracked,
		std::chrono::milliseconds budget
	) {
		auto const deadline = std::chrono::steady_clock::now() + budget;
		while (std::chrono::steady_clock::now() < deadline) {
			std::size_t signaled = 0u;
			for (auto const& counts : tracked) {
				if (counts->Signals() != 0) {
					++signaled;
				}
			}
			if (signaled == tracked.size()) {
				return true;
			}
			std::this_thread::sleep_for(1ms);
		}
		return false;
	}

	void RequireExactlyOneSignal(std::vector<std::shared_ptr<Counts>> const& tracked, char const* label) {
		for (std::size_t index = 0u; index < tracked.size(); ++index) {
			auto const signals = tracked[index]->Signals();
			if (signals != 1) {
				throw std::runtime_error(
					std::string{ label } + ": graph " + std::to_string(index) +
					" reported " + std::to_string(signals) + " terminal signals instead of 1"
				);
			}
		}
	}

	// ---------------------------------------------------------------------------------
	// Case 1: every graph reports exactly one terminal signal, none is lost or duplicated.
	// ---------------------------------------------------------------------------------
	void CaseExactlyOnce(fyuu_rhi::LogicalDevice& device, fyuu_rhi::execution::CommandScheduler const& scheduler) {
		std::cout << "  exactly-once: submitting 32 concurrent graphs" << std::endl;
		std::vector<std::shared_ptr<Counts>> tracked;
		tracked.reserve(32u);
		for (std::size_t index = 0u; index < 32u; ++index) {
			auto counts = std::make_shared<Counts>();
			tracked.emplace_back(counts);
			Submit(device, scheduler, counts);
		}
		if (!WaitForSignals(tracked, 60s)) {
			throw std::runtime_error("exactly-once: timed out waiting for every graph to complete");
		}
		RequireExactlyOneSignal(tracked, "exactly-once");
		std::cout << "  exactly-once: 32/32 graphs reported exactly one signal" << std::endl;
	}

	// ---------------------------------------------------------------------------------
	// Case 2: completion latency is not quantised to a fixed polling tick.
	//
	// There is no "one slow receiver must not delay unrelated graphs" case here by design.
	// Delivery runs on a single driver thread, so a receiver that blocks does serialise the
	// completions behind it. That property was bought with a worker per in-flight graph and is
	// not worth the thread count; the timings below are what the driver has to keep instead.
	// ---------------------------------------------------------------------------------
	void CaseLatency(fyuu_rhi::LogicalDevice& device, fyuu_rhi::execution::CommandScheduler const& scheduler) {
		constexpr std::size_t Iterations = 200u;
		std::vector<double> samples;
		samples.reserve(Iterations);
		for (std::size_t index = 0u; index < Iterations; ++index) {
			auto counts = std::make_shared<Counts>();
			Submit(device, scheduler, counts);
			if (!WaitForSignals({ counts }, 10s)) {
				throw std::runtime_error(
					"latency: iteration " + std::to_string(index) + " did not complete"
				);
			}
			auto const nanoseconds = counts->latency_nanoseconds.load(std::memory_order_relaxed);
			if (nanoseconds >= 0) {
				samples.push_back(static_cast<double>(nanoseconds) / 1'000'000.0);
			}
		}
		if (samples.empty()) {
			throw std::runtime_error("latency: no samples were recorded");
		}
		std::ranges::sort(samples);
		auto const at = [&samples](double quantile) {
			auto const index = static_cast<std::size_t>(
				quantile * static_cast<double>(samples.size() - 1u)
			);
			return samples[index];
		};
		std::cout << "  latency: n=" << samples.size()
			<< " p50=" << at(0.50) << "ms"
			<< " p90=" << at(0.90) << "ms"
			<< " p99=" << at(0.99) << "ms"
			<< " max=" << samples.back() << "ms" << std::endl;
	}

#if defined(_WIN32)
	std::uint32_t ProcessThreadCount() {
		auto const snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0u);
		if (snapshot == INVALID_HANDLE_VALUE) {
			return 0u;
		}
		THREADENTRY32 entry{};
		entry.dwSize = sizeof(entry);
		auto const self = GetCurrentProcessId();
		std::uint32_t count = 0u;
		if (Thread32First(snapshot, &entry)) {
			do {
				if (entry.th32OwnerProcessID == self) {
					++count;
				}
			} while (Thread32Next(snapshot, &entry));
		}
		CloseHandle(snapshot);
		return count;
	}
#endif // defined(_WIN32)

	// ---------------------------------------------------------------------------------
	// Case 3: the driver thread count does not grow with submission count.
	//
	// There is deliberately no "idle CPU" case: a 1ms polling worker was measured at 0ms of CPU
	// over a 2s window, since it sleeps between wake-ups, so CPU time cannot tell the two designs
	// apart. The previous design's observable defects were completion-latency quantisation and
	// signal loss, and both have a case here with a number attached.
	// ---------------------------------------------------------------------------------
	void CaseThreadHygiene(fyuu_rhi::LogicalDevice& device, fyuu_rhi::execution::CommandScheduler const& scheduler) {
#if defined(_WIN32)
		auto const baseline = ProcessThreadCount();
		for (std::size_t index = 0u; index < 50u; ++index) {
			auto counts = std::make_shared<Counts>();
			Submit(device, scheduler, counts);
			if (!WaitForSignals({ counts }, 10s)) {
				throw std::runtime_error("thread-hygiene: a graph did not complete");
			}
		}
		auto const after_work = ProcessThreadCount();
		std::cout << "  threads: baseline=" << baseline << " after-50-graphs=" << after_work << std::endl;
		// Exactly one thread is added, ever: the driver's.
		if (after_work > baseline + 1u) {
			throw std::runtime_error("thread-hygiene: completion threads accumulated across submissions");
		}
#else
		std::cout << "  threads: skipped (Windows-only measurement)" << std::endl;
#endif // defined(_WIN32)
	}

	// ---------------------------------------------------------------------------------
	// Case 4: shutting down delivers a terminal signal for work still in flight.
	//
	// Runs last: after ShutdownCompletionPool() the driver delivers completions inline on the
	// submitting thread, which would make the timing cases above meaningless.
	// ---------------------------------------------------------------------------------
	void CaseShutdownDrain(fyuu_rhi::LogicalDevice& device, fyuu_rhi::execution::CommandScheduler const& scheduler) {
		constexpr std::size_t Pending = 16u;
		std::vector<std::shared_ptr<Counts>> tracked;
		tracked.reserve(Pending);
		for (std::size_t index = 0u; index < Pending; ++index) {
			auto counts = std::make_shared<Counts>();
			tracked.emplace_back(counts);
			Submit(device, scheduler, counts);
		}
		std::cout << "  shutdown: " << Pending << " graphs submitted, shutting the pool down" << std::endl;
		// Draining must reach every queued operation, whether it had already finished or not.
		fyuu_rhi::execution::ShutdownCompletionPool();
		std::cout << "  shutdown: pool drained" << std::endl;
		if (!WaitForSignals(tracked, 30s)) {
			throw std::runtime_error("shutdown: some graphs never reported a terminal signal");
		}
		RequireExactlyOneSignal(tracked, "shutdown");
		std::size_t stopped = 0u;
		for (auto const& counts : tracked) {
			stopped += static_cast<std::size_t>(counts->stopped.load(std::memory_order_relaxed));
		}
		std::cout << "  shutdown: " << tracked.size() << "/" << Pending
			<< " graphs signalled, " << stopped << " of them stopped" << std::endl;
	}

	int Run(char const* backend_name, std::string_view only) {
		using namespace fyuu_rhi;
		auto const name = std::string_view{ backend_name };
		auto const backend = name == "d3d12" ? Backend::DirectX12 :
			name == "vulkan" ? Backend::Vulkan :
			name == "webgpu" ? Backend::WebGPU :
			name == "metal" ? Backend::Metal : Backend::OpenGL;

		InitializeRHIContext("Completion pool tests", {}, "FyuuEngine", {}, nullptr);
		auto& instance = RequestInstance(backend);
		auto devices = instance.EnumeratePhysicalDevices();
		auto device = BestPerformance(devices).CreateLogicalDevice();
		auto scheduler = device.CreateScheduler();

		auto const wants = [only](std::string_view label) {
			return only.empty() || only == label;
		};

		std::cout << name << " completion tests:" << std::endl;
		if (wants("exactly_once")) {
			CaseExactlyOnce(device, scheduler);
		}
		if (wants("latency")) {
			CaseLatency(device, scheduler);
		}
		if (wants("threads")) {
			CaseThreadHygiene(device, scheduler);
		}
		if (wants("shutdown")) {
			CaseShutdownDrain(device, scheduler);
		}
		std::cout << name << " completion tests passed" << std::endl;
		return 0;
	}

} // namespace

int main(int argc, char** argv) try {
	auto const name = argc > 1 ? std::string_view{ argv[1] } : std::string_view{ "opengl" };
	auto const only = argc > 2 ? std::string_view{ argv[2] } : std::string_view{};
	return Run(std::string{ name }.c_str(), only);
}
catch (std::exception const& error) {
	std::cerr << "Completion pool tests failed: " << error.what() << std::endl;
	return 1;
}
