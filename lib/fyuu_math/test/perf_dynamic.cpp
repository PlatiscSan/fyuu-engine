// Manual performance harness for the runtime-length (dynamic span / vector) path.
// Build-only target: cmake --build <dir> --target fyuu_math_perf_dynamic ; then run the exe.
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>
import fyuu_math;

namespace fm = fyuu_math;

namespace {
using Clock = std::chrono::steady_clock;

// Total elementwise operations to keep each measurement in the tens-of-ms range.
constexpr std::size_t ElementTarget = 1u << 22;

void report(char const* name, std::size_t n, std::size_t iters, double ns_total, double sink) {
	double per_op = ns_total / static_cast<double>(iters);
	std::printf("%-22s N=%6zu  %9.1f ns/op  %7.1f GB/s   sink %.1f\n",
	            name, n, per_op,
	            (3.0 * n * 4) / (per_op * 1e-9) / 1e9, sink);
	(void)sink;
}

void measure(std::size_t n) {
	std::size_t const iters = (ElementTarget + n - 1) / n;
	std::vector<float> a(n, 1.0f), b(n), c(n);
	for (std::size_t i = 0; i < n; ++i) b[i] = float((i * 2654435761u) % 997 + 1) / 100.0f;
	for (std::size_t i = 0; i < n; ++i) c[i] = float(i % 7) / 10.0f;

	{ // raw add baseline
		std::vector<float> out(n);
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it)
			for (std::size_t i = 0; i < n; ++i) out[i] = a[i] + b[i];
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		for (auto v : out) sink += v;
		report("raw add", n, iters, double(ns), sink);
	}
	{ // dyn add -> fresh owning std::vector (includes allocation)
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it) {
			auto r = (fm::AsVector(a) + fm::AsVector(b)) >> fm::As<std::vector<float>>;
			sink += r && !r->empty() ? static_cast<double>((*r)[0]) : 0.0;
		}
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		report("dyn add->vector", n, iters, double(ns), sink);
	}
	{ // dyn scale
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it) {
			auto r = (fm::AsVector(a) * 2.0f) >> fm::As<std::vector<float>>;
			sink += r && !r->empty() ? static_cast<double>((*r)[0]) : 0.0;
		}
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		report("dyn scale->vector", n, iters, double(ns), sink);
	}
	{ // dyn divide (terminal)
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it) {
			auto r = (fm::AsVector(a) / 3.0f) >> fm::As<std::vector<float>>;
			sink += r && !r->empty() ? static_cast<double>((*r)[0]) : 0.0;
		}
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		report("dyn div->vector", n, iters, double(ns), sink);
	}
	{ // dyn composite (a+b)+c -> vector
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it) {
			auto r = ((fm::AsVector(a) + fm::AsVector(b)) + fm::AsVector(c)) >>
			    fm::As<std::vector<float>>;
			sink += r && !r->empty() ? static_cast<double>((*r)[0]) : 0.0;
		}
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		report("dyn (a+b)+c->v", n, iters, double(ns), sink);
	}
	{ // dot (allocation-free reduction)
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it) {
			auto d = fm::AsVector(a) | fm::Dot{fm::AsVector(b)};
			sink += d ? static_cast<double>(*d) : 0.0;
		}
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		report("dyn dot", n, iters, double(ns), sink);
	}
	{ // length (allocation-free reduction)
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it) {
			auto l = fm::AsVector(a) | fm::Length;
			sink += l ? static_cast<double>(*l) : 0.0;
		}
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		report("dyn length", n, iters, double(ns), sink);
	}
}
} // namespace

int main() {
	std::puts("dynamic span perf (owning-copy output)");
	for (std::size_t n : {std::size_t{64}, std::size_t{1024}, std::size_t{16384}}) measure(n);
	return 0;
}
