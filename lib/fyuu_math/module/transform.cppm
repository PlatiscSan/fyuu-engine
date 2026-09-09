module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <utility>
#include <cmath>
#include <array>
#include <concepts>
#include <expected>
#endif // !defined(__cpp_lib_modules)

export module fyuu_math:transform;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :traits;
import :concepts;
import :operations;

export namespace fyuu_math {
	// Matrices act on column vectors and use logical row/column notation, independent of backend storage.
	// Derivations assume real arithmetic; floating-point checks enforce tolerance, not exact orthogonality.
	namespace detail {
		// Homogeneous coordinates express translation as matrix multiplication:
		//     [ L00 L01 L02 tx ] [ vx ]   [ L00*vx + L01*vy + L02*vz + tx*h ]
		//     [ L10 L11 L12 ty ] [ vy ] = [ L10*vx + L11*vy + L12*vz + ty*h ]
		//     [ L20 L21 L22 tz ] [ vz ]   [ L20*vx + L21*vy + L22*vz + tz*h ]
		//     [  0   0   0   1 ] [  h ]   [                h                ]
		// Dimensions: (4-by-4)*(4-by-1) = (4-by-1); the fourth component remains h.
		// Points use h=1 and directions h=0, so only points receive translation. A fold computes each row's dot product.
		template <std::size_t Row, MathLike M, MathLike V, std::size_t... C>
		constexpr Scalar<M> TransformComponent(
		    M const& matrix,
		    V const& vector,
		    Scalar<M> initial,
		    std::index_sequence<C...>
		) noexcept(NothrowRead<M>() && NothrowRead<V>()) {
			return (initial + ... + (Read(matrix, Row * 4 + C) * Read(vector, C)));
		}

		template <MathLike M, MathLike V, std::size_t... R>
		constexpr auto TransformComponents(
		    M const& matrix,
		    V const& vector,
		    bool point,
		    std::index_sequence<R...>
		) {
			return std::array<Scalar<M>, sizeof...(R)>{TransformComponent<R>(
			    matrix,
			    vector,
			    point ? Read(matrix, R * 4 + 3) : Scalar<M>(0),
			    std::make_index_sequence<3>{}
			)...};
		}

		template <MathValue Out, class S, std::size_t N>
		constexpr std::expected<Out, MathError> CheckedMake(std::array<S, N> const& values) {
			for (std::size_t i = 0; i < N; ++i)
				if (!std::isfinite(values[i]))
					return std::unexpected(MathError::NonFinite);
			auto result = detail::Build<Out>(values);
			if (!Finite(result))
				return std::unexpected(MathError::NonFinite);
			return result;
		}
		// Unit quaternions satisfy |q|=1, giving q^{-1}=conjugate(q) and an orthogonal rotation matrix.
		// Relative to the target length 1, the mixed tolerance is absolute + relative*1.
		// This validates without normalizing; accepted length deviations may still introduce orthogonality errors.
		template <QuaternionLike Q>
		constexpr std::expected<void, MathError> CheckRotation(Q const& q, Tolerance<Scalar<Q>> t) {
			if (!ValidTolerance(t))
				return std::unexpected(MathError::InvalidTolerance);
			if (!Finite(q))
				return std::unexpected(MathError::NonFinite);
			auto length = Norm(q);
			if (!std::isfinite(length))
				return std::unexpected(MathError::NonFinite);
			if (std::abs(length - Scalar<Q>(1)) > t.absolute + t.relative)
				return std::unexpected(MathError::NonUnitQuaternion);
			return {};
		}
		// An affine matrix has last row [0 0 0 1]. This check uses a uniform unit-scale tolerance.
		// Once accepted, point/direction transforms return xyz without dividing by the approximately 1/0 homogeneous component.
		template <MatrixLike M> constexpr bool Affine(M const& m, Tolerance<Scalar<M>> t) {
			for (std::size_t i = 0; i < 4; ++i)
				if (std::abs(Read(m, 12 + i) - (i == 3 ? Scalar<M>(1) : Scalar<M>(0))) >
				    t.absolute + t.relative)
					return false;
			return true;
		}
		template <class Out, class M, class V>
		concept TransformCompatible =
		    VectorValue<Out> && VectorOf<Out, 3> && MatrixOf<M, 4, 4> && VectorOf<V, 3> &&
		    std::same_as<Scalar<Out>, Scalar<M>> && std::same_as<Scalar<Out>, Scalar<V>>;

