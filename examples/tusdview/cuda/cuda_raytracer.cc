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

// Per-instance record (must match `Inst` in the kernel). All 4-byte fields, so the
// layout is identical host/device with no padding.
struct Inst {
  float w2o[12];   // world->object (affine inverse of o2w)
  float o2w[12];   // object->world (row-major 3x4)
  float tint[3];   // per-instance color
  int blasRoot;    // global node index of this instance's BLAS root
  int instId;      // stable instance id (instance-id AOV)
};

const char* kKernelSrc = R"CUDA(
struct Node { float bmin[3]; float bmax[3]; int left; int right; int count; };
struct Cam { float invVP[16]; float camPos[4]; float lightDir[4]; float clear[4]; float sceneMin[4]; float sceneExtent[4]; };
// Per-instance placement for 2-level traversal. w2o transforms a world ray into
// the prototype's object space (kept un-normalized so object t == world t). o2w
// reconstructs world-space verts for area/normal AOVs. blasRoot = global node
// index of this prototype's BLAS subtree root.
struct Inst { float w2o[12]; float o2w[12]; float tint[3]; int blasRoot; int instId; };

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

// row-major 3x4 (o2w/w2o) * point / direction.
__device__ F3 xfPt(const float* m, F3 p){
  return mk(m[0]*p.x+m[1]*p.y+m[2]*p.z+m[3],
            m[4]*p.x+m[5]*p.y+m[6]*p.z+m[7],
            m[8]*p.x+m[9]*p.y+m[10]*p.z+m[11]);
}
__device__ F3 xfDir(const float* m, F3 v){
  return mk(m[0]*v.x+m[1]*v.y+m[2]*v.z,
            m[4]*v.x+m[5]*v.y+m[6]*v.z,
            m[8]*v.x+m[9]*v.y+m[10]*v.z);
}
// Object->world normal = (o2w^-1)^T * n = w2o^T * n (ignoring translation).
__device__ F3 xfNrm(const float* w2o, F3 n){
  return mk(w2o[0]*n.x+w2o[4]*n.y+w2o[8]*n.z,
            w2o[1]*n.x+w2o[5]*n.y+w2o[9]*n.z,
            w2o[2]*n.x+w2o[6]*n.y+w2o[10]*n.z);
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

// Traverse one prototype's BLAS subtree (rooted at `root`) for the nearest hit in
// OBJECT space. leaf `left` is already a GLOBAL tri id (rebased at concat time).
__device__ float traverseBLAS(const Node* nodes, const float* tris, int root, F3 o,
                              F3 d, float tmax, int* hitTri, float* bu, float* bv,
                              bool anyHit){
  F3 inv = mk(1.f/d.x, 1.f/d.y, 1.f/d.z);
  int stack[64]; int sp=0; stack[sp++]=root;
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

// 2-level traversal: walk the TLAS; for each candidate instance transform the
// world ray into object space (no dir renorm, so object t == world t) and descend
// its BLAS. Tracks the nearest hit across instances + the hit instance index.
__device__ float traverseTLAS(const Node* tlas, const Inst* insts, const Node* blas,
                              const float* tris, F3 ow, F3 dw, float tmax,
                              int* hitTri, float* bu, float* bv, int* hitInst,
                              bool anyHit){
  F3 invW = mk(1.f/dw.x, 1.f/dw.y, 1.f/dw.z);
  int stack[48]; int sp=0; stack[sp++]=0;
  float best=tmax; *hitTri=-1; *hitInst=-1;
  while (sp>0){
    const Node* nd=&tlas[stack[--sp]];
    if (!hitAabb(nd,ow,invW,best)) continue;
    if (nd->count>0){
      for (int i=0;i<nd->count;i++){
        const Inst* I=&insts[nd->left+i];
        F3 oo=xfPt(I->w2o,ow), dd=xfDir(I->w2o,dw);  // un-normalized dd: t matches
        int lt; float lu,lv;
        float t=traverseBLAS(blas,tris,I->blasRoot,oo,dd,best,&lt,&lu,&lv,anyHit);
        if (lt>=0 && t<best){ best=t; *hitTri=lt; *bu=lu; *bv=lv; *hitInst=nd->left+i;
                              if (anyHit) return best; }
      }
    } else {
      stack[sp++]=nd->left;
      stack[sp++]=nd->right;
    }
  }
  return best;
}

// Count node visits (TLAS + visited BLAS subtrees) for the traversal-cost heatmap.
__device__ int traverseSteps(const Node* tlas, const Inst* insts, const Node* blas,
                             const float* tris, F3 ow, F3 dw, float tmax){
  F3 invW=mk(1.f/dw.x,1.f/dw.y,1.f/dw.z);
  int stack[48]; int sp=0; stack[sp++]=0; float best=tmax; int steps=0;
  while (sp>0){
    const Node* nd=&tlas[stack[--sp]]; steps++;
    if (!hitAabb(nd,ow,invW,best)) continue;
    if (nd->count>0){
      for (int i=0;i<nd->count;i++){
        const Inst* I=&insts[nd->left+i];
        F3 oo=xfPt(I->w2o,ow), dd=xfDir(I->w2o,dw);
        F3 invO=mk(1.f/dd.x,1.f/dd.y,1.f/dd.z);
        int bstack[64]; int bsp=0; bstack[bsp++]=I->blasRoot;
        while (bsp>0){
          const Node* bn=&blas[bstack[--bsp]]; steps++;
          if (!hitAabb(bn,oo,invO,best)) continue;
          if (bn->count>0){
            for (int j=0;j<bn->count;j++){ int ti=bn->left+j; float u,v; float t=triHit(&tris[ti*9],oo,dd,&u,&v); if (t>0.f&&t<best) best=t; }
          } else { bstack[bsp++]=bn->left; bstack[bsp++]=bn->right; }
        }
      }
    } else { stack[sp++]=nd->left; stack[sp++]=nd->right; }
  }
  return steps;
}

extern "C" __global__ void trace(const float* tris, const float* nrms,
                                 const float* cols, const unsigned char* geo,
                                 const int* mats, const float* matPbr, int numMats,
                                 const float* uvs, const float* uvs1,
                                 const float* infls, const int* faces,
                                 const float* domw, const int* domj,
                                 const Node* blas, const Node* tlas, const Inst* insts,
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
  int ht, hitInst; float bu,bv;
  float t=traverseTLAS(tlas,insts,blas,tris,o,d,1e30f,&ht,&bu,&bv,&hitInst,false);
  int rmodePre=(int)(cam.clear[3]+0.5f);
  if (rmodePre==27){  // BVH traversal-cost heatmap (per primary ray, all pixels)
    int steps=traverseSteps(tlas,insts,blas,tris,o,d,1e30f);
    float c=fminf(steps/256.0f,1.0f);  // 2-level visits more nodes than single BVH
    outc=mk(c, 1.f-fabsf(c-0.5f)*2.f, 1.f-c);
  } else if (ht>=0){
    const Inst* I=&insts[hitInst];
    float w0=1.f-bu-bv;
    F3 hit=add(o,scale(d,t));  // world hit (world ray + world t)
    // World-space triangle verts (geometric normal / facing / tangent / area AOVs).
    const float* tv=&tris[ht*9];
    F3 wp0=xfPt(I->o2w,mk(tv[0],tv[1],tv[2]));
    F3 wp1=xfPt(I->o2w,mk(tv[3],tv[4],tv[5]));
    F3 wp2=xfPt(I->o2w,mk(tv[6],tv[7],tv[8]));
    F3 N;
    if (geo[ht]&1){
      N=norm3(cross3(sub(wp1,wp0),sub(wp2,wp0)));
      if (dot3(N,d)>0.f) N=scale(N,-1.f);  // two-sided
    } else {
      const float* nv=&nrms[ht*9];
      F3 nl=mk(nv[0]*w0+nv[3]*bu+nv[6]*bv, nv[1]*w0+nv[4]*bu+nv[7]*bv, nv[2]*w0+nv[5]*bu+nv[8]*bv);
      N=norm3(xfNrm(I->w2o,nl));  // object->world normal (inverse-transpose)
    }
    const float* cv=&cols[ht*9];
    F3 base=mk(cv[0]*w0+cv[3]*bu+cv[6]*bv, cv[1]*w0+cv[4]*bu+cv[7]*bv, cv[2]*w0+cv[5]*bu+cv[8]*bv);
    base=mk(base.x*I->tint[0], base.y*I->tint[1], base.z*I->tint[2]);  // per-instance tint

    F3 L=norm3(mk(cam.lightDir[0],cam.lightDir[1],cam.lightDir[2]));
    float diff=fmaxf(dot3(N,L),0.f);
    float shadow=1.f;
    if (diff>0.f){
      int sht,si; float su,sv;
      F3 so=add(hit,scale(N,1e-3f));
      traverseTLAS(tlas,insts,blas,tris,so,L,1e30f,&sht,&su,&sv,&si,true);
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
      F3 Ng=norm3(cross3(sub(wp1,wp0),sub(wp2,wp0)));
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
      outc=(dot3(cross3(sub(wp1,wp0),sub(wp2,wp0)),d)<0.f)?mk(0.1f,0.7f,0.1f):mk(0.7f,0.1f,0.1f);
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
      const float* uv=&uvs[ht*6];
      F3 e1=sub(wp1,wp0), e2=sub(wp2,wp0);
      float d1x=uv[2]-uv[0], d1y=uv[3]-uv[1], d2x=uv[4]-uv[0], d2y=uv[5]-uv[1];
      float r=d1x*d2y-d2x*d1y;
      F3 T = (fabsf(r)>1e-8f) ? scale(sub(scale(e1,d2y),scale(e2,d1y)),1.f/r) : e1;
      T=norm3(T); outc=mk(T.x*0.5f+0.5f,T.y*0.5f+0.5f,T.z*0.5f+0.5f);
    } else if (rmode==14){  // barycentric
      outc=mk(w0,bu,bv);
    } else if (rmode==15){  // prim id
      outc=idColor(ht);
    } else if (rmode==26){  // instance id (TLAS instance) -- matches VK-RT
      outc=idColor(I->instId);
    } else if (rmode==19){  // missing normals
      outc=(geo[ht]&1)?mk(0.95f,0.1f,0.85f):mk(0.2f,0.2f,0.2f);
    } else if (rmode==18){  // purpose (bits1-2 of geo byte)
      int p=(geo[ht]>>1)&3;
      outc=(p==1)?mk(0.2f,0.8f,0.3f):(p==2)?mk(0.2f,0.45f,0.95f):(p==3)?mk(0.95f,0.75f,0.1f):mk(0.5f,0.5f,0.5f);
    } else if (rmode==29){  // kind (bits3-5 of geo byte)
      int k=(geo[ht]>>3)&7;
      outc=(k==1)?mk(0.2f,0.8f,0.8f):(k==2)?mk(0.85f,0.3f,0.85f):(k==3)?mk(0.95f,0.6f,0.15f):(k==4)?mk(0.5f,0.85f,0.4f):mk(0.35f,0.35f,0.35f);
    } else if (rmode==30){  // udim tile from UV set 0
      const float* uv=&uvs[ht*6];
      float uu=uv[0]*w0+uv[2]*bu+uv[4]*bv, vv2=uv[1]*w0+uv[3]*bu+uv[5]*bv;
      int tile=int(floorf(uu))+10*int(floorf(vv2));
      outc=idColor(tile);
    } else if (rmode==33){  // texel density (per-triangle UV/world area ratio)
      const float* uv=&uvs[ht*6];
      F3 cr=cross3(sub(wp1,wp0),sub(wp2,wp0));
      float worldArea=sqrtf(dot3(cr,cr));
      float d1x=uv[2]-uv[0], d1y=uv[3]-uv[1], d2x=uv[4]-uv[0], d2y=uv[5]-uv[1];
      float uvArea=fabsf(d1x*d2y-d2x*d1y);
      float td=sqrtf(uvArea/fmaxf(worldArea,1e-12f));
      float c=fminf(fmaxf(td*cam.lightDir[3]*0.5f,0.f),1.f);
      outc=mk(c, 1.f-fabsf(c-0.5f)*2.f, 1.f-c);
    } else if (rmode==34){  // source USD face id
      outc=idColor(faces[ht]);
    } else if (rmode==31){  // uv set 1 (multi-UV)
      const float* uv=&uvs1[ht*6];
      float uu=uv[0]*w0+uv[2]*bu+uv[4]*bv, vv2=uv[1]*w0+uv[3]*bu+uv[5]*bv;
      outc=mk(uu-floorf(uu), vv2-floorf(vv2), 0.f);
    } else if (rmode==32){  // blendshape influence
      const float* f=&infls[ht*3];
      float infl=f[0]*w0+f[1]*bu+f[2]*bv;
      float c=fminf(fmaxf(infl/fmaxf(cam.lightDir[3]*0.1f,1e-4f),0.f),1.f);
      outc=mk(c, 1.f-fabsf(c-0.5f)*2.f, 1.f-c);
    } else if (rmode==21){  // skin weights: dominant joint tinted by its weight
      int dj=domj[ht];                          // provoking-vertex dominant joint (flat)
      const float* dw=&domw[ht*3];
      float wt=dw[0]*w0+dw[1]*bu+dw[2]*bv;        // interpolated dominant weight
      F3 ic=idColor(dj);
      float k2=0.3f+0.7f*fminf(fmaxf(wt,0.f),1.f);
      outc=(dj<0)?mk(0.45f,0.45f,0.45f):mk(ic.x*k2,ic.y*k2,ic.z*k2);
    } else if (rmode==25){  // curvature (geometric: vertex-normal spread across the tri)
      if (geo[ht]&1){
        outc=mk(0.f,1.f,1.f);                     // flat-shaded mesh -> zero curvature
      } else {
        const float* nv=&nrms[ht*9];
        F3 n0=norm3(mk(nv[0],nv[1],nv[2])), n1=norm3(mk(nv[3],nv[4],nv[5])), n2=norm3(mk(nv[6],nv[7],nv[8]));
        float spread=(1.f-dot3(n0,n1))+(1.f-dot3(n1,n2))+(1.f-dot3(n2,n0));
        float c=fminf(fmaxf(spread*4.f,0.f),1.f);
        outc=mk(c, 1.f-fabsf(c-0.5f)*2.f, 1.f-c);
      }
    } else if (rmode==24){  // ray-traced ambient occlusion
      F3 Ng=norm3(cross3(sub(wp1,wp0),sub(wp2,wp0)));
      if (dot3(Ng,d)>0.f) Ng=scale(Ng,-1.f);
      float aoR=fmaxf(cam.lightDir[3],1e-3f)*0.15f;
      unsigned seed=hashU((unsigned)px*1973u+(unsigned)py*9277u+1u);
      const int NS=24; float occ=0.f;
      for (int s=0;s<NS;s++){
        F3 sd=cosHemi(Ng,rndf(seed),rndf(seed));
        F3 so=add(hit,scale(Ng,aoR*1e-2f));
        int sht,si; float su,sv;
        traverseTLAS(tlas,insts,blas,tris,so,sd,aoR,&sht,&su,&sv,&si,true);
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
        int sht,si; float su,sv;
        traverseTLAS(tlas,insts,blas,tris,so,sd,1e30f,&sht,&su,&sv,&si,true);
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
// column-major mat4 (DrawMeshCPU.world) -> row-major 3x4 o2w (instance format).
inline void Mat4ToO2W(const float m[16], float o2w[12]) {
  o2w[0] = m[0]; o2w[1] = m[4]; o2w[2] = m[8];  o2w[3] = m[12];
  o2w[4] = m[1]; o2w[5] = m[5]; o2w[6] = m[9];  o2w[7] = m[13];
  o2w[8] = m[2]; o2w[9] = m[6]; o2w[10] = m[10]; o2w[11] = m[14];
}
// Affine inverse of a row-major 3x4 o2w (3x3 cofactor inverse, t' = -R^-1 t).
// Returns false (and leaves identity) if near-singular.
inline bool Affine3x4Inverse(const float o2w[12], float w2o[12]) {
  const float a = o2w[0], b = o2w[1], c = o2w[2];
  const float d = o2w[4], e = o2w[5], f = o2w[6];
  const float g = o2w[8], h = o2w[9], i = o2w[10];
  const float A = e * i - f * h, B = f * g - d * i, C = d * h - e * g;
  float det = a * A + b * B + c * C;
  if (std::fabs(det) < 1e-20f) {
    for (int k = 0; k < 12; ++k) w2o[k] = 0.0f;
    w2o[0] = w2o[5] = w2o[10] = 1.0f;
    return false;
  }
  const float inv = 1.0f / det;
  // R^-1 (row-major 3x3): adjugate / det.
  const float r00 = A * inv,             r01 = (c * h - b * i) * inv, r02 = (b * f - c * e) * inv;
  const float r10 = B * inv,             r11 = (a * i - c * g) * inv, r12 = (c * d - a * f) * inv;
  const float r20 = C * inv,             r21 = (b * g - a * h) * inv, r22 = (a * e - b * d) * inv;
  const float tx = o2w[3], ty = o2w[7], tz = o2w[11];
  w2o[0] = r00; w2o[1] = r01; w2o[2] = r02; w2o[3] = -(r00 * tx + r01 * ty + r02 * tz);
  w2o[4] = r10; w2o[5] = r11; w2o[6] = r12; w2o[7] = -(r10 * tx + r11 * ty + r12 * tz);
  w2o[8] = r20; w2o[9] = r21; w2o[10] = r22; w2o[11] = -(r20 * tx + r21 * ty + r22 * tz);
  return true;
}
// World AABB = transform the 8 corners of a local AABB by a row-major 3x4 o2w.
inline void O2WAabb(const float o2w[12], const float lo[3], const float hi[3],
                    float wlo[3], float whi[3]) {
  for (int k = 0; k < 3; ++k) { wlo[k] = 1e30f; whi[k] = -1e30f; }
  for (int c = 0; c < 8; ++c) {
    float p[3] = {(c & 1) ? hi[0] : lo[0], (c & 2) ? hi[1] : lo[1],
                  (c & 4) ? hi[2] : lo[2]};
    float w[3];
    O2WPt(o2w, p[0], p[1], p[2], w);
    for (int k = 0; k < 3; ++k) { wlo[k] = std::min(wlo[k], w[k]); whi[k] = std::max(whi[k], w[k]); }
  }
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
  F(dTris_); F(dNrms_); F(dCols_); F(dGeo_); F(dMat_); F(dMatPbr_); F(dUV_); F(dUV1_); F(dInfl_); F(dFace_); F(dDomW_); F(dDomJoint_);
  F(dBlasNodes_); F(dTlasNodes_); F(dInstances_); F(dOut_);
  numMats_ = 0;
  outCap_ = 0; triCount_ = 0; nodeCount_ = 0;
  instCount_ = 0; blasNodeCount_ = 0; tlasNodeCount_ = 0;
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

// Recursive median-split BVH over instance WORLD-AABB centroids (the TLAS).
// `aabb` = 6 floats/instance (lo.xyz, hi.xyz). Leaves reference contiguous
// instance ranges (count>0 -> `left` = first instance index, `count` instances).
int BuildTlas(std::vector<Node>& nodes, std::vector<int>& idx, int first, int count,
              const std::vector<float>& cent, const std::vector<float>& aabb) {
  int self = static_cast<int>(nodes.size());
  nodes.push_back({});
  Node nd{};
  float bmin[3] = {1e30f, 1e30f, 1e30f}, bmax[3] = {-1e30f, -1e30f, -1e30f};
  for (int i = 0; i < count; ++i) {
    const float* a = &aabb[idx[first + i] * 6];
    for (int k = 0; k < 3; ++k) {
      bmin[k] = std::min(bmin[k], a[k]);
      bmax[k] = std::max(bmax[k], a[3 + k]);
    }
  }
  for (int k = 0; k < 3; ++k) { nd.bmin[k] = bmin[k]; nd.bmax[k] = bmax[k]; }
  if (count <= 4) {
    nd.left = first; nd.right = 0; nd.count = count;
    nodes[self] = nd;
    return self;
  }
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
  int left = BuildTlas(nodes, idx, first, count / 2, cent, aabb);
  int right = BuildTlas(nodes, idx, mid, count - count / 2, cent, aabb);
  nd.left = left; nd.right = right; nd.count = 0;
  nodes[self] = nd;
  return self;
}
}  // namespace

bool CudaRayTracer::build(const DrawScene& scene, size_t maxTris, std::string* err) {
  if (!ctx_) { if (err) *err = "CUDA not initialized"; return false; }
  cuCtxSetCurrent(reinterpret_cast<CUcontext>(ctx_));
  freeScene();
  truncated_ = false;

  // 2-level instancing: each DrawMeshCPU becomes one BLAS over its LOCAL-space
  // triangles (geometry stored ONCE per prototype). Instanced meshes add N
  // instances, non-instanced meshes a single identity-style instance. Per-tri SoA
  // is concatenated + indexed by GLOBAL tri id; BLAS nodes are concatenated +
  // rebased to global. A TLAS over per-instance world AABBs ties it together.
  std::vector<float> gTris, gNrms, gCols, gUV, gUV1, gInfl, gDomW;
  std::vector<uint8_t> gGeo;
  std::vector<int> gMat, gFace, gDomJoint;
  std::vector<Node> gBlas;
  std::vector<Inst> instances;
  std::vector<float> instAabb;  // 6 floats/instance (world lo,hi) for the TLAS
  // The cap now bounds UNIQUE prototype geometry (tiny vs the old flatten cap).
  const size_t cap = maxTris ? maxTris : (size_t(1) << 62);

  for (const DrawMeshCPU& m : scene.meshes) {
    if (m.vertices.empty() || m.indices.empty()) continue;
    if (gTris.size() / 9 >= cap) { truncated_ = true; break; }
    const bool instanced = m.instanceCount() > 0;
    const bool hasVtxCol = m.vertexColors.size() == m.vertices.size() * 3;
    const bool hasUV1 = m.uv1.size() == m.vertices.size() * 2;
    const bool hasInfl = m.morphInfluence.size() == m.vertices.size();
    const bool hasSkin = m.jointIdx.size() == m.vertices.size() * 4 &&
                         m.jointWt.size() == m.vertices.size() * 4;
    const bool hasFace = m.sourceFaceId.size() == m.indices.size() / 3;
    // geo byte: bit0 = geometricNormal, bits1-2 = purpose, bits3-5 = kind.
    const uint8_t g = static_cast<uint8_t>((m.geometricNormal ? 1 : 0) |
                                           ((PurposeId(m.purpose) & 3) << 1) |
                                           ((m.kindId & 7) << 3));
    auto submeshMat = [&](uint32_t triIdx0) -> const DrawMaterialCPU* {
      for (const DrawSubmesh& s : m.submeshes)
        if (triIdx0 >= s.indexOffset && triIdx0 < s.indexOffset + s.indexCount)
          return (s.materialId >= 0 && size_t(s.materialId) < scene.materials.size())
                     ? &scene.materials[s.materialId] : nullptr;
      return nullptr;
    };
    auto submeshMatId = [&](uint32_t triIdx0) -> int {
      for (const DrawSubmesh& s : m.submeshes)
        if (triIdx0 >= s.indexOffset && triIdx0 < s.indexOffset + s.indexCount)
          return s.materialId;
      return -1;
    };

    // --- Emit this mesh's LOCAL-space triangles into temp BLAS arrays. ---
    std::vector<float> lt, ln, lc, luv, luv1, linfl, ldomw;
    std::vector<uint8_t> lg;
    std::vector<int> lm, lf, ldomj;
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
      float wp[9], wn[9], wc[9], wuv[6], wuv1[6], winfl[3], wdomw[3];
      int domJoint = -1;
      // Non-instanced meshes bake the submesh material color into cols (single
      // instance); instanced prototypes keep only displayColor and apply the
      // per-instance tint in the kernel (Inst::tint).
      float curTint[3] = {0.6f, 0.6f, 0.6f};
      if (instanced) {
        curTint[0] = curTint[1] = curTint[2] = 1.0f;
      } else if (const DrawMaterialCPU* mat = submeshMat(static_cast<uint32_t>(t))) {
        curTint[0] = mat->baseColor[0];
        curTint[1] = mat->baseColor[1];
        curTint[2] = mat->baseColor[2];
      }
      for (int k = 0; k < 3; ++k) {
        const uint32_t vidx = m.indices[t + k];
        const DrawVertex& vtx = m.vertices[vidx];
        wp[k * 3 + 0] = vtx.px; wp[k * 3 + 1] = vtx.py; wp[k * 3 + 2] = vtx.pz;  // LOCAL
        wn[k * 3 + 0] = vtx.nx; wn[k * 3 + 1] = vtx.ny; wn[k * 3 + 2] = vtx.nz;  // LOCAL
        wuv[k * 2 + 0] = vtx.u; wuv[k * 2 + 1] = vtx.v;
        wuv1[k * 2 + 0] = hasUV1 ? m.uv1[vidx * 2 + 0] : 0.0f;
        wuv1[k * 2 + 1] = hasUV1 ? m.uv1[vidx * 2 + 1] : 0.0f;
        winfl[k] = hasInfl ? m.morphInfluence[vidx] : 0.0f;
        int dj = -1; float dw = 0.0f;
        if (hasSkin)
          for (int b = 0; b < 4; ++b) {
            const float wb = m.jointWt[vidx * 4 + b];
            if (wb > dw) { dw = wb; dj = static_cast<int>(m.jointIdx[vidx * 4 + b]); }
          }
        wdomw[k] = dw;
        if (k == 0) domJoint = dj;
        float dc[3] = {1, 1, 1};
        if (hasVtxCol) {
          const float* c = &m.vertexColors[vidx * 3];
          dc[0] = c[0]; dc[1] = c[1]; dc[2] = c[2];
        }
        wc[k * 3 + 0] = curTint[0] * dc[0];
        wc[k * 3 + 1] = curTint[1] * dc[1];
        wc[k * 3 + 2] = curTint[2] * dc[2];
      }
      lt.insert(lt.end(), wp, wp + 9);
      ln.insert(ln.end(), wn, wn + 9);
      lc.insert(lc.end(), wc, wc + 9);
      luv.insert(luv.end(), wuv, wuv + 6);
      luv1.insert(luv1.end(), wuv1, wuv1 + 6);
      linfl.insert(linfl.end(), winfl, winfl + 3);
      ldomw.insert(ldomw.end(), wdomw, wdomw + 3);
      ldomj.push_back(domJoint);
      lg.push_back(g);
      lm.push_back(submeshMatId(static_cast<uint32_t>(t)));
      lf.push_back(hasFace ? static_cast<int>(m.sourceFaceId[t / 3]) : -1);
    }
    const size_t ltc = lt.size() / 9;
    if (ltc == 0) continue;

    // Prototype-local AABB (transformed per instance for the TLAS).
    float lo[3] = {lt[0], lt[1], lt[2]}, hi[3] = {lt[0], lt[1], lt[2]};
    for (size_t i = 0; i < ltc; ++i)
      for (int v = 0; v < 3; ++v)
        for (int k = 0; k < 3; ++k) {
          float c = lt[i * 9 + v * 3 + k];
          lo[k] = std::min(lo[k], c); hi[k] = std::max(hi[k], c);
        }

    // Build this BLAS's BVH over local tris; reorder per-tri arrays to leaf order.
    std::vector<float> cent(ltc * 3);
    for (size_t i = 0; i < ltc; ++i) {
      const float* tv = &lt[i * 9];
      for (int k = 0; k < 3; ++k) cent[i * 3 + k] = (tv[k] + tv[3 + k] + tv[6 + k]) / 3.f;
    }
    std::vector<int> bidx(ltc);
    for (size_t i = 0; i < ltc; ++i) bidx[i] = static_cast<int>(i);
    std::vector<Node> bnodes;
    bnodes.reserve(ltc * 2);
    BuildBvh(bnodes, bidx, 0, static_cast<int>(ltc), cent, lt);

    const size_t triOff = gTris.size() / 9;
    const size_t nodeOff = gBlas.size();
    const int blasRoot = static_cast<int>(nodeOff);
    for (size_t i = 0; i < ltc; ++i) {
      int s = bidx[i];
      gTris.insert(gTris.end(), &lt[s * 9], &lt[s * 9] + 9);
      gNrms.insert(gNrms.end(), &ln[s * 9], &ln[s * 9] + 9);
      gCols.insert(gCols.end(), &lc[s * 9], &lc[s * 9] + 9);
      gUV.insert(gUV.end(), &luv[s * 6], &luv[s * 6] + 6);
      gUV1.insert(gUV1.end(), &luv1[s * 6], &luv1[s * 6] + 6);
      gInfl.insert(gInfl.end(), &linfl[s * 3], &linfl[s * 3] + 3);
      gDomW.insert(gDomW.end(), &ldomw[s * 3], &ldomw[s * 3] + 3);
      gGeo.push_back(lg[s]);
      gMat.push_back(lm[s]);
      gFace.push_back(lf[s]);
      gDomJoint.push_back(ldomj[s]);
    }
    // Rebase BLAS node child/leaf indices into the global arrays.
    for (Node& nd : bnodes) {
      if (nd.count > 0) nd.left += static_cast<int>(triOff);
      else { nd.left += static_cast<int>(nodeOff); nd.right += static_cast<int>(nodeOff); }
    }
    gBlas.insert(gBlas.end(), bnodes.begin(), bnodes.end());

    // --- Instances of this BLAS (geometry shared; placement-only). ---
    auto addInst = [&](const float o2w[12], const float tint[3]) {
      Inst I{};
      for (int k = 0; k < 12; ++k) I.o2w[k] = o2w[k];
      Affine3x4Inverse(o2w, I.w2o);  // identity fallback on a singular transform
      I.tint[0] = tint[0]; I.tint[1] = tint[1]; I.tint[2] = tint[2];
      I.blasRoot = blasRoot;
      I.instId = static_cast<int>(instances.size());
      instances.push_back(I);
      float wlo[3], whi[3];
      O2WAabb(o2w, lo, hi, wlo, whi);
      instAabb.insert(instAabb.end(), wlo, wlo + 3);
      instAabb.insert(instAabb.end(), whi, whi + 3);
    };
    if (instanced) {
      const size_t ninst = m.instanceCount();
      const bool perColor = m.instanceColors.size() == ninst * 3;
      for (size_t k = 0; k < ninst; ++k) {
        const float tint[3] = {perColor ? m.instanceColors[k * 3 + 0] : m.flatColor[0],
                               perColor ? m.instanceColors[k * 3 + 1] : m.flatColor[1],
                               perColor ? m.instanceColors[k * 3 + 2] : m.flatColor[2]};
        addInst(&m.instanceXforms[k * 12], tint);
      }
    } else {
      float o2w[12];
      Mat4ToO2W(m.world, o2w);
      const float white[3] = {1.0f, 1.0f, 1.0f};
      addInst(o2w, white);
    }
  }

  triCount_ = gTris.size() / 9;
  instCount_ = instances.size();
  if (triCount_ == 0 || instCount_ == 0) { if (err) *err = "CUDA: no geometry"; return false; }
  blasNodeCount_ = gBlas.size();

  // Build the TLAS over instance world AABBs (reorders the instance table to leaf
  // order; instId stays with each Inst so the AOV is deterministic).
  std::vector<float> tcent(instCount_ * 3);
  for (size_t i = 0; i < instCount_; ++i)
    for (int k = 0; k < 3; ++k)
      tcent[i * 3 + k] = 0.5f * (instAabb[i * 6 + k] + instAabb[i * 6 + 3 + k]);
  std::vector<int> tidx(instCount_);
  for (size_t i = 0; i < instCount_; ++i) tidx[i] = static_cast<int>(i);
  std::vector<Node> tnodes;
  tnodes.reserve(instCount_ * 2);
  BuildTlas(tnodes, tidx, 0, static_cast<int>(instCount_), tcent, instAabb);
  tlasNodeCount_ = tnodes.size();
  nodeCount_ = blasNodeCount_ + tlasNodeCount_;
  std::vector<Inst> orderedInsts(instCount_);
  for (size_t i = 0; i < instCount_; ++i) orderedInsts[i] = instances[tidx[i]];

  // Upload.
  auto up = [&](const void* host, size_t bytes, uintptr_t* dptr) -> bool {
    CUdeviceptr p;
    CU_OK(cuMemAlloc(&p, bytes), "cuMemAlloc");
    CU_OK(cuMemcpyHtoD(p, host, bytes), "cuMemcpyHtoD");
    *dptr = static_cast<uintptr_t>(p);
    return true;
  };
  if (!up(gTris.data(), gTris.size() * sizeof(float), &dTris_)) return false;
  if (!up(gNrms.data(), gNrms.size() * sizeof(float), &dNrms_)) return false;
  if (!up(gCols.data(), gCols.size() * sizeof(float), &dCols_)) return false;
  if (!up(gGeo.data(), gGeo.size(), &dGeo_)) return false;
  if (!up(gMat.data(), gMat.size() * sizeof(int), &dMat_)) return false;
  if (!up(gFace.data(), gFace.size() * sizeof(int), &dFace_)) return false;
  if (!up(gUV.data(), gUV.size() * sizeof(float), &dUV_)) return false;
  if (!up(gUV1.data(), gUV1.size() * sizeof(float), &dUV1_)) return false;
  if (!up(gInfl.data(), gInfl.size() * sizeof(float), &dInfl_)) return false;
  if (!up(gDomW.data(), gDomW.size() * sizeof(float), &dDomW_)) return false;
  if (!up(gDomJoint.data(), gDomJoint.size() * sizeof(int), &dDomJoint_)) return false;
  if (!up(gBlas.data(), gBlas.size() * sizeof(Node), &dBlasNodes_)) return false;
  if (!up(tnodes.data(), tnodes.size() * sizeof(Node), &dTlasNodes_)) return false;
  if (!up(orderedInsts.data(), orderedInsts.size() * sizeof(Inst), &dInstances_)) return false;

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
              dMP = dMatPbr_, dU = dUV_, dU1 = dUV1_, dIn = dInfl_, dF = dFace_,
              dDw = dDomW_, dDj = dDomJoint_, dBl = dBlasNodes_, dTl = dTlasNodes_,
              dI = dInstances_, dO = dOut_;
  int numMats = numMats_;
  // ORDER MUST MATCH the kernel signature: tris,nrms,cols,geo,mats,matPbr,numMats,
  // uvs,uvs1,infls,faces,domw,domj,blas,tlas,insts,out,W,H,cam.
  void* args[] = {&dT,  &dN,  &dC, &dG, &dM, &dMP, &numMats, &dU, &dU1, &dIn,
                  &dF,  &dDw, &dDj, &dBl, &dTl, &dI, &dO, &w, &h, &cam};
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
