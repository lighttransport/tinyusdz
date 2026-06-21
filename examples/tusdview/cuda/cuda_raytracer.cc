// SPDX-License-Identifier: Apache-2.0
#include "cuda_raytracer.hh"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "cuew.h"

namespace tusdview {

namespace {

// Flattened BVH node (must match the `Node` struct in the kernel). count>0 -> leaf
// (left = first triangle in the reordered arrays); count==0 -> interior (left/right
// = child node indices -- stored explicitly since a recursive build does not place
// the right child at left+1).
struct Node {
  float bmin[3];
  float bmax[3];
  int left;
  int right;
  int count;
};

// Camera/light block passed by value to the kernel (must match `Cam` there).
struct Cam {
  float invVP[16];
  float camPos[4];
  float lightDir[4];   // xyz light, w depthScale
  float clear[4];      // rgb clear, w RenderMode
  float sceneMin[4];   // position AOV bbox
  float sceneExtent[4];
};

const char* kKernelSrc = R"CUDA(
struct Node { float bmin[3]; float bmax[3]; int left; int right; int count; };
struct Cam { float invVP[16]; float camPos[4]; float lightDir[4]; float clear[4]; float sceneMin[4]; float sceneExtent[4]; };

typedef struct { float x, y, z; } F3;
__device__ F3 mk(float x, float y, float z){ F3 r; r.x=x; r.y=y; r.z=z; return r; }
__device__ F3 sub(F3 a, F3 b){ return mk(a.x-b.x, a.y-b.y, a.z-b.z); }
__device__ F3 add(F3 a, F3 b){ return mk(a.x+b.x, a.y+b.y, a.z+b.z); }
__device__ F3 scale(F3 a, float s){ return mk(a.x*s, a.y*s, a.z*s); }
__device__ float dot3(F3 a, F3 b){ return a.x*b.x + a.y*b.y + a.z*b.z; }
__device__ F3 cross3(F3 a, F3 b){ return mk(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x); }
__device__ F3 norm3(F3 a){ float l=sqrtf(dot3(a,a)); return l>0.f?scale(a,1.f/l):a; }
// Stable distinct color per material id (-1 -> neutral gray).
__device__ F3 idColor(int id){
  if (id<0) return mk(0.45f,0.45f,0.45f);
  unsigned int h=((unsigned int)id+1u)*2654435761u;
  return mk((h&255u)*(1.f/255.f), ((h>>8)&255u)*(1.f/255.f), ((h>>16)&255u)*(1.f/255.f));
}

// Hash RNG + cosine-weighted hemisphere sampler for ambient occlusion.
__device__ unsigned hashU(unsigned x){ x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; x^=x>>16; return x; }
__device__ float rndf(unsigned& s){ s=s*747796405u+2891336453u; unsigned r=((s>>((s>>28)+4u))^s)*277803737u; return float((r>>22)^r)/4294967296.0f; }
__device__ F3 cosHemi(F3 n, float u1, float u2){
  float r=sqrtf(u1), phi=6.2831853f*u2;
  F3 t=norm3(fabsf(n.x)>0.9f?mk(0.f,1.f,0.f):mk(1.f,0.f,0.f));
  F3 b=norm3(cross3(n,t)); t=cross3(b,n);
  return norm3(add(add(scale(t,r*cosf(phi)),scale(b,r*sinf(phi))),scale(n,sqrtf(fmaxf(0.f,1.f-u1)))));
}

// column-major mat4 (invVP) times (ndc.x, ndc.y, z, 1) -> homogeneous xyz/w.
__device__ F3 unproject(const float* m, float nx, float ny, float z){
  float x = m[0]*nx + m[4]*ny + m[8]*z + m[12];
  float y = m[1]*nx + m[5]*ny + m[9]*z + m[13];
  float w3= m[2]*nx + m[6]*ny + m[10]*z + m[14];
  float w = m[3]*nx + m[7]*ny + m[11]*z + m[15];
  return mk(x/w, y/w, w3/w);
}

// slab test; returns true if [0,tmax] overlaps the node AABB along the ray.
__device__ bool hitAabb(const Node* nd, F3 o, F3 inv, float tmax){
  float t1=(nd->bmin[0]-o.x)*inv.x, t2=(nd->bmax[0]-o.x)*inv.x;
  float tmin=fminf(t1,t2), tmx=fmaxf(t1,t2);
  t1=(nd->bmin[1]-o.y)*inv.y; t2=(nd->bmax[1]-o.y)*inv.y;
  tmin=fmaxf(tmin,fminf(t1,t2)); tmx=fminf(tmx,fmaxf(t1,t2));
  t1=(nd->bmin[2]-o.z)*inv.z; t2=(nd->bmax[2]-o.z)*inv.z;
  tmin=fmaxf(tmin,fminf(t1,t2)); tmx=fminf(tmx,fmaxf(t1,t2));
  return tmx>=fmaxf(tmin,0.f) && tmin<tmax;
}

// Moller-Trumbore. Returns t (>0) on hit and the barycentrics, else -1.
__device__ float triHit(const float* tv, F3 o, F3 d, float* bu, float* bv){
  F3 p0=mk(tv[0],tv[1],tv[2]), p1=mk(tv[3],tv[4],tv[5]), p2=mk(tv[6],tv[7],tv[8]);
  F3 e1=sub(p1,p0), e2=sub(p2,p0);
  F3 pv=cross3(d,e2); float det=dot3(e1,pv);
  if (fabsf(det)<1e-12f) return -1.f;
  float inv=1.f/det;
  F3 tv0=sub(o,p0); float u=dot3(tv0,pv)*inv;
  if (u<0.f||u>1.f) return -1.f;
  F3 qv=cross3(tv0,e1); float v=dot3(d,qv)*inv;
  if (v<0.f||u+v>1.f) return -1.f;
  float t=dot3(e2,qv)*inv;
  if (t<=1e-4f) return -1.f;
  *bu=u; *bv=v; return t;
}

// Traverse the BVH for the nearest hit (anyHit=skip on first for shadow rays).
__device__ float traverse(const Node* nodes, const float* tris, F3 o, F3 d,
                          float tmax, int* hitTri, float* bu, float* bv, bool anyHit){
  F3 inv = mk(1.f/d.x, 1.f/d.y, 1.f/d.z);
  int stack[64]; int sp=0; stack[sp++]=0;
  float best=tmax; *hitTri=-1;
  while (sp>0){
    const Node* nd=&nodes[stack[--sp]];
    if (!hitAabb(nd,o,inv,best)) continue;
    if (nd->count>0){
      for (int i=0;i<nd->count;i++){
        int ti=nd->left+i;
        float u,v; float t=triHit(&tris[ti*9],o,d,&u,&v);
        if (t>0.f && t<best){ best=t; *hitTri=ti; *bu=u; *bv=v; if (anyHit) return best; }
      }
    } else {
      stack[sp++]=nd->left;
      stack[sp++]=nd->right;
    }
  }
  return best;
}

// Count BVH node visits for the primary ray (traversal-cost heatmap).
__device__ int traverseSteps(const Node* nodes, const float* tris, F3 o, F3 d, float tmax){
  F3 inv=mk(1.f/d.x,1.f/d.y,1.f/d.z);
  int stack[64]; int sp=0; stack[sp++]=0; float best=tmax; int steps=0;
  while (sp>0){
    const Node* nd=&nodes[stack[--sp]]; steps++;
    if (!hitAabb(nd,o,inv,best)) continue;
    if (nd->count>0){
      for (int i=0;i<nd->count;i++){ int ti=nd->left+i; float u,v; float t=triHit(&tris[ti*9],o,d,&u,&v); if (t>0.f&&t<best) best=t; }
    } else { stack[sp++]=nd->left; stack[sp++]=nd->right; }
  }
  return steps;
}

extern "C" __global__ void trace(const float* tris, const float* nrms,
                                 const float* cols, const unsigned char* geo,
                                 const int* mats, const float* matPbr, int numMats,
                                 const float* uvs, const Node* nodes,
                                 unsigned char* out, int W, int H, Cam cam){
  int px=blockIdx.x*blockDim.x+threadIdx.x;
  int py=blockIdx.y*blockDim.y+threadIdx.y;
  if (px>=W||py>=H) return;
  float u=(px+0.5f)/W, vv=(py+0.5f)/H;
  float ndcx=u*2.f-1.f, ndcy=1.f-vv*2.f;
  F3 nearW=unproject(cam.invVP,ndcx,ndcy,0.f);
  F3 farW =unproject(cam.invVP,ndcx,ndcy,1.f);
  F3 o=nearW; F3 d=norm3(sub(farW,nearW));

  F3 outc = mk(cam.clear[0],cam.clear[1],cam.clear[2]);
  int ht; float bu,bv;
  float t=traverse(nodes,tris,o,d,1e30f,&ht,&bu,&bv,false);
  int rmodePre=(int)(cam.clear[3]+0.5f);
  if (rmodePre==27){  // BVH traversal-cost heatmap (per primary ray, all pixels)
    int steps=traverseSteps(nodes,tris,o,d,1e30f);
    float c=fminf(steps/128.0f,1.0f);
    outc=mk(c, 1.f-fabsf(c-0.5f)*2.f, 1.f-c);
  } else if (ht>=0){
    float w0=1.f-bu-bv;
    F3 hit=add(o,scale(d,t));
    F3 N;
    if (geo[ht]&1){
      const float* tv=&tris[ht*9];
      F3 p0=mk(tv[0],tv[1],tv[2]),p1=mk(tv[3],tv[4],tv[5]),p2=mk(tv[6],tv[7],tv[8]);
      N=norm3(cross3(sub(p1,p0),sub(p2,p0)));
      if (dot3(N,d)>0.f) N=scale(N,-1.f);  // two-sided
    } else {
      const float* nv=&nrms[ht*9];
      N=norm3(mk(nv[0]*w0+nv[3]*bu+nv[6]*bv, nv[1]*w0+nv[4]*bu+nv[7]*bv, nv[2]*w0+nv[5]*bu+nv[8]*bv));
    }
    const float* cv=&cols[ht*9];
    F3 base=mk(cv[0]*w0+cv[3]*bu+cv[6]*bv, cv[1]*w0+cv[4]*bu+cv[7]*bv, cv[2]*w0+cv[5]*bu+cv[8]*bv);

    F3 L=norm3(mk(cam.lightDir[0],cam.lightDir[1],cam.lightDir[2]));
    float diff=fmaxf(dot3(N,L),0.f);
    float shadow=1.f;
    if (diff>0.f){
      int sht; float su,sv;
      F3 so=add(hit,scale(N,1e-3f));
      traverse(nodes,tris,so,L,1e30f,&sht,&su,&sv,true);
      if (sht>=0) shadow=0.f;
    }
    float k=0.25f+0.85f*diff*shadow;
    outc=mk(base.x*k,base.y*k,base.z*k);
    // Render mode (clear[3]): 1=wireframe, 2=shading normal, 3=material-id,
    // 4=geometric normal, 5=uv set 0, 6=depth (t/depthScale in lightDir[3]).
    int rmode=(int)(cam.clear[3]+0.5f);
    if (rmode==1){
      float e=fminf(w0,fminf(bu,bv));
      if (e>0.02f) outc=mk(cam.clear[0],cam.clear[1],cam.clear[2]);
    } else if (rmode==2){
      outc=mk(N.x*0.5f+0.5f, N.y*0.5f+0.5f, N.z*0.5f+0.5f);
    } else if (rmode==3){
      outc=idColor(mats[ht]);
    } else if (rmode==4){
      const float* tv=&tris[ht*9];
      F3 p0=mk(tv[0],tv[1],tv[2]),p1=mk(tv[3],tv[4],tv[5]),p2=mk(tv[6],tv[7],tv[8]);
      F3 Ng=norm3(cross3(sub(p1,p0),sub(p2,p0)));
      if (dot3(Ng,d)>0.f) Ng=scale(Ng,-1.f);
      outc=mk(Ng.x*0.5f+0.5f, Ng.y*0.5f+0.5f, Ng.z*0.5f+0.5f);
    } else if (rmode==5){
      const float* uv=&uvs[ht*6];
      float uu=uv[0]*w0+uv[2]*bu+uv[4]*bv, vv2=uv[1]*w0+uv[3]*bu+uv[5]*bv;
      outc=mk(uu-floorf(uu), vv2-floorf(vv2), 0.f);
    } else if (rmode==6){
      float dd=fminf(fmaxf(t/fmaxf(cam.lightDir[3],1e-3f),0.f),1.f);
      outc=mk(1.f-dd,1.f-dd,1.f-dd);
    } else if (rmode==7){
      outc=base;  // albedo (pre-lighting)
    } else if (rmode==8){  // facing
      const float* tv=&tris[ht*9];
      F3 p0=mk(tv[0],tv[1],tv[2]),p1=mk(tv[3],tv[4],tv[5]),p2=mk(tv[6],tv[7],tv[8]);
      outc=(dot3(cross3(sub(p1,p0),sub(p2,p0)),d)<0.f)?mk(0.1f,0.7f,0.1f):mk(0.7f,0.1f,0.1f);
    } else if (rmode==9){  // roughness (matPbr layout: metal,rough,emitR,emitG,emitB,alpha)
      int mid=mats[ht]; float r=(mid>=0&&mid<numMats)?matPbr[mid*6+1]:0.5f; outc=mk(r,r,r);
    } else if (rmode==10){  // metallic
      int mid=mats[ht]; float mt=(mid>=0&&mid<numMats)?matPbr[mid*6+0]:0.f; outc=mk(mt,mt,mt);
    } else if (rmode==11){  // emissive
      int mid=mats[ht];
      outc=(mid>=0&&mid<numMats)?mk(matPbr[mid*6+2],matPbr[mid*6+3],matPbr[mid*6+4]):mk(0.f,0.f,0.f);
    } else if (rmode==12){  // opacity
      int mid=mats[ht]; float a=(mid>=0&&mid<numMats)?matPbr[mid*6+5]:1.f; outc=mk(a,a,a);
    } else if (rmode==13){  // world position
      outc=mk(fminf(fmaxf((hit.x-cam.sceneMin[0])/fmaxf(cam.sceneExtent[0],1e-4f),0.f),1.f),
              fminf(fmaxf((hit.y-cam.sceneMin[1])/fmaxf(cam.sceneExtent[1],1e-4f),0.f),1.f),
              fminf(fmaxf((hit.z-cam.sceneMin[2])/fmaxf(cam.sceneExtent[2],1e-4f),0.f),1.f));
    } else if (rmode==23){  // uv checker
      const float* uv=&uvs[ht*6];
      float uu=uv[0]*w0+uv[2]*bu+uv[4]*bv, vv2=uv[1]*w0+uv[3]*bu+uv[5]*bv;
      float cx=floorf((uu-floorf(uu))*16.f), cy=floorf((vv2-floorf(vv2))*16.f);
      float kk=fmodf(cx+cy,2.f); float g=0.25f+0.6f*kk; outc=mk(g,g,g);
    } else if (rmode==22){  // tangent (from triangle UV gradient)
      const float* tv=&tris[ht*9]; const float* uv=&uvs[ht*6];
      F3 q0=mk(tv[0],tv[1],tv[2]),q1=mk(tv[3],tv[4],tv[5]),q2=mk(tv[6],tv[7],tv[8]);
      F3 e1=sub(q1,q0), e2=sub(q2,q0);
      float d1x=uv[2]-uv[0], d1y=uv[3]-uv[1], d2x=uv[4]-uv[0], d2y=uv[5]-uv[1];
      float r=d1x*d2y-d2x*d1y;
      F3 T = (fabsf(r)>1e-8f) ? scale(sub(scale(e1,d2y),scale(e2,d1y)),1.f/r) : e1;
      T=norm3(T); outc=mk(T.x*0.5f+0.5f,T.y*0.5f+0.5f,T.z*0.5f+0.5f);
    } else if (rmode==14){  // barycentric
      outc=mk(w0,bu,bv);
    } else if (rmode==15){  // prim id
      outc=idColor(ht);
    } else if (rmode==26){  // instance id: CUDA flattens instances -> gray
      outc=idColor(-1);
    } else if (rmode==19){  // missing normals
      outc=(geo[ht]&1)?mk(0.95f,0.1f,0.85f):mk(0.2f,0.2f,0.2f);
    } else if (rmode==18){  // purpose (bits1-2 of geo byte)
      int p=(geo[ht]>>1)&3;
      outc=(p==1)?mk(0.2f,0.8f,0.3f):(p==2)?mk(0.2f,0.45f,0.95f):(p==3)?mk(0.95f,0.75f,0.1f):mk(0.5f,0.5f,0.5f);
    } else if (rmode==24){  // ray-traced ambient occlusion
      const float* tv=&tris[ht*9];
      F3 q0=mk(tv[0],tv[1],tv[2]),q1=mk(tv[3],tv[4],tv[5]),q2=mk(tv[6],tv[7],tv[8]);
      F3 Ng=norm3(cross3(sub(q1,q0),sub(q2,q0)));
      if (dot3(Ng,d)>0.f) Ng=scale(Ng,-1.f);
      float aoR=fmaxf(cam.lightDir[3],1e-3f)*0.15f;
      unsigned seed=hashU((unsigned)px*1973u+(unsigned)py*9277u+1u);
      const int NS=24; float occ=0.f;
      for (int s=0;s<NS;s++){
        F3 sd=cosHemi(Ng,rndf(seed),rndf(seed));
        F3 so=add(hit,scale(Ng,aoR*1e-2f));
        int sht; float su,sv;
        traverse(nodes,tris,so,sd,aoR,&sht,&su,&sv,true);
        if (sht>=0) occ+=1.f;
      }
      float a=1.f-occ/float(NS); outc=mk(a,a,a);
    } else if (rmode==28){  // soft shadow / sky visibility toward the light
      F3 Lc=norm3(mk(cam.lightDir[0],cam.lightDir[1],cam.lightDir[2]));
      F3 tt=norm3(fabsf(Lc.x)>0.9f?mk(0.f,1.f,0.f):mk(1.f,0.f,0.f));
      F3 bb=norm3(cross3(Lc,tt)); tt=cross3(bb,Lc);
      unsigned seed=hashU((unsigned)px*2719u+(unsigned)py*5051u+7u);
      const int NS=16; float vis=0.f; const float coneR=0.08f;
      for (int s=0;s<NS;s++){
        float ang=rndf(seed)*6.2831853f, rr=sqrtf(rndf(seed))*coneR;
        F3 sd=norm3(add(Lc,add(scale(tt,rr*cosf(ang)),scale(bb,rr*sinf(ang)))));
        F3 so=add(hit,scale(N,1e-3f));
        int sht; float su,sv;
        traverse(nodes,tris,so,sd,1e30f,&sht,&su,&sv,true);
        if (sht<0) vis+=1.f;
      }
      float v=vis/float(NS); outc=mk(v,v,v);
    }
  }
  int idx=(py*W+px)*4;
  out[idx+0]=(unsigned char)(fminf(fmaxf(outc.x,0.f),1.f)*255.f+0.5f);
  out[idx+1]=(unsigned char)(fminf(fmaxf(outc.y,0.f),1.f)*255.f+0.5f);
  out[idx+2]=(unsigned char)(fminf(fmaxf(outc.z,0.f),1.f)*255.f+0.5f);
  out[idx+3]=255;
}
)CUDA";

#define CU_OK(call, what)                                          \
  do {                                                             \
    CUresult _r = (call);                                          \
    if (_r != CUDA_SUCCESS) {                                      \
      const char* _s = nullptr;                                    \
      if (cuGetErrorString) cuGetErrorString(_r, &_s);             \
      if (err) *err = std::string("CUDA ") + (what) + ": " + (_s ? _s : "error"); \
      return false;                                                \
    }                                                              \
  } while (0)

// world (column-major mat4) * point.
inline void XformPt(const float m[16], float x, float y, float z, float o[3]) {
  o[0] = m[0] * x + m[4] * y + m[8] * z + m[12];
  o[1] = m[1] * x + m[5] * y + m[9] * z + m[13];
  o[2] = m[2] * x + m[6] * y + m[10] * z + m[14];
}
inline void XformN(const float m[16], float x, float y, float z, float o[3]) {
  o[0] = m[0] * x + m[4] * y + m[8] * z;
  o[1] = m[1] * x + m[5] * y + m[9] * z;
  o[2] = m[2] * x + m[6] * y + m[10] * z;
}
// 3x4 row-major o2w (instanceXforms) * point.
inline void O2WPt(const float o2w[12], float x, float y, float z, float o[3]) {
  o[0] = o2w[0] * x + o2w[1] * y + o2w[2] * z + o2w[3];
  o[1] = o2w[4] * x + o2w[5] * y + o2w[6] * z + o2w[7];
  o[2] = o2w[8] * x + o2w[9] * y + o2w[10] * z + o2w[11];
}
inline void O2WN(const float o2w[12], float x, float y, float z, float o[3]) {
  o[0] = o2w[0] * x + o2w[1] * y + o2w[2] * z;
  o[1] = o2w[4] * x + o2w[5] * y + o2w[6] * z;
  o[2] = o2w[8] * x + o2w[9] * y + o2w[10] * z;
}

}  // namespace

