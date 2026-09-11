module;
#ifndef FYUU_MATH_ENABLE_SIMD
#define FYUU_MATH_ENABLE_SIMD 1
#endif
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <algorithm>
#include <array>
#include <span>
#endif // !defined(__cpp_lib_modules)
// The standard <simd> backend needs no intrinsic headers; only pull them when the
// library has no std::simd to offer.
#if !defined(__cpp_lib_simd)
#if FYUU_MATH_ENABLE_SIMD && (defined(__SSE2__) || defined(_M_X64))
#include <immintrin.h>
#define FYUU_MATH_SSE2
#endif
#if FYUU_MATH_ENABLE_SIMD && (defined(_M_ARM64) || (defined(__aarch64__) && defined(__ARM_NEON)))
#if defined(_MSC_VER) && !defined(__clang__)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#define FYUU_MATH_NEON
#endif
#else
#if FYUU_MATH_ENABLE_SIMD
#include <simd>
#endif // FYUU_MATH_ENABLE_SIMD
#endif // !defined(__cpp_lib_simd)
// Internal module partition: intrinsic types never enter the public interface.
// Reference: https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html
module fyuu_math;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :operations;
#if defined(_MSC_VER)
#define FYUU_MATH_FORCE_INLINE __forceinline
#else
#define FYUU_MATH_FORCE_INLINE inline __attribute__((always_inline))
#endif
// Keep the kernel out of the module interface while allowing Clang/x86 LTO
// to optimize the fixed double matrix product together with its caller.
#if defined(__clang__) && (defined(__SSE2__) || defined(_M_X64))
#define FYUU_MATH_LTO_INLINE __attribute__((always_inline))
#else
#define FYUU_MATH_LTO_INLINE
#endif
// With FYUU_MATH_ENABLE_SIMD=OFF the whole kernel body is skipped: callers already
// gate on simd::available, so the namespace stays empty and nothing is emitted.
#if FYUU_MATH_ENABLE_SIMD
namespace fyuu_math::simd {
	// Each lane computes one output column:
	// C[r,:] = A[r,0]*B[0,:] + ... + A[r,3]*B[3,:].
	// Source-level addition order matches the scalar recurrence, without horizontal
	// reductions or approximate reciprocals. Compiler FP contraction settings may
	// still change rounding. All arrays use logical row order.
	FYUU_MATH_FORCE_INLINE void Multiply4Impl(
	    float const* a,
	    float const* b,
	    float* result
	) noexcept {

#if defined(FYUU_MATH_SSE2)
		auto b0 = _mm_loadu_ps(b);
		auto b1 = _mm_loadu_ps(b + 4);
		auto b2 = _mm_loadu_ps(b + 8);
		auto b3 = _mm_loadu_ps(b + 12);
		for (std::size_t r = 0; r < 4; ++r) {
			auto coefficients = _mm_loadu_ps(a + r * 4);
			auto row = _mm_mul_ps(_mm_shuffle_ps(coefficients, coefficients, 0x00), b0);
			row = _mm_add_ps(row, _mm_mul_ps(_mm_shuffle_ps(coefficients, coefficients, 0x55), b1));
			row = _mm_add_ps(row, _mm_mul_ps(_mm_shuffle_ps(coefficients, coefficients, 0xaa), b2));
			row = _mm_add_ps(row, _mm_mul_ps(_mm_shuffle_ps(coefficients, coefficients, 0xff), b3));
			_mm_storeu_ps(result + r * 4, row);
		}
#elif defined(FYUU_MATH_NEON)
		// NEON lanes hold the four columns of one row, exactly as in the SSE path.
		// vld1q/vst1q require only the alignment of float, not 16-byte alignment.
		auto b0 = vld1q_f32(b);
		auto b1 = vld1q_f32(b + 4);
		auto b2 = vld1q_f32(b + 8);
		auto b3 = vld1q_f32(b + 12);
		for (std::size_t r = 0; r < 4; ++r) {
			auto row = vmulq_n_f32(b0, a[r * 4]);
			row = vaddq_f32(row, vmulq_n_f32(b1, a[r * 4 + 1]));
			row = vaddq_f32(row, vmulq_n_f32(b2, a[r * 4 + 2]));
			row = vaddq_f32(row, vmulq_n_f32(b3, a[r * 4 + 3]));
			vst1q_f32(result + r * 4, row);
		}
#else
		for (std::size_t i = 0; i < 16; ++i) {
			auto r = i / 4;
			auto c = i % 4;
			result[i] = (a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c]) + a[r * 4 + 2] * b[8 + c];
			result[i] += a[r * 4 + 3] * b[12 + c];
		}
#endif
	}

	void Multiply4(float const* a, float const* b, float* result) noexcept {
		Multiply4Impl(a, b, result);
	}
	std::array<float, 16> Multiply4(float const* a, float const* b) noexcept {
		std::array<float, 16> result;
		Multiply4(a, b, result.data());
		return result;
	}
	// Backend selection. With __cpp_lib_simd the standard library supplies the lanes
	// and no intrinsic headers are involved; otherwise these register helpers are
	// private implementation details built on SSE2/NEON (four float lanes, two double
	// lanes) with a scalar fallback. https://arm-software.github.io/acle/neon_intrinsics/advsimd.html
