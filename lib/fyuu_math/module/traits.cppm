module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <type_traits>
#include <array>
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
		NonAffine
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
} // namespace fyuu_math
