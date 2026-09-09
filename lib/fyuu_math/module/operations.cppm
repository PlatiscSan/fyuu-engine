module;
#ifndef FYUU_MATH_ENABLE_SIMD
#define FYUU_MATH_ENABLE_SIMD 1
#endif
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <utility>
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <array>
#include <concepts>
#include <expected>
#endif // !defined(__cpp_lib_modules)

export module fyuu_math:operations;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :traits;
import :concepts;

// Module-linkage implementation boundary. These declarations are reachable by
// the implementation unit, but are deliberately absent from `import fyuu_math`.
namespace fyuu_math::simd {
#if FYUU_MATH_ENABLE_SIMD &&                                                                       \
    (defined(__SSE2__) || defined(_M_X64) || defined(_M_ARM64) ||                                  \
     (defined(__aarch64__) && defined(__ARM_NEON)))
	inline constexpr bool available = true;
#else
	inline constexpr bool available = false;
#endif
	// Clang on x86 optimizes the dimension-known scalar expressions better for
	// small matrix products and ordered reductions. Keep explicit kernels for
	// other toolchains/targets; dispatch is a performance choice, not capability.
#if defined(__clang__) && (defined(__SSE2__) || defined(_M_X64))
	inline constexpr bool prefer_inline_reductions = true;
#else
	inline constexpr bool prefer_inline_reductions = false;
#endif
	enum class Operation { Add, Subtract, Scale, Divide };
	template <Operation Op, class S>
	void ScalarComponents(S const* a, S scalar, S* result, std::size_t size) noexcept;
	std::array<float, 9> Multiply3(float const* a, float const* b) noexcept;
	std::array<double, 9> Multiply3(double const* a, double const* b) noexcept;
	std::array<double, 16> Multiply4(double const* a, double const* b) noexcept;
	std::array<float, 3> MatrixVector3(float const* a, float const* b) noexcept;
	std::array<double, 3> MatrixVector3(double const* a, double const* b) noexcept;
	std::array<float, 4> MatrixVector4(float const* a, float const* b) noexcept;
	std::array<double, 4> MatrixVector4(double const* a, double const* b) noexcept;
	float Dot3(float const* a, float const* b) noexcept;
	double Dot3(double const* a, double const* b) noexcept;
	template <Operation Op>
	void Components3(float const* a, float const* b, float* result) noexcept;
	template <Operation Op>
	void Components3(double const* a, double const* b, double* result) noexcept;
	// Only scalar arrays cross the kernel boundary. Intrinsics are private to simd.cpp.
	std::array<float, 16> Multiply4(float const* a, float const* b) noexcept;
	void Multiply4(float const* a, float const* b, float* result) noexcept;
	template <Operation Op>
	void Components(float const* a, float const* b, float* result, std::size_t size) noexcept;
	void Multiply(
	    float const* a,
	    float const* b,
	    float* result,
	    std::size_t rows,
	    std::size_t inner,
	    std::size_t columns
	) noexcept;
	float Dot(float const* a, float const* b, std::size_t size) noexcept;
	void Cross(float const* a, float const* b, float* result) noexcept;

	template <Operation Op>
	void Components(double const* a, double const* b, double* result, std::size_t size) noexcept;
	void Multiply(
	    double const* a,
	    double const* b,
	    double* result,
	    std::size_t rows,
	    std::size_t inner,
	    std::size_t columns
	) noexcept;
	double Dot(double const* a, double const* b, std::size_t size) noexcept;
	void Cross(double const* a, double const* b, double* result) noexcept;
} // namespace fyuu_math::simd

export namespace fyuu_math {
	// Mathematical conventions: proofs assume exact real arithmetic; floating-point results approximate these identities.
	// Vectors are columns. A[r,c] denotes a logical matrix element with zero-based indices.
	// Read/Make map logical coordinates to backend storage; formulas are independent of row/column-major layout.
	// Write quaternions as q=(u,w), u=(x,y,z), using Hamilton multiplication and the right-handed cross product.
	// ADL customizations must preserve these semantics; Concepts check interfaces, not numerical correctness.
	struct AddTag {};
	struct SubtractTag {};
	struct NegateTag {};
	struct ScaleTag {};
	struct DivideTag {};
	struct DotTag {};
	struct CrossTag {};
	struct LengthTag {};
	struct NormalizeTag {};
	struct MultiplyTag {};
	struct TransposeTag {};
	struct IdentityTag {};
	struct InverseTag {};
	struct ConjugateTag {};
	struct ComposeRotationTag {};
	struct RotateTag {};
	struct ConvertTag {};

	template <class Out> inline constexpr ResultType<Out> As{};
	inline constexpr LengthTag Length{};
	inline constexpr TransposeTag Transpose{};
	inline constexpr ConjugateTag Conjugate{};
	inline constexpr IdentityTag Identity{};
	template <MathScalar S> struct Normalize {
		Tolerance<S> tolerance;
	};
	template <MathScalar S> struct Inverse {
		Tolerance<S> tolerance;
	};

