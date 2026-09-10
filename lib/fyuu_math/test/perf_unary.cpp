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
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
NOINLINE Vector<float,64> Negate(Vector<float,64> const& a) { return (-fm::AsVector(a))>>fm::As<Vector<float,64>>; }
NOINLINE Matrix Transpose(Matrix const& a) { return (fm::AsMatrix(a)|fm::Transpose)>>fm::As<Matrix>; }
NOINLINE Quaternion Conjugate(Quaternion const& a) { return (fm::AsQuaternion(a)|fm::Conjugate)>>fm::As<Quaternion>; }
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
NOINLINE Vector<float,64> ScaleAdd(Vector<float,64> const& a) { return (fm::AsVector(a)+fm::AsVector(a)*-2.0f)>>fm::As<Vector<float,64>>; }
NOINLINE Vector<float,64> SumScale(Vector<float,64> const& a) { return ((fm::AsVector(a)+fm::AsVector(a))*-0.5f)>>fm::As<Vector<float,64>>; }
int main() {
 Vector<float,64> vector{}; for(std::size_t i=0;i<64;++i) vector.values[i]=float(i+1);
 Matrix matrix{}; for(std::size_t i=0;i<16;++i) matrix.values[i]=float(i+1);
 Measure("sum_scale64",vector,SumScale);
 Measure("scale_add64",vector,ScaleAdd);
 Measure("negate64",vector,Negate);
 Measure("transpose4",matrix,Transpose);
 Measure("conjugate",Quaternion{{1,2,3,4}},Conjugate);
}