CudaRayTracer::~CudaRayTracer() {
  if (ctx_) {
    freeScene();
    if (module_) cuModuleUnload(reinterpret_cast<CUmodule>(module_));
    cuCtxDestroy(reinterpret_cast<CUcontext>(ctx_));
  }
}

void CudaRayTracer::freeScene() {
  auto F = [](uintptr_t& p) { if (p) { cuMemFree(static_cast<CUdeviceptr>(p)); p = 0; } };
  F(dTris_); F(dNrms_); F(dCols_); F(dGeo_); F(dMat_); F(dMatPbr_); F(dUV_); F(dNodes_); F(dOut_);
  numMats_ = 0;
  outCap_ = 0; triCount_ = 0; nodeCount_ = 0;
}

bool CudaRayTracer::init(std::string* err) {
  if (ctx_) return true;
  if (cuewInit(CUEW_INIT_CUDA) != CUEW_SUCCESS) {
    if (err) *err = "cuew: CUDA driver not available";
    return false;
  }
  if (cuewInit(CUEW_INIT_NVRTC) != CUEW_SUCCESS) {
    if (err) *err = "cuew: NVRTC not available (needed for runtime kernel compile)";
    return false;
  }
  CU_OK(cuInit(0), "cuInit");
  int count = 0;
  CU_OK(cuDeviceGetCount(&count), "cuDeviceGetCount");
  if (count < 1) { if (err) *err = "no CUDA device"; return false; }
  CUdevice dev;
  CU_OK(cuDeviceGet(&dev, 0), "cuDeviceGet");
  device_ = dev;
  char name[256] = {0};
  cuDeviceGetName(name, sizeof(name), dev);
  deviceName_ = name;
  int major = 0, minor = 0;
  cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, dev);
  cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, dev);
  CUcontext ctx;
  CU_OK(cuCtxCreate(&ctx, 0, dev), "cuCtxCreate");
  ctx_ = ctx;

  // NVRTC compile the kernel for this device's architecture.
  nvrtcProgram prog;
  if (nvrtcCreateProgram(&prog, kKernelSrc, "trace.cu", 0, nullptr, nullptr) !=
      NVRTC_SUCCESS) {
    if (err) *err = "nvrtcCreateProgram failed";
    return false;
  }
  std::string arch = "--gpu-architecture=compute_" + std::to_string(major) +
                     std::to_string(minor);
  const char* opts[] = {arch.c_str(), "--use_fast_math"};
  nvrtcResult nr = nvrtcCompileProgram(prog, 2, opts);
  if (nr != NVRTC_SUCCESS) {
    size_t logSize = 0;
    nvrtcGetProgramLogSize(prog, &logSize);
    std::string log(logSize, '\0');
    if (logSize) nvrtcGetProgramLog(prog, &log[0]);
    if (err) *err = "NVRTC compile failed:\n" + log;
    nvrtcDestroyProgram(&prog);
    return false;
  }
  size_t ptxSize = 0;
  nvrtcGetPTXSize(prog, &ptxSize);
  std::string ptx(ptxSize, '\0');
  nvrtcGetPTX(prog, &ptx[0]);
  nvrtcDestroyProgram(&prog);

  CUmodule mod;
  CU_OK(cuModuleLoadData(&mod, ptx.c_str()), "cuModuleLoadData");
  module_ = mod;
  CUfunction fn;
  CU_OK(cuModuleGetFunction(&fn, mod, "trace"), "cuModuleGetFunction(trace)");
  kernel_ = fn;
  return true;
}

