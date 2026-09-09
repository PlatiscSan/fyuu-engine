#include <array>
#include <cmath>
#include <cstddef>
#include <span>
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
	float a[4]{1.0f, 2.0f, 3.0f, 4.0f};
	float b[4]{10.0f, 20.0f, 30.0f, 40.0f};
	float c[4]{0.0f, 1.0f, 0.0f, 1.0f};
	std::span<float> as{a, 4}, bs{b, 4}, cs{c, 4};
	float out[4]{};
	std::span<float> outs{out, 4};

	// Add / subtract equal-length dynamic spans into a caller-provided output span.
	auto add = (fm::AsVector(as) + fm::AsVector(bs)) >> outs;
	if (!add || out[0] != 11 || out[1] != 22 || out[2] != 33 || out[3] != 44) return 1;

	// Const-element span input.
	std::span<float const> ac{a, 4};
	auto scaled = (fm::AsVector(ac) * 3.0f) >> outs;
	if (!scaled || out[3] != 12) return 1;

	// Output-length mismatch reports SizeMismatch and leaves the buffer untouched.
	for (auto& v : out) v = -1.0f;
	std::span<float> short_out{out, 2};
	auto bad_out = (fm::AsVector(as) + fm::AsVector(bs)) >> short_out;
	if (bad_out || bad_out.error() != fm::MathError::SizeMismatch) return 1;
	for (auto v : out) if (v != -1.0f) return 1;

	// Operand-length mismatch is detected at evaluation, never by reading out of bounds.
	float s3[3]{1.0f, 2.0f, 3.0f};
	std::span<float> short3{s3, 3};
	for (auto& v : out) v = -1.0f;
	auto bad_ops = (fm::AsVector(as) + fm::AsVector(short3)) >> outs;
	if (bad_ops || bad_ops.error() != fm::MathError::SizeMismatch) return 1;
	for (auto v : out) if (v != -1.0f) return 1;

	// Division by +0 and -0 reports DivisionByZero without touching the output.
	auto zero = fm::AsVector(as) / 0.0f;
	auto minus_zero = fm::AsVector(as) / -0.0f;
	for (auto& v : out) v = -1.0f;
	auto dz = zero >> outs;
	if (dz || dz.error() != fm::MathError::DivisionByZero) return 1;
	auto dzm = minus_zero >> outs;
	if (dzm || dzm.error() != fm::MathError::DivisionByZero) return 1;
	for (auto v : out) if (v != -1.0f) return 1;

	auto divided = (fm::AsVector(as) / 2.0f) >> outs;
	if (!divided || out[0] != 0.5f || out[3] != 2.0f) return 1;

	// Negation, scale in both orders, and a nested expression.
	auto neg = (-fm::AsVector(as)) >> outs;
	if (!neg || out[0] != -1.0f || out[3] != -4.0f) return 1;
	auto scaled_l = (2.0f * fm::AsVector(as)) >> outs;
	if (!scaled_l || out[0] != 2.0f) return 1;
	auto nested = ((fm::AsVector(as) - fm::AsVector(bs)) + (fm::AsVector(cs) * 2.0f)) >> outs;
	if (!nested) return 1;
	if (out[0] != 1 - 10 + 0 || out[1] != 2 - 20 + 2 || out[2] != 3 - 30 + 0 || out[3] != 4 - 40 + 2) return 1;

	// Scalar reductions return expected; a mismatched tree is an error.
	auto len = fm::AsVector(as) | fm::Length;
	if (!len || std::abs(*len - std::hypot(std::hypot(1.0f, 2.0f), std::hypot(3.0f, 4.0f))) > 1e-4f) return 1;
	auto bad_len = (fm::AsVector(as) + fm::AsVector(short3)) | fm::Length;
	if (bad_len || bad_len.error() != fm::MathError::SizeMismatch) return 1;

	auto dot = fm::AsVector(as) | fm::Dot{fm::AsVector(bs)};
	if (!dot || *dot != 10.0f + 40.0f + 90.0f + 160.0f) return 1;
	auto bad_dot = fm::AsVector(as) | fm::Dot{fm::AsVector(short3)};
	if (bad_dot || bad_dot.error() != fm::MathError::SizeMismatch) return 1;

	// Bridge a runtime vector into a fixed-size owning vector when lengths match.
	auto bridged = (fm::AsVector(as) + fm::AsVector(bs)) >> fm::As<std::array<float, 4>>;
	if (!bridged || (*bridged)[3] != 44.0f) return 1;
	auto mismatched_bridge = fm::AsVector(as) >> fm::As<std::array<float, 3>>;
	if (mismatched_bridge || mismatched_bridge.error() != fm::MathError::SizeMismatch) return 1;

	// Empty spans are legal: size 0 vectors with length/dot of 0.
	std::span<float> empty{};
	auto add_empty = (fm::AsVector(empty) + fm::AsVector(empty)) >> empty;
	if (!add_empty) return 1;
	auto empty_len = fm::AsVector(empty) | fm::Length;
	if (!empty_len || *empty_len != 0.0f) return 1;
	auto empty_dot = fm::AsVector(empty) | fm::Dot{fm::AsVector(empty)};
	if (!empty_dot || *empty_dot != 0.0f) return 1;

	return 0;
}