		template <MathValue Out, MathLike M, MathLike V>
		constexpr std::expected<Out, MathError> TransformVector(
		    M const& m,
		    V const& v,
		    Tolerance<Scalar<M>> t,
		    bool point,
		    bool project
		) {
			using S = Scalar<M>;
			if (!ValidTolerance(t))
				return std::unexpected(MathError::InvalidTolerance);
			if (!Finite(m) || !Finite(v))
				return std::unexpected(MathError::NonFinite);
			if (!project && !Affine(m, t))
				return std::unexpected(MathError::NonAffine);
			auto values = TransformComponents(m, v, point, std::make_index_sequence<4>{});
			for (std::size_t i = 0; i < 4; ++i)
				if (!std::isfinite(values[i]))
					return std::unexpected(MathError::NonFinite);
			if (project) {
				// Nonzero multiples of (x,y,z,w) represent the same point; choosing w=1 gives (x/w,y/w,z/w).
				// w=0 represents a point at infinity; division near zero is unstable and rejected using absolute tolerance.
				// This only performs perspective division, not frustum testing; negative w is not automatically culled.
				if (std::abs(values[3]) <= t.absolute)
					return std::unexpected(MathError::Degenerate);
				for (std::size_t i = 0; i < 3; ++i)
					values[i] /= values[3];
			}
			return CheckedMake<Out>(std::array<S, 3>{values[0], values[1], values[2]});
		}
	} // namespace detail

	// Compose M=T*R*S: scale first, then rotate, then translate:
	//     M = [ R00*sx R01*sy R02*sz tx ]
	//         [ R10*sx R11*sy R12*sz ty ]
	//         [ R20*sx R21*sy R22*sz tz ]
	//         [   0      0      0    1 ].
	// For unit q=(u,w), u=(x,y,z), expanding q*(v,0)*conjugate(q) gives
	//     R*v = (w*w-u·u)*v + 2*u*(u·v) + 2*w*(u×v).
	// Substitute each unit basis vector and use w*w+x*x+y*y+z*z=1 to obtain the three columns:
	//     R = [ 1-2(y²+z²)  2(xy-zw)     2(xz+yw)    ]
	//         [ 2(xy+zw)    1-2(x²+z²)   2(yz-xw)    ]
	//         [ 2(xz-yw)    2(yz+xw)     1-2(x²+y²)  ].
	// Right multiplication by diagonal scale multiplies each column by its scale component. Zero/negative scales are allowed.
	// Zero scale makes the matrix singular; construction does not require invertibility, but inverse/normal transforms do.
	template <MatrixValue Out, VectorLike T, QuaternionLike Q, VectorLike V>
	    requires MatrixOf<Out, 4, 4> && VectorOf<T, 3> && VectorOf<V, 3> &&
	    std::same_as<Scalar<Out>, Scalar<T>> && std::same_as<Scalar<Out>, Scalar<Q>> &&
	    std::same_as<Scalar<Out>, Scalar<V>>
	[[nodiscard]] constexpr std::expected<Out, MathError> TryComposeTransform(
	    T const& translation,
	    Q const& rotation,
	    V const& scale,
	    Tolerance<Scalar<Out>> tolerance
	) {
		using S = Scalar<Out>;
		auto valid = detail::CheckRotation(rotation, tolerance);
		if (!valid)
			return std::unexpected(valid.error());
		if (!detail::Finite(translation) || !detail::Finite(scale))
			return std::unexpected(MathError::NonFinite);
		auto x = detail::Read(rotation, 0);
		auto y = detail::Read(rotation, 1);
		auto z = detail::Read(rotation, 2);
		auto w = detail::Read(rotation, 3);
		std::array<S, 16> values{
		    S(1) - S(2) * (y * y + z * z),
		    S(2) * (x * y - z * w),
		    S(2) * (x * z + y * w),
		    detail::Read(translation, 0),
		    S(2) * (x * y + z * w),
		    S(1) - S(2) * (x * x + z * z),
		    S(2) * (y * z - x * w),
		    detail::Read(translation, 1),
		    S(2) * (x * z - y * w),
		    S(2) * (y * z + x * w),
		    S(1) - S(2) * (x * x + y * y),
		    detail::Read(translation, 2),
		    S(0),
		    S(0),
		    S(0),
		    S(1)
		};
		for (std::size_t i = 0; i < 9; ++i)
			values[(i / 3) * 4 + i % 3] *= detail::Read(scale, i % 3);
		return detail::CheckedMake<Out>(values);
	}

