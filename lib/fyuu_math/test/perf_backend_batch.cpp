#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <Eigen/Core>
import fyuu_math;
import fyuu_math_eigen;
using M=Eigen::Matrix<float,4,4,Eigen::RowMajor>;
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
NOINLINE void Products(M const* a,M const* b,M* out,int count) {
 for(int i=0;i<count;++i) out[i]=fyuu_math::AsMatrix(a[i]*b[i])>>fyuu_math::As<M>;
}
void Run(int count) {
 M a[64],b[64],out[64];
 for(int i=0;i<count;++i) for(int r=0;r<4;++r) for(int c=0;c<4;++c) {
  a[i](r,c)=float((i+r*4+c)%7-3); b[i](r,c)=float((i+r+c)%5-2);
 }
 constexpr int iterations=1000;
 for(int round=0;round<8;++round) {
  double checksum=0;
  auto start=std::chrono::steady_clock::now();
  for(int i=0;i<iterations;++i) { a[0](0,0)=float(i%7); Products(a,b,out,count); checksum+=out[0](0,0); }
  auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/iterations;
  for(int i=0;i<count;++i) for(int r=0;r<4;++r) for(int c=0;c<4;++c) {
   float reference=0; for(int k=0;k<4;++k) reference+=a[i](r,k)*b[i](k,c);
   if(!std::isfinite(out[i](r,c))||std::abs(out[i](r,c)-reference)>1e-5f) throw std::runtime_error("product mismatch");
  }
  if(round) std::cout<<count<<','<<ns<<','<<checksum<<'\n';
 }
}
int main(){
 Eigen::Vector3f a(1,2,3), b(4,5,6);
 auto sum=fyuu_math::AsVector(a+b)>>fyuu_math::As<Eigen::Vector3f>;
 if(sum[0]!=5 || sum[1]!=7 || sum[2]!=9) return 1;
 Eigen::RowVector3f row(1,2,3);
 auto doubled=fyuu_math::AsVector(row*2.0f)>>fyuu_math::As<Eigen::Vector3f>;
 if(doubled[0]!=2 || doubled[2]!=6) return 1;
 Run(1);Run(64);
}
