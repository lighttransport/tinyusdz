// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace tusdview_texture_bench {
namespace astc_gpu {

__device__ uchar4 astcPx(const uchar4* s,unsigned x,unsigned y,unsigned w,unsigned h){return s[min(y,h-1)*w+min(x,w-1)];}
__device__ float astcChan(uchar4 c,int k){return (k==0?c.x:k==1?c.y:k==2?c.z:c.w)/255.f;}
__device__ unsigned rev8(unsigned v){v=((v&0xf)<<4)|((v>>4)&0xf);v=((v&0x33)<<2)|((v>>2)&0x33);return ((v&0x55)<<1)|((v>>1)&0x55);}
__device__ void astcBits(unsigned* b,int bit,int n,unsigned v){for(int i=0;i<n;++i)if((v>>i)&1)b[(bit+i)>>5]|=1u<<((bit+i)&31);}
static __global__ void encodeCuda(const uchar4* src,unsigned* dst,unsigned w,unsigned h){unsigned bx=blockIdx.x*blockDim.x+threadIdx.x,by=blockIdx.y*blockDim.y+threadIdx.y;if(bx*4>=w||by*4>=h)return;float lo[3]={1,1,1},hi[3]={0,0,0};for(unsigned i=0;i<16;++i){uchar4 c=astcPx(src,bx*4+i%4,by*4+i/4,w,h);for(int k=0;k<3;++k){float v=astcChan(c,k);lo[k]=fminf(lo[k],v);hi[k]=fmaxf(hi[k],v);}}unsigned weights[12];for(unsigned gy=0;gy<3;++gy)for(unsigned gx=0;gx<4;++gx){unsigned sy=min((gy*3+1)/2,3u),ti=sy*4+gx;uchar4 c=astcPx(src,bx*4+ti%4,by*4+ti/4,w,h);float q=0,d=0;for(int k=0;k<3;++k){float v=astcChan(c,k);q+=(v-lo[k])*(hi[k]-lo[k]);d+=(hi[k]-lo[k])*(hi[k]-lo[k]);}float target=d>0?q*64.f/d:0;unsigned best=0;float bd=1e30f;const float lv[4]={0,21,43,64};for(unsigned k=0;k<4;++k){float e=fabsf(target-lv[k]);if(e<bd)bd=e,best=k;}weights[gy*4+gx]=best;}unsigned wb[3]={0,0,0};for(unsigned i=0;i<12;++i)wb[i>>2]|=weights[i]<<((i&3)*2);unsigned b[4]={0,0,0,0};astcBits(b,0,11,34);astcBits(b,11,2,0);astcBits(b,13,4,8);int bit=17;for(int k=0;k<3;++k){astcBits(b,bit,8,(unsigned)(lo[k]*255+.5f));bit+=8;astcBits(b,bit,8,(unsigned)(hi[k]*255+.5f));bit+=8;}b[3]|=rev8(wb[2])<<8;b[3]|=rev8(wb[1])<<16;b[3]|=rev8(wb[0])<<24;unsigned base=(by*((w+3)/4)+bx)*4;for(unsigned i=0;i<4;++i)dst[base+i]=b[i];}

}  // namespace astc_gpu
}  // namespace tusdview_texture_bench
