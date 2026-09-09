module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <type_traits>
#include <array>
#include <concepts>
#endif // !defined(__cpp_lib_modules)

export module fyuu_math:concepts;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :traits;

export namespace fyuu_math {
	template <class T>
	concept MathScalar = std::same_as<T, float> || std::same_as<T, double>;

	template <class T>
	concept VectorLike = requires(T const& v) {
		typename Scalar<T>;
		requires MathScalar<Scalar<T>>;
		requires Traits<T>::category == Category::Vector;
		requires std::integral<std::remove_cv_t<decltype(Traits<T>::extent)>>;
		requires Traits<T>::extent > 0;
		{ Traits<T>::Read(v, std::size_t{}) } -> std::same_as<Scalar<T>>;
	};
	template <class T>
	concept MatrixLike = requires(T const& v) {
		typename Scalar<T>;
		requires MathScalar<Scalar<T>>;
		requires Traits<T>::category == Category::Matrix;
		requires std::integral<std::remove_cv_t<decltype(Traits<T>::rows)>>;
		requires std::integral<std::remove_cv_t<decltype(Traits<T>::columns)>>;
		requires Traits<T>::rows > 0 && Traits<T>::columns > 0;
		{ Traits<T>::Read(v, std::size_t{}, std::size_t{}) } -> std::same_as<Scalar<T>>;
	};
	template <class T>
	concept QuaternionLike = requires(T const& v) {
		typename Scalar<T>;
		requires MathScalar<Scalar<T>>;
		requires Traits<T>::category == Category::Quaternion;
		{ Traits<T>::Read(v, QuaternionComponent::X) } -> std::same_as<Scalar<T>>;
	};
	// The ...Like concepts read logical components by value, so third-party expressions and
	// borrowed views need no contiguous storage. Concepts validate interface shape; checked
	// operations validate finiteness, nondegeneracy, and unit length at runtime.
	template <class T>
	concept MathLike = VectorLike<T> || MatrixLike<T> || QuaternionLike<T>;
	// Optional SIMD input capability. Data must address every component in the
	// same logical order used by Read: row-major for matrices and XYZW for quaternions.
	// Types without it remain fully supported through Read and a private snapshot.
	template <class T>
	concept LogicalContiguous = MathLike<T> && requires(T const& value) {
		{ Traits<T>::Data(value) } noexcept -> std::same_as<Scalar<T> const*>;
	};
	template <class T, std::size_t N>
	concept VectorOf = VectorLike<T> && Traits<T>::extent == N;
	template <class T, std::size_t R, std::size_t C>
	concept MatrixOf = MatrixLike<T> && Traits<T>::rows == R && Traits<T>::columns == C;

	template <class T>
	concept WritableVector = VectorLike<T> && !std::is_const_v<std::remove_reference_t<T>> &&
	    requires(T& v, Scalar<T> s) {
		    { Traits<T>::Write(v, std::size_t{}, s) } -> std::same_as<void>;
	    };
	template <class T>
	concept WritableMatrix = MatrixLike<T> && !std::is_const_v<std::remove_reference_t<T>> &&
	    requires(T& v, Scalar<T> s) {
		    { Traits<T>::Write(v, std::size_t{}, std::size_t{}, s) } -> std::same_as<void>;
	    };
	template <class T>
	concept WritableQuaternion = QuaternionLike<T> &&
	    !std::is_const_v<std::remove_reference_t<T>> && requires(T& v, Scalar<T> s) {
		    { Traits<T>::Write(v, QuaternionComponent::X, s) } -> std::same_as<void>;
	    };

	namespace detail {
		template <MathLike T> consteval std::size_t Rows() {
			if constexpr (MatrixLike<T>)
				return Traits<T>::rows;
			else if constexpr (VectorLike<T>)
				return Traits<T>::extent;
			else
				return 4;
		}
		template <MathLike T> consteval std::size_t Columns() {
			if constexpr (MatrixLike<T>)
				return Traits<T>::columns;
			else
				return 1;
		}
		template <MathLike T> inline constexpr std::size_t count = Rows<T>() * Columns<T>();
		template <MathLike T>
		using Canonical = Block<Scalar<T>, Traits<T>::category, Rows<T>(), Columns<T>()>;