	namespace detail {
		namespace adl {
			struct Missing {};
			// Declared only: the ellipsis is used in unevaluated lookup, never called.
			Missing tag_invoke(...);
			template <class Tag, class Out, class... A>
			concept Callable =
			    requires(A const&... a) { tag_invoke(Tag{}, ResultType<Out>{}, a...); };
			template <class Tag, class Out, class... A>
			using Result =
			    decltype(tag_invoke(Tag{}, ResultType<Out>{}, std::declval<A const&>()...));
			template <class Tag, class Out, class F, class... A> consteval bool Nothrow() {
				if constexpr (!Callable<Tag, Out, A...>)
					return false;
				else if constexpr (std::same_as<Result<Tag, Out, A...>, Missing>)
					return noexcept(std::declval<F>()());
				else
					return noexcept(
					    tag_invoke(Tag{}, ResultType<Out>{}, std::declval<A const&>()...)
					);
			}
			template <class Tag, class Out, class F, class... A>
			constexpr Out Dispatch(F fallback, A const&... a) noexcept(
			    Nothrow<Tag, Out, F, A...>()
			) {
				static_assert(Callable<Tag, Out, A...>, "Ambiguous fyuu_math tag_invoke");
				if constexpr (Callable<Tag, Out, A...>) {
					using R = Result<Tag, Out, A...>;
					static_assert(
					    std::same_as<R, Missing> || std::same_as<R, Out>,
					    "fyuu_math tag_invoke must return exactly the requested result type"
					);
					if constexpr (std::same_as<R, Missing>)
						return fallback();
					else
						return tag_invoke(Tag{}, ResultType<Out>{}, a...);
				}
			}
			// Checked hooks have ResultType<Out>, but return expected<Out, MathError>.
			template <class Tag, class Out, class A, class S> consteval bool CheckedNothrow() {
				if constexpr (!Callable<Tag, Out, A, Tolerance<S>>)
					return false;
				else {
					using R = Result<Tag, Out, A, Tolerance<S>>;
					constexpr bool access = NothrowRead<A>() && NothrowRead<Out>() &&
					    std::is_nothrow_move_constructible_v<Out>;
					if constexpr (std::same_as<R, Missing>)
						return access &&
						    noexcept(detail::Build<Out>(
						        std::declval<std::array<Scalar<Out>, count<Out>> const&>()
						    ));
					else
						return access &&
						    noexcept(tag_invoke(
						        Tag{},
						        ResultType<Out>{},
						        std::declval<A const&>(),
						        std::declval<Tolerance<S> const&>()
						    ));
				}
			}
			template <class Tag, class Out, class F, class A, class S>
			constexpr std::expected<Out, MathError> Checked(
			    F fallback,
			    A const& a,
			    Tolerance<S> tolerance
			) noexcept(CheckedNothrow<Tag, Out, A, S>()) {
				static_assert(
				    requires { tag_invoke(Tag{}, ResultType<Out>{}, a, std::as_const(tolerance)); },
				    "Ambiguous checked fyuu_math tag_invoke"
				);
				using R =
				    decltype(tag_invoke(Tag{}, ResultType<Out>{}, a, std::as_const(tolerance)));
				static_assert(
				    std::same_as<R, Missing> || std::same_as<R, std::expected<Out, MathError>>,
				    "Checked tag_invoke must return expected<Out, MathError>"
				);
				if constexpr (std::same_as<R, Missing>)
					return fallback();
				else
					return tag_invoke(Tag{}, ResultType<Out>{}, a, std::as_const(tolerance));
			}
		} // namespace adl

		template <MathLike T>
		constexpr bool Finite(T const& value) noexcept(noexcept(Read(value, 0))) {
			for (std::size_t i = 0; i < count<T>; ++i)
				if (!std::isfinite(Read(value, i)))
					return false;
			return true;
		}
		template <MathScalar S> constexpr bool ValidTolerance(Tolerance<S> t) noexcept {
			return std::isfinite(t.absolute) && std::isfinite(t.relative) && t.absolute >= 0 &&
			    t.relative >= 0;
		}
		template <MathLike T>
		constexpr Scalar<T> Norm(T const& value) noexcept(noexcept(Read(value, 0))) {
			// Euclidean norm: ||v||_2 = sqrt(v_0^2 + ... + v_(n-1)^2).
			// Set h_0=0 and h_(i+1)=hypot(h_i,v_i). Induction gives
			// h_i^2 = sum_(j<i) v_j^2, hence h_n=||v||_2.
			// Squaring first can overflow even when the final norm is representable, e.g. for one large component.
			// hypot avoids unnecessary intermediate overflow/underflow; the true norm may still exceed Scalar's range.
			Scalar<T> result = 0;
			for (std::size_t i = 0; i < count<T>; ++i)
				result = std::hypot(result, Read(value, i));
			return result;
		}
		template <MathValue Out, MathLike A>
		    requires SameShape<Out, A>
		constexpr Out Convert(A const& a) noexcept(
		    noexcept(Read(a, 0)) &&
		    noexcept(detail::Build<Out>(std::declval<std::array<Scalar<Out>, count<Out>> const&>()))
		) {
			std::array<Scalar<Out>, count<Out>> values{};
			for (std::size_t i = 0; i < count<Out>; ++i)
				values[i] = static_cast<Scalar<Out>>(Read(a, i));
			return detail::Build<Out>(values);
		}

		template <class T> consteval bool NothrowOperandRead() {
			if constexpr (MathLike<T>)
				return NothrowRead<T>();
			else
				return true;
		}

		// Matrix product derivation: let A be R-by-K and B be K-by-C, applying B before A.
		// For any column vector x:
		//   (A(Bx))_r = sum_k A[r,k] (sum_c B[k,c] x_c)
		//             = sum_c (sum_k A[r,k] B[k,c]) x_c.
		// Thus (AB)[r,c] = sum_k A[r,k] B[k,c], the dot product of row r of A and column c of B.
		// For example:
		//   [a b] [e f] = [ae+bg  af+bh]
		//   [c d] [g h]   [ce+dg  cf+dh].
		// A column vector B has C=1 and uses the same formula. A's column count must equal B's row count.
		// The outer expansion enumerates outputs; the inner unary fold sums the K products, left to right.
		// This specifies source-level association, not bitwise reproducibility across floating-point optimization modes.
		template <std::size_t Row, std::size_t Column, MathLike A, MathLike B, std::size_t... K>
		constexpr Scalar<A> MatrixProductElement(
		    A const& a,
		    B const& b,
		    std::index_sequence<K...>
		) noexcept(NothrowRead<A>() && NothrowRead<B>()) {
			return (... + (Read(a, Row * Columns<A>() + K) * Read(b, K * Columns<B>() + Column)));
		}