#if !defined(__cpp_lib_simd)
	template <class S> struct Lanes;
	template <> struct Lanes<float> {
#if defined(FYUU_MATH_SSE2)
		using Register = __m128;
		static constexpr std::size_t width = 4;
		static Register Load(float const* p) noexcept {
			return _mm_loadu_ps(p);
		}
		static void Store(float* p, Register x) noexcept {
			_mm_storeu_ps(p, x);
		}
		static Register Splat(float x) noexcept {
			return _mm_set1_ps(x);
		}
		static Register Add(Register a, Register b) noexcept {
			return _mm_add_ps(a, b);
		}
		static Register Subtract(Register a, Register b) noexcept {
			return _mm_sub_ps(a, b);
		}
		static Register Multiply(Register a, Register b) noexcept {
			return _mm_mul_ps(a, b);
		}
		static Register Divide(Register a, Register b) noexcept {
			return _mm_div_ps(a, b);
		}
#elif defined(FYUU_MATH_NEON)
		using Register = float32x4_t;
		static constexpr std::size_t width = 4;
		static Register Load(float const* p) noexcept {
			return vld1q_f32(p);
		}
		static void Store(float* p, Register x) noexcept {
			vst1q_f32(p, x);
		}
		static Register Splat(float x) noexcept {
			return vdupq_n_f32(x);
		}
		static Register Add(Register a, Register b) noexcept {
			return vaddq_f32(a, b);
		}
		static Register Subtract(Register a, Register b) noexcept {
			return vsubq_f32(a, b);
		}
		static Register Multiply(Register a, Register b) noexcept {
			return vmulq_f32(a, b);
		}
		static Register Divide(Register a, Register b) noexcept {
			return vdivq_f32(a, b);
		}
#else
		using Register = float;
		static constexpr std::size_t width = 1;
		static Register Load(float const* p) noexcept {
			return *p;
		}
		static void Store(float* p, Register x) noexcept {
			*p = x;
		}
		static Register Splat(float x) noexcept {
			return x;
		}
		static Register Add(Register a, Register b) noexcept {
			return a + b;
		}
		static Register Subtract(Register a, Register b) noexcept {
			return a - b;
		}
		static Register Multiply(Register a, Register b) noexcept {
			return a * b;
		}
		static Register Divide(Register a, Register b) noexcept {
			return a / b;
		}
#endif
	};
	template <> struct Lanes<double> {
#if defined(FYUU_MATH_SSE2)
		using Register = __m128d;
		static constexpr std::size_t width = 2;
		static Register Load(double const* p) noexcept {
			return _mm_loadu_pd(p);
		}
		static void Store(double* p, Register x) noexcept {
			_mm_storeu_pd(p, x);
		}
		static Register Splat(double x) noexcept {
			return _mm_set1_pd(x);
		}
		static Register Add(Register a, Register b) noexcept {
			return _mm_add_pd(a, b);
		}
		static Register Subtract(Register a, Register b) noexcept {
			return _mm_sub_pd(a, b);
		}
		static Register Multiply(Register a, Register b) noexcept {
			return _mm_mul_pd(a, b);
		}
		static Register Divide(Register a, Register b) noexcept {
			return _mm_div_pd(a, b);
		}
#elif defined(FYUU_MATH_NEON)
		using Register = float64x2_t;
		static constexpr std::size_t width = 2;
		static Register Load(double const* p) noexcept {
			return vld1q_f64(p);
		}
		static void Store(double* p, Register x) noexcept {
			vst1q_f64(p, x);
		}
		static Register Splat(double x) noexcept {
			return vdupq_n_f64(x);
		}
		static Register Add(Register a, Register b) noexcept {
			return vaddq_f64(a, b);
		}
		static Register Subtract(Register a, Register b) noexcept {
			return vsubq_f64(a, b);
		}
		static Register Multiply(Register a, Register b) noexcept {
			return vmulq_f64(a, b);
		}
		static Register Divide(Register a, Register b) noexcept {
			return vdivq_f64(a, b);
		}
#else
		using Register = double;
		static constexpr std::size_t width = 1;
		static Register Load(double const* p) noexcept {
			return *p;
		}
		static void Store(double* p, Register x) noexcept {
			*p = x;
		}
		static Register Splat(double x) noexcept {
			return x;
		}
		static Register Add(Register a, Register b) noexcept {
			return a + b;
		}
		static Register Subtract(Register a, Register b) noexcept {
			return a - b;
		}
		static Register Multiply(Register a, Register b) noexcept {
			return a * b;
		}
		static Register Divide(Register a, Register b) noexcept {
			return a / b;
		}
#endif
	};

