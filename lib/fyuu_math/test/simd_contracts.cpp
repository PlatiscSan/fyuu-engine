#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
import fyuu_math;

namespace fm = fyuu_math;
template <class S, std::size_t N> struct Vector {
	std::array<S, N> values;
};
template <class S, std::size_t N> struct fyuu_math::MathTraits<Vector<S, N>> {
	using Scalar = S;
	static constexpr Category category = Category::Vector;
	static constexpr bool is_owning = true;
	static constexpr std::size_t extent = N;
	static constexpr S Read(Vector<S, N> const& v, std::size_t i) noexcept {
		return v.values[i];
	}
	static constexpr S const* Data(Vector<S, N> const& v) noexcept {
		return v.values.data();
	}
	static constexpr Vector<S,N> Create() noexcept { return {}; }
 static constexpr void Write(Vector<S,N>& v,std::size_t i,S s) noexcept { v.values[i]=s; }
 static constexpr S* Data(Vector<S,N>& v) noexcept { return v.values.data(); }
};
void Check(double actual, double expected) {
	if (!std::isfinite(actual) || !std::isfinite(expected) || std::abs(actual - expected) > 1e-5 * (1 + std::abs(expected)))
		throw std::runtime_error("numerical mismatch");
}
struct Matrix {
	std::array<float, 16> values;
};
template <> struct fyuu_math::MathTraits<Matrix> {
	using Scalar = float;
	static constexpr Category category = Category::Matrix;
	static constexpr bool is_owning = true;
	static constexpr std::size_t rows = 4, columns = 4;
	static constexpr float Read(Matrix const& m, std::size_t r, std::size_t c) noexcept {
		return m.values[r * 4 + c];
	}
	static constexpr float const* Data(Matrix const& m) noexcept {
		return m.values.data();
	}
	static constexpr Matrix Create() noexcept { return {}; }
 static constexpr void Write(Matrix& v,std::size_t r,std::size_t c,float s) noexcept { v.values[r*4+c]=s; }
 static constexpr float* Data(Matrix& v) noexcept { return v.values.data(); }
};
struct Quaternion {
	std::array<float, 4> values;
};
template <> struct fyuu_math::MathTraits<Quaternion> {
	using Scalar = float;
	static constexpr Category category = Category::Quaternion;
	static constexpr bool is_owning = true;
	static constexpr float Read(Quaternion const& q, QuaternionComponent c) noexcept {
		return q.values[static_cast<std::size_t>(c)];
	}
	static constexpr Quaternion Create() noexcept { return {}; }
 static constexpr void Write(Quaternion& v,QuaternionComponent c,float s) noexcept { v.values[static_cast<std::size_t>(c)]=s; }
};
void TestMatrixQuaternion() {
	Matrix a{}, b{};
	for (std::size_t i = 0; i < 16; ++i) {
		a.values[i] = float(int(i % 7) - 3);
		b.values[i] = float(i % 5 + 1);
	}
	auto result = (fm::AsMatrix(a) * fm::AsMatrix(b)) >> fm::As<Matrix>;
	for (std::size_t r = 0; r < 4; ++r)
		for (std::size_t c = 0; c < 4; ++c) {
			float expected = 0;
			for (std::size_t k = 0; k < 4; ++k)
				expected += a.values[r * 4 + k] * b.values[k * 4 + c];
			Check(result.values[r * 4 + c], expected);
		}
	Quaternion q{{1, 2, 3, 4}};
	Vector<float,4> v{{1,2,3,4}};
	auto mv=(fm::AsMatrix(a)*fm::AsVector(v))>>fm::As<Vector<float,4>>;
	for(std::size_t r=0;r<4;++r) {
		float expected=0;
		for(std::size_t c=0;c<4;++c) expected+=a.values[r*4+c]*v.values[c];
		Check(mv.values[r],expected);
	}
	auto identity=fm::Identity>>fm::As<Matrix>;
	auto transpose=(fm::AsMatrix(a)|fm::Transpose)>>fm::As<Matrix>;
	for(std::size_t r=0;r<4;++r)
		for(std::size_t c=0;c<4;++c) Check(transpose.values[r*4+c],a.values[c*4+r]);
	auto inverse=(fm::AsMatrix(identity)|fm::Inverse{fm::Tolerance<float>{1e-6f,1e-5f}})>>fm::As<Matrix>;
	if(!inverse) throw std::runtime_error("identity inverse failed");
	for(std::size_t i=0;i<16;++i) Check(inverse->values[i],identity.values[i]);
	Quaternion unit{{0,0,0,1}};
	auto composed=(fm::AsQuaternion(unit)*fm::AsQuaternion(q))>>fm::As<Quaternion>;
	for(std::size_t i=0;i<4;++i) Check(composed.values[i],q.values[i]);
	auto conjugate = (fm::AsQuaternion(q) | fm::Conjugate) >> fm::As<Quaternion>;
	for (std::size_t i = 0; i < 4; ++i)
		Check(conjugate.values[i], q.values[i] * (i == 3 ? 1 : -1));
}
template<class S> void SpecialValues() {
	using V=Vector<S,7>;
	V input{{-S(0),S(0),std::numeric_limits<S>::infinity(),-std::numeric_limits<S>::infinity(),std::numeric_limits<S>::quiet_NaN(),S(7),-S(7)}};
	auto divided=(fm::AsVector(input)/S(3))>>fm::As<V>;
	auto negative=(-fm::AsVector(input))>>fm::As<V>;
	if(std::signbit(negative.values[0]) || !std::signbit(negative.values[1]) ||
	   !std::isinf(negative.values[2]) || !std::signbit(negative.values[2]) ||
	   !std::isinf(negative.values[3]) || std::signbit(negative.values[3]) ||
	   !std::isnan(negative.values[4])) throw std::runtime_error("negation special value mismatch");
	auto scaled=(fm::AsVector(input)*S(3))>>fm::As<V>;
	for(auto const& result : {divided.value(),scaled}) {
		if(!std::signbit(result.values[0]) || std::signbit(result.values[1]) ||
		   !std::isinf(result.values[2]) || std::signbit(result.values[2]) ||
		   !std::isinf(result.values[3]) || !std::signbit(result.values[3]) ||
		   !std::isnan(result.values[4])) throw std::runtime_error("special value mismatch");
	}
	Check(divided.value().values[5],S(7)/S(3)); Check(divided.value().values[6],-S(7)/S(3));
	Check(scaled.values[5],S(21)); Check(scaled.values[6],-S(21));
	Vector<S,3> zero{};
	auto normalized=(fm::AsVector(zero)|fm::Normalize{fm::Tolerance<S>{S(1e-6),S(1e-5)}})>>fm::As<Vector<S,3>>;
	if(normalized || normalized.error()!=fm::MathError::Degenerate) throw std::runtime_error("zero normalization accepted");
}
template <class S, std::size_t N> void Run() {
	Vector<S, N> a{}, b{};
	for (std::size_t i = 0; i < N; ++i) {
		a.values[i] = S(int(i % 7) - 3);
		b.values[i] = S(i % 5 + 1);
	}
	auto sum = (fm::AsVector(a) + fm::AsVector(b)) >> fm::As<Vector<S, N>>;
	auto combined = (fm::AsVector(a) + fm::AsVector(b)*S(3)) >> fm::As<Vector<S,N>>;
	auto negative = (-fm::AsVector(a)) >> fm::As<Vector<S,N>>;
	auto sub = (fm::AsVector(a) - fm::AsVector(b)) >> fm::As<Vector<S, N>>;
	auto scale = (fm::AsVector(a) * S(2)) >> fm::As<Vector<S, N>>;
	auto div = (fm::AsVector(a) / S(2)) >> fm::As<Vector<S, N>>;
	S dot = 0;
	for (std::size_t i = 0; i < N; ++i) {
		Check(sum.values[i], a.values[i] + b.values[i]);
		Check(combined.values[i], a.values[i] + b.values[i]*S(3));
		Check(negative.values[i], -a.values[i]);
		Check(sub.values[i], a.values[i] - b.values[i]);
		Check(scale.values[i], a.values[i] * 2);
		Check(div.value().values[i], a.values[i] / 2);
		dot += a.values[i] * b.values[i];
	}
	Check(fm::AsVector(a) | fm::Dot{fm::AsVector(b)}, dot);
	if constexpr (N == 3) {
		auto cross = (fm::AsVector(a) | fm::Cross{fm::AsVector(b)}) >> fm::As<Vector<S, N>>;
		for (std::size_t i = 0; i < 3; ++i)
			Check(
			    cross.values[i],
			    a.values[(i + 1) % 3] * b.values[(i + 2) % 3] -
			        a.values[(i + 2) % 3] * b.values[(i + 1) % 3]
			);
	}
}
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
Vector<float, 64> Add(Vector<float, 64> const& a, Vector<float, 64> const& b) {
	return (fm::AsVector(a) + fm::AsVector(b)) >> fm::As<Vector<float, 64>>;
}
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
Matrix Product(Matrix const& a, Matrix const& b) {
	return (fm::AsMatrix(a)*fm::AsMatrix(b))>>fm::As<Matrix>;
}
void BenchmarkMatrix() {
	Matrix a{{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}};
	Matrix b{{0,1,0,0,0,0,1,0,0,0,0,1,1,0,0,0}};
	auto start=std::chrono::steady_clock::now();
	for(int i=0;i<2000000;++i) a=Product(a,b);
	auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
	Check(a.values[0],1); Check(a.values[15],16);
	std::cout<<"matrix4 product: "<<double(ns)/2000000<<" ns/op; checksum "<<a.values[15]<<'\n';
}
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
NOINLINE Vector<float,64> Negate(Vector<float,64> const& a) { return (-fm::AsVector(a))>>fm::As<Vector<float,64>>; }
NOINLINE Matrix Transpose(Matrix const& a) { return (fm::AsMatrix(a)|fm::Transpose)>>fm::As<Matrix>; }
NOINLINE Quaternion Conjugate(Quaternion const& a) { return (fm::AsQuaternion(a)|fm::Conjugate)>>fm::As<Quaternion>; }
NOINLINE Vector<float,64> ScaleAdd(Vector<float,64> const& a) { return (fm::AsVector(a)+fm::AsVector(a)*-2.0f)>>fm::As<Vector<float,64>>; }
NOINLINE Vector<float,64> SumScale(Vector<float,64> const& a) { return ((fm::AsVector(a)+fm::AsVector(a))*-0.5f)>>fm::As<Vector<float,64>>; }
// Times an operation and asserts it is an involution of the initial value
// (negate/transpose/conjugate applied twice return the input).
template<class T, class F> void Measure(char const* name,T initial,F operation) {
	T value=initial;
	for(int i=0;i<10000;++i) value=operation(value);
	constexpr int iterations=2000000;
	for(int round=0;round<9;++round) {
		auto start=std::chrono::steady_clock::now();
		for(int i=0;i<iterations;++i) value=operation(value);
		auto elapsed=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();
		double checksum=0;
		for(std::size_t i=0;i<value.values.size();++i) { Check(value.values[i],initial.values[i]); checksum+=value.values[i]; }
		std::cout<<name<<','<<elapsed/iterations<<','<<checksum<<'\n';
	}
}
int main() {
	try {
		TestMatrixQuaternion();
		BenchmarkMatrix();
		Vector<float, 64> uvec{};
		for (std::size_t i = 0; i < 64; ++i) uvec.values[i] = float(i + 1);
		Matrix umat{};
		for (std::size_t i = 0; i < 16; ++i) umat.values[i] = float(i + 1);
		Measure("sum_scale64", uvec, SumScale);
		Measure("scale_add64", uvec, ScaleAdd);
		Measure("negate64", uvec, Negate);
		Measure("transpose4", umat, Transpose);
		Measure("conjugate", Quaternion{{1, 2, 3, 4}}, Conjugate);
		SpecialValues<float>(); SpecialValues<double>();
		Run<float,1>(); Run<float,2>(); Run<float,5>(); Run<float,17>(); Run<float,65>();
		Run<double,1>(); Run<double,2>(); Run<double,5>(); Run<double,17>(); Run<double,65>();
		Run<float, 3>();
		Run<float, 4>();
		Run<float, 7>();
		Run<float, 16>();
		Run<double, 3>();
		Run<double, 4>();
		Run<double, 7>();
		Run<double, 16>();
		Vector<float, 64> a{}, b{};
		b.values.fill(0.00001f);
		auto start = std::chrono::steady_clock::now();
		for (int i = 0; i < 2000000; ++i)
			a = Add(a, b);
		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
		              std::chrono::steady_clock::now() - start
		)
		              .count();
		if (a.values[63] < 1)
			throw std::runtime_error("benchmark result missing");
		std::cout << "vector64 add: " << double(ns) / 2000000 << " ns/op; checksum " << a.values[63]
		          << '\n';
		return 0;
	} catch (std::exception const& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
