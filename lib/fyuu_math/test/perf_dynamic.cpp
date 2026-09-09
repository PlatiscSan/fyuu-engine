// Manual performance harness for the runtime-length (dynamic span) vector path.
// Build-only target: cmake --build <dir> --target fyuu_math_perf_dynamic ; then run the exe.
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <span>
#include <vector>
import fyuu_math;

namespace fm = fyuu_math;

namespace {
using Clock = std::chrono::steady_clock;

// Total elementwise operations to keep each measurement in the tens-of-ms range.
constexpr std::size_t ElementTarget = 1u << 23;

double checksum(std::span<float const> s) noexcept {
	double acc = 0;
	for (auto v : s) acc += static_cast<double>(v);
	return acc;
}

double run(std::size_t n, std::size_t iters, auto&& op, auto&& consume) {
	op(); // warm-up (also accumulates for in-place cases, harmless)
	auto t0 = Clock::now();
	for (std::size_t i = 0; i < iters; ++i) op();
	auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
	return consume();
	(void)ns; // consume() keeps results alive; timing measured above
}

void report(char const* name, std::size_t n, std::size_t iters, double ns_total, double sum) {
	double per_op = ns_total / static_cast<double>(iters);
	std::printf("%-22s N=%6zu  %8.1f ns/op  %7.1f GB/s   checksum %.1f\n",
	            name, n, per_op,
	            (3.0 * n * 4) / (per_op * 1e-9) / 1e9, sum);
	(void)sum;
}

void measure(std::size_t n) {
	std::size_t const iters = (ElementTarget + n - 1) / n;
	std::vector<float> a(n, 1.0f), b(n), c(n), out(n);
	for (std::size_t i = 0; i < n; ++i) b[i] = float((i * 2654435761u) % 997 + 1) / 100.0f;
	for (std::size_t i = 0; i < n; ++i) c[i] = float(i % 7) / 10.0f;

	std::span<float> sa{a}, sb{b}, sc{c}, sout{out};

	{ // raw baseline: a[i]+b[i]
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it)
			for (std::size_t i = 0; i < n; ++i) out[i] = a[i] + b[i];
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		sink = checksum(out);
		report("raw add", n, iters, double(ns), sink);
		(void)sink;
	}
	{ // raw length baseline: double-accumulated sum of squares over the same data
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it) {
			double s = 0;
			for (std::size_t i = 0; i < n; ++i) s += double(a[i]) * double(a[i]);
			sink += std::sqrt(s);
		}
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		report("raw length", n, iters, double(ns), sink);
	}
	{ // leaf+leaf, disjoint output
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it) {
			auto r = (fm::AsVector(sa) + fm::AsVector(sb)) >> sout;
			if (!r) std::abort();
		}
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		sink = checksum(out);
		report("dyn add", n, iters, double(ns), sink);
		(void)sink;
	}
	{ // in-place a = a + b (output aliases first leaf)
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it) {
			auto r = (fm::AsVector(sa) + fm::AsVector(sb)) >> sa;
			if (!r) std::abort();
		}
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		sink = checksum(a);
		report("dyn add in-place", n, iters, double(ns), sink);
		(void)sink;
	}
	{ // scale leaf*2.0, disjoint
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it) {
			auto r = (fm::AsVector(sa) * 2.0f) >> sout;
			if (!r) std::abort();
		}
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		sink = checksum(out);
		report("dyn scale", n, iters, double(ns), sink);
		(void)sink;
	}
	{ // divide leaf / 3.0, disjoint
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it) {
			auto r = (fm::AsVector(sa) / 3.0f) >> sout;
			if (!r) std::abort();
		}
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		sink = checksum(out);
		report("dyn divide", n, iters, double(ns), sink);
		(void)sink;
	}
	{ // raw divide baseline
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it)
			for (std::size_t i = 0; i < n; ++i) out[i] = a[i] / 3.0f;
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		sink = checksum(out);
		report("raw divide", n, iters, double(ns), sink);
		(void)sink;
	}
	{ // raw dot baseline
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it) {
			double s = 0;
			for (std::size_t i = 0; i < n; ++i) s += double(a[i]) * double(b[i]);
			sink += s;
		}
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		report("raw dot", n, iters, double(ns), sink);
	}
	{ // composite (a+b)+c
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it) {
			auto r = ((fm::AsVector(sa) + fm::AsVector(sb)) + fm::AsVector(sc)) >> sout;
			if (!r) std::abort();
		}
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		sink = checksum(out);
		report("dyn (a+b)+c", n, iters, double(ns), sink);
		(void)sink;
	}
	{ // composite (a-b)+(c*2)
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it) {
			auto r = ((fm::AsVector(sa) - fm::AsVector(sb)) + (fm::AsVector(sc) * 2.0f)) >> sout;
			if (!r) std::abort();
		}
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		sink = checksum(out);
		report("dyn (a-b)+c*2", n, iters, double(ns), sink);
		(void)sink;
	}
	{ // dot
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it) {
			auto d = fm::AsVector(sa) | fm::Dot{fm::AsVector(sb)};
			sink += d ? static_cast<double>(*d) : 0.0;
		}
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		report("dyn dot", n, iters, double(ns), sink);
	}
	{ // length
		double sink = 0;
		auto t0 = Clock::now();
		for (std::size_t it = 0; it < iters; ++it) {
			auto l = fm::AsVector(sa) | fm::Length;
			sink += l ? static_cast<double>(*l) : 0.0;
		}
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
		report("dyn length", n, iters, double(ns), sink);
	}
}
} // namespace

int main() {
	std::puts("dynamic span perf (SIMD build)");
	for (std::size_t n : {std::size_t{64}, std::size_t{1024}, std::size_t{16384}}) measure(n);
	return 0;
}