namespace {
// Recursive median-split BVH over triangle centroids. Reorders `idx` so leaves
// reference contiguous ranges; appends nodes to `nodes`. Returns the node index.
int BuildBvh(std::vector<Node>& nodes, std::vector<int>& idx, int first, int count,
             const std::vector<float>& cent, const std::vector<float>& tris) {
  int self = static_cast<int>(nodes.size());
  nodes.push_back({});
  Node nd{};
  float bmin[3] = {1e30f, 1e30f, 1e30f}, bmax[3] = {-1e30f, -1e30f, -1e30f};
  for (int i = 0; i < count; ++i) {
    const float* tv = &tris[idx[first + i] * 9];
    for (int v = 0; v < 3; ++v)
      for (int k = 0; k < 3; ++k) {
        bmin[k] = std::min(bmin[k], tv[v * 3 + k]);
        bmax[k] = std::max(bmax[k], tv[v * 3 + k]);
      }
  }
  for (int k = 0; k < 3; ++k) { nd.bmin[k] = bmin[k]; nd.bmax[k] = bmax[k]; }
  if (count <= 4) {
    nd.left = first;
    nd.right = 0;
    nd.count = count;
    nodes[self] = nd;
    return self;
  }
  // Split on the widest centroid axis at the median.
  float cmin[3] = {1e30f, 1e30f, 1e30f}, cmax[3] = {-1e30f, -1e30f, -1e30f};
  for (int i = 0; i < count; ++i)
    for (int k = 0; k < 3; ++k) {
      float c = cent[idx[first + i] * 3 + k];
      cmin[k] = std::min(cmin[k], c);
      cmax[k] = std::max(cmax[k], c);
    }
  int axis = 0;
  if (cmax[1] - cmin[1] > cmax[axis] - cmin[axis]) axis = 1;
  if (cmax[2] - cmin[2] > cmax[axis] - cmin[axis]) axis = 2;
  int mid = first + count / 2;
  std::nth_element(idx.begin() + first, idx.begin() + mid, idx.begin() + first + count,
                   [&](int a, int b) { return cent[a * 3 + axis] < cent[b * 3 + axis]; });
  int left = BuildBvh(nodes, idx, first, count / 2, cent, tris);
  int right = BuildBvh(nodes, idx, mid, count - count / 2, cent, tris);
  nd.left = left;
  nd.right = right;
  nd.count = 0;
  nodes[self] = nd;
  return self;
}
}  // namespace