		template <MathValue Out, MathLike A, MathLike B, std::size_t... I>
		// I enumerates results in logical row order: row=I/C, column=I%C.
		// Make converts this canonical order to Out's physical layout; no implicit transpose occurs.
		constexpr Out MatrixProduct(A const& a, B const& b, std::index_sequence<I...>) noexcept(
		    NothrowRead<A>() && NothrowRead<B>() &&
		    noexcept(detail::Build<Out>(std::declval<std::array<Scalar<Out>, count<Out>> const&>()))
		) {
			if constexpr (simd::available && MatrixOf<A, 4, 4> && MatrixOf<B, 4, 4>) {
				if (!std::is_constant_evaluated()) {
					auto left = SIMDInput<A>{a};
					auto right = SIMDInput<B>{b};
					if constexpr (std::same_as<Scalar<Out>,float> && requires(Out& v) {
						{ Traits<Out>::Data(v) } noexcept -> std::same_as<float*>;
					}) {
						auto out = Traits<Out>::Create();
						simd::Multiply4(left.data,right.data,Traits<Out>::Data(out));
						return out;
					}
					return detail::Build<Out>(simd::Multiply4(left.data, right.data));
				}
			}
			if constexpr (
			    simd::available &&
			    !(simd::prefer_inline_reductions && MatrixOf<A, 3, 3> && MatrixOf<B, 3, 3>)
			) {
				if (!std::is_constant_evaluated()) {
					auto left = SIMDInput<A>{a};
					auto right = SIMDInput<B>{b};
					std::array<Scalar<Out>, count<Out>> values{};
					if constexpr (MatrixOf<A, 3, 3> && MatrixOf<B, 3, 3>) {
						return detail::Build<Out>(simd::Multiply3(left.data, right.data));
					} else if constexpr (MatrixOf<A, 3, 3> && VectorOf<B, 3>) {
						return detail::Build<Out>(simd::MatrixVector3(left.data, right.data));
					} else if constexpr (MatrixOf<A, 4, 4> && VectorOf<B, 4>) {
						return detail::Build<Out>(simd::MatrixVector4(left.data, right.data));
					}
					simd::Multiply(
					    left.data,
					    right.data,
					    values.data(),
					    Rows<A>(),
					    Columns<A>(),
					    Columns<B>()
					);
					return detail::Build<Out>(values);
				}
			}
			return detail::Build<Out>(
			    std::array<Scalar<Out>, count<Out>>{
			        MatrixProductElement<I / Columns<Out>(), I % Columns<Out>()>(
			            a,
			            b,
			            std::make_index_sequence<Columns<A>()>{}
			        )...
			    }
			);
		}

		template <MathLike A, MathLike B, std::size_t... I>
		// Real dot product: <a,b>=a^T b=sum_i a_i b_i.
		// Combining ||a-b||^2=||a||^2+||b||^2-2<a,b> with the cosine rule gives, for nonzero vectors,
		// <a,b>=||a|| ||b|| cos(theta). Orthogonal vectors have zero dot product (use tolerance in floating point).
		// The result is a scalar, not a componentwise product. Scalar is real, so no complex conjugation is needed.
		constexpr Scalar<A> DotElements(A const& a, B const& b, std::index_sequence<I...>) noexcept(
		    NothrowRead<A>() && NothrowRead<B>()
		) {
			if constexpr (simd::available && !simd::prefer_inline_reductions && count<A> >= 2) {
				if (!std::is_constant_evaluated()) {
					auto left = SIMDInput<A>{a};
					auto right = SIMDInput<B>{b};
					if constexpr (count<A> == 3) {
						return simd::Dot3(left.data, right.data);
					}
					return simd::Dot(left.data, right.data, count<A>);
				}
			}
			return (... + (Read(a, I) * Read(b, I)));
		}

