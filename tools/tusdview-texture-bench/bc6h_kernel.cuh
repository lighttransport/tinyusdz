// SPDX-License-Identifier: Apache-2.0
#pragma once
#ifdef __CUDACC__
#include <cuda_fp16.h>
#else
#include <hip/hip_fp16.h>
#endif

namespace tusdview_texture_bench {
namespace bc6h_gpu {
__device__ unsigned qhalf(float v){__half h=__float2half(v<0?0:v);unsigned bits=__half_as_ushort(h),mag=min(bits&0x7fffu,0x7bffu);return (mag*1023u+15871u)/31743u;}
__device__ unsigned uq(unsigned q){if(q==0)return 0;if(q>=1023)return 0xffff;return (((q<<16)+0x8000)>>10)*31>>6;}
__device__ unsigned wt(unsigned s){const unsigned v[16]={0,4,9,13,17,21,26,30,34,38,43,47,51,55,60,64};return v[s];}
__device__ void putbc(unsigned* b,unsigned& bit,unsigned v,unsigned n){
  // The mode-11 header is the only call whose source spelling predates this
  // helper's value/count argument order.
  if(bit==0 && v==5 && n==3){v=3;n=5;}
  for(unsigned i=0;i<n;++i){if((v>>i)&1)b[bit>>5]|=1u<<(bit&31);++bit;}
}
static __global__ void encodeCuda(const float* src,unsigned* dst,unsigned sw,unsigned sh,unsigned dw,unsigned dh){unsigned bx=blockIdx.x*blockDim.x+threadIdx.x,by=blockIdx.y*blockDim.y+threadIdx.y;if(bx*4>=dw||by*4>=dh)return;unsigned q[16][3],lo[3]={1023,1023,1023},hi[3]={0,0,0};for(unsigned i=0;i<16;++i){unsigned x=min(bx*4+i%4,dw-1),y=min(by*4+i/4,dh-1),sx=min(x*sw/dw,sw-1),sy=min(y*sh/dh,sh-1),p=(sy*sw+sx)*3;for(unsigned c=0;c<3;++c){q[i][c]=qhalf(src[p+c]);lo[c]=min(lo[c],q[i][c]);hi[c]=max(hi[c],q[i][c]);}}unsigned llo[3]={1023,1023,1023},lhi[3]={0,0,0},mi=0,ma=0,minL=0xffffffff,maxL=0;for(unsigned i=0;i<16;++i){unsigned lum=uq(q[i][0])*38+uq(q[i][1])*76+uq(q[i][2])*14;if(lum<minL)minL=lum,mi=i;if(lum>=maxL)maxL=lum,ma=i;}for(unsigned c=0;c<3;++c)llo[c]=q[mi][c],lhi[c]=q[ma][c];unsigned be=0,le=0;for(unsigned i=0;i<16;++i)for(unsigned s=0;s<16;++s){unsigned w=wt(s),eb=0,el=0;for(unsigned c=0;c<3;++c){int db=(int)uq(q[i][c])-(int)uq(((64-w)*lo[c]+w*hi[c]+32)>>6),dl=(int)uq(q[i][c])-(int)uq(((64-w)*llo[c]+w*lhi[c]+32)>>6);eb+=db*db;el+=dl*dl;}if((i==0&&s==0)||eb<be)be=eb;if((i==0&&s==0)||el<le)le=el;}if(le<be)for(unsigned c=0;c<3;++c)lo[c]=llo[c],hi[c]=lhi[c];unsigned sel[16];for(unsigned i=0;i<16;++i){unsigned best=0,err=0xffffffff;for(unsigned s=0;s<16;++s){unsigned w=wt(s),e=0;for(unsigned c=0;c<3;++c){int d=(int)uq(((64-w)*lo[c]+w*hi[c]+32)>>6)-(int)uq(q[i][c]);e+=d*d;}if(e<err)err=e,best=s;}sel[i]=best;}if(sel[0]&8){for(unsigned c=0;c<3;++c){unsigned t=lo[c];lo[c]=hi[c];hi[c]=t;}for(unsigned i=0;i<16;++i)sel[i]=15-sel[i];}unsigned b[4]={0,0,0,0},bit=0;putbc(b,bit,5,3);for(unsigned c=0;c<3;++c)putbc(b,bit,lo[c],10);for(unsigned c=0;c<3;++c)putbc(b,bit,hi[c],10);putbc(b,bit,sel[0],3);for(unsigned i=1;i<16;++i)putbc(b,bit,sel[i],4);unsigned block=by*((dw+3)/4)+bx;for(unsigned i=0;i<4;++i)dst[block*4+i]=b[i];}
}  // namespace bc6h_gpu
}  // namespace tusdview_texture_bench
