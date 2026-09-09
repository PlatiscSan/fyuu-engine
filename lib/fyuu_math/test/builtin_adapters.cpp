#include <array>
#include <cstddef>
#include <span>
import fyuu_math;

namespace fm = fyuu_math;

static_assert(fm::VectorLike<float[3]>);
static_assert(fm::VectorLike<float const[3]>);
static_assert(fm::VectorLike<std::array<float, 3>>);
static_assert(fm::VectorLike<std::span<float, 3>>);
static_assert(fm::VectorLike<std::span<float const, 3>>);
static_assert(fm::MatrixLike<float[2][3]>);
static_assert(fm::MatrixLike<std::array<std::array<float, 3>, 2>>);
static_assert(fm::VectorValue<std::array<float, 3>>);
static_assert(fm::MatrixValue<std::array<std::array<float, 2>, 2>>);
static_assert(fm::LogicalContiguous<float[3]>);
static_assert(fm::LogicalContiguous<std::span<float const, 3>>);
// Raw arrays and spans are borrowed views, never owning outputs.
static_assert(!fm::VectorValue<float[3]>);
static_assert(!fm::VectorValue<std::span<float, 3>>);
static_assert(!fm::MathValue<float[2][2]>);

constexpr float constexpr_a[3]{1.0f, 2.0f, 3.0f};
constexpr auto constexpr_sum =
    (fm::AsVector(constexpr_a) + fm::AsVector(constexpr_a)) >> fm::As<std::array<float, 3>>;
static_assert(constexpr_sum[0] == 2.0f && constexpr_sum[2] == 6.0f);

int main() {
	float a[3]{1.0f, 2.0f, 3.0f};
	float b[3]{4.0f, 5.0f, 6.0f};

	// Raw array inputs, std::array outputs.
	auto sum = (fm::AsVector(a) + fm::AsVector(b)) >> fm::As<std::array<float, 3>>;
	if (sum[0] != 5 || sum[1] != 7 || sum[2] != 9) return 1;

	// Fixed-extent span over raw storage.
	std::span<float const, 3> sa{a};
	auto scaled = (fm::AsVector(sa) * 2.0f) >> fm::As<std::array<float, 3>>;
	if (scaled[0] != 2 || scaled[1] != 4 || scaled[2] != 6) return 1;

	auto dot = fm::AsVector(sa) | fm::Dot{fm::AsVector(b)};
	if (dot != 32.0f) return 1;

	auto cross = (fm::AsVector(a) | fm::Cross{fm::AsVector(b)}) >> fm::As<std::array<float, 3>>;
	if (cross[0] != -3 || cross[1] != 6 || cross[2] != -3) return 1;

	auto zero_div = (fm::AsVector(a) / 0.0f) >> fm::As<std::array<float, 3>>;
	if (zero_div || zero_div.error() != fm::MathError::DivisionByZero) return 1;

	// Matrices come from 2-D raw arrays or nested std::array only.
	float m1[2][2]{{1.0f, 2.0f}, {3.0f, 4.0f}};
	float m2[2][2]{{5.0f, 6.0f}, {7.0f, 8.0f}};
	auto product = (fm::AsMatrix(m1) * fm::AsMatrix(m2)) >> fm::As<std::array<std::array<float, 2>, 2>>;
	if (product[0][0] != 19 || product[0][1] != 22) return 1;
	if (product[1][0] != 43 || product[1][1] != 50) return 1;

	float v[2]{1.0f, 2.0f};
	auto mv = (fm::AsMatrix(m1) * fm::AsVector(v)) >> fm::As<std::array<float, 2>>;
	if (mv[0] != 5 || mv[1] != 11) return 1;

	std::array<std::array<float, 2>, 2> mat{};
	mat[0][0] = 1.0f; mat[0][1] = 2.0f;
	mat[1][0] = 3.0f; mat[1][1] = 4.0f;
	auto transposed = (fm::AsMatrix(mat) | fm::Transpose) >> fm::As<std::array<std::array<float, 2>, 2>>;
	if (transposed[0][1] != 3 || transposed[1][0] != 2) return 1;

	float eye[3][3]{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
	auto inverse = (fm::AsMatrix(eye) | fm::Inverse{fm::Tolerance<float>{1e-6f, 1e-5f}}) >>
	    fm::As<std::array<std::array<float, 3>, 3>>;
	if (!inverse) return 1;
	if ((*inverse)[0][0] != 1 || (*inverse)[1][1] != 1 || (*inverse)[2][2] != 1) return 1;

	std::array<float, 3> zero{0.0f, 0.0f, 0.0f};
	auto degenerate =
	    (fm::AsVector(zero) | fm::Normalize{fm::Tolerance<float>{1e-6f, 1e-5f}}) >> fm::As<std::array<float, 3>>;
	if (degenerate || degenerate.error() != fm::MathError::Degenerate) return 1;

	return 0;
}