		template <class Tag, MathValue Out, MathLike A, class B>
		constexpr Out Binary(A const& a, B const& b) noexcept(
		    noexcept(Read(a, 0)) && NothrowOperandRead<B>() &&
		    noexcept(detail::Build<Out>(std::declval<std::array<Scalar<Out>, count<Out>> const&>()))
		) {
			using S = Scalar<Out>;
			if constexpr (std::same_as<Tag, AddTag> || std::same_as<Tag, SubtractTag> ||
			              std::same_as<Tag, ScaleTag> || std::same_as<Tag, DivideTag>) {
				auto out = Traits<Out>::Create();
				if constexpr (simd::available && requires(Out& v) {
					{ Traits<Out>::Data(v) } noexcept -> std::same_as<S*>;
				}) {
					if (!std::is_constant_evaluated()) {
						constexpr auto op = [] {
							if constexpr (std::same_as<Tag, AddTag>) return simd::Operation::Add;
							else if constexpr (std::same_as<Tag, SubtractTag>) return simd::Operation::Subtract;
							else if constexpr (std::same_as<Tag, ScaleTag>) return simd::Operation::Scale;
							else return simd::Operation::Divide;
						}();
						auto left = SIMDInput<A>{a};
						if constexpr (MathLike<B>) {
							auto right = SIMDInput<B>{b};
							simd::Components<op>(left.data,right.data,Traits<Out>::Data(out),count<Out>);
						} else simd::ScalarComponents<op>(left.data,b,Traits<Out>::Data(out),count<Out>);
						return out;
					}
				}
				for(std::size_t i=0;i<count<Out>;++i) {
					if constexpr (std::same_as<Tag, AddTag>) Write(out,i,Read(a,i)+Read(b,i));
					else if constexpr (std::same_as<Tag, SubtractTag>) Write(out,i,Read(a,i)-Read(b,i));
					else if constexpr (std::same_as<Tag, ScaleTag>) Write(out,i,Read(a,i)*b);
					else Write(out,i,Read(a,i)/b);
				}
				return out;
			}
			std::array<S, count<Out>> result;
			if constexpr (std::same_as<Tag, MultiplyTag>) {
				return MatrixProduct<Out>(a, b, std::make_index_sequence<count<Out>>{});
			} else if constexpr (std::same_as<Tag, CrossTag>) {
				if constexpr (simd::available) {
					if (!std::is_constant_evaluated()) {
						auto left = SIMDInput<A>{a};
						auto right = SIMDInput<B>{b};
						simd::Cross(left.data, right.data, result.data());
						return detail::Build<Out>(result);
					}
				}
				// Expand the formal determinant for the right-handed cross product:
				//             | e_x e_y e_z |
				//   a × b  =  | a_x a_y a_z |
				//             | b_x b_y b_z |
				//          = (a_y b_z-a_z b_y, a_z b_x-a_x b_z, a_x b_y-a_y b_x).
				// In a·(a×b), the six terms cancel in pairs; likewise for b·(a×b), proving orthogonality to both.
				// Exchanging a and b negates the result; e_x×e_y=e_z fixes the right-handed orientation.
				// (i+1)%3 and (i+2)%3 enumerate the three cyclic coordinate permutations.
				for (std::size_t i = 0; i < 3; ++i)
					result[i] = Read(a, (i + 1) % 3) * Read(b, (i + 2) % 3) -
					    Read(a, (i + 2) % 3) * Read(b, (i + 1) % 3);
			} else if constexpr (std::same_as<Tag, ComposeRotationTag>) {
				// Expand Hamilton multiplication using i^2=j^2=k^2=ijk=-1, with a=(u,w), b=(v,s):
				//   a*b = (w v + s u + u×v, w s - u·v).
				// The first three components form the vector part and the last is real; the two blocks below compute them.
				// For unit quaternions, define rotation by R(q)p=q*(p,0)*conj(q). Associativity gives:
				//   (a*b) p conj(a*b) = a (b p conj(b)) conj(a).
				// Thus a*b applies rotation b before a; the order generally cannot be exchanged.
				for (std::size_t i = 0; i < 3; ++i)
					result[i] = Read(a, 3) * Read(b, i) + Read(b, 3) * Read(a, i) +
					    Read(a, (i + 1) % 3) * Read(b, (i + 2) % 3) -
					    Read(a, (i + 2) % 3) * Read(b, (i + 1) % 3);
				result[3] = Read(a, 3) * Read(b, 3);
				for (std::size_t i = 0; i < 3; ++i)
					result[3] -= Read(a, i) * Read(b, i);
			} else if constexpr (std::same_as<Tag, RotateTag>) {
				// To rotate p by unit q=(u,w), take the vector part of q*(p,0)*conj(q).
				// Expanding the Hamilton products gives
				//   p'=(w^2-u·u)p + 2u(u·p) + 2w(u×p).
				// Using w^2+u·u=1 and u×(u×p)=u(u·p)-(u·u)p:
				//   p'=p+2w(u×p)+2u×(u×p).
				// Set t=2(u×p); then p'=p+w*t+u×t, requiring only two cross products.
				// cross[] stores t. Unit length is a precondition; this unchecked operator does not normalize q.
				std::array<S, 3> cross{};
				for (std::size_t i = 0; i < 3; ++i)
					cross[i] = S(2) *
					    (Read(a, (i + 1) % 3) * Read(b, (i + 2) % 3) -
					     Read(a, (i + 2) % 3) * Read(b, (i + 1) % 3));
				for (std::size_t i = 0; i < 3; ++i)
					result[i] = Read(b, i) + Read(a, 3) * cross[i] +
					    Read(a, (i + 1) % 3) * cross[(i + 2) % 3] -
					    Read(a, (i + 2) % 3) * cross[(i + 1) % 3];
			}
			return detail::Build<Out>(result);
		}

		template <class Tag, MathValue Out, MathLike A>
		constexpr Out Unary(A const& a) noexcept(
		    noexcept(Read(a, 0)) &&
		    noexcept(detail::Build<Out>(std::declval<std::array<Scalar<Out>, count<Out>> const&>()))
		) {
			// Transpose exchanges logical coordinates: (A^T)[r,c]=A[c,r].
			// For Out, r=i/Columns<Out>() and c=i%Columns<Out>(); the source index is c*Columns<A>()+r.
			// Quaternion conjugation gives conj(x,y,z,w)=(-x,-y,-z,w), with q*conj(q)=(0,||q||^2).
			// Hence q^(-1)=conj(q)/||q||^2; conjugation equals inversion only for unit quaternions.
			std::array<Scalar<Out>, count<Out>> result{};
			for (std::size_t i = 0; i < count<Out>; ++i) {
				if constexpr (std::same_as<Tag, TransposeTag>)
					result[i] = Read(a, (i % Columns<Out>()) * Columns<A>() + i / Columns<Out>());
				else if constexpr (std::same_as<Tag, ConjugateTag>)
					result[i] = i == 3 ? Read(a, i) : -Read(a, i);
				else
					result[i] = -Read(a, i);
			}
			return detail::Build<Out>(result);
		}