#else
	// C++26 <simd> backend
	template <class S> struct Lanes {
		using Register = std::simd::vec<S>;
		static constexpr std::size_t width = Register::size();
		static Register Load(S const* p) noexcept {
			return std::simd::unchecked_load<Register>(std::span<S const>{p, width});
		}
		static void Store(S* p, Register x) noexcept {
			std::simd::unchecked_store(x, std::span<S>{p, width});
		}
		static Register Splat(S x) noexcept {
			return Register(x); // broadcast constructor
		}
		static Register Add(Register a, Register b) noexcept {
			return a + b;
		}
		static Register Subtract(Register a, Register b) noexcept {
			return a - b;
		}
		static Register Multiply(Register a, Register b) noexcept {
			return a * b;
		}
		static Register Divide(Register a, Register b) noexcept {
			return a / b;
		}
	};
#endif // !defined(__cpp_lib_simd)

#if defined(__cpp_lib_simd)
	// std::simd provides partial_load/partial_store for exactly this case.
	template <class S>
	FYUU_MATH_FORCE_INLINE auto LoadPartial(
	    S const* p,
	    std::size_t count,
	    [[maybe_unused]] S padding = S(0)
	) noexcept {
		using V = Lanes<S>;
		if (count == V::width) {
			return V::Load(p);
		}
		return std::simd::partial_load<typename V::Register>(p, static_cast<std::ptrdiff_t>(count));
	}
	template <class S>
	FYUU_MATH_FORCE_INLINE void StorePartial(
	    S* p,
	    typename Lanes<S>::Register x,
	    std::size_t count
	) noexcept {
		using V = Lanes<S>;
		if (count == V::width) {
			V::Store(p, x);
			return;
		}
		std::simd::partial_store(x, p, static_cast<std::ptrdiff_t>(count));
	}
#else
	// Partial loads never touch a neighbor object. Inactive divisor lanes are one,
	// while all other inactive lanes are zero, avoiding spurious 0/0 or infinity*0.
	template <class S>
	FYUU_MATH_FORCE_INLINE auto LoadPartial(
	    S const* p,
	    std::size_t count,
	    S padding = S(0)
	) noexcept {
		using V = Lanes<S>;
		if (count == V::width) {
			return V::Load(p);
		}
#if defined(FYUU_MATH_SSE2)
		if constexpr (sizeof(S) == sizeof(float)) {
			if (count == 3) {
				// Read exactly two floats plus one float; no fourth component is assumed.
				auto low = _mm_loadl_pi(_mm_setzero_ps(), reinterpret_cast<__m64 const*>(p));
				auto high = _mm_unpacklo_ps(_mm_load_ss(p + 2), _mm_set_ss(padding));
				return _mm_movelh_ps(low, high);
			}
		}
#elif defined(FYUU_MATH_NEON)
		if constexpr (sizeof(S) == sizeof(float)) {
			if (count == 3) {
				auto high = vset_lane_f32(p[2], vdup_n_f32(padding), 0);
				return vcombine_f32(vld1_f32(p), high);
			}
		}
#endif
		S lanes[V::width]{};
		for (std::size_t i = 0; i < V::width; ++i) {
			lanes[i] = i < count ? p[i] : padding;
		}
		return V::Load(lanes);
	}
	template <class S>
	FYUU_MATH_FORCE_INLINE void StorePartial(
	    S* p,
	    typename Lanes<S>::Register x,
	    std::size_t count
	) noexcept {
		using V = Lanes<S>;
		if (count == V::width) {
			V::Store(p, x);
			return;
		}
		S lanes[V::width];
		V::Store(lanes, x);
		for (std::size_t i = 0; i < count; ++i) {
			p[i] = lanes[i];
		}
	}