bool CudaRayTracer::build(const DrawScene& scene, size_t maxTris, std::string* err) {
  if (!ctx_) { if (err) *err = "CUDA not initialized"; return false; }
  cuCtxSetCurrent(reinterpret_cast<CUcontext>(ctx_));
  freeScene();
  truncated_ = false;

  // Flatten to world-space triangles. Per tri: 9 pos, 9 normals, 9 colors, 1 geo,
  // 1 material id (for material-id visualization).
  std::vector<float> tris, nrms, cols, uvs;
  std::vector<uint8_t> geo;
  std::vector<int> mats;
  const size_t cap = maxTris ? maxTris : (size_t(1) << 62);

  auto emitTri = [&](const float wp[9], const float wn[9], const float wc[9],
                     const float wuv[6], uint8_t g, int matId) {
    tris.insert(tris.end(), wp, wp + 9);
    nrms.insert(nrms.end(), wn, wn + 9);
    cols.insert(cols.end(), wc, wc + 9);
    uvs.insert(uvs.end(), wuv, wuv + 6);
    geo.push_back(g);
    mats.push_back(matId);
  };

  for (const DrawMeshCPU& m : scene.meshes) {
    if (m.vertices.empty() || m.indices.empty()) break;  // partial guard
    const bool instanced = m.instanceCount() > 0;
    const bool hasVtxCol = m.vertexColors.size() == m.vertices.size() * 3;
    // geo byte: bit0 = geometricNormal, bits1-2 = USD purpose id (Purpose AOV).
    const uint8_t g = static_cast<uint8_t>((m.geometricNormal ? 1 : 0) |
                                           ((PurposeId(m.purpose) & 3) << 1));

    // Resolve the mesh's base tint (per submesh material; instanced uses flat/inst).
    auto submeshMat = [&](uint32_t triIdx0) -> const DrawMaterialCPU* {
      for (const DrawSubmesh& s : m.submeshes)
        if (triIdx0 >= s.indexOffset && triIdx0 < s.indexOffset + s.indexCount)
          return (s.materialId >= 0 && size_t(s.materialId) < scene.materials.size())
                     ? &scene.materials[s.materialId]
                     : nullptr;
      return nullptr;
    };
    auto submeshMatId = [&](uint32_t triIdx0) -> int {
      for (const DrawSubmesh& s : m.submeshes)
        if (triIdx0 >= s.indexOffset && triIdx0 < s.indexOffset + s.indexCount)
          return s.materialId;
      return -1;
    };

    const size_t ninst = instanced ? m.instanceCount() : 1;
    for (size_t inst = 0; inst < ninst; ++inst) {
      if (tris.size() / 9 >= cap) { truncated_ = true; break; }
      const float* o2w = instanced ? &m.instanceXforms[inst * 12] : nullptr;
      // Base tint for this instance.
      float tint[3];
      if (instanced) {
        if (m.instanceColors.size() == ninst * 3) {
          tint[0] = m.instanceColors[inst * 3 + 0];
          tint[1] = m.instanceColors[inst * 3 + 1];
          tint[2] = m.instanceColors[inst * 3 + 2];
        } else {
          tint[0] = m.flatColor[0]; tint[1] = m.flatColor[1]; tint[2] = m.flatColor[2];
        }
      } else {
        tint[0] = tint[1] = tint[2] = 0.6f;  // default gray (overridden per submesh)
      }

      for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        if (tris.size() / 9 >= cap) { truncated_ = true; break; }
        float curTint[3] = {tint[0], tint[1], tint[2]};
        if (!instanced) {
          if (const DrawMaterialCPU* mat = submeshMat(static_cast<uint32_t>(t))) {
            curTint[0] = mat->baseColor[0];
            curTint[1] = mat->baseColor[1];
            curTint[2] = mat->baseColor[2];
          }
        }
        float wp[9], wn[9], wc[9], wuv[6];
        for (int k = 0; k < 3; ++k) {
          const DrawVertex& vtx = m.vertices[m.indices[t + k]];
          wuv[k * 2 + 0] = vtx.u;
          wuv[k * 2 + 1] = vtx.v;
          if (instanced) {
            O2WPt(o2w, vtx.px, vtx.py, vtx.pz, &wp[k * 3]);
            O2WN(o2w, vtx.nx, vtx.ny, vtx.nz, &wn[k * 3]);
          } else {
            XformPt(m.world, vtx.px, vtx.py, vtx.pz, &wp[k * 3]);
            XformN(m.world, vtx.nx, vtx.ny, vtx.nz, &wn[k * 3]);
          }
          float dc[3] = {1, 1, 1};
          if (hasVtxCol) {
            const float* c = &m.vertexColors[m.indices[t + k] * 3];
            dc[0] = c[0]; dc[1] = c[1]; dc[2] = c[2];
          }
          wc[k * 3 + 0] = curTint[0] * dc[0];
          wc[k * 3 + 1] = curTint[1] * dc[1];
          wc[k * 3 + 2] = curTint[2] * dc[2];
        }
        emitTri(wp, wn, wc, wuv, g, submeshMatId(static_cast<uint32_t>(t)));
      }
      if (truncated_) break;
    }
    if (truncated_) break;
  }

  triCount_ = tris.size() / 9;
  if (triCount_ == 0) { if (err) *err = "CUDA: no triangles"; return false; }

  // Build the BVH (reorders a triangle-index permutation).
  std::vector<float> cent(triCount_ * 3);
  for (size_t i = 0; i < triCount_; ++i) {
    const float* tv = &tris[i * 9];
    for (int k = 0; k < 3; ++k)
      cent[i * 3 + k] = (tv[0 * 3 + k] + tv[1 * 3 + k] + tv[2 * 3 + k]) / 3.f;
  }
  std::vector<int> idx(triCount_);
  for (size_t i = 0; i < triCount_; ++i) idx[i] = static_cast<int>(i);
  std::vector<Node> nodes;
  nodes.reserve(triCount_ * 2);
  BuildBvh(nodes, idx, 0, static_cast<int>(triCount_), cent, tris);
  nodeCount_ = nodes.size();

  // Reorder tri data into BVH leaf order so leaves reference contiguous ranges.
  std::vector<float> rt(triCount_ * 9), rn(triCount_ * 9), rc(triCount_ * 9),
      ruv(triCount_ * 6);
  std::vector<uint8_t> rg(triCount_);
  std::vector<int> rm(triCount_);
  for (size_t i = 0; i < triCount_; ++i) {
    int s = idx[i];
    std::memcpy(&rt[i * 9], &tris[s * 9], 9 * sizeof(float));
    std::memcpy(&rn[i * 9], &nrms[s * 9], 9 * sizeof(float));
    std::memcpy(&rc[i * 9], &cols[s * 9], 9 * sizeof(float));
    std::memcpy(&ruv[i * 6], &uvs[s * 6], 6 * sizeof(float));
    rg[i] = geo[s];
    rm[i] = mats[s];
  }

  // Upload.
  auto up = [&](const void* host, size_t bytes, uintptr_t* dptr) -> bool {
    CUdeviceptr p;
    CU_OK(cuMemAlloc(&p, bytes), "cuMemAlloc");
    CU_OK(cuMemcpyHtoD(p, host, bytes), "cuMemcpyHtoD");
    *dptr = static_cast<uintptr_t>(p);
    return true;
  };
  if (!up(rt.data(), rt.size() * sizeof(float), &dTris_)) return false;
  if (!up(rn.data(), rn.size() * sizeof(float), &dNrms_)) return false;
  if (!up(rc.data(), rc.size() * sizeof(float), &dCols_)) return false;
  if (!up(rg.data(), rg.size(), &dGeo_)) return false;
  if (!up(rm.data(), rm.size() * sizeof(int), &dMat_)) return false;
  if (!up(ruv.data(), ruv.size() * sizeof(float), &dUV_)) return false;
  if (!up(nodes.data(), nodes.size() * sizeof(Node), &dNodes_)) return false;

  // Per-material PBR scalars indexed by tri matId: metal, rough, emitR/G/B, alpha.
  numMats_ = static_cast<int>(scene.materials.size());
  std::vector<float> matPbr(std::max<size_t>(scene.materials.size(), 1) * 6, 0.0f);
  for (size_t i = 0; i < scene.materials.size(); ++i) {
    const DrawMaterialCPU& dm = scene.materials[i];
    matPbr[i * 6 + 0] = dm.metallic;
    matPbr[i * 6 + 1] = dm.roughness;
    matPbr[i * 6 + 2] = dm.emissive[0];
    matPbr[i * 6 + 3] = dm.emissive[1];
    matPbr[i * 6 + 4] = dm.emissive[2];
    matPbr[i * 6 + 5] = dm.alpha;
  }
  if (!up(matPbr.data(), matPbr.size() * sizeof(float), &dMatPbr_)) return false;
  return true;
}