		template <MathValue Out, MathLike A>
		// For l=||v||>0, normalization n=v/l gives ||n||^2=(v·v)/l^2=1.
		// Direction is undefined at l=0; small l amplifies input perturbations, so an absolute threshold rejects it.
		// relative is unused here: without an additional scale, it cannot define an absolute neighborhood of zero.
		// Quaternions use the four-dimensional Euclidean norm. Rounding prevents a guarantee of exactly unit length.
		constexpr std::expected<Out, MathError> NormalizeValue(
		    A const& a,
		    Tolerance<Scalar<A>> t
		) noexcept(adl::CheckedNothrow<NormalizeTag, Out, A, Scalar<A>>()) {
			if (!ValidTolerance(t))
				return std::unexpected(MathError::InvalidTolerance);
			if (!Finite(a))
				return std::unexpected(MathError::NonFinite);
			auto length = Norm(a);
			if (!std::isfinite(length))
				return std::unexpected(MathError::NonFinite);
			if (length <= t.absolute)
				return std::unexpected(MathError::Degenerate);
			auto fallback = [&] {
				return std::expected<Out, MathError>{Binary<DivideTag, Out>(a, length)};
			};
			auto result = adl::Checked<NormalizeTag, Out>(fallback, a, t);
			if (result && !Finite(*result))
				return std::unexpected(MathError::NonFinite);
			return result;
		}

		template <MatrixValue Out, MatrixLike A>
		// Gauss-Jordan inverse proof with partial pivoting:
		//   [ A | I ] --identical elementary row operations--> [ E*A | E ].
		// Swapping rows, nonzero row scaling, and adding a multiple of another row each left-multiply by an invertible matrix.
		// By induction, the right half always holds E, the product of all elementary transformations.
		// Step c reduces column c to the unit column e_c while preserving previously completed columns.
		// After N successful steps, the left half is I, so E*A=I; for a square matrix, E=A^(-1).
		// Each work row has 2N elements: the evolving A on the left and the identically transformed I on the right.
		// Pivot selection depends on updated values, so elimination steps cannot be reordered as independent computations.
		constexpr std::expected<Out, MathError> InverseValue(
		    A const& a,
		    Tolerance<Scalar<A>> t
		) noexcept(adl::CheckedNothrow<InverseTag, Out, A, Scalar<A>>()) {
			using S = Scalar<A>;
			constexpr auto N = Rows<A>();
			if (!ValidTolerance(t))
				return std::unexpected(MathError::InvalidTolerance);
			if (!Finite(a))
				return std::unexpected(MathError::NonFinite);
			// Use partial pivoting with a rejection threshold based on the original matrix scale, not row-scaled pivot scores.
			// long double uses the precision available on the toolchain; it is not necessarily wider than double.
			std::array<long double, N * N * 2> work{};
			long double scale = 0;
			for (std::size_t i = 0; i < N * N; ++i) {
				auto value = static_cast<long double>(Read(a, i));
				work[(i / N) * 2 * N + i % N] = value;
				scale = std::max(scale, std::abs(value));
			}
			for (std::size_t r = 0; r < N; ++r)
				work[r * 2 * N + N + r] = 1;
			auto threshold = static_cast<long double>(t.absolute) + t.relative * scale;
			// threshold=absolute+relative*max|A_ij|.
			// The relative term tracks input scale; the absolute term supplies a floor near zero.
			// This is a heuristic rejection of near-degenerate inputs, not a condition-number estimate or error-bound proof.
			// Singular may mean a pivot is below tolerance, rather than exact algebraic singularity.
			for (std::size_t c = 0; c < N; ++c) {
				// Choose the largest absolute entry in this column among remaining rows to avoid smaller candidate divisors.
				std::size_t pivot = c;
				for (std::size_t r = c + 1; r < N; ++r)
					if (std::abs(work[r * 2 * N + c]) > std::abs(work[pivot * 2 * N + c]))
						pivot = r;
				auto divisor = work[pivot * 2 * N + c];
				if (!std::isfinite(divisor))
					return std::unexpected(MathError::NonFinite);
				if (std::abs(divisor) <= threshold)
					return std::unexpected(MathError::Singular);
				if (pivot != c)
					for (std::size_t k = 0; k < 2 * N; ++k)
						std::swap(work[c * 2 * N + k], work[pivot * 2 * N + k]);
				for (std::size_t k = 0; k < 2 * N; ++k)
					work[c * 2 * N + k] /= divisor;
				// Normalize the pivot row R_c <- R_c / pivot, making A[c,c]=1.
				// For each other row, R_r <- R_r - A[r,c]*R_c zeros its entry in column c.
				// Update both halves together to maintain the [E*A | E] invariant.
				for (std::size_t r = 0; r < N; ++r) {
					if (r == c)
						continue;
					auto factor = work[r * 2 * N + c];
					for (std::size_t k = 0; k < 2 * N; ++k)
						work[r * 2 * N + k] -= factor * work[c * 2 * N + k];
				}
			}
			std::array<S, N * N> values{};
			for (std::size_t i = 0; i < N * N; ++i) {
				auto value = work[(i / N) * 2 * N + N + i % N];
				if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<S>::max())
					return std::unexpected(MathError::NonFinite);
				values[i] = static_cast<S>(value);
			}
			// Validate singularity even when a hook is supplied; hooks cannot bypass the contract.
			auto fallback = [&] {
				return std::expected<Out, MathError>{detail::Build<Out>(values)};
			};
			auto result = adl::Checked<InverseTag, Out>(fallback, a, t);
			if (result && !Finite(*result))
				return std::unexpected(MathError::NonFinite);
			return result;
		}