#endif // !defined(__cpp_lib_simd)
	template <class S>
	FYUU_MATH_FORCE_INLINE auto BroadcastPartial(S value, std::size_t count) noexcept {
		using V = Lanes<S>;
		if (count == V::width) {
			return V::Splat(value);
		}
		S lanes[V::width]{};
		for (std::size_t i = 0; i < count; ++i) {
			lanes[i] = value;
		}
		return V::Load(lanes);
	}
	template <class S>
	FYUU_MATH_FORCE_INLINE auto GatherColumn(
	    S const* a,
	    std::size_t stride,
	    std::size_t count
	) noexcept {
		using V = Lanes<S>;
		S lanes[V::width]{};
		for (std::size_t i = 0; i < count; ++i) {
			lanes[i] = a[i * stride];
		}
		return V::Load(lanes);
	}
	template <Operation Op, class S, std::size_t FixedSize = 0>
	FYUU_MATH_FORCE_INLINE void ComponentsImpl(
	    S const* a,
	    S const* b,
	    S* result,
	    std::size_t input_size
	) noexcept {
		const std::size_t size = FixedSize ? FixedSize : input_size;
		using V = Lanes<S>;
		const std::size_t full = size - size % V::width;
		for (std::size_t i = 0; i < full; i += V::width) {
			auto x = V::Load(a + i);
			auto y = V::Load(b + i);
			if constexpr (Op == Operation::Add) {
				x = V::Add(x, y);
			} else if constexpr (Op == Operation::Subtract) {
				x = V::Subtract(x, y);
			} else if constexpr (Op == Operation::Scale) {
				x = V::Multiply(x, y);
			} else {
				x = V::Divide(x, y);
			}
			V::Store(result + i, x);
		}
		// The full-block loop has no masks or temporary lane arrays. Only the
		// final incomplete block requires bounded loads and neutral padding.
		if (full != size) {
			auto count = size - full;
			auto x = LoadPartial(a + full, count);
			auto y = LoadPartial(b + full, count, Op == Operation::Divide ? S(1) : S(0));
			if constexpr (Op == Operation::Add) {
				x = V::Add(x, y);
			} else if constexpr (Op == Operation::Subtract) {
				x = V::Subtract(x, y);
			} else if constexpr (Op == Operation::Scale) {
				x = V::Multiply(x, y);
			} else {
				x = V::Divide(x, y);
			}
			StorePartial(result + full, x, count);
		}
	}
	// Broadcast once instead of constructing and reading a full scalar-filled array.
	// Keep division as division to preserve its rounding and special-value behavior.
	template <Operation Op, class S>
	void ScalarComponentsImpl(S const* a, S scalar, S* result, std::size_t size) noexcept {
		static_assert(Op == Operation::Scale || Op == Operation::Divide);
		using V = Lanes<S>;
		auto factor = V::Splat(scalar);
		const auto full = size - size % V::width;
		for (std::size_t i = 0; i < full; i += V::width) {
			auto value = V::Load(a + i);
			if constexpr (Op == Operation::Scale)
				V::Store(result + i, V::Multiply(value, factor));
			else
				V::Store(result + i, V::Divide(value, factor));
		}
		for (std::size_t i = full; i < size; ++i) {
			if constexpr (Op == Operation::Scale)
				result[i] = a[i] * scalar;
			else
				result[i] = a[i] / scalar;
		}
	}
	// Operation travels as a runtime stack argument: the public kernels are not
	// templates, so no declared-here/defined-elsewhere template instantiation is
	// required. The switch resolves once per call, outside the vector loops.
	void ScalarComponents(
	    Operation op,
	    float const* a,
	    float scalar,
	    float* result,
	    std::size_t size
	) noexcept {
		switch (op) {
			case Operation::Scale:
				return ScalarComponentsImpl<Operation::Scale>(a, scalar, result, size);
			case Operation::Divide:
				return ScalarComponentsImpl<Operation::Divide>(a, scalar, result, size);
			default:
				return;
		}
	}
	void ScalarComponents(
	    Operation op,
	    double const* a,
	    double scalar,
	    double* result,
	    std::size_t size
	) noexcept {
		switch (op) {
			case Operation::Scale:
				return ScalarComponentsImpl<Operation::Scale>(a, scalar, result, size);
			case Operation::Divide:
				return ScalarComponentsImpl<Operation::Divide>(a, scalar, result, size);
			default:
				return;
		}
	}
	template <class S, std::size_t FixedSize = 0>
	FYUU_MATH_FORCE_INLINE S DotImpl(S const* a, S const* b, std::size_t input_size) noexcept {
		const std::size_t size = FixedSize ? FixedSize : input_size;
		using V = Lanes<S>;
		if (size == 0) {
			return S(0);
		}
		const auto first = std::min(V::width, size);
		S products[V::width];
		V::Store(products, V::Multiply(LoadPartial(a, first), LoadPartial(b, first)));
		// Seed with the first product to preserve negative zero. SIMD multiplies
		// independent components; addition remains ordered, even across block boundaries.
		S result = products[0];
		for (std::size_t lane = 1; lane < first; ++lane) {
			result += products[lane];
		}
		const auto full = size - size % V::width;
		for (std::size_t i = V::width; i < full; i += V::width) {
			V::Store(products, V::Multiply(V::Load(a + i), V::Load(b + i)));
			for (std::size_t lane = 0; lane < V::width; ++lane) {
				result += products[lane];
			}
		}
		for (std::size_t i = std::max(first, full); i < size; ++i) {
			result += a[i] * b[i];
		}
		return result;
	}
	template <class S>
	FYUU_MATH_FORCE_INLINE void CrossImpl(S const* a, S const* b, S* result) noexcept {
		using V = Lanes<S>;
		// Cyclic permutations form [ay,az,ax]*[bz,bx,by] - [az,ax,ay]*[by,bz,bx].
		// Padding both factors with zero avoids reading a fourth component of Vector3.
		const S ayzx[4]{a[1], a[2], a[0], 0}, azxy[4]{a[2], a[0], a[1], 0};
		const S byzx[4]{b[1], b[2], b[0], 0}, bzxy[4]{b[2], b[0], b[1], 0};
		for (std::size_t i = 0; i < 3; i += V::width) {
			auto x = V::Subtract(
			    V::Multiply(V::Load(ayzx + i), V::Load(bzxy + i)),
			    V::Multiply(V::Load(azxy + i), V::Load(byzx + i))
			);
			StorePartial(result + i, x, std::min(V::width, std::size_t(3) - i));
		}
	}
	template <class S, std::size_t R = 0, std::size_t K = 0, std::size_t C = 0>
	FYUU_MATH_FORCE_INLINE void MultiplyImpl(
	    S const* a,
	    S const* b,
	    S* result,
	    std::size_t input_rows,
	    std::size_t input_inner,
	    std::size_t input_columns
	) noexcept {
		const std::size_t rows = R ? R : input_rows;
		const std::size_t inner = K ? K : input_inner;
		const std::size_t columns = C ? C : input_columns;
		using V = Lanes<S>;
		if (columns == 1) {
			// Matrix-vector multiplication: each lane owns an output row.
			// Gather a column of A, multiply by b[k], and accumulate in increasing k.
			for (std::size_t r = 0; r < rows; r += V::width) {
				auto count = std::min(V::width, rows - r);
				auto x = V::Multiply(
				    GatherColumn(a + r * inner, inner, count),
				    BroadcastPartial(b[0], count)
				);
				for (std::size_t k = 1; k < inner; ++k) {
					x = V::Add(
					    x,
					    V::Multiply(
					        GatherColumn(a + r * inner + k, inner, count),
					        BroadcastPartial(b[k], count)
					    )
					);
				}
				StorePartial(result + r, x, count);
			}
			return;
		}
		// Matrix-matrix multiplication: each lane owns an output column. Flatten
		// row/block traversal; the inner recurrence never changes its reduction order.
		auto blocks = (columns + V::width - 1) / V::width;
		for (std::size_t block = 0; block < rows * blocks; ++block) {
			auto r = block / blocks;
			auto c = (block % blocks) * V::width;
			auto count = std::min(V::width, columns - c);
			// Full registers need no bounded loads or neutral padding. Branch once
			// per output block instead of checking every inner-product component.
			if (sizeof(S) == sizeof(float) && count == V::width) {
				auto x = V::Multiply(V::Splat(a[r * inner]), V::Load(b + c));
				for (std::size_t k = 1; k < inner; ++k)
					x = V::Add(
					    x,
					    V::Multiply(V::Splat(a[r * inner + k]), V::Load(b + k * columns + c))
					);
				V::Store(result + r * columns + c, x);
				continue;
			}
			auto x = V::Multiply(BroadcastPartial(a[r * inner], count), LoadPartial(b + c, count));
			for (std::size_t k = 1; k < inner; ++k) {
				x = V::Add(
				    x,
				    V::Multiply(
				        BroadcastPartial(a[r * inner + k], count),
				        LoadPartial(b + k * columns + c, count)
				    )
				);
			}
			StorePartial(result + r * columns + c, x, count);
		}
	}

	void Components(
	    Operation op,
	    float const* a,
	    float const* b,
	    float* result,
	    std::size_t size
	) noexcept {
		switch (op) {
			case Operation::Add:
				return ComponentsImpl<Operation::Add>(a, b, result, size);
			case Operation::Subtract:
				return ComponentsImpl<Operation::Subtract>(a, b, result, size);
			case Operation::Scale:
				return ComponentsImpl<Operation::Scale>(a, b, result, size);
			case Operation::Divide:
				return ComponentsImpl<Operation::Divide>(a, b, result, size);
			default:
				return;
		}
	}
	void Multiply(
	    float const* a,
	    float const* b,
	    float* result,
	    std::size_t rows,
	    std::size_t inner,
	    std::size_t columns
	) noexcept {

		if (rows == 3 && inner == 3 && columns == 3) {
			return MultiplyImpl<float, 3, 3, 3>(a, b, result, rows, inner, columns);
		}
		if (rows == 4 && inner == 4 && columns == 4) {
			return MultiplyImpl<float, 4, 4, 4>(a, b, result, rows, inner, columns);
		}
		if (rows == 3 && inner == 3 && columns == 1) {
			return MultiplyImpl<float, 3, 3, 1>(a, b, result, rows, inner, columns);
		}
		if (rows == 4 && inner == 4 && columns == 1) {
			return MultiplyImpl<float, 4, 4, 1>(a, b, result, rows, inner, columns);
		}
		MultiplyImpl(a, b, result, rows, inner, columns);
	}
	float Dot(float const* a, float const* b, std::size_t size) noexcept {
		return DotImpl(a, b, size);
	}
	void Cross(float const* a, float const* b, float* result) noexcept {
		CrossImpl(a, b, result);
	}

	void Components(
	    Operation op,
	    double const* a,
	    double const* b,
	    double* result,
	    std::size_t size
	) noexcept {
		switch (op) {
			case Operation::Add:
				return ComponentsImpl<Operation::Add>(a, b, result, size);
			case Operation::Subtract:
				return ComponentsImpl<Operation::Subtract>(a, b, result, size);
			case Operation::Scale:
				return ComponentsImpl<Operation::Scale>(a, b, result, size);
			case Operation::Divide:
				return ComponentsImpl<Operation::Divide>(a, b, result, size);
			default:
				return;
		}
	}
	void Multiply(
	    double const* a,
	    double const* b,
	    double* result,
	    std::size_t rows,
	    std::size_t inner,
	    std::size_t columns
	) noexcept {

		if (rows == 3 && inner == 3 && columns == 3) {
			return MultiplyImpl<double, 3, 3, 3>(a, b, result, rows, inner, columns);
		}
		if (rows == 4 && inner == 4 && columns == 4) {
			return MultiplyImpl<double, 4, 4, 4>(a, b, result, rows, inner, columns);
		}
		if (rows == 3 && inner == 3 && columns == 1) {
			return MultiplyImpl<double, 3, 3, 1>(a, b, result, rows, inner, columns);
		}
		if (rows == 4 && inner == 4 && columns == 1) {
			return MultiplyImpl<double, 4, 4, 1>(a, b, result, rows, inner, columns);
		}
		MultiplyImpl(a, b, result, rows, inner, columns);
	}
	double Dot(double const* a, double const* b, std::size_t size) noexcept {
		return DotImpl(a, b, size);
	}
	void Cross(double const* a, double const* b, double* result) noexcept {
		CrossImpl(a, b, result);
	}

	template <class S, std::size_t N>
	FYUU_MATH_FORCE_INLINE std::array<S, N * N> SquareProduct(S const* a, S const* b) noexcept {
		using V = Lanes<S>;
		constexpr std::size_t blocks = (N + V::width - 1) / V::width;
		// Load the right matrix before any result stores. Reuse its rows instead of
		// repeatedly gathering them for every output row through potentially aliased pointers.
		typename V::Register right[N * blocks];
		for (std::size_t k = 0; k < N; ++k) {
			for (std::size_t block = 0; block < blocks; ++block) {
				auto c = block * V::width;
				right[k * blocks + block] = LoadPartial(b + k * N + c, std::min(V::width, N - c));
			}
		}
		std::array<S, N * N> result;
		for (std::size_t block = 0; block < N * blocks; ++block) {
			auto r = block / blocks;
			auto c = (block % blocks) * V::width;
			auto count = std::min(V::width, N - c);
			if constexpr (N % V::width == 1) {
				if (count == 1) {
					// A single output needs no padding or vector broadcast. The other
					// columns still share full SIMD registers and the same reduction order.
					auto sum = a[r * N] * b[c];
					for (std::size_t k = 1; k < N; ++k) {
						sum += a[r * N + k] * b[k * N + c];
					}
					result[r * N + c] = sum;
					continue;
				}
			}
			auto x = V::Multiply(BroadcastPartial(a[r * N], count), right[block % blocks]);
			for (std::size_t k = 1; k < N; ++k) {
				x = V::Add(
				    x,
				    V::Multiply(
				        BroadcastPartial(a[r * N + k], count),
				        right[k * blocks + block % blocks]
				    )
				);
			}
			StorePartial(result.data() + r * N + c, x, count);
		}
		return result;
	}
	template <class S, std::size_t N>
	FYUU_MATH_FORCE_INLINE std::array<S, N> FixedMatrixVector(S const* a, S const* b) noexcept {
		using V = Lanes<S>;
		std::array<S, N> result;
		for (std::size_t r = 0; r < N; r += V::width) {
			auto count = std::min(V::width, N - r);
			auto x = V::Multiply(GatherColumn(a + r * N, N, count), BroadcastPartial(b[0], count));
			for (std::size_t k = 1; k < N; ++k) {
				x = V::Add(
				    x,
				    V::Multiply(
				        GatherColumn(a + r * N + k, N, count),
				        BroadcastPartial(b[k], count)
				    )
				);
			}
			StorePartial(result.data() + r, x, count);
		}
		return result;
	}
	std::array<float, 9> Multiply3(float const* a, float const* b) noexcept {
		return SquareProduct<float, 3>(a, b);
	}
	std::array<float, 3> MatrixVector3(float const* a, float const* b) noexcept {
		return FixedMatrixVector<float, 3>(a, b);
	}
	std::array<float, 4> MatrixVector4(float const* a, float const* b) noexcept {
		return FixedMatrixVector<float, 4>(a, b);
	}
	float Dot3(float const* a, float const* b) noexcept {
		return DotImpl<float, 3>(a, b, 3);
	}
	std::array<double, 9> Multiply3(double const* a, double const* b) noexcept {
		return SquareProduct<double, 3>(a, b);
	}
	std::array<double, 3> MatrixVector3(double const* a, double const* b) noexcept {
		return FixedMatrixVector<double, 3>(a, b);
	}
	FYUU_MATH_LTO_INLINE std::array<double, 16> Multiply4(
	    double const* a,
	    double const* b
	) noexcept {
		return SquareProduct<double, 4>(a, b);
	}
	std::array<double, 4> MatrixVector4(double const* a, double const* b) noexcept {
		return FixedMatrixVector<double, 4>(a, b);
	}
	double Dot3(double const* a, double const* b) noexcept {
		return DotImpl<double, 3>(a, b, 3);
	}
} // namespace fyuu_math::simd
#endif // FYUU_MATH_ENABLE_SIMD
#undef FYUU_MATH_SSE2
#undef FYUU_MATH_NEON
#undef FYUU_MATH_FORCE_INLINE
#undef FYUU_MATH_LTO_INLINE