	// Camera pose maps camera coordinates to world coordinates by C=[R p; 0 1]; the view matrix reverses this mapping.
	// For a unit rotation, R^{-1}=R^T, hence
	// In block notation, R is 3-by-3, p is 3-by-1, and the lower-left zero is 1-by-3:
	//     V = C^{-1} = [ R^T  -R^T*p ]
	//                  [  0      1   ]
	//     V*C = [ R^T*R  R^T*p-R^T*p ] = I_4.
	//           [   0          1       ]
	// Quaternion conjugation constructs R^T. Translation must be rotated by R^T before negation, not simply set to -p.
	// The pose has no scale; this derivation does not invert a general model matrix with scale or shear.
	template <MatrixValue Out, VectorLike V, QuaternionLike Q>
	    requires MatrixOf<Out, 4, 4> && VectorOf<V, 3> && std::same_as<Scalar<Out>, Scalar<V>> &&
	    std::same_as<Scalar<Out>, Scalar<Q>>
	[[nodiscard]] constexpr std::expected<Out, MathError> TryViewFromPose(
	    V const& position,
	    Q const& rotation,
	    Tolerance<Scalar<Out>> tolerance
	) {
		using S = Scalar<Out>;
		using M = detail::Block<S, Category::Matrix, 4, 4>;
		using Vector = detail::Block<S, Category::Vector, 3>;
		if (!detail::Finite(position))
			return std::unexpected(MathError::NonFinite);
		auto valid = detail::CheckRotation(rotation, tolerance);
		if (!valid)
			return std::unexpected(valid.error());
		auto inverse_rotation = (detail::Capture(rotation) | Conjugate) >> As<detail::Canonical<Q>>;
		auto matrix = TryComposeTransform<M>(
		    Vector{},
		    inverse_rotation,
		    Vector{{S(1), S(1), S(1)}},
		    tolerance
		);
		if (!matrix)
			return std::unexpected(matrix.error());
		auto offsets =
		    detail::TransformComponents(*matrix, position, false, std::make_index_sequence<3>{});
		for (std::size_t r = 0; r < 3; ++r)
			matrix->elements[r * 4 + 3] = -offsets[r];
		return detail::CheckedMake<Out>(matrix->elements);
	}

	template <class Out, class M, class V>
	    requires detail::TransformCompatible<Out, M, V>
	[[nodiscard]] constexpr std::expected<Out, MathError> TryTransformPoint(
	    M const& matrix,
	    V const& point,
	    Tolerance<Scalar<Out>> tolerance
	) {
		return detail::TransformVector<Out>(matrix, point, tolerance, true, false);
	}
	template <class Out, class M, class V>
	    requires detail::TransformCompatible<Out, M, V>
	[[nodiscard]] constexpr std::expected<Out, MathError> TryTransformDirection(
	    M const& matrix,
	    V const& direction,
	    Tolerance<Scalar<Out>> tolerance
	) {
		return detail::TransformVector<Out>(matrix, direction, tolerance, false, false);
	}
	template <class Out, class M, class V>
	    requires detail::TransformCompatible<Out, M, V>
	[[nodiscard]] constexpr std::expected<Out, MathError> TryProjectPoint(
	    M const& matrix,
	    V const& point,
	    Tolerance<Scalar<Out>> tolerance
	) {
		return detail::TransformVector<Out>(matrix, point, tolerance, true, true);
	}
	// Normal transforms must preserve orthogonality to every tangent, rather than treat the normal as a direction.
	// Let L be the linear part and t a tangent with n^T*t=0. After t'=L*t, choose
	//     n'=L^{-T}*n, giving n'^T*t'=n^T*L^{-1}*L*t=n^T*t=0.
	// Translation does not affect tangents, so extract only the upper-left 3-by-3 block. Nonuniform scale usually gives L != L^{-T}.
	// Normalize afterward to restore unit length; inverse/normalization checks reject singular L or degenerate normals.
	// For reflections, this preserves covector semantics; a cross product based on vertex winding also carries the sign of det(L).
	template <class Out, class M, class V>
	    requires detail::TransformCompatible<Out, M, V>
	[[nodiscard]] constexpr std::expected<Out, MathError> TryTransformNormal(
	    M const& matrix,
	    V const& normal,
	    Tolerance<Scalar<Out>> tolerance
	) {
		using S = Scalar<Out>;
		using Linear = detail::Block<S, Category::Matrix, 3, 3>;
		using Vector = detail::Block<S, Category::Vector, 3>;
		if (!detail::ValidTolerance(tolerance))
			return std::unexpected(MathError::InvalidTolerance);
		if (!detail::Finite(matrix) || !detail::Finite(normal))
			return std::unexpected(MathError::NonFinite);
		if (!detail::Affine(matrix, tolerance))
			return std::unexpected(MathError::NonAffine);
		Linear linear;
		for (std::size_t i = 0; i < 9; ++i)
			linear.elements[i] = detail::Read(matrix, (i / 3) * 4 + i % 3);
		auto inverse = (detail::Capture(linear) | Inverse{tolerance}) >> As<Linear>;
		if (!inverse)
			return std::unexpected(inverse.error());
		auto transformed = ((detail::Capture(*inverse) | Transpose) * detail::Capture(normal)) >> As<Vector>;
		return (detail::Capture(transformed) | Normalize{tolerance}) >> As<Out>;
	}
} // namespace fyuu_math