		template <MathLike T> consteval bool NothrowRead() {
			if constexpr (MatrixLike<T>)
				return noexcept(Traits<T>::Read(std::declval<T const&>(), 0, 0));
			else if constexpr (VectorLike<T>)
				return noexcept(Traits<T>::Read(std::declval<T const&>(), 0));
			else
				return noexcept(Traits<T>::Read(std::declval<T const&>(), QuaternionComponent::X));
		}
		template <MathLike T>
		constexpr Scalar<T> Read(T const& value, std::size_t index) noexcept(NothrowRead<T>()) {
			if constexpr (MatrixLike<T>)
				return Traits<T>::Read(value, index / Columns<T>(), index % Columns<T>());
			else if constexpr (VectorLike<T>)
				return Traits<T>::Read(value, index);
			else
				return Traits<T>::Read(value, static_cast<QuaternionComponent>(index));
		}
		template <MathLike T>
		constexpr auto Snapshot(T const& value) noexcept(noexcept(Read(value, 0))) {
			Canonical<T> result;
			for (std::size_t i = 0; i < count<T>; ++i)
				result.elements[i] = Read(value, i);
			return result;
		}
		template <MathLike T> struct SIMDInput {
			Canonical<T> snapshot;
			Scalar<T> const* data;

			constexpr explicit SIMDInput(T const& value) noexcept(NothrowRead<T>()) {
				if constexpr (LogicalContiguous<T>)
					data = Traits<T>::Data(value);
				else {
					snapshot = Snapshot(value);
					data = snapshot.elements.data();
				}
			}
		};
		// A contiguous input needs only a pointer, never an unused owning buffer.
		template <LogicalContiguous T> struct SIMDInput<T> {
			Scalar<T> const* data;
			constexpr explicit SIMDInput(T const& value) noexcept : data(Traits<T>::Data(value)) {
			}
		};
	} // namespace detail
	// Outputs must construct an independent value from all logical components, avoiding reference results or dangling views.
	// Create establishes independent writable storage; Write maps logical coordinates to physical layout.
	template <class T>
	concept MathValue = MathLike<T> && std::same_as<T, std::remove_cvref_t<T>> && requires {
		requires Traits<T>::is_owning;
	} && (WritableVector<T> || WritableMatrix<T> || WritableQuaternion<T>) && requires {
		{ Traits<T>::Create() } -> std::same_as<T>;
	};
	namespace detail {
		template <MathValue T> consteval bool NothrowOutput() {
			if constexpr (MatrixLike<T>)
				return noexcept(Traits<T>::Create()) && std::is_nothrow_move_constructible_v<T> && noexcept(Traits<T>::Write(std::declval<T&>(),0,0,Scalar<T>{}));
			else if constexpr (VectorLike<T>)
				return noexcept(Traits<T>::Create()) && std::is_nothrow_move_constructible_v<T> && noexcept(Traits<T>::Write(std::declval<T&>(),0,Scalar<T>{}));
			else
				return noexcept(Traits<T>::Create()) && std::is_nothrow_move_constructible_v<T> && noexcept(Traits<T>::Write(std::declval<T&>(),QuaternionComponent::X,Scalar<T>{}));
		}
		template <MathValue T> constexpr void Write(T& v, std::size_t i, Scalar<T> s) noexcept(NothrowOutput<T>()) {
			if constexpr (MatrixLike<T>) Traits<T>::Write(v,i/Columns<T>(),i%Columns<T>(),s);
			else if constexpr (VectorLike<T>) Traits<T>::Write(v,i,s);
			else Traits<T>::Write(v,static_cast<QuaternionComponent>(i),s);
		}
		template <MathValue T, class Values> constexpr T Build(Values const& values) noexcept(NothrowOutput<T>()) {
			auto out = Traits<T>::Create();
			for(std::size_t i=0;i<count<T>;++i) Write(out,i,values[i]);
			return out;
		}
	}
	template <class T>
	concept VectorValue = VectorLike<T> && MathValue<T>;
	template <class T>
	concept MatrixValue = MatrixLike<T> && MathValue<T>;
	template <class T>
	concept QuaternionValue = QuaternionLike<T> && MathValue<T>;
	template <class T>
	concept SIMDCompatible = MathLike<T> && LogicalContiguous<T>;
	template <class A, class B>
	concept SameShape = MathLike<A> && MathLike<B> && Traits<A>::category == Traits<B>::category &&
	    detail::Rows<A>() == detail::Rows<B>() && detail::Columns<A>() == detail::Columns<B>();
	template <class A, class B>
	concept Compatible = SameShape<A, B> && std::same_as<Scalar<A>, Scalar<B>>;
} // namespace fyuu_math