		template <class Out, class A> struct ConvertFallback {
			A const& a;
			constexpr Out operator()() const noexcept(noexcept(Convert<Out>(a))) {
				return Convert<Out>(a);
			}
		};
		template <class Tag, class Out, class A, class B> struct BinaryFallback {
			A const& a;
			B const& b;
			constexpr Out operator()() const noexcept(noexcept(Binary<Tag, Out>(a, b))) {
				return Binary<Tag, Out>(a, b);
			}
		};
		template <class Tag, class Out, class A> struct UnaryFallback {
			A const& a;
			constexpr Out operator()() const noexcept(noexcept(Unary<Tag, Out>(a))) {
				return Unary<Tag, Out>(a);
			}
		};
		// Stored operands are borrowed directly; expression nodes compute an owning
		// result. Preserve references here so adapting a leaf never copies its value.
		template <class E> consteval bool NothrowEvaluateOperand() {
			if constexpr (requires(E const& e) { e.value; })
				return true;
			else if constexpr (requires { typename E::Shape; })
				return noexcept(std::declval<E const&>().template Evaluate<typename E::Shape>());
			else
				return true;
		}
		template <class E>
		constexpr decltype(auto) EvaluateOperand(E const& expression) noexcept(
		    NothrowEvaluateOperand<E>()
		) {
			if constexpr (requires(E const& e) { e.value; })
				return (expression.value);
			else if constexpr (requires { typename E::Shape; })
				return expression.template Evaluate<typename E::Shape>();
			else
				return (expression);
		}
		template <class E>
		using Evaluated = std::remove_cvref_t<decltype(EvaluateOperand(std::declval<E const&>()))>;
		template <class A, class B> struct DotFallback {
			A const& a;
			B const& b;
			constexpr Scalar<A> operator()() const noexcept(NothrowRead<A>() && NothrowRead<B>()) {
				return DotElements(a, b, std::make_index_sequence<count<A>>{});
			}
		};
		template <class A> struct LengthFallback {
			A const& a;
			constexpr Scalar<A> operator()() const noexcept(noexcept(Norm(a))) {
				return Norm(a);
			}
		};
		template <class Out> struct IdentityFallback {
			constexpr Out operator()() const noexcept(noexcept(
			    detail::Build<Out>(std::declval<std::array<Scalar<Out>, count<Out>> const&>())
			)) {
				std::array<Scalar<Out>, count<Out>> values{};
				for (std::size_t i = 0; i < Rows<Out>(); ++i)
					values[i * Columns<Out>() + i] = Scalar<Out>(1);
				return detail::Build<Out>(values);
			}
		};
		template <MathLike T> consteval bool NothrowCapture() {
			if constexpr (std::is_lvalue_reference_v<T>)
				return true;
			else
				return std::is_nothrow_constructible_v<std::remove_cvref_t<T>, T&&>;
		}
		template <class T> struct Leaf {
			using Shape = Canonical<T>;
			T value;
			template <MathValue Out>
			    requires SameShape<Out, T>
			constexpr Out Evaluate() const noexcept(
			    adl::Nothrow<ConvertTag, Out, ConvertFallback<Out, T>, T>()
			) {
				return adl::Dispatch<ConvertTag, Out>(ConvertFallback<Out, T>{value}, value);
			}
		};
		template <class E>
		concept Expression = requires { typename E::Shape; } && MathValue<typename E::Shape>;

