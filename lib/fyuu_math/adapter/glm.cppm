module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>

#endif // !defined(__cpp_lib_modules)
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

export module fyuu_math_glm;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_math;

export namespace fyuu_math {
	template <glm::length_t N, MathScalar S, glm::qualifier Q>
	struct MathTraits<glm::vec<N, S, Q>> {
		using Scalar = S;
		using T = glm::vec<N, S, Q>;
		static constexpr Category category = Category::Vector;
		static constexpr std::size_t extent = N;
		static constexpr bool is_owning = true;
		static constexpr S Read(T const& v, std::size_t i) noexcept {
			return v[static_cast<glm::length_t>(i)];
		}
		static constexpr void Write(T& v, std::size_t i, S value) noexcept {
			v[static_cast<glm::length_t>(i)] = value;
		}
		static constexpr T Create() noexcept { return T{}; }
	};
	template <glm::length_t C, glm::length_t R, MathScalar S, glm::qualifier Q>
	struct MathTraits<glm::mat<C, R, S, Q>> {
		using Scalar = S;
		using T = glm::mat<C, R, S, Q>;
		static constexpr Category category = Category::Matrix;
		static constexpr std::size_t rows = R, columns = C;
		static constexpr bool is_owning = true;
		static constexpr S Read(T const& m, std::size_t r, std::size_t c) noexcept {
			return m[static_cast<glm::length_t>(c)][static_cast<glm::length_t>(r)];
		}
		static constexpr void Write(T& m, std::size_t r, std::size_t c, S value) noexcept {
			m[static_cast<glm::length_t>(c)][static_cast<glm::length_t>(r)] = value;
		}
		static constexpr T Create() noexcept { return T{}; }
	};
	template <MathScalar S, glm::qualifier Q> struct MathTraits<glm::qua<S, Q>> {
		using Scalar = S;
		using T = glm::qua<S, Q>;
		static constexpr Category category = Category::Quaternion;
		static constexpr bool is_owning = true;
		static constexpr S Read(T const& q, QuaternionComponent component) noexcept {
			switch (component) {
				case QuaternionComponent::X:
					return q.x;
				case QuaternionComponent::Y:
					return q.y;
				case QuaternionComponent::Z:
					return q.z;
				case QuaternionComponent::W:
					return q.w;
			}
			return S(0);
		}
		static constexpr void Write(T& q, QuaternionComponent component, S value) noexcept {
			switch (component) {
				case QuaternionComponent::X:
					q.x = value;
					break;
				case QuaternionComponent::Y:
					q.y = value;
					break;
				case QuaternionComponent::Z:
					q.z = value;
					break;
				case QuaternionComponent::W:
					q.w = value;
					break;
			}
		}
		static constexpr T Create() noexcept { return T{}; }
	};
} // namespace fyuu_math
