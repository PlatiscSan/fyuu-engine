#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
import fyuu_math;
namespace fm=fyuu_math;
template<class S,std::size_t N> struct Matrix { S values[N*N]; };
template<class S,std::size_t N> struct fyuu_math::MathTraits<Matrix<S,N>> {
 using Scalar=S;
 static constexpr auto category=Category::Matrix;
 static constexpr bool is_owning=true;
 static constexpr std::size_t rows=N,columns=N;
 static Matrix<S,N> Create() noexcept { return {}; }
 static S Read(Matrix<S,N> const& m,std::size_t r,std::size_t c) noexcept { return m.values[r*N+c]; }
 static void Write(Matrix<S,N>& m,std::size_t r,std::size_t c,S v) noexcept { m.values[r*N+c]=v; }
 static S const* Data(Matrix<S,N> const& m) noexcept { return m.values; }
 static S* Data(Matrix<S,N>& m) noexcept { return m.values; }
};
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
template<class S,std::size_t N> NOINLINE Matrix<S,N> Product(Matrix<S,N> const& a,Matrix<S,N> const& b) {
 return (fm::AsMatrix(a)*fm::AsMatrix(b))>>fm::As<Matrix<S,N>>;
}
template<class S,std::size_t N> void Run(char const* name) {
 Matrix<S,N> a{},b{};
 for(std::size_t i=0;i<N*N;++i) { a.values[i]=S(int(i%13)-6); b.values[i]=S(int(i%7)-3); }
 auto result=Product(a,b);
 for(std::size_t r=0;r<N;++r) for(std::size_t c=0;c<N;++c) {
  S ref=0;
  for(std::size_t k=0;k<N;++k) ref+=a.values[r*N+k]*b.values[k*N+c];
  if(!std::isfinite(result.values[r*N+c]) || std::abs(result.values[r*N+c]-ref)>S(1e-5)) throw std::runtime_error("matrix mismatch");
 }
 b={}; for(std::size_t r=0;r<N;++r) b.values[r*N+(r+1)%N]=1;
 auto initial=a;
 constexpr int iterations=20000*N;
 for(int round=0;round<8;++round) {
  auto start=std::chrono::steady_clock::now();
  for(int i=0;i<iterations;++i) a=Product(a,b);
  auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/iterations;
  double checksum=0;
  for(std::size_t i=0;i<N*N;++i) { if(a.values[i]!=initial.values[i]) throw std::runtime_error("benchmark mismatch"); checksum+=a.values[i]; }
  if(round) std::cout<<name<<','<<ns<<','<<checksum<<'\n';
 }
}
int main() { Run<float,8>("f32x8"); Run<double,8>("f64x8"); Run<float,7>("f32x7"); Run<double,7>("f64x7"); }