bool CudaRayTracer::trace(const float invViewProj[16], const float camPos[3],
                          const float lightDir[3], const float clearColor[3],
                          int renderMode, float depthScale, const float sceneMin[3],
                          const float sceneExtent[3], int w, int h,
                          std::vector<uint8_t>* rgba, std::string* err) {
  if (!ctx_ || !dTris_) { if (err) *err = "CUDA scene not built"; return false; }
  cuCtxSetCurrent(reinterpret_cast<CUcontext>(ctx_));
  const size_t bytes = size_t(w) * h * 4;
  if (outCap_ < bytes) {
    if (dOut_) cuMemFree(static_cast<CUdeviceptr>(dOut_));
    CUdeviceptr p;
    CU_OK(cuMemAlloc(&p, bytes), "cuMemAlloc(out)");
    dOut_ = static_cast<uintptr_t>(p);
    outCap_ = bytes;
  }
  Cam cam{};
  std::memcpy(cam.invVP, invViewProj, 16 * sizeof(float));
  for (int i = 0; i < 3; ++i) {
    cam.camPos[i] = camPos[i];
    cam.lightDir[i] = lightDir[i];
    cam.clear[i] = clearColor[i];
  }
  cam.clear[3] = static_cast<float>(renderMode);
  cam.lightDir[3] = depthScale;  // depth AOV normalizer
  for (int i = 0; i < 3; ++i) { cam.sceneMin[i] = sceneMin[i]; cam.sceneExtent[i] = sceneExtent[i]; }
  CUdeviceptr dT = dTris_, dN = dNrms_, dC = dCols_, dG = dGeo_, dM = dMat_,
              dMP = dMatPbr_, dU = dUV_, dNo = dNodes_, dO = dOut_;
  int numMats = numMats_;
  void* args[] = {&dT, &dN, &dC, &dG, &dM, &dMP, &numMats, &dU, &dNo, &dO, &w, &h, &cam};
  unsigned gx = (w + 7) / 8, gy = (h + 7) / 8;
  CU_OK(cuLaunchKernel(reinterpret_cast<CUfunction>(kernel_), gx, gy, 1, 8, 8, 1, 0,
                       nullptr, args, nullptr),
        "cuLaunchKernel");
  CU_OK(cuCtxSynchronize(), "cuCtxSynchronize");
  rgba->resize(bytes);
  CU_OK(cuMemcpyDtoH(rgba->data(), static_cast<CUdeviceptr>(dOut_), bytes),
        "cuMemcpyDtoH");
  return true;
}

}  // namespace tusdview