		template <class Tag, class ShapeT, class A, class B> struct BinaryExpression {
			using Shape = ShapeT;
			A left;
			B right;
			template <MathValue Out>
			    requires Compatible<Out, Shape>
			constexpr Out Evaluate() const noexcept(
			    noexcept(EvaluateOperand(left)) && noexcept(EvaluateOperand(right)) &&
			    adl::Nothrow<
			        Tag,
			        Out,
			        BinaryFallback<Tag, Out, Evaluated<A>, Evaluated<B>>,
			        Evaluated<A>,
			        Evaluated<B>>()
			) {
				auto const& a = EvaluateOperand(left);
				auto const& b = EvaluateOperand(right);
				return adl::Dispatch<Tag, Out>(
				    BinaryFallback<Tag, Out, Evaluated<A>, Evaluated<B>>{a, b},
				    a,
				    b
				);
			}
		};
		template <class Tag, class ShapeT, class A> struct UnaryExpression {
			using Shape = ShapeT;
			A source;
			template <MathValue Out>
			    requires Compatible<Out, Shape>
			constexpr Out Evaluate() const noexcept(
			    noexcept(EvaluateOperand(source)) &&
			    adl::Nothrow<Tag, Out, UnaryFallback<Tag, Out, Evaluated<A>>, Evaluated<A>>()
			) {
				auto const& a = EvaluateOperand(source);
				return adl::Dispatch<Tag, Out>(UnaryFallback<Tag, Out, Evaluated<A>>{a}, a);
			}
		};
		template <class Tag, class E> struct CheckedExpression {
			E source;
			Tolerance<Scalar<typename E::Shape>> tolerance;
		};
		template <class E, class S> struct DivisionExpression {
			E source;
			S divisor;
		};
		template <class A, class B>
		concept SameExpressions =
		    Expression<A> && Expression<B> && Compatible<typename A::Shape, typename B::Shape>;
		template <Expression E> using EScalar = Scalar<typename E::Shape>;
		template <Expression E>
		inline constexpr Category kind = Traits<typename E::Shape>::category;
		template <Expression E> inline constexpr auto rows = Rows<typename E::Shape>();
		template <Expression E> inline constexpr auto columns = Columns<typename E::Shape>();
		template <class A, class B>
		concept Multipliable =
		    Expression<A> && Expression<B> && std::same_as<EScalar<A>, EScalar<B>> &&
		    ((kind<A> == Category::Matrix &&
		      (kind<B> == Category::Matrix || kind<B> == Category::Vector) &&
		      columns<A> == rows<B>) ||
		     (kind<A> == Category::Quaternion &&
		      (kind<B> == Category::Quaternion || (kind<B> == Category::Vector && rows<B> == 3))));
	} // namespace detail

	template <detail::Expression E>
	    requires(detail::kind<E> == Category::Vector)
	struct Dot {
		E right;
	};

	template <detail::Expression E>
	    requires(detail::kind<E> == Category::Vector && detail::rows<E> == 3)
	struct Cross {
		E right;
	};

	namespace detail {
		template <MathLike T>
		    requires(
		        std::is_lvalue_reference_v<T> ||
		        std::constructible_from<std::remove_cvref_t<T>, T &&>
		    )
		[[nodiscard]] constexpr auto Capture(T&& value) noexcept(NothrowCapture<T>()) {
			using U = std::remove_cvref_t<T>;
			if constexpr (std::is_lvalue_reference_v<T>)
				return Leaf<U const&>{value};
			else
				return Leaf<U>{std::forward<T>(value)};
		}
	} // namespace detail

	template <VectorLike T>
	[[nodiscard]] constexpr auto AsVector(T&& value) noexcept(detail::NothrowCapture<T>()) {
		return detail::Capture(std::forward<T>(value));
	}
	template <MatrixLike T>
	[[nodiscard]] constexpr auto AsMatrix(T&& value) noexcept(detail::NothrowCapture<T>()) {
		return detail::Capture(std::forward<T>(value));
	}
	template <QuaternionLike T>
	[[nodiscard]] constexpr auto AsQuaternion(T&& value) noexcept(detail::NothrowCapture<T>()) {
		return detail::Capture(std::forward<T>(value));
	}

	template <detail::Expression E, MathValue Out>
	    requires Compatible<Out, typename E::Shape>
	[[nodiscard]] constexpr Out operator>>(E const& expression, ResultType<Out>) noexcept(
	    noexcept(expression.template Evaluate<Out>())
	) {
		return expression.template Evaluate<Out>();
	}
	template <detail::Expression A, detail::Expression B>
	    requires detail::SameExpressions<A, B> && (detail::kind<A> != Category::Quaternion)
	[[nodiscard]] constexpr auto operator+(A a, B b) noexcept(
	    std::is_nothrow_move_constructible_v<A> && std::is_nothrow_move_constructible_v<B>
	) {
		return detail::BinaryExpression<AddTag, typename A::Shape, A, B>{
		    std::move(a),
		    std::move(b)
		};
	}
	template <detail::Expression A, detail::Expression B>
	    requires detail::SameExpressions<A, B> && (detail::kind<A> != Category::Quaternion)
	[[nodiscard]] constexpr auto operator-(A a, B b) noexcept(
	    std::is_nothrow_move_constructible_v<A> && std::is_nothrow_move_constructible_v<B>
	) {
		return detail::BinaryExpression<SubtractTag, typename A::Shape, A, B>{
		    std::move(a),
		    std::move(b)
		};
	}
	template <detail::Expression E>
	    requires(detail::kind<E> != Category::Quaternion)
	[[nodiscard]] constexpr auto operator-(E e) noexcept(std::is_nothrow_move_constructible_v<E>) {
		return detail::UnaryExpression<NegateTag, typename E::Shape, E>{std::move(e)};
	}
	template <detail::Expression E, MathScalar S>
	    requires std::same_as<detail::EScalar<E>, S> && (detail::kind<E> != Category::Quaternion)
	[[nodiscard]] constexpr auto operator*(E e, S scalar) noexcept(
	    std::is_nothrow_move_constructible_v<E>
	) {
		return detail::BinaryExpression<ScaleTag, typename E::Shape, E, S>{std::move(e), scalar};
	}
	template <MathScalar S, detail::Expression E>
	    requires std::same_as<detail::EScalar<E>, S> && (detail::kind<E> != Category::Quaternion)
	[[nodiscard]] constexpr auto operator*(S scalar, E e) noexcept(
	    std::is_nothrow_move_constructible_v<E>
	) {
		return std::move(e) * scalar;
	}
	template <detail::Expression E, MathScalar S>
	    requires std::same_as<detail::EScalar<E>, S> && (detail::kind<E> != Category::Quaternion)
	[[nodiscard]] constexpr auto operator/(E e, S scalar) noexcept(
	    std::is_nothrow_move_constructible_v<E>
	) {
		return detail::DivisionExpression<E, S>{std::move(e), scalar};
	}
	template <detail::Expression E, MathScalar S, MathValue Out>
	    requires Compatible<Out, typename E::Shape>
	[[nodiscard]] constexpr std::expected<Out, MathError>
	operator>>(detail::DivisionExpression<E, S> const& expression, ResultType<Out>) noexcept(
	    noexcept(detail::EvaluateOperand(expression.source)) &&
	    detail::adl::Nothrow<DivideTag, Out,
	        detail::BinaryFallback<DivideTag, Out, detail::Evaluated<E>, S>, detail::Evaluated<E>, S>()
	) {
		// Validate before evaluating the source or entering a backend customization.
		if (expression.divisor == S(0))
			return std::unexpected(MathError::DivisionByZero);
		auto const& value = detail::EvaluateOperand(expression.source);
		return detail::adl::Dispatch<DivideTag, Out>(
		    detail::BinaryFallback<DivideTag, Out, detail::Evaluated<E>, S>{value, expression.divisor},
		    value, expression.divisor);
	}
	template <detail::Expression A, detail::Expression B>
	    requires detail::Multipliable<A, B>
	[[nodiscard]] constexpr auto operator*(A a, B b) noexcept(
	    std::is_nothrow_move_constructible_v<A> && std::is_nothrow_move_constructible_v<B>
	) {
		using S = detail::EScalar<A>;
		if constexpr (detail::kind<A> == Category::Quaternion) {
			if constexpr (detail::kind<B> == Category::Quaternion)
				return detail::BinaryExpression<ComposeRotationTag, typename A::Shape, A, B>{
				    std::move(a),
				    std::move(b)
				};
			else
				return detail::BinaryExpression<RotateTag, typename B::Shape, A, B>{
				    std::move(a),
				    std::move(b)
				};
		} else {
			using Shape = detail::Block<S, detail::kind<B>, detail::rows<A>, detail::columns<B>>;
			return detail::BinaryExpression<MultiplyTag, Shape, A, B>{std::move(a), std::move(b)};
		}
	}
	template <detail::Expression A, detail::Expression B>
	    requires detail::SameExpressions<A, B> && (detail::kind<A> == Category::Vector)
	[[nodiscard]] constexpr auto operator|(A const& a, Dot<B> const& operation) noexcept(
	    noexcept(detail::EvaluateOperand(a)) &&
	    noexcept(detail::EvaluateOperand(operation.right)) &&
	    detail::adl::Nothrow<
	        DotTag,
	        detail::EScalar<A>,
	        detail::DotFallback<detail::Evaluated<A>, detail::Evaluated<B>>,
	        detail::Evaluated<A>,
	        detail::Evaluated<B>>()
	) {
		auto const& av = detail::EvaluateOperand(a);
		auto const& bv = detail::EvaluateOperand(operation.right);
		return detail::adl::Dispatch<DotTag, detail::EScalar<A>>(
		    detail::DotFallback<detail::Evaluated<A>, detail::Evaluated<B>>{av, bv},
		    av,
		    bv
		);
	}
	template <detail::Expression A, detail::Expression B>
	    requires detail::SameExpressions<A, B> &&
	    (detail::kind<A> == Category::Vector && detail::rows<A> == 3)
	[[nodiscard]] constexpr auto operator|(A a, Cross<B> operation) noexcept(
	    std::is_nothrow_move_constructible_v<A> && std::is_nothrow_move_constructible_v<B>
	) {
		return detail::BinaryExpression<CrossTag, typename A::Shape, A, B>{
		    std::move(a),
		    std::move(operation.right)
		};
	}
	template <detail::Expression E>
	    requires(detail::kind<E> == Category::Matrix)
	[[nodiscard]] constexpr auto operator|(E e, TransposeTag) noexcept(
	    std::is_nothrow_move_constructible_v<E>
	) {
		using Shape = detail::
		    Block<detail::EScalar<E>, Category::Matrix, detail::columns<E>, detail::rows<E>>;
		return detail::UnaryExpression<TransposeTag, Shape, E>{std::move(e)};
	}
	template <detail::Expression E>
	    requires(detail::kind<E> == Category::Quaternion)
	[[nodiscard]] constexpr auto operator|(E e, ConjugateTag) noexcept(
	    std::is_nothrow_move_constructible_v<E>
	) {
		return detail::UnaryExpression<ConjugateTag, typename E::Shape, E>{std::move(e)};
	}
	template <detail::Expression E>
	    requires(detail::kind<E> == Category::Vector)
	[[nodiscard]] constexpr auto operator|(E const& e, LengthTag) noexcept(
	    noexcept(detail::EvaluateOperand(e)) &&
	    detail::adl::Nothrow<
	        LengthTag,
	        detail::EScalar<E>,
	        detail::LengthFallback<detail::Evaluated<E>>,
	        detail::Evaluated<E>>()
	) {
		auto const& value = detail::EvaluateOperand(e);
		return detail::adl::Dispatch<LengthTag, detail::EScalar<E>>(
		    detail::LengthFallback<detail::Evaluated<E>>{value},
		    value
		);
	}
	template <detail::Expression E, MathScalar S>
	    requires std::same_as<detail::EScalar<E>, S> && (detail::kind<E> != Category::Matrix)
	[[nodiscard]] constexpr auto operator|(E e, Normalize<S> operation) noexcept(
	    std::is_nothrow_move_constructible_v<E>
	) {
		return detail::CheckedExpression<NormalizeTag, E>{std::move(e), operation.tolerance};
	}
	template <detail::Expression E, MathScalar S>
	    requires std::same_as<detail::EScalar<E>, S> &&
	    (detail::kind<E> == Category::Matrix && detail::rows<E> == detail::columns<E> &&
	     (detail::rows<E> == 3 || detail::rows<E> == 4))
	[[nodiscard]] constexpr auto operator|(E e, Inverse<S> operation) noexcept(
	    std::is_nothrow_move_constructible_v<E>
	) {
		return detail::CheckedExpression<InverseTag, E>{std::move(e), operation.tolerance};
	}
	template <class Tag, detail::Expression E, MathValue Out>
	    requires Compatible<Out, typename E::Shape>
	[[nodiscard]] constexpr std::expected<Out, MathError>
	operator>>(detail::CheckedExpression<Tag, E> const& e, ResultType<Out>) noexcept(
	    noexcept(detail::EvaluateOperand(e.source)) &&
	    detail::adl::CheckedNothrow<Tag, Out, detail::Evaluated<E>, Scalar<Out>>()
	) {
		auto const& value = detail::EvaluateOperand(e.source);
		if constexpr (std::same_as<Tag, NormalizeTag>)
			return detail::NormalizeValue<Out>(value, e.tolerance);
		else
			return detail::InverseValue<Out>(value, e.tolerance);
	}
	template <MatrixValue Out>
	    requires(detail::Rows<Out>() == detail::Columns<Out>())
	[[nodiscard]] constexpr Out operator>>(IdentityTag, ResultType<Out>) noexcept(
	    detail::adl::Nothrow<IdentityTag, Out, detail::IdentityFallback<Out>>()
	) {
		return detail::adl::Dispatch<IdentityTag, Out>(detail::IdentityFallback<Out>{});
	}

	// Expression types live in detail; make the constrained operators visible to their ADL.
	namespace detail {
		using fyuu_math::operator+;
		using fyuu_math::operator-;
		using fyuu_math::operator*;
		using fyuu_math::operator/;
		using fyuu_math::operator|;
		using fyuu_math::operator>>;
	} // namespace detail
} // namespace fyuu_math
