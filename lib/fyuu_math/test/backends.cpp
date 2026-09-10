#include <glm/glm.hpp>
#include <Eigen/Core>
import fyuu_math;
import fyuu_math_glm;
import fyuu_math_eigen;

int main() {
	namespace fm = fyuu_math;
	glm::vec3 a(1, 2, 3);
	Eigen::Vector3f b(4, 5, 6);
	auto sum = (fm::AsVector(a) + fm::AsVector(b)) >> fm::As<Eigen::Vector3f>;
	auto converted = fm::AsVector(sum) >> fm::As<glm::vec3>;
	if (converted.x != 5 || converted.y != 7 || converted.z != 9)
		return 1;
	auto matrix = fm::Identity >> fm::As<glm::mat3>;
	auto expression = fm::AsVector(b+b) >> fm::As<glm::vec3>;
	if(expression.x!=8 || expression.y!=10 || expression.z!=12) return 1;
	auto product = (fm::AsMatrix(matrix) * fm::AsVector(b)) >> fm::As<Eigen::Vector3f>;
	return product.isApprox(b) ? 0 : 1;
}
