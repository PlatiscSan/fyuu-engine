#include <array>
#include <cstddef>
import fyuu_math;

struct Vec3 { float values[3]{}; };

template <> struct fyuu_math::MathTraits<Vec3> {
	using Scalar = float;
	using T = Vec3;
	static constexpr Category category = Category::Vector;
	static constexpr bool is_owning = true;
	static constexpr std::size_t extent = 3;
	static constexpr Scalar Read(T const& value, std::size_t i) noexcept { return value.values[i]; }
	static constexpr Scalar const* Data(T const& value) noexcept { return value.values; }
	static constexpr T Create() noexcept { return {}; }
 static constexpr void Write(T& v,std::size_t i,float s) noexcept { v.values[i]=s; }
};

struct CustomOutput {
	float x, padding, y, z;
	CustomOutput() = delete;
	constexpr explicit CustomOutput(int) : x(0), padding(99), y(0), z(0) {}
};
template <> struct fyuu_math::MathTraits<CustomOutput> {
	using Scalar = float;
	static constexpr auto category = fyuu_math::Category::Vector;
	static constexpr bool is_owning = true;
	static constexpr std::size_t extent = 3;
	static constexpr CustomOutput Create() noexcept { return CustomOutput(0); }
	static constexpr float Read(CustomOutput const& v, std::size_t i) noexcept {
		return i == 0 ? v.x : i == 1 ? v.y : v.z;
	}
	static constexpr void Write(CustomOutput& v, std::size_t i, float s) noexcept {
		if(i == 0) v.x = s; else if(i == 1) v.y = s; else v.z = s;
	}
};
static_assert(fyuu_math::VectorValue<CustomOutput>);
static_assert(fyuu_math::VectorValue<Vec3>);
static_assert(fyuu_math::VectorOf<Vec3, 3>);
static_assert(fyuu_math::MathValue<Vec3>);
static_assert(fyuu_math::Traits<Vec3>::is_owning);

int main() {
	Vec3 value{{1.0f, 2.0f, 3.0f}};
	auto custom = (fyuu_math::AsVector(value) * 3.0f) >> fyuu_math::As<CustomOutput>;
	if(custom.x != 3 || custom.y != 6 || custom.z != 9 || custom.padding != 99) return 1;
	auto borrowed = fyuu_math::AsVector(value);
	for (float divisor : {0.0f, -0.0f}) {
		auto failure = (borrowed / divisor) >> fyuu_math::As<Vec3>;
		if (failure || failure.error() != fyuu_math::MathError::DivisionByZero) return 1;
	}
	value.values[0] = 4;
	auto result = (borrowed * 2.0f) >> fyuu_math::As<Vec3>;
	if (result.values[0] != 8 || result.values[2] != 6) return 1;
	auto owned = fyuu_math::AsVector(Vec3{{7,8,9}});
	auto saved = owned >> fyuu_math::As<Vec3>;
	if (saved.values[0] != 7 || saved.values[2] != 9) return 1;
	return 0;
}

constexpr auto scaled = (fyuu_math::AsVector(Vec3{{1,2,3}}) * 2.0f) >> fyuu_math::As<Vec3>;
static_assert(scaled.values[0] == 2 && scaled.values[2] == 6);
constexpr auto invalid = (fyuu_math::AsVector(Vec3{{1,2,3}}) / 0.0f) >> fyuu_math::As<Vec3>;
static_assert(!invalid && invalid.error() == fyuu_math::MathError::DivisionByZero);
