module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#include <cmath>
#include <array>
#include <concepts>
#include <numbers>
#include <expected>
#endif // !defined(__cpp_lib_modules)

export module fyuu_math:projection;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :traits;
import :concepts;
import :operations;
import :transform;

export namespace fyuu_math {
	// Projection acts on column vectors with view-space Y up. Let s=-1 for right-handed or +1 for left-handed coordinates.
	// Positive forward distance is d=s*z. near/far are always positive distances, not signed z coordinates.
	// Depth range and direction are independent. These options define matrices, not GPU depth comparison/clear state.
	enum class Handedness { Right, Left };
	enum class DepthRange { ZeroToOne, NegativeOneToOne };
	enum class DepthDirection { Forward, Reversed };
	struct ProjectionConvention {
		Handedness handedness;
		DepthRange depth_range;
		DepthDirection depth_direction;
	};
	template <MathScalar S> struct PerspectiveDescriptor {
		S vertical_fov_radians;
		S aspect_ratio;
		S near_plane;
		S far_plane;
	};
	template <MathScalar S> struct OrthographicDescriptor {
		S vertical_size;
		S aspect_ratio;
		S near_plane;
		S far_plane;
	};
	namespace detail {
		constexpr bool ValidConvention(ProjectionConvention c) noexcept {
			return (c.handedness == Handedness::Right || c.handedness == Handedness::Left) &&
			    (c.depth_range == DepthRange::ZeroToOne ||
			     c.depth_range == DepthRange::NegativeOneToOne) &&
			    (c.depth_direction == DepthDirection::Forward ||
			     c.depth_direction == DepthDirection::Reversed);
		}
		template <MathScalar S>
		constexpr std::expected<void, MathError> CheckProjection(
		    S extent,
		    S aspect,
		    S near_plane,
		    S far_plane,
		    ProjectionConvention c
		) noexcept {
			if (!std::isfinite(extent) || !std::isfinite(aspect) || !std::isfinite(near_plane) ||
			    !std::isfinite(far_plane))
				return std::unexpected(MathError::NonFinite);
			if (!ValidConvention(c) || extent <= 0 || aspect <= 0 || near_plane <= 0 ||
			    far_plane <= near_plane)
				return std::unexpected(MathError::InvalidProjection);
			return {};
		}
		// Required near/far NDC depths (dn,df):
		//                        Forward    Reversed
		//     ZeroToOne           (0,1)       (1,0)
		//     NegativeOneToOne   (-1,1)       (1,-1)
		// Reversed depth only swaps boundary conditions; the same equations below cover all four cases.
		template <MathScalar S> constexpr auto DepthEndpoints(ProjectionConvention c) noexcept {
			S near_depth = c.depth_range == DepthRange::ZeroToOne ? S(0) : S(-1);
			S far_depth = 1;
			if (c.depth_direction == DepthDirection::Reversed)
				std::swap(near_depth, far_depth);
			return std::array<S, 2>{near_depth, far_depth};
		}
	} // namespace detail
	// Symmetric perspective projection: vertical field of view theta, aspect ratio aspect, and g=cot(theta/2).
	// At distance d, half-height is d*tan(theta/2), hence y_ndc=g*y/d and x_ndc=g*x/(aspect*d).
	// To obtain this with one homogeneous division, set w_clip=d=s*z and use
	//     P = [ g/aspect  0    0    0 ]
	//         [    0     g    0    0 ]
	//         [    0     0   s*A   B ]
	//         [    0     0    s    0 ].
	// Then z_ndc=(A*d+B)/d=A+B/d. The near/far boundary conditions are
	//     dn=A+B/n, df=A+B/f
	// Subtracting gives B=(dn-df)*n*f/(f-n), then A=df-B/f.
	// Substitution verifies depth dn at d=n and df at d=f, deriving every convention from the same constraints.
	// Requires finite parameters, 0<theta<pi, aspect>0, and 0<n<f; infinite far planes are not supported.
	// The derivation assumes real arithmetic. Computing B in stages avoids forming n*f first, but overflow still needs checking.
	template <MatrixValue Out, MathScalar S>
	    requires MatrixOf<Out, 4, 4> && std::same_as<Scalar<Out>, S>
	[[nodiscard]] constexpr std::expected<Out, MathError> TryPerspective(
	    PerspectiveDescriptor<S> d,
	    ProjectionConvention convention
	) {
		auto valid = detail::CheckProjection(
		    d.vertical_fov_radians,
		    d.aspect_ratio,
		    d.near_plane,
		    d.far_plane,
		    convention
		);
		if (!valid)
			return std::unexpected(valid.error());
		if (d.vertical_fov_radians >= std::numbers::pi_v<S>)
			return std::unexpected(MathError::InvalidProjection);
		auto endpoints = detail::DepthEndpoints<S>(convention);
		S sign = convention.handedness == Handedness::Right ? S(-1) : S(1);
		S y = S(1) / std::tan(d.vertical_fov_radians / S(2));
		S range = d.far_plane - d.near_plane;
		// Evaluate B as (dn-df)*(n/(f-n))*f; recover A from the far-plane boundary condition.
		S b = (endpoints[0] - endpoints[1]) * (d.near_plane / range) * d.far_plane;
		S a = endpoints[1] - b / d.far_plane;
		std::array<S, 16>
		    values{y / d.aspect_ratio, 0, 0, 0, 0, y, 0, 0, 0, 0, sign * a, b, 0, 0, sign, 0};
		return detail::CheckedMake<Out>(values);
	}
	// Centered orthographic projection does not shrink objects with distance; set w_clip=1. The window has height h and width h*aspect.
	// Map x in [-h*aspect/2,h*aspect/2] and y in [-h/2,h/2] linearly to [-1,1].
	// Use z_ndc=alpha*d+beta. Solving dn=alpha*n+beta and df=alpha*f+beta gives
	//     alpha=(df-dn)/(f-n), beta=dn-alpha*n.
	// Since d=s*z, the resulting matrix is
	//     P = [ 2/(h*aspect)  0       0       0   ]
	//         [      0      2/h      0       0   ]
	//         [      0       0    s*alpha   beta ]
	//         [      0       0       0       1   ].
	// Substitution of window edges and near/far distances verifies each endpoint. This interface also requires 0<n<f.
	template <MatrixValue Out, MathScalar S>
	    requires MatrixOf<Out, 4, 4> && std::same_as<Scalar<Out>, S>
	[[nodiscard]] constexpr std::expected<Out, MathError> TryOrthographic(
	    OrthographicDescriptor<S> d,
	    ProjectionConvention convention
	) {
		auto valid = detail::CheckProjection(
		    d.vertical_size,
		    d.aspect_ratio,
		    d.near_plane,
		    d.far_plane,
		    convention
		);
		if (!valid)
			return std::unexpected(valid.error());
		auto endpoints = detail::DepthEndpoints<S>(convention);
		S sign = convention.handedness == Handedness::Right ? S(-1) : S(1);
		S z = (endpoints[1] - endpoints[0]) / (d.far_plane - d.near_plane);
		S offset = endpoints[0] - z * d.near_plane;
		std::array<S, 16> values{
		    (S(2) / d.vertical_size) / d.aspect_ratio,
		    0,
		    0,
		    0,
		    0,
		    S(2) / d.vertical_size,
		    0,
		    0,
		    0,
		    0,
		    sign * z,
		    offset,
		    0,
		    0,
		    0,
		    1
		};
		return detail::CheckedMake<Out>(values);
	}
} // namespace fyuu_math
