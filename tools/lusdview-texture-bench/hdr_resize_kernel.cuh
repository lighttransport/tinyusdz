// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace lusdview_texture_bench {
namespace hdr_gpu {

__device__ float hdrAt(const float* s,unsigned x,unsigned y,unsigned w,unsigned h,unsigned c){x=min(x,w-1);y=min(y,h-1);return s[(y*w+x)*3+c];}
static __global__ void resizeCuda(const float* src,float* dst,unsigned sw,unsigned sh,unsigned dw,unsigned dh){unsigned x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;if(x>=dw||y>=dh)return;float sx=(x+.5f)*sw/dw-.5f,sy=(y+.5f)*sh/dh-.5f;unsigned x0=(unsigned)max(0.f,floorf(sx)),y0=(unsigned)max(0.f,floorf(sy));float fx=sx-floorf(sx),fy=sy-floorf(sy);for(unsigned c=0;c<3;++c)dst[(y*dw+x)*3+c]=(1-fy)*((1-fx)*hdrAt(src,x0,y0,sw,sh,c)+fx*hdrAt(src,x0+1,y0,sw,sh,c))+fy*((1-fx)*hdrAt(src,x0,y0+1,sw,sh,c)+fx*hdrAt(src,x0+1,y0+1,sw,sh,c));}

}  // namespace hdr_gpu
}  // namespace lusdview_texture_bench
