#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <Eigen/Core>
import fyuu_math;
import fyuu_math_eigen;
namespace fm=fyuu_math;
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
template<int N,int Step> NOINLINE float Vector(float const* data) {
 using V=Eigen::Matrix<float,N,1>;
 Eigen::Map<V const,0,Eigen::InnerStride<Step>> view(data);
 auto out=fm::AsVector(view)>>fm::As<V>;
 return out.sum();
}
NOINLINE float Matrix(float const* data) {
 using M=Eigen::Matrix<float,8,8,Eigen::RowMajor>;
 Eigen::Map<M const,0,Eigen::Stride<10,1>> view(data);
 auto out=fm::AsMatrix(view)>>fm::As<M>;
 return out.sum();
}
void Validate() {
 float data[128]; for(int i=0;i<128;++i) data[i]=float(i-20);
 using V=Eigen::Matrix<float,64,1>;
 Eigen::Map<V const,0,Eigen::InnerStride<2>> vector(data);
 auto v=fm::AsVector(vector)>>fm::As<V>;
 for(int i=0;i<64;++i) if(v[i]!=data[2*i]) throw std::runtime_error("vector stride mismatch");
 using M=Eigen::Matrix<float,8,8,Eigen::RowMajor>;
 Eigen::Map<M const,0,Eigen::Stride<10,1>> matrix(data);
 auto m=(fm::AsMatrix(matrix)|fm::Transpose)>>fm::As<M>;
 for(int r=0;r<8;++r) for(int c=0;c<8;++c)
  if(m(r,c)!=data[c*10+r]) throw std::runtime_error("matrix stride mismatch");
 auto block=matrix.template block<3,3>(2,1);
 auto b=fm::AsMatrix(block)>>fm::As<Eigen::Matrix3f>;
 for(int r=0;r<3;++r) for(int c=0;c<3;++c)
  if(b(r,c)!=data[(r+2)*10+c+1]) throw std::runtime_error("block stride mismatch");
 auto product=fm::AsMatrix(b*b)>>fm::As<Eigen::Matrix3f>;
 if(!product.isApprox((b*b).eval())) throw std::runtime_error("product expression mismatch");
}
template<class F> void Measure(char const* name,F f,float expected) {
 float data[128]; for(float& v:data) v=1;
 constexpr int count=20000;
 for(int round=0;round<8;++round) {
  double checksum=0;
  auto start=std::chrono::steady_clock::now();
  for(int i=0;i<count;++i) { data[0]=float(i%3); checksum+=f(data); }
  auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/count;
  double ref=double(count)*(expected-1)+19999;
  if(checksum!=ref) throw std::runtime_error("strided result mismatch");
  if(round) std::cout<<name<<','<<ns<<','<<checksum<<'\n';
 }
}
int main() { Validate(); Measure("vector64_stride2",Vector<64,2>,64); Measure("matrix8_stride10",Matrix,64); }
