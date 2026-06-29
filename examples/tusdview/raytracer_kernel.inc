// SPDX-License-Identifier: Apache-2.0
// raytracer_kernel.inc — shared GPU trace kernel source for the CUDA (NVRTC)
// and HIP (hiprtc) backends. Plain CUDA C using only builtins available under
// both runtime compilers (blockIdx/threadIdx, __uint_as_float, fminf/fmaxf,
// struct-by-value params), so the SAME source compiles for NVIDIA and AMD.
// Included inside an anonymous namespace by each backend .cc.
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

// --- UsdVol volume (dense float grid, object-space AABB) ---
// invModel: column-major mat4 (world->object). dim[3]=voxel dims, dim.w (packed
// as a 4th int) = float offset into the shared `volDens` buffer.
struct VolParam {
  float invModel[16];
  float bmin[4];      // .xyz aabb min
  float bmax[4];      // .xyz aabb max
  int   dim[4];       // .xyz dims, .w = data offset (floats) into volDens
  float albedo[4];    // .xyz albedo, .w densityScale
  float emission[4];  // .xyz emission, .w background
};
__device__ F3 xfPt4(const float* m, F3 p){  // column-major mat4 * point
  float x = m[0]*p.x + m[4]*p.y + m[8]*p.z + m[12];
  float y = m[1]*p.x + m[5]*p.y + m[9]*p.z + m[13];
  float z = m[2]*p.x + m[6]*p.y + m[10]*p.z + m[14];
  float w = m[3]*p.x + m[7]*p.y + m[11]*p.z + m[15];
  if (w==0.f) w=1.f;
  return mk(x/w, y/w, z/w);
}
__device__ F3 xfDir4(const float* m, F3 v){
  return mk(m[0]*v.x + m[4]*v.y + m[8]*v.z,
            m[1]*v.x + m[5]*v.y + m[9]*v.z,
            m[2]*v.x + m[6]*v.y + m[10]*v.z);
}
__device__ float volFetch(const float* dens, int off, int dx, int dy, int dz,
                          int x, int y, int z){
  x = x<0?0:(x>=dx?dx-1:x);
  y = y<0?0:(y>=dy?dy-1:y);
  z = z<0?0:(z>=dz?dz-1:z);
  return dens[off + x + dx*(y + dy*z)];
}
__device__ float sampleVol(const float* dens, const VolParam* vp, F3 p){
  float ex=vp->bmax[0]-vp->bmin[0], ey=vp->bmax[1]-vp->bmin[1], ez=vp->bmax[2]-vp->bmin[2];
  if (ex<=0.f||ey<=0.f||ez<=0.f) return 0.f;
  int dx=vp->dim[0], dy=vp->dim[1], dz=vp->dim[2], off=vp->dim[3];
  float gx=(p.x-vp->bmin[0])/ex*dx-0.5f;
  float gy=(p.y-vp->bmin[1])/ey*dy-0.5f;
  float gz=(p.z-vp->bmin[2])/ez*dz-0.5f;
  int x0=(int)floorf(gx), y0=(int)floorf(gy), z0=(int)floorf(gz);
  float fx=gx-x0, fy=gy-y0, fz=gz-z0;
  float c00=volFetch(dens,off,dx,dy,dz,x0,y0,z0)*(1-fx)+volFetch(dens,off,dx,dy,dz,x0+1,y0,z0)*fx;
  float c10=volFetch(dens,off,dx,dy,dz,x0,y0+1,z0)*(1-fx)+volFetch(dens,off,dx,dy,dz,x0+1,y0+1,z0)*fx;
  float c01=volFetch(dens,off,dx,dy,dz,x0,y0,z0+1)*(1-fx)+volFetch(dens,off,dx,dy,dz,x0+1,y0,z0+1)*fx;
  float c11=volFetch(dens,off,dx,dy,dz,x0,y0+1,z0+1)*(1-fx)+volFetch(dens,off,dx,dy,dz,x0+1,y0+1,z0+1)*fx;
  float c0=c00*(1-fy)+c10*fy, c1=c01*(1-fy)+c11*fy;
  return c0*(1-fz)+c1*fz;
}
// Emission/absorption composite of one volume over background color `bg`.
__device__ F3 compositeVolume(const float* dens, const VolParam* vp, F3 worg, F3 wdir, F3 bg){
  F3 oo=xfPt4(vp->invModel, worg);
  F3 od=norm3(xfDir4(vp->invModel, wdir));
  F3 inv=mk(1.f/od.x, 1.f/od.y, 1.f/od.z);
  float t1a=(vp->bmin[0]-oo.x)*inv.x, t2a=(vp->bmax[0]-oo.x)*inv.x;
  float tmin=fminf(t1a,t2a), tmax=fmaxf(t1a,t2a);
  t1a=(vp->bmin[1]-oo.y)*inv.y; t2a=(vp->bmax[1]-oo.y)*inv.y;
  tmin=fmaxf(tmin,fminf(t1a,t2a)); tmax=fminf(tmax,fmaxf(t1a,t2a));
  t1a=(vp->bmin[2]-oo.z)*inv.z; t2a=(vp->bmax[2]-oo.z)*inv.z;
  tmin=fmaxf(tmin,fminf(t1a,t2a)); tmax=fminf(tmax,fmaxf(t1a,t2a));
  if (tmax<=fmaxf(tmin,0.f)) return bg;
)CUDA"
// NOTE: the kernel source is split into two adjacent raw-string literals here.
// MSVC caps a single string literal at 16380 bytes (C2026); the two pieces are
// concatenated by the compiler into one kKernelSrc. Keep the split on a line
// boundary so the NVRTC source is byte-identical.
R"CUDA(  float t0=fmaxf(tmin,0.f);
  float ex=vp->bmax[0]-vp->bmin[0], ey=vp->bmax[1]-vp->bmin[1], ez=vp->bmax[2]-vp->bmin[2];
  float vmin=fminf(ex/vp->dim[0], fminf(ey/vp->dim[1], ez/vp->dim[2]));
  float step=vmin*0.5f; if (step<=0.f) step=(tmax-t0)/256.f;
  float scaleD=vp->albedo[3], background=vp->emission[3];
  float T=1.f; F3 L=mk(0.f,0.f,0.f);
  float t=t0;
  for (int i=0;i<256 && t<tmax;i++,t+=step){
    F3 p=add(oo, scale(od, t+0.5f*step));
    float dn=(sampleVol(dens,vp,p)-background)*scaleD;
    if (dn>0.f){
      float a=1.f-expf(-dn*step);
      L=add(L, scale(add(scale(mk(vp->albedo[0],vp->albedo[1],vp->albedo[2]),a),
                         scale(mk(vp->emission[0],vp->emission[1],vp->emission[2]),dn*step)), T));
      T*=(1.f-a);
      if (T<0.003f) break;
    }
  }
  return add(L, scale(bg, T));
}

