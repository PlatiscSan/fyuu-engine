#include <chrono>
#include <cmath>
#include <iostream>
#include <span>
#include <vector>
#include <stdexcept>
#include <algorithm>
import fyuu_math;
namespace fm=fyuu_math;
struct Matrix { float values[16]; };
struct View { float const* values; };
template<> struct fm::MathTraits<Matrix> {
 using Scalar=float; static constexpr auto category=fm::Category::Matrix;
 static constexpr bool is_owning=true; static constexpr std::size_t rows=4,columns=4;
 static Matrix Create() noexcept {return {};}
 static float Read(Matrix const& v,std::size_t r,std::size_t c) noexcept {return v.values[r*4+c];}
 static void Write(Matrix& v,std::size_t r,std::size_t c,float s) noexcept {v.values[r*4+c]=s;}
 static float const* Data(Matrix const& v) noexcept {return v.values;}
 static float* Data(Matrix& v) noexcept {return v.values;}
};
template<> struct fm::MathTraits<View> {
 using Scalar=float; static constexpr auto category=fm::Category::Matrix;
 static constexpr bool is_owning=false; static constexpr std::size_t rows=4,columns=4;
 static float Read(View const& v,std::size_t r,std::size_t c) noexcept {return v.values[r*4+c];}
 static float const* Data(View const& v) noexcept {return v.values;}
};
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
NOINLINE void Individual(std::span<float const> a,std::span<float const> b,std::span<float> out) {
 for(std::size_t i=0;i<a.size();i+=16) {
  auto result=(fm::AsMatrix(View{a.data()+i})*fm::AsMatrix(View{b.data()}))>>fm::As<Matrix>;
  std::copy_n(result.values,16,out.data()+i);
 }
}
NOINLINE void Batch(std::span<float const> a,std::span<float const> b,std::span<float> out) {
 auto status=(fm::AsMatrixBatch<4,4>(a)*fm::AsMatrixBatch<4,4>(b))>>out;
 if(!status) throw std::runtime_error("batch failed");
}
NOINLINE void Allocate(std::span<float const> a,std::span<float const> b,std::span<float> out) {
 auto value=(fm::AsVector(a)+fm::AsVector(b))>>fm::As<std::vector<float>>;
 if(!value) throw std::runtime_error("allocate failed");
 std::copy(value->begin(),value->end(),out.begin());
}
NOINLINE void Reuse(std::span<float const> a,std::span<float const> b,std::span<float> out) {
 if(!((fm::AsVector(a)+fm::AsVector(b))>>out)) throw std::runtime_error("reuse failed");
}
template<class F> void Measure(char const* name,F function,std::size_t size,bool matrix) {
 std::vector<float> a(size),b(matrix?16:size),out(size),reference(size);
 for(std::size_t i=0;i<size;++i) a[i]=float(int(i%7)-3);
 for(std::size_t i=0;i<b.size();++i) b[i]=float(int(i%5)-2);
 constexpr int iterations=20000;
 for(int round=0;round<8;++round) {
  auto start=std::chrono::steady_clock::now();
  for(int i=0;i<iterations;++i) {a[0]=float(i%3);function(a,b,out);}
  auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/iterations;
  if(matrix) Individual(a,b,reference); else for(std::size_t i=0;i<size;++i) reference[i]=a[i]+b[i];
  double checksum=0;
  for(std::size_t i=0;i<size;++i) { if(out[i]!=reference[i]) throw std::runtime_error("value mismatch"); checksum+=out[i]; }
  if(round) std::cout<<name<<','<<ns<<','<<checksum<<'\n';
 }
}
int main() {
 Measure("allocated64",Allocate,64,false); Measure("reuse64",Reuse,64,false);
 Measure("individual64",Individual,64*16,true); Measure("batch64",Batch,64*16,true);
 Measure("individual256",Individual,256*16,true); Measure("batch256",Batch,256*16,true);
}
