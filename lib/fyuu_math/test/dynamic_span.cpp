#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>
import fyuu_math;

namespace fm = fyuu_math;

// A dynamic-extent span is a runtime vector, not a compile-time-shaped one.
static_assert(!fm::VectorLike<std::span<float>>);
static_assert(!fm::VectorValue<std::span<float>>);
// Fixed-extent spans keep the static path: the result is a plain value, not an expected.
constexpr float fixed_data[3]{1.0f, 2.0f, 3.0f};
constexpr auto fixed_value =
    (fm::AsVector(std::span<float const, 3>{fixed_data}) * 2.0f) >> fm::As<std::array<float, 3>>;
static_assert(std::same_as<std::remove_cv_t<decltype(fixed_value)>, std::array<float, 3>>);
static_assert(fixed_value[0] == 2.0f && fixed_value[2] == 6.0f);

int main() {
	std::vector<float> a{1.0f, 2.0f, 3.0f, 4.0f};
	std::vector<float> b{10.0f, 20.0f, 30.0f, 40.0f};
	std::vector<float> c{0.0f, 1.0f, 0.0f, 1.0f};

	// lvalue std::vector inputs; results are fresh owning copies.
	auto add = (fm::AsVector(a) + fm::AsVector(b)) >> fm::As<std::vector<float>>;
	if (!add || add->size() != 4) return 1;
	if ((*add)[0] != 11 || (*add)[1] != 22 || (*add)[2] != 33 || (*add)[3] != 44) return 1;

	// The result is independent storage: mutating inputs later does not affect it.
	a[0] = 1000.0f;
	if ((*add)[0] != 11) return 1;
	a[0] = 1.0f;

	// Const lvalue vector and const-span inputs.
	std::vector<float> const ca{1.0f, 2.0f, 3.0f, 4.0f};
	auto scaled = (fm::AsVector(ca) * 3.0f) >> fm::As<std::vector<float>>;
	if (!scaled || (*scaled)[3] != 12) return 1;
	std::span<float const> as_c{ca};
	auto scaled_span = (fm::AsVector(as_c) * 2.0f) >> fm::As<std::vector<float>>;
	if (!scaled_span || (*scaled_span)[2] != 6) return 1;

	// Operand-length mismatch surfaces as SizeMismatch.
	std::vector<float> short3{1.0f, 2.0f, 3.0f};
	auto bad_ops = (fm::AsVector(a) + fm::AsVector(short3)) >> fm::As<std::vector<float>>;
	if (bad_ops || bad_ops.error() != fm::MathError::SizeMismatch) return 1;

	// Division by +0 and -0 reports DivisionByZero.
	auto zero = fm::AsVector(a) / 0.0f;
	auto minus_zero = fm::AsVector(a) / -0.0f;
	auto dz = zero >> fm::As<std::vector<float>>;
	if (dz || dz.error() != fm::MathError::DivisionByZero) return 1;
	auto dzm = minus_zero >> fm::As<std::vector<float>>;
	if (dzm || dzm.error() != fm::MathError::DivisionByZero) return 1;

	auto divided = (fm::AsVector(a) / 3.0f) >> fm::As<std::vector<float>>;
	if (!divided) return 1;
	if (std::abs((*divided)[0] - 1.0f / 3.0f) > 1e-6f) return 1;

	// Negation, scale in both orders, and a nested expression.
	auto neg = (-fm::AsVector(a)) >> fm::As<std::vector<float>>;
	if (!neg || (*neg)[0] != -1.0f || (*neg)[3] != -4.0f) return 1;
	auto scaled_l = (2.0f * fm::AsVector(a)) >> fm::As<std::vector<float>>;
	if (!scaled_l || (*scaled_l)[0] != 2.0f) return 1;
	auto nested = ((fm::AsVector(a) - fm::AsVector(b)) + (fm::AsVector(c) * 2.0f)) >>
	    fm::As<std::vector<float>>;
	if (!nested) return 1;
	if ((*nested)[0] != 1 - 10 + 0 || (*nested)[1] != 2 - 20 + 2 || (*nested)[2] != 3 - 30 + 0 ||
	    (*nested)[3] != 4 - 40 + 2)
		return 1;

	// Scalar reductions return expected; a mismatched tree is an error.
	auto len = fm::AsVector(a) | fm::Length;
	if (!len || std::abs(*len - std::hypot(std::hypot(1.0f, 2.0f), std::hypot(3.0f, 4.0f))) > 1e-4f)
		return 1;
	auto bad_len = (fm::AsVector(a) + fm::AsVector(short3)) | fm::Length;
	if (bad_len || bad_len.error() != fm::MathError::SizeMismatch) return 1;

	auto dot = fm::AsVector(a) | fm::Dot{fm::AsVector(b)};
	if (!dot || *dot != 10.0f + 40.0f + 90.0f + 160.0f) return 1;
	auto bad_dot = fm::AsVector(a) | fm::Dot{fm::AsVector(short3)};
	if (bad_dot || bad_dot.error() != fm::MathError::SizeMismatch) return 1;

	// Fixed-size owning output still works when the runtime length matches.
	auto bridged = (fm::AsVector(a) + fm::AsVector(b)) >> fm::As<std::array<float, 4>>;
	if (!bridged || (*bridged)[3] != 44.0f) return 1;
	auto mismatched_bridge = fm::AsVector(a) >> fm::As<std::array<float, 3>>;
	if (mismatched_bridge || mismatched_bridge.error() != fm::MathError::SizeMismatch) return 1;

	// Empty vectors are legal: size-0 vectors with length/dot of 0 and an empty result.
	std::vector<float> empty{};
	auto add_empty = (fm::AsVector(empty) + fm::AsVector(empty)) >> fm::As<std::vector<float>>;
	if (!add_empty || !add_empty->empty()) return 1;
	auto empty_len = fm::AsVector(empty) | fm::Length;
	if (!empty_len || *empty_len != 0.0f) return 1;
	auto empty_dot = fm::AsVector(empty) | fm::Dot{fm::AsVector(empty)};
	if (!empty_dot || *empty_dot != 0.0f) return 1;

	return 0;
}