extern "C" __global__ void trace(const float* tris, const float* nrms,
                                 const float* cols, const unsigned char* geo,
                                 const int* mats, const float* matPbr, int numMats,
                                 const float* uvs, const float* uvs1,
                                 const float* infls, const int* faces,
                                 const float* domw, const int* domj,
                                 const Node* blas, const Node* tlas, const Inst* insts,
                                 unsigned char* out, int W, int H, Cam cam,
                                 const float* volDens, const VolParam* volParams,
                                 int numVols){
  int px=blockIdx.x*blockDim.x+threadIdx.x;
  int py=blockIdx.y*blockDim.y+threadIdx.y;
  if (px>=W||py>=H) return;
  // Sub-pixel jitter for supersampled AA rides the spare sceneMin.w/sceneExtent.w
  // lanes (0 for the 1-spp / center-sample case, so unchanged then).
  float u=(px+0.5f+cam.sceneMin[3])/W, vv=(py+0.5f+cam.sceneExtent[3])/H;
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

    // Default shaded view: match the tusdrender -vk/-vkr preview exactly so the
    // two GPU ray-traced previews agree (tools/tusdrender/tusdr_vulkan.cc). Orient
    // the shading normal toward the eye (USD winding/normals are not guaranteed
    // consistent), then key light + a dim camera headlight + ambient (0.05). No
    // cast shadows here: -vkr has none, and parity is the goal for this view.
    F3 V=norm3(scale(d,-1.f));               // toward the eye
    F3 Ns=(dot3(N,V)<0.f)?scale(N,-1.f):N;   // forward-facing shading normal
    F3 L=norm3(mk(cam.lightDir[0],cam.lightDir[1],cam.lightDir[2]));
    float key=fmaxf(dot3(Ns,L),0.f);
    float head=fmaxf(dot3(Ns,V),0.f);
    float k=0.05f+0.8f*key+0.35f*head;
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
  // UsdVol volumes: composite over the shaded color (default mode only).
  if (rmodePre==0){
    for (int vi=0; vi<numVols; vi++){
      outc=compositeVolume(volDens, &volParams[vi], o, d, outc);
    }
  }
  int idx=(py*W+px)*4;
  out[idx+0]=(unsigned char)(fminf(fmaxf(outc.x,0.f),1.f)*255.f+0.5f);
  out[idx+1]=(unsigned char)(fminf(fmaxf(outc.y,0.f),1.f)*255.f+0.5f);
  out[idx+2]=(unsigned char)(fminf(fmaxf(outc.z,0.f),1.f)*255.f+0.5f);
  out[idx+3]=255;
}
)CUDA";
