module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <type_traits>

#include <concepts>
#endif // !defined(__cpp_lib_modules)
#include <Eigen/Core>
#include <Eigen/Geometry>

export module fyuu_math_eigen;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_math;

export namespace fyuu_math {
	namespace eigen_detail {
		template <class D>
		concept FixedDense = requires {
			typename D::Scalar;
			typename D::PlainObject;
			requires MathScalar<typename D::Scalar>;
			requires(D::RowsAtCompileTime > 0 && D::ColsAtCompileTime > 0);
		} && std::derived_from<D, Eigen::MatrixBase<D>>;
	} // namespace eigen_detail
	template <eigen_detail::FixedDense D> struct MathTraits<D> {
		using Scalar = typename D::Scalar;
		static constexpr bool vector = D::RowsAtCompileTime == 1 || D::ColsAtCompileTime == 1;
		static constexpr Category category = vector ? Category::Vector : Category::Matrix;
		static constexpr std::size_t rows = D::RowsAtCompileTime;
		static constexpr std::size_t columns = D::ColsAtCompileTime;
		static constexpr std::size_t extent = rows * columns;
		static constexpr bool is_owning = std::same_as<D, typename D::PlainObject>;
		static Scalar Read(D const& value, std::size_t r, std::size_t c) {
			if constexpr (is_owning)
				return value.coeff(static_cast<Eigen::Index>(r), static_cast<Eigen::Index>(c));
			else {
				// Products do not all expose coeff(). Evaluate before reading; never retain references.
				auto evaluated = value.eval();
				return evaluated.coeff(static_cast<Eigen::Index>(r), static_cast<Eigen::Index>(c));
			}
		}
		static Scalar Read(D const& value, std::size_t i)
		    requires vector
		{
			return Read(value, rows == 1 ? 0 : i, rows == 1 ? i : 0);
		}
		// Plain objects are mutable; expression inputs deliberately remain read-only.
		static void Write(D& value, std::size_t r, std::size_t c, Scalar scalar)
		    requires is_owning
		{
			value.coeffRef(static_cast<Eigen::Index>(r), static_cast<Eigen::Index>(c)) = scalar;
		}
		static void Write(D& value, std::size_t i, Scalar scalar)
		    requires(vector && is_owning)
		{
			Write(value, rows == 1 ? 0 : i, rows == 1 ? i : 0, scalar);
		}
		static D Create() requires is_owning { return D{}; }
	};
	template <MathScalar S, int Options> struct MathTraits<Eigen::Quaternion<S, Options>> {
		using Scalar = S;
		using T = Eigen::Quaternion<S, Options>;
		static constexpr Category category = Category::Quaternion;
		static constexpr bool is_owning = true;
		static S Read(T const& q, QuaternionComponent c) {
			switch (c) {
				case QuaternionComponent::X:
					return q.x();
				case QuaternionComponent::Y:
					return q.y();
				case QuaternionComponent::Z:
					return q.z();
				case QuaternionComponent::W:
					return q.w();
			}
			return S(0);
		}
		static void Write(T& q, QuaternionComponent c, S value) {
			switch (c) {
				case QuaternionComponent::X:
					q.x() = value;
					break;
				case QuaternionComponent::Y:
					q.y() = value;
					break;
				case QuaternionComponent::Z:
					q.z() = value;
					break;
				case QuaternionComponent::W:
					q.w() = value;
					break;
			}
		}
		static T Create() { return T{}; }
	};
} // namespace fyuu_math
