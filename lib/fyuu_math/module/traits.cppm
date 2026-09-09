module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <type_traits>
#include <array>
#include <span>
#include <concepts>
#endif // !defined(__cpp_lib_modules)

export module fyuu_math:traits;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_math {
	enum class Category { Vector, Matrix, Quaternion };
	enum class QuaternionComponent { X, Y, Z, W };
	enum class MatrixLayout { RowMajor, ColumnMajor };
	enum class QuaternionLayout { XYZW, WXYZ };
	enum class MathError {
		DivisionByZero,
		NonFinite,
		InvalidTolerance,
		Degenerate,
		Singular,
		NonUnitQuaternion,
		InvalidProjection,
		NonAffine,
		// Runtime-sized dynamic-span vectors only; fixed-size shape mismatches
		// remain compile-time errors and never produce this value.
		SizeMismatch
	};

	// Read/Write access logical components without exposing physical memory order to algorithms.
	// Read/Write(v,r,c) address matrix row r, column c; Create supplies independent output storage.
	// Logical quaternion order is always (x,y,z,w): vector part first, real part last.
	// is_owning must accurately describe data ownership; Concepts cannot prove lifetime semantics.
	template <class T> struct MathTraits;
	template <class T> using Traits = MathTraits<std::remove_cvref_t<T>>;
	template <class T> using Scalar = typename Traits<T>::Scalar;
	template <class T> struct ResultType {
		using Type = T;
	};
	// Both terms must be finite and nonnegative. Unit length uses absolute+relative; inversion uses
	// absolute+relative*max|Aij|; normalization length and projection denominators use absolute.
	// Tolerance defines an algorithm's rejection policy, not a proven bound on computational error.
	template <class S> struct Tolerance {
		S absolute;
		S relative;
	};

	// Internal owning evaluation storage. It has no public arithmetic members.
	namespace detail {
		template <class S, Category K, std::size_t R, std::size_t C = 1> struct Block {
			std::array<S, R * C> elements{};
		};
	} // namespace detail

	template <class S, Category K, std::size_t R, std::size_t C>
	struct MathTraits<detail::Block<S, K, R, C>> {
		using Scalar = S;
		using T = detail::Block<S, K, R, C>;
		static constexpr Category category = K;
		static constexpr std::size_t extent = R;
		static constexpr std::size_t rows = R;
		static constexpr std::size_t columns = C;
		static constexpr bool is_owning = true;
		static constexpr S const* Data(T const& value) noexcept {
			return value.elements.data();
		}
		static constexpr S Read(T const& v, std::size_t i) noexcept {
			return v.elements[i];
		}
		static constexpr S Read(T const& v, std::size_t r, std::size_t c) noexcept {
			return v.elements[r * C + c];
		}
		static constexpr S Read(T const& v, QuaternionComponent c) noexcept {
			return v.elements[static_cast<std::size_t>(c)];
		}
		static constexpr T Create() noexcept { return {}; }
		static constexpr S* Data(T& v) noexcept { return v.elements.data(); }
		static constexpr void Write(T& v, std::size_t i, S s) noexcept { v.elements[i] = s; }
		static constexpr void Write(T& v, std::size_t r, std::size_t c, S s) noexcept { v.elements[r*C+c] = s; }
		static constexpr void Write(T& v, QuaternionComponent c, S s) noexcept { v.elements[static_cast<std::size_t>(c)] = s; }
	};

	// Built-in scalar-container adapters. A flat buffer cannot encode a matrix or
	// quaternion shape by itself, so 1-D forms map to Vector and matrices come only
	// from 2-D/nested forms. Keep BuiltinScalar in sync with MathScalar (concepts.cppm).
	namespace detail {
		template <class S>
		concept BuiltinScalar = std::same_as<S, float> || std::same_as<S, double>;
	} // namespace detail

	// Raw arrays and fixed-extent spans are borrowed inputs only: they cannot be
	// returned from Create(), so they are never owning outputs.
	template <detail::BuiltinScalar S, std::size_t N>
		requires(N > 0)
	struct MathTraits<S[N]> {
		using Scalar = S;
		static constexpr Category category = Category::Vector;
		static constexpr bool is_owning = false;
		static constexpr std::size_t extent = N;
		static constexpr S const* Data(S const (&value)[N]) noexcept { return value; }
		static constexpr S Read(S const (&value)[N], std::size_t i) noexcept { return value[i]; }
	};

	template <detail::BuiltinScalar S, std::size_t R, std::size_t C>
		requires(R > 0 && C > 0)
	struct MathTraits<S[R][C]> {
		using Scalar = S;
		static constexpr Category category = Category::Matrix;
		static constexpr bool is_owning = false;
		static constexpr std::size_t rows = R;
		static constexpr std::size_t columns = C;
		static constexpr S const* Data(S const (&value)[R][C]) noexcept { return &value[0][0]; }
		static constexpr S Read(S const (&value)[R][C], std::size_t r, std::size_t c) noexcept {
			return value[r][c];
		}
	};

	// std::array owns its storage, so these forms also act as writable outputs.
	template <detail::BuiltinScalar S, std::size_t N>
		requires(N > 0)
	struct MathTraits<std::array<S, N>> {
		using Scalar = S;
		using T = std::array<S, N>;
		static constexpr Category category = Category::Vector;
		static constexpr bool is_owning = true;
		static constexpr std::size_t extent = N;
		static constexpr S const* Data(T const& value) noexcept { return value.data(); }
		static constexpr S Read(T const& value, std::size_t i) noexcept { return value[i]; }
		static constexpr T Create() noexcept { return {}; }
		static constexpr S* Data(T& value) noexcept { return value.data(); }
		static constexpr void Write(T& value, std::size_t i, S s) noexcept { value[i] = s; }
	};

	template <detail::BuiltinScalar S, std::size_t R, std::size_t C>
		requires(R > 0 && C > 0)
	struct MathTraits<std::array<std::array<S, C>, R>> {
		using Scalar = S;
		using T = std::array<std::array<S, C>, R>;
		static constexpr Category category = Category::Matrix;
		static constexpr bool is_owning = true;
		static constexpr std::size_t rows = R;
		static constexpr std::size_t columns = C;
		static constexpr S Read(T const& value, std::size_t r, std::size_t c) noexcept {
			return value[r][c];
		}
		static constexpr T Create() noexcept { return {}; }
		static constexpr void Write(T& value, std::size_t r, std::size_t c, S s) noexcept {
			value[r][c] = s;
		}
	};

	template <detail::BuiltinScalar S, std::size_t E>
		requires(E != std::dynamic_extent)
	struct MathTraits<std::span<S, E>> {
		using Scalar = S;
		static constexpr Category category = Category::Vector;
		static constexpr bool is_owning = false;
		static constexpr std::size_t extent = E;
		static constexpr S const* Data(std::span<S, E> const& value) noexcept { return value.data(); }
		static constexpr S Read(std::span<S, E> const& value, std::size_t i) noexcept {
			return value[i];
		}
	};

	template <detail::BuiltinScalar S, std::size_t E>
		requires(E != std::dynamic_extent)
	struct MathTraits<std::span<S const, E>> {
		using Scalar = S;
		static constexpr Category category = Category::Vector;
		static constexpr bool is_owning = false;
		static constexpr std::size_t extent = E;
		static constexpr S const* Data(std::span<S const, E> const& value) noexcept {
			return value.data();
		}
		static constexpr S Read(std::span<S const, E> const& value, std::size_t i) noexcept {
			return value[i];
		}
	};
} // namespace fyuu_math
