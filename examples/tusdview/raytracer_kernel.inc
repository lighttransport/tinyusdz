// SPDX-License-Identifier: Apache-2.0
// raytracer_kernel.inc — shared GPU trace kernel source for the CUDA (NVRTC)
// and HIP (hiprtc) backends. Plain CUDA C using only builtins available under
// both runtime compilers (blockIdx/threadIdx, __uint_as_float, fminf/fmaxf,
// struct-by-value params), so the SAME source compiles for NVIDIA and AMD.
// Included inside an anonymous namespace by each backend .cc.
const char* kKernelSrc = R"CUDA(
struct Node { float bmin[3]; float bmax[3]; int left; int right; int count; };
struct Cam { float invVP[16]; float camPos[4]; float lightDir[4]; float clear[4]; float sceneMin[4]; float sceneExtent[4]; float vp[16]; };
// Per-instance placement for 2-level traversal. w2o transforms a world ray into
// the prototype's object space (kept un-normalized so object t == world t). o2w
// reconstructs world-space verts for area/normal AOVs. blasRoot = global node
// index of this prototype's BLAS subtree root.
struct Inst { float w2o[12]; float o2w[12]; float tint[4]; int blasRoot; int instId; };
struct TexDesc { int offset; int width; int height; int wrapS; int wrapT; int srgb; int isUdim; int mipCount; int firstMip; int udimLayer[100]; };

typedef struct { float x, y, z; } F3;
typedef struct { float x, y, z, w; } F4;
__device__ F3 mk(float x, float y, float z){ F3 r; r.x=x; r.y=y; r.z=z; return r; }
__device__ F4 mk4(float x, float y, float z, float w){ F4 r; r.x=x; r.y=y; r.z=z; r.w=w; return r; }
__device__ F3 sub(F3 a, F3 b){ return mk(a.x-b.x, a.y-b.y, a.z-b.z); }
__device__ F3 add(F3 a, F3 b){ return mk(a.x+b.x, a.y+b.y, a.z+b.z); }
__device__ F3 scale(F3 a, float s){ return mk(a.x*s, a.y*s, a.z*s); }
__device__ float dot3(F3 a, F3 b){ return a.x*b.x + a.y*b.y + a.z*b.z; }
__device__ F3 cross3(F3 a, F3 b){ return mk(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x); }
__device__ F3 norm3(F3 a){ float l=sqrtf(dot3(a,a)); return l>0.f?scale(a,1.f/l):a; }
__device__ float ggxD(float nh,float r){float a=fmaxf(r*r,0.002f),a2=a*a,d=nh*nh*(a2-1.f)+1.f;return a2/fmaxf(3.14159265f*d*d,1e-6f);}
__device__ float ggxG1(float nx,float r){float k=(r+1.f)*(r+1.f)*0.125f;return nx/fmaxf(nx*(1.f-k)+k,1e-6f);}
__device__ F3 fresnelSchlick(float vh,F3 f0){float q=powf(1.f-fminf(fmaxf(vh,0.f),1.f),5.f);return mk(f0.x+(1.f-f0.x)*q,f0.y+(1.f-f0.y)*q,f0.z+(1.f-f0.z)*q);}
__device__ float linearToSrgb1(float x){x=fminf(fmaxf(x,0.f),1.f);return x<=0.0031308f?x*12.92f:1.055f*powf(x,1.f/2.4f)-0.055f;}
// Stable distinct color per material id (-1 -> neutral gray).
__device__ F3 idColor(int id){
  if (id<0) return mk(0.45f,0.45f,0.45f);
  unsigned int h=((unsigned int)id+1u)*2654435761u;
  return mk((h&255u)*(1.f/255.f), ((h>>8)&255u)*(1.f/255.f), ((h>>16)&255u)*(1.f/255.f));
}
__device__ float wrapCoord(float x, int mode){
  if (mode==0 || mode==3) return fminf(fmaxf(x,0.f),1.f);       // clamp
  if (mode==2){ float t=floorf(x); float f=x-t; return ((int)t&1)?(1.f-f):f; } // mirror
  return x-floorf(x);                                            // repeat
}
__device__ float srgbToLinear(float x){
  return x<=0.04045f ? x*(1.f/12.92f) : powf((x+0.055f)*(1.f/1.055f),2.4f);
}
__device__ F4 sampleTexLevel(const unsigned char* texels, TexDesc td,
                             float u, float v){
  float su=wrapCoord(u,td.wrapS)*(td.width-1);
  float sv=wrapCoord(v,td.wrapT)*(td.height-1);
  int x0=(int)floorf(su), y0=(int)floorf(sv);
  int x1=(x0+1<td.width)?(x0+1):(td.width-1);
  int y1=(y0+1<td.height)?(y0+1):(td.height-1);
  float tx=su-x0, ty=sv-y0;
  const unsigned char* p00=texels+td.offset+(y0*td.width+x0)*4;
  const unsigned char* p10=texels+td.offset+(y0*td.width+x1)*4;
  const unsigned char* p01=texels+td.offset+(y1*td.width+x0)*4;
  const unsigned char* p11=texels+td.offset+(y1*td.width+x1)*4;
  F4 c00=mk4(p00[0]*(1.f/255.f),p00[1]*(1.f/255.f),p00[2]*(1.f/255.f),p00[3]*(1.f/255.f));
  F4 c10=mk4(p10[0]*(1.f/255.f),p10[1]*(1.f/255.f),p10[2]*(1.f/255.f),p10[3]*(1.f/255.f));
  F4 c01=mk4(p01[0]*(1.f/255.f),p01[1]*(1.f/255.f),p01[2]*(1.f/255.f),p01[3]*(1.f/255.f));
  F4 c11=mk4(p11[0]*(1.f/255.f),p11[1]*(1.f/255.f),p11[2]*(1.f/255.f),p11[3]*(1.f/255.f));
  if (td.srgb) {
    c00.x=srgbToLinear(c00.x); c00.y=srgbToLinear(c00.y); c00.z=srgbToLinear(c00.z);
    c10.x=srgbToLinear(c10.x); c10.y=srgbToLinear(c10.y); c10.z=srgbToLinear(c10.z);
    c01.x=srgbToLinear(c01.x); c01.y=srgbToLinear(c01.y); c01.z=srgbToLinear(c01.z);
    c11.x=srgbToLinear(c11.x); c11.y=srgbToLinear(c11.y); c11.z=srgbToLinear(c11.z);
  }
  F4 a=mk4(c00.x+(c10.x-c00.x)*tx,c00.y+(c10.y-c00.y)*tx,c00.z+(c10.z-c00.z)*tx,c00.w+(c10.w-c00.w)*tx);
  F4 b=mk4(c01.x+(c11.x-c01.x)*tx,c01.y+(c11.y-c01.y)*tx,c01.z+(c11.z-c01.z)*tx,c01.w+(c11.w-c01.w)*tx);
  return mk4(a.x+(b.x-a.x)*ty,a.y+(b.y-a.y)*ty,a.z+(b.z-a.z)*ty,a.w+(b.w-a.w)*ty);
}
__device__ F4 sampleTexRGBAFallbackLod(const unsigned char* texels,
                                      const TexDesc* tex, int numTex, int id,
                                      float u, float v, float lod, F4 missing){
  if (!texels || !tex || id<0 || id>=numTex) return missing;
  TexDesc td=tex[id];
  int baseId=id;
  if (td.isUdim) {
    int tileU=(int)floorf(u), tileV=(int)floorf(v);
    int lut=tileU+tileV*10;
    if (lut<0 || lut>=100) return missing;
    int tileId=td.udimLayer[lut];
    if (tileId<0 || tileId>=numTex) return missing;
    baseId=tileId; td=tex[tileId]; u=u-floorf(u); v=v-floorf(v);
  }
  if (td.width<=0 || td.height<=0) return missing;
  float cl=fminf(fmaxf(lod,0.f),(float)((td.mipCount>0?td.mipCount:1)-1));
  int l0=(int)floorf(cl), l1=(l0+1<td.mipCount)?l0+1:l0;
  int id0=l0==0?baseId:td.firstMip+l0-1;
  int id1=l1==0?baseId:td.firstMip+l1-1;
  TexDesc t0=l0==0?td:tex[id0];
  TexDesc t1=l1==0?td:tex[id1];
  F4 a=sampleTexLevel(texels,t0,u,v), b=sampleTexLevel(texels,t1,u,v);
  float f=cl-floorf(cl);
  return mk4(a.x+(b.x-a.x)*f,a.y+(b.y-a.y)*f,a.z+(b.z-a.z)*f,a.w+(b.w-a.w)*f);
}
__device__ F4 sampleTexRGBAFallback(const unsigned char* texels,
                                    const TexDesc* tex, int numTex, int id,
                                    float u, float v, F4 missing){
  return sampleTexRGBAFallbackLod(texels,tex,numTex,id,u,v,0.f,missing);
}
__device__ F4 sampleTexRGBA(const unsigned char* texels, const TexDesc* tex,
                            int numTex, int id, float u, float v){
  return sampleTexRGBAFallback(texels,tex,numTex,id,u,v,
                               mk4(1.f,1.f,1.f,1.f));
}
__device__ float textureLod(const TexDesc* tex, int numTex, int id,
                            float u, float v, float uvFootprint){
  if (!tex || id<0 || id>=numTex) return 0.f;
  TexDesc td=tex[id];
  if (td.isUdim) {
    int lut=(int)floorf(u)+(int)floorf(v)*10;
    if (lut<0 || lut>=100 || td.udimLayer[lut]<0 ||
        td.udimLayer[lut]>=numTex) return 0.f;
    td=tex[td.udimLayer[lut]];
  }
  float rho=uvFootprint*(float)(td.width>td.height?td.width:td.height);
  float lod=log2f(fmaxf(rho,1.f));
  return fminf(fmaxf(lod,0.f),(float)((td.mipCount>0?td.mipCount:1)-1));
}
__device__ F4 sampleTexRGBALod(const unsigned char* texels, const TexDesc* tex,
                               int numTex, int id, float u, float v, float lod){
  return sampleTexRGBAFallbackLod(texels,tex,numTex,id,u,v,lod,
                                  mk4(1.f,1.f,1.f,1.f));
}
__device__ F3 sampleTexRGB(const unsigned char* texels, const TexDesc* tex,
                           int numTex, int id, float u, float v){
  F4 c=sampleTexRGBA(texels,tex,numTex,id,u,v);
  return mk(c.x,c.y,c.z);
}
__device__ float channel4(F4 c, int ch){
  if (ch==1) return c.y;
  if (ch==2) return c.z;
  if (ch==3) return c.w;
  return c.x;
}
__device__ void xformUv(const float* p, int off, float u, float v, float* ou, float* ov){
  const float* x=p+off;
  *ou=x[0]*u+x[1]*v+x[2];
  *ov=x[3]*u+x[4]*v+x[5];
}
__device__ float xformFootprint(const float* p, int off, float footprint){
  if (!p) return footprint;
  float sx=sqrtf(p[off+0]*p[off+0]+p[off+3]*p[off+3]);
  float sy=sqrtf(p[off+1]*p[off+1]+p[off+4]*p[off+4]);
  return footprint*fmaxf(sx,sy);
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

// Project a world point to pixel coords (column-major vp = proj*view). Returns
// (pixelX, pixelY, clipW); clipW<=0 means behind the eye. Pixel origin top-left,
// matching the primary-ray generation (ndcy -> (1-ndcy)/2 * H).
__device__ F3 projPix(const float* m, F3 p, float W, float H){
  float x = m[0]*p.x + m[4]*p.y + m[8]*p.z + m[12];
  float y = m[1]*p.x + m[5]*p.y + m[9]*p.z + m[13];
  float w = m[3]*p.x + m[7]*p.y + m[11]*p.z + m[15];
  float iw = (w>1e-9f||w<-1e-9f)?1.f/w:0.f;
  float sx = (x*iw*0.5f+0.5f)*W;
  float sy = (1.f-(y*iw*0.5f+0.5f))*H;
  return mk(sx, sy, w);
}
// 2D distance from point (px,py) to segment (a..b), in pixels.
__device__ float pixDistSeg(float px,float py,float ax,float ay,float bx,float by){
  float vx=bx-ax, vy=by-ay, wx=px-ax, wy=py-ay;
  float vv=vx*vx+vy*vy;
  float tt=vv>1e-9f?(wx*vx+wy*vy)/vv:0.f;
  tt=tt<0.f?0.f:(tt>1.f?1.f:tt);
  float dx=px-(ax+tt*vx), dy=py-(ay+tt*vy);
  return sqrtf(dx*dx+dy*dy);
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

// Alpha-mask intersections participate in traversal, not just compositing: a
// rejected texel must expose the next triangle and must not occlude AO/shadow
// rays. Blend materials remain intersections and composite after shading.
__device__ bool cutoutAllowsHit(
    int ht, float bu, float bv, int hitInst, const float* cols,
    const float* tris, F3 rayDir, const int* mats, const int* backMats,
    const float* matPbr, const float* matLightRt, int numMats,
    const int* matTex, const float* matTexParam, const unsigned char* texels,
    const TexDesc* textures, int numTextures, const float* uvs,
    const float* uvs1, const Inst* insts) {
  int mid=mats[ht];
  if (backMats && backMats[ht]>=0) {
    const float* tv=&tris[ht*9];
    const Inst* I=&insts[hitInst];
    F3 p0=xfPt(I->o2w,mk(tv[0],tv[1],tv[2]));
    F3 p1=xfPt(I->o2w,mk(tv[3],tv[4],tv[5]));
    F3 p2=xfPt(I->o2w,mk(tv[6],tv[7],tv[8]));
    if (dot3(cross3(sub(p1,p0),sub(p2,p0)),rayDir)>0.f) mid=backMats[ht];
  }
  if (mid<0 || mid>=numMats || !matLightRt) return true;
  const float* lrt=&matLightRt[mid*56];
  float alphaMode=lrt[54];
  if (!(alphaMode>0.5f && alphaMode<1.5f)) return true;
  float w0=1.f-bu-bv;
  const float* uvh=&uvs[ht*6];
  float u=uvh[0]*w0+uvh[2]*bu+uvh[4]*bv;
  float v=uvh[1]*w0+uvh[3]*bu+uvh[5]*bv;
  float u1=u,v1=v;
  if (uvs1){
    const float* uvh1=&uvs1[ht*6];
    u1=uvh1[0]*w0+uvh1[2]*bu+uvh1[4]*bv;
    v1=uvh1[1]*w0+uvh1[3]*bu+uvh1[5]*bv;
  }
  const float* tp=matTexParam?&matTexParam[mid*72]:0;
  int uvbits=tp?(int)(tp[69]+0.5f):0;
  float buu=u,bvv=v,ouu=u,ovv=v;
  if (tp){
    xformUv(tp,0,(uvbits&1)?u1:u,(uvbits&1)?v1:v,&buu,&bvv);
    xformUv(tp,30,(uvbits&32)?u1:u,(uvbits&32)?v1:v,&ouu,&ovv);
  }
  int baseTex=matTex?matTex[mid*6+0]:-1;
  F4 bt=sampleTexRGBA(texels,textures,numTextures,baseTex,buu,bvv);
  if (tp) bt=mk4(bt.x*tp[36]+tp[40],bt.y*tp[37]+tp[41],
                 bt.z*tp[38]+tp[42],bt.w*tp[39]+tp[43]);
  float opacity=(lrt[51]>0.5f)?lrt[39]:matPbr[mid*6+5];
  opacity*=bt.w;
  int opacityTex=matTex?matTex[mid*6+5]:-1;
  if (opacityTex>=0){
    F4 ot=sampleTexRGBA(texels,textures,numTextures,opacityTex,ouu,ovv);
    int ch=tp?(int)(tp[66]+0.5f):0;
    opacity*=channel4(ot,ch)*(tp?tp[67]:1.f)+(tp?tp[68]:0.f);
  }
  const float* cv=&cols[ht*12];
  opacity*=(cv[3]*w0+cv[7]*bu+cv[11]*bv)*insts[hitInst].tint[3];
  return opacity>=lrt[55];
}

__device__ float traverseCutout(
    const Node* tlas, const Inst* insts, const Node* blas, const float* tris,
    F3 o, F3 d, float tmax, int* ht, float* bu, float* bv, int* hitInst,
    const float* cols, const int* mats, const int* backMats, const float* matPbr,
    const float* matLightRt, int numMats, const int* matTex,
    const float* matTexParam, const unsigned char* texels,
    const TexDesc* textures, int numTextures, const float* uvs,
    const float* uvs1) {
  float base=0.f;
  for (int pass=0; pass<64 && base<tmax; ++pass){
    F3 ro=add(o,scale(d,base));
    float t=traverseTLAS(tlas,insts,blas,tris,ro,d,tmax-base,
                         ht,bu,bv,hitInst,false);
    if (*ht<0) return tmax;
    if (cutoutAllowsHit(*ht,*bu,*bv,*hitInst,cols,tris,d,mats,backMats,
                        matPbr,matLightRt,
                        numMats,matTex,matTexParam,texels,textures,numTextures,
                        uvs,uvs1,insts)) return base+t;
    base+=t+1e-4f;
  }
  *ht=-1; *hitInst=-1;
  return tmax;
}
)CUDA"
R"CUDA(

extern "C" __global__ void trace(const float* tris, const float* nrms,
                                 const float* cols, const unsigned char* geo,
                                 const int* mats, const int* backMats,
                                 const float* matPbr, const float* matBase,
                                 const float* matLightRt, int numMats,
                                 const float* lightParams, int numLights,
                                 const int* matTex, const float* matTexParam,
                                 const unsigned char* texels,
                                 const TexDesc* textures, int numTextures,
                                 const float* uvs, const float* uvs1,
                                 const float* infls, const int* faces,
                                 const float* domw, const int* domj,
                                 const Node* blas, const Node* tlas, const Inst* insts,
                                 unsigned char* out, int W, int H, Cam cam,
                                 const float* volDens, const VolParam* volParams,
                                 int numVols, const unsigned char* emask,
                                 float* accum, int sampleIdx, int numSamples){
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
  float t=traverseCutout(tlas,insts,blas,tris,o,d,1e30f,&ht,&bu,&bv,
                         &hitInst,cols,mats,backMats,matPbr,matLightRt,numMats,matTex,
                         matTexParam,texels,textures,numTextures,uvs,uvs1);
  int rmodePre=(int)(cam.clear[3]+0.5f);
  // Dome env background on miss (shaded mode only): sample the dome's latlong
  // envmap scaled by its effectiveColor. Packed light record: [0]=type,
  // [1]=flags (bit 4 = envmap mapped), [2]=texture id, [16..18]=effectiveColor.
  if (ht<0 && rmodePre==0 && lightParams && numLights>0 && texels && textures){
    for (int li=0; li<numLights; ++li){
      const float* lp=&lightParams[li*80];
      if ((int)(lp[0]+0.5f)!=6) continue;         // DomeLight
      int lflags=(int)(lp[1]+0.5f);
      if (!(lflags&(1<<4))) continue;             // has mapped envmap
      int envTex=(int)(lp[2]+0.5f);
      // Rows 10-12: world -> environment rotation (dome orientation).
      F3 de=mk(lp[40]*d.x+lp[41]*d.y+lp[42]*d.z,
               lp[44]*d.x+lp[45]*d.y+lp[46]*d.z,
               lp[48]*d.x+lp[49]*d.y+lp[50]*d.z);
      float eu=atan2f(de.x,-de.z)*0.15915494f+0.5f; // latlong u
      float ev=acosf(fminf(fmaxf(de.y,-1.f),1.f))*0.31830989f;
      F3 env=sampleTexRGB(texels,textures,numTextures,envTex,eu,ev);
      outc=mk(env.x*lp[16],env.y*lp[17],env.z*lp[18]);
      break;
    }
  }
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
    const float* cv=&cols[ht*12];
    F3 base=mk(cv[0]*w0+cv[4]*bu+cv[8]*bv, cv[1]*w0+cv[5]*bu+cv[9]*bv, cv[2]*w0+cv[6]*bu+cv[10]*bv);
    float vertexOpacity=cv[3]*w0+cv[7]*bu+cv[11]*bv;
    base=mk(base.x*I->tint[0], base.y*I->tint[1], base.z*I->tint[2]);  // per-instance tint
    const float* uvh=&uvs[ht*6];
    float uu=uvh[0]*w0+uvh[2]*bu+uvh[4]*bv;
    float vv2=uvh[1]*w0+uvh[3]*bu+uvh[5]*bv;
    float uu1=uu,vv21=vv2;
    if (uvs1) {
      const float* uvh1=&uvs1[ht*6];
      uu1=uvh1[0]*w0+uvh1[2]*bu+uvh1[4]*bv;
      vv21=uvh1[1]*w0+uvh1[3]*bu+uvh1[5]*bv;
    }
    F3 sp0=projPix(cam.vp,wp0,(float)W,(float)H);
    F3 sp1=projPix(cam.vp,wp1,(float)W,(float)H);
    F3 sp2=projPix(cam.vp,wp2,(float)W,(float)H);
    float screenArea2=fabsf((sp1.x-sp0.x)*(sp2.y-sp0.y)-
                            (sp1.y-sp0.y)*(sp2.x-sp0.x));
    float uve1x=uvh[2]-uvh[0], uve1y=uvh[3]-uvh[1];
    float uve2x=uvh[4]-uvh[0], uve2y=uvh[5]-uvh[1];
    float uvArea2=fabsf(uve1x*uve2y-uve1y*uve2x);
    float uvFootprint=sqrtf(uvArea2/fmaxf(screenArea2,1e-8f));
    const bool backHit =
        dot3(cross3(sub(wp1,wp0),sub(wp2,wp0)), d) > 0.f;
    int mid=(backHit && backMats && backMats[ht]>=0) ? backMats[ht] : mats[ht];
    int baseTex=-1, metallicTex=-1, roughnessTex=-1, normalTex=-1;
    int emissiveTex=-1, opacityTex=-1;
    const float* tp = 0;
    if (matTex && mid>=0 && mid<numMats){
      baseTex=matTex[mid*6+0]; metallicTex=matTex[mid*6+1];
      roughnessTex=matTex[mid*6+2]; normalTex=matTex[mid*6+3];
      emissiveTex=matTex[mid*6+4]; opacityTex=matTex[mid*6+5];
      if (matTexParam) tp=&matTexParam[mid*72];
    }
    const float* lrt = (matLightRt && mid>=0 && mid<numMats &&
                        matLightRt[mid*56+51] > 0.5f) ? &matLightRt[mid*56] : 0;
    if (mid>=0 && mid<numMats && matBase) {
      F3 mb=mk(matBase[mid*3],matBase[mid*3+1],matBase[mid*3+2]);
      if (lrt) mb=mk(lrt[0],lrt[1],lrt[2]);
      base=mk(base.x*mb.x,base.y*mb.y,base.z*mb.z);
    }
    float buu=uu,bvv=vv2,muu=uu,mvv=vv2,ruu=uu,rvv=vv2;
    float nuu=uu,nvv=vv2,euu=uu,evv=vv2,ouu=uu,ovv=vv2;
    if (tp) {
      int uvbits=(int)(tp[69]+0.5f);
      xformUv(tp,0,(uvbits&1)?uu1:uu,(uvbits&1)?vv21:vv2,&buu,&bvv);
      xformUv(tp,6,(uvbits&2)?uu1:uu,(uvbits&2)?vv21:vv2,&muu,&mvv);
      xformUv(tp,12,(uvbits&4)?uu1:uu,(uvbits&4)?vv21:vv2,&ruu,&rvv);
      xformUv(tp,18,(uvbits&8)?uu1:uu,(uvbits&8)?vv21:vv2,&nuu,&nvv);
      xformUv(tp,24,(uvbits&16)?uu1:uu,(uvbits&16)?vv21:vv2,&euu,&evv);
      xformUv(tp,30,(uvbits&32)?uu1:uu,(uvbits&32)?vv21:vv2,&ouu,&ovv);
    }
    F4 bt4=sampleTexRGBALod(texels,textures,numTextures,baseTex,buu,bvv,
        textureLod(textures,numTextures,baseTex,buu,bvv,
                   xformFootprint(tp,0,uvFootprint)));
    if (tp) bt4=mk4(bt4.x*tp[36]+tp[40],bt4.y*tp[37]+tp[41],bt4.z*tp[38]+tp[42],bt4.w*tp[39]+tp[43]);
    F3 bt=mk(bt4.x,bt4.y,bt4.z);
    base=mk(base.x*bt.x,base.y*bt.y,base.z*bt.z);
    float metallic=(mid>=0&&mid<numMats)?matPbr[mid*6+0]:0.f;
    float roughness=(mid>=0&&mid<numMats)?matPbr[mid*6+1]:0.5f;
    float opacity=(mid>=0&&mid<numMats)?matPbr[mid*6+5]:1.f;
    float alphaMode=(mid>=0&&mid<numMats&&matLightRt)?matLightRt[mid*56+54]:0.f;
    float alphaCutoff=(mid>=0&&mid<numMats&&matLightRt)?matLightRt[mid*56+55]:0.5f;
    float specWeight=1.f, coatWeight=0.f, coatRoughness=0.1f;
    F3 coatColor=mk(1.f,1.f,1.f);
    F3 emissionConst=(mid>=0&&mid<numMats)?mk(matPbr[mid*6+2],matPbr[mid*6+3],matPbr[mid*6+4]):mk(0.f,0.f,0.f);
    if (lrt) {
      metallic=lrt[40];
      roughness=lrt[42];
      opacity=lrt[39];
      specWeight=lrt[7];
      coatWeight=lrt[27];
      coatColor=mk(lrt[24],lrt[25],lrt[26]);
      coatRoughness=lrt[44];
      emissionConst=mk(lrt[32]*lrt[35], lrt[33]*lrt[35], lrt[34]*lrt[35]);
    }
    F4 mt=sampleTexRGBALod(texels,textures,numTextures,metallicTex,muu,mvv,
        textureLod(textures,numTextures,metallicTex,muu,mvv,
                   xformFootprint(tp,6,uvFootprint)));
    F4 rt=sampleTexRGBALod(texels,textures,numTextures,roughnessTex,ruu,rvv,
        textureLod(textures,numTextures,roughnessTex,ruu,rvv,
                   xformFootprint(tp,12,uvFootprint)));
    int metalCh=tp?(int)(tp[60]+0.5f):2;
    int roughCh=tp?(int)(tp[63]+0.5f):1;
    float metalS=tp?tp[61]:1.f, metalB=tp?tp[62]:0.f;
    float roughS=tp?tp[64]:1.f, roughB=tp?tp[65]:0.f;
    metallic=fminf(fmaxf(metallic*(channel4(mt,metalCh)*metalS+metalB),0.f),1.f);
    roughness=fminf(fmaxf(roughness*(channel4(rt,roughCh)*roughS+roughB),0.f),1.f);
    opacity=fminf(fmaxf(opacity*bt4.w,0.f),1.f);
    if (opacityTex>=0) {
      F4 ot=sampleTexRGBALod(texels,textures,numTextures,opacityTex,ouu,ovv,
          textureLod(textures,numTextures,opacityTex,ouu,ovv,
                     xformFootprint(tp,30,uvFootprint)));
      int opacityCh=tp?(int)(tp[66]+0.5f):0;
      float opacityS=tp?tp[67]:1.f, opacityB=tp?tp[68]:0.f;
      opacity*=channel4(ot,opacityCh)*opacityS+opacityB;
    }
    float materialOpacity=fminf(fmaxf(opacity*vertexOpacity*I->tint[3],0.f),1.f);
    if (alphaMode > 0.5f && alphaMode < 1.5f) {
      materialOpacity=(materialOpacity >= alphaCutoff) ? 1.f : 0.f;
    }
    if (I->tint[3] < 1.f && materialOpacity > 0.f) alphaMode=2.f;
    if (normalTex>=0) {
      F4 nt=sampleTexRGBAFallbackLod(texels,textures,numTextures,normalTex,nuu,nvv,
          textureLod(textures,numTextures,normalTex,nuu,nvv,
                     xformFootprint(tp,18,uvFootprint)),
          mk4(0.5f,0.5f,1.f,1.f));
      if (tp) nt=mk4(nt.x*tp[44]+tp[48],nt.y*tp[45]+tp[49],nt.z*tp[46]+tp[50],nt.w*tp[47]+tp[51]);
      else nt=mk4(nt.x*2.f-1.f,nt.y*2.f-1.f,nt.z*2.f-1.f,nt.w);
      int uvbits=tp?(int)(tp[69]+0.5f):0;
      const float* nUv=((uvbits&8)&&uvs1)?&uvs1[ht*6]:uvh;
      float tu0=nUv[0],tv0=nUv[1],tu1=nUv[2],tv1=nUv[3],tu2=nUv[4],tv2=nUv[5];
      if (tp) {
        xformUv(tp,18,tu0,tv0,&tu0,&tv0);
        xformUv(tp,18,tu1,tv1,&tu1,&tv1);
        xformUv(tp,18,tu2,tv2,&tu2,&tv2);
      }
      float du1=tu1-tu0, dv1=tv1-tv0;
      float du2=tu2-tu0, dv2=tv2-tv0;
      float det=du1*dv2-du2*dv1;
      F3 T;
      if (fabsf(det)>1e-12f) {
        float invDet=1.f/det;
        T=norm3(scale(sub(scale(sub(wp1,wp0),dv2),
                            scale(sub(wp2,wp0),dv1)),invDet));
      } else {
        F3 axis=fabsf(N.z)<0.999f?mk(0.f,0.f,1.f):mk(0.f,1.f,0.f);
        T=norm3(cross3(axis,N));
      }
      F3 B=norm3(cross3(N,T));
      N=norm3(add(add(scale(T,nt.x),scale(B,nt.y)),scale(N,nt.z)));
    }

    // Default shaded view: limited LightRT/OpenPBR preview. This is not the full
    // BSDF, but it consumes the baked OpenPBR constants for roughness, metalness,
    // specular, coat, emission and opacity while preserving the existing texture
    // sampling and debug-AOV behavior.
    F3 V=norm3(scale(d,-1.f));               // toward the eye
    F3 Ns=(dot3(N,V)<0.f)?scale(N,-1.f):N;   // forward-facing shading normal
    float head=fmaxf(dot3(Ns,V),0.f);
    // Ambient: dome SH irradiance when baked (rows 13-19; matches raster IBL),
    // else the constant headlight floor.
    float ambS=0.05f+0.35f*head;
    F3 amb3=mk(ambS,ambS,ambS);
    F3 specIBL3=mk(0.f,0.f,0.f);
    if (lightParams && numLights>0){
      for (int li=0; li<numLights; ++li){
        const float* lp=&lightParams[li*80];
        if ((int)(lp[0]+0.5f)!=6) continue;            // DomeLight
        if (!(((int)(lp[1]+0.5f))&(1<<7))) continue;   // baked IBL present
        F3 Ne=norm3(mk(lp[40]*Ns.x+lp[41]*Ns.y+lp[42]*Ns.z,
                       lp[44]*Ns.x+lp[45]*Ns.y+lp[46]*Ns.z,
                       lp[48]*Ns.x+lp[49]*Ns.y+lp[50]*Ns.z));
        float b[9];
        b[0]=0.282095f;
        b[1]=0.488603f*Ne.y; b[2]=0.488603f*Ne.z; b[3]=0.488603f*Ne.x;
        b[4]=1.092548f*Ne.x*Ne.y; b[5]=1.092548f*Ne.y*Ne.z;
        b[6]=0.315392f*(3.f*Ne.z*Ne.z-1.f);
        b[7]=1.092548f*Ne.x*Ne.z; b[8]=0.546274f*(Ne.x*Ne.x-Ne.y*Ne.y);
        F3 e=mk(0.f,0.f,0.f);
        const float* sh=&lp[52];
        for (int k=0;k<9;++k){
          e.x+=b[k]*sh[k*3+0]; e.y+=b[k]*sh[k*3+1]; e.z+=b[k]*sh[k*3+2];
        }
        amb3=mk(fmaxf(e.x,0.f)*lp[16], fmaxf(e.y,0.f)*lp[17],
                fmaxf(e.z,0.f)*lp[18]);
        // Specular IBL: the CUDA path has no prefiltered cube, so sample the
        // 8-bit latlong env (when mapped, bit 4) along the reflection vector,
        // blurred toward the normal by roughness (cheap prefilter proxy), and
        // weight by an analytic split-sum DFG (Karis' mobile approximation).
        int envTexS=(int)(lp[2]+0.5f);
        if ((((int)(lp[1]+0.5f))&(1<<4)) && envTexS>=0){
          float NoV=fmaxf(dot3(Ns,V),0.f);
          F3 R=add(scale(V,-1.f), scale(Ns, 2.f*dot3(V,Ns)));
          float tb=roughness*roughness;
          F3 Rb=norm3(add(scale(R,1.f-tb), scale(Ns,tb)));
          F3 Re=norm3(mk(lp[40]*Rb.x+lp[41]*Rb.y+lp[42]*Rb.z,
                         lp[44]*Rb.x+lp[45]*Rb.y+lp[46]*Rb.z,
                         lp[48]*Rb.x+lp[49]*Rb.y+lp[50]*Rb.z));
)CUDA"
// Second split: the tail of the previous piece was still over the 16380-byte
// cap on its own; see the NOTE above.
R"CUDA(          float su=atan2f(Re.x,-Re.z)*0.15915494f+0.5f;
          float sv=acosf(fminf(fmaxf(Re.y,-1.f),1.f))*0.31830989f;
          F3 envc=sampleTexRGB(texels,textures,numTextures,envTexS,su,sv);
          float rx=roughness*-1.f+1.f, ry=roughness*-0.0275f+0.0425f;
          float rz=roughness*-0.572f+1.04f, rw=roughness*0.022f-0.04f;
          float a004=fminf(rx*rx, exp2f(-9.28f*NoV))*rx+ry;
          float dfgx=-1.04f*a004+rz, dfgy=1.04f*a004+rw;
          F3 F0=mk(0.04f+(base.x-0.04f)*metallic,
                   0.04f+(base.y-0.04f)*metallic,
                   0.04f+(base.z-0.04f)*metallic);
          float sw=fmaxf(specWeight,0.f);
          specIBL3=mk(envc.x*(F0.x*dfgx+dfgy)*lp[16]*sw,
                      envc.y*(F0.y*dfgx+dfgy)*lp[17]*sw,
                      envc.z*(F0.z*dfgx+dfgy)*lp[18]*sw);
        }
        break;
      }
    }
    F3 em=mk(0.f,0.f,0.f);
    if (mid>=0&&mid<numMats) {
      em=emissionConst;
      F4 et4=sampleTexRGBALod(texels,textures,numTextures,emissiveTex,euu,evv,
          textureLod(textures,numTextures,emissiveTex,euu,evv,
                     xformFootprint(tp,24,uvFootprint)));
      if (tp) et4=mk4(et4.x*tp[52]+tp[56],et4.y*tp[53]+tp[57],et4.z*tp[54]+tp[58],et4.w*tp[55]+tp[59]);
      F3 et=mk(et4.x,et4.y,et4.z);
      em=mk(em.x*et.x,em.y*et.y,em.z*et.z);
    }
    F3 direct3=mk(0.f,0.f,0.f);
    bool hadDirect=false;
    float nv=fmaxf(dot3(Ns,V),1e-4f);
    for (int li=0; lightParams && li<numLights; ++li) {
      const float* lp=&lightParams[li*80];
      int lt=(int)(lp[0]+0.5f);
      if (lt==6 || lt==8) continue;
      hadDirect=true;
      F3 Ll; float maxT=1e30f, attenuation=1.f;
      if (lt==5) Ll=mul(norm3(mk(lp[8],lp[9],lp[10])),-1.0f);
      else {
        F3 toL=sub(mk(lp[4],lp[5],lp[6]),hit);
        float dist2=fmaxf(dot3(toL,toL),1e-6f);
        maxT=sqrtf(dist2); Ll=scale(toL,1.f/maxT); attenuation=1.f/dist2;
      }
      int lflags=(int)(lp[1]+0.5f);
      float shape=1.f;
      if ((lflags&4)!=0 && lt!=5) {
        float cc=dot3(norm3(mk(lp[8],lp[9],lp[10])),scale(Ll,-1.f));
        float angle=fminf(fmaxf(lp[24],0.f),180.f)*0.01745329252f;
        float soft=fminf(fmaxf(lp[25],0.f),1.f);
        float outer=cosf(angle),inner=cosf(angle*(1.f-soft));
        float den=fmaxf(inner-outer,1e-5f);
        float t=fminf(fmaxf((cc-outer)/den,0.f),1.f);
        shape=t*t*(3.f-2.f*t)*powf(fmaxf(cc,0.f),fmaxf(lp[26],0.f));
      }
      float nl=fmaxf(dot3(Ns,Ll),0.f); if (nl<=0.f || shape<=0.f) continue;
      float shadow=1.f;
      if ((lflags&2)!=0) {
        int sht,shi; float sbu,sbv;
        F3 so=add(hit,scale(Ns,1e-3f));
        float st=traverseCutout(tlas,insts,blas,tris,so,Ll,
                    fmaxf(maxT-2e-3f,1e-3f),&sht,&sbu,&sbv,&shi,cols,mats,
                    backMats,matPbr,matLightRt,numMats,matTex,matTexParam,
                    texels,textures,numTextures,uvs,uvs1);
        if (sht>=0 && st<maxT-2e-3f) shadow=0.f;
      }
      F3 Hh=norm3(add(Ll,V)); float nh=fmaxf(dot3(Ns,Hh),0.f),vh=fmaxf(dot3(V,Hh),0.f);
      F3 f0=mk(0.04f+(base.x-0.04f)*metallic,
               0.04f+(base.y-0.04f)*metallic,
               0.04f+(base.z-0.04f)*metallic);
      F3 F=fresnelSchlick(vh,f0);
      float sg=ggxD(nh,roughness)*ggxG1(nv,roughness)*ggxG1(nl,roughness)/fmaxf(4.f*nv*nl,1e-5f)*fmaxf(specWeight,0.f);
      F3 specB=scale(F,sg);
      F3 diffB=mk((1.f-F.x)*(1.f-metallic)*base.x*0.31830989f,
                  (1.f-F.y)*(1.f-metallic)*base.y*0.31830989f,
                  (1.f-F.z)*(1.f-metallic)*base.z*0.31830989f);
      F3 cF=fresnelSchlick(vh,mk(0.04f,0.04f,0.04f));
      float cg=ggxD(nh,coatRoughness)*ggxG1(nv,coatRoughness)*ggxG1(nl,coatRoughness)/fmaxf(4.f*nv*nl,1e-5f)*fminf(fmaxf(coatWeight,0.f),1.f);
      F3 coatB=mk(cF.x*cg*coatColor.x,cF.y*cg*coatColor.y,cF.z*cg*coatColor.z);
      float ds=lp[29],ss=lp[30];
      F3 brdf=add(scale(diffB,ds),scale(add(specB,coatB),ss));
      F3 rad=scale(mk(lp[12],lp[13],lp[14]),attenuation*shape*nl*shadow);
      direct3=add(direct3,mk(brdf.x*rad.x,brdf.y*rad.y,brdf.z*rad.z));
    }
    if (!hadDirect) {
      F3 Ll=norm3(mk(cam.lightDir[0],cam.lightDir[1],cam.lightDir[2]));
      float nl=fmaxf(dot3(Ns,Ll),0.f); F3 Hh=norm3(add(Ll,V));
      float nh=fmaxf(dot3(Ns,Hh),0.f),vh=fmaxf(dot3(V,Hh),0.f);
      F3 f0=mk(0.04f+(base.x-0.04f)*metallic,0.04f+(base.y-0.04f)*metallic,0.04f+(base.z-0.04f)*metallic);
      F3 F=fresnelSchlick(vh,f0);
      float sg=ggxD(nh,roughness)*ggxG1(nv,roughness)*ggxG1(nl,roughness)/fmaxf(4.f*nv*nl,1e-5f)*fmaxf(specWeight,0.f);
      direct3=mk(((1.f-F.x)*(1.f-metallic)*base.x*0.31830989f+F.x*sg)*nl,
                 ((1.f-F.y)*(1.f-metallic)*base.y*0.31830989f+F.y*sg)*nl,
                 ((1.f-F.z)*(1.f-metallic)*base.z*0.31830989f+F.z*sg)*nl);
    }
    F3 ambientBase=mk(base.x*amb3.x,base.y*amb3.y,base.z*amb3.z);
    outc=add(add(add(ambientBase,direct3),specIBL3),em);
    // Render mode (clear[3]): 1=wireframe, 2=shading normal, 3=material-id,
    // 4=geometric normal, 5=uv set 0, 6=depth (t/depthScale in lightDir[3]).
    int rmode=(int)(cam.clear[3]+0.5f);
    if (rmode==0) {
      F3 bg=mk(cam.clear[0],cam.clear[1],cam.clear[2]);
      if (alphaMode > 0.5f && alphaMode < 1.5f && materialOpacity <= 0.f) {
        outc=bg;
      } else if (alphaMode > 1.5f && materialOpacity < 1.f) {
        outc=mk(bg.x+(outc.x-bg.x)*materialOpacity,
                bg.y+(outc.y-bg.y)*materialOpacity,
                bg.z+(outc.z-bg.z)*materialOpacity);
      }
    }
    if (rmode==1){
      // Original-polygon wireframe with screen-space analytic AA (usdview look):
      // project the triangle's world verts to pixels and measure the sample's
      // pixel distance to each REAL polygon edge (emask drops triangulation
      // diagonals), then feather. emask==7 (no polygon data) draws all 3 edges.
      unsigned em = emask ? emask[ht] : 7u;
      if (em==0u) em=7u;
      float sampX=px+0.5f+cam.sceneMin[3], sampY=py+0.5f+cam.sceneExtent[3];
      F3 P0=projPix(cam.vp,wp0,(float)W,(float)H);
      F3 P1=projPix(cam.vp,wp1,(float)W,(float)H);
      F3 P2=projPix(cam.vp,wp2,(float)W,(float)H);
      float dmin=1e30f;
      if ((em&1u)&&P1.z>0.f&&P2.z>0.f) dmin=fminf(dmin,pixDistSeg(sampX,sampY,P1.x,P1.y,P2.x,P2.y));
      if ((em&2u)&&P2.z>0.f&&P0.z>0.f) dmin=fminf(dmin,pixDistSeg(sampX,sampY,P2.x,P2.y,P0.x,P0.y));
      if ((em&4u)&&P0.z>0.f&&P1.z>0.f) dmin=fminf(dmin,pixDistSeg(sampX,sampY,P0.x,P0.y,P1.x,P1.y));
      const float hw=1.3f;  // pixels: ~1px line + 1px AA edge (matches the GL path)
      float cov=1.f-fminf(fmaxf((dmin-(hw-1.f))/1.f,0.f),1.f);  // smooth-ish feather
      cov=cov*cov*(3.f-2.f*cov);  // smoothstep
      // Diagnostic wire color: never tint it with the scene/preview light.
      F3 wireCol=mk(0.04f,0.24f,0.10f);
      F3 bg=mk(cam.clear[0],cam.clear[1],cam.clear[2]);
      outc=mk(bg.x+(wireCol.x-bg.x)*cov, bg.y+(wireCol.y-bg.y)*cov, bg.z+(wireCol.z-bg.z)*cov);
    } else if (rmode==2){
      outc=mk(N.x*0.5f+0.5f, N.y*0.5f+0.5f, N.z*0.5f+0.5f);
    } else if (rmode==3){
      outc=idColor(mid);
    } else if (rmode==4){
      F3 Ng=norm3(cross3(sub(wp1,wp0),sub(wp2,wp0)));
      if (dot3(Ng,d)>0.f) Ng=scale(Ng,-1.f);
      outc=mk(Ng.x*0.5f+0.5f, Ng.y*0.5f+0.5f, Ng.z*0.5f+0.5f);
    } else if (rmode==5){
      outc=mk(uu-floorf(uu), vv2-floorf(vv2), 0.f);
    } else if (rmode==6){
      float dd=fminf(fmaxf(t/fmaxf(cam.lightDir[3],1e-3f),0.f),1.f);
      outc=mk(1.f-dd,1.f-dd,1.f-dd);
    } else if (rmode==7){
      outc=base;  // albedo (pre-lighting)
    } else if (rmode==8){  // facing
      outc=(dot3(cross3(sub(wp1,wp0),sub(wp2,wp0)),d)<0.f)?mk(0.1f,0.7f,0.1f):mk(0.7f,0.1f,0.1f);
    } else if (rmode==9){  // roughness
      outc=mk(roughness,roughness,roughness);
    } else if (rmode==10){  // metallic
      outc=mk(metallic,metallic,metallic);
    } else if (rmode==11){  // emissive
      outc=em;
    } else if (rmode==12){  // opacity
      outc=mk(materialOpacity,materialOpacity,materialOpacity);
    } else if (rmode==13){  // world position
      outc=mk(fminf(fmaxf((hit.x-cam.sceneMin[0])/fmaxf(cam.sceneExtent[0],1e-4f),0.f),1.f),
              fminf(fmaxf((hit.y-cam.sceneMin[1])/fmaxf(cam.sceneExtent[1],1e-4f),0.f),1.f),
              fminf(fmaxf((hit.z-cam.sceneMin[2])/fmaxf(cam.sceneExtent[2],1e-4f),0.f),1.f));
    } else if (rmode==23){  // uv checker
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
        traverseCutout(tlas,insts,blas,tris,so,sd,aoR,&sht,&su,&sv,&si,
                       cols,mats,backMats,matPbr,matLightRt,numMats,matTex,matTexParam,
                       texels,textures,numTextures,uvs,uvs1);
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
        traverseCutout(tlas,insts,blas,tris,so,sd,1e30f,&sht,&su,&sv,&si,
                       cols,mats,backMats,matPbr,matLightRt,numMats,matTex,matTexParam,
                       texels,textures,numTextures,uvs,uvs1);
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
  if (rmodePre==0) {
    float exposureScale=exp2f(cam.camPos[3]);
    outc=scale(outc,exposureScale);
  }
  if (rmodePre==0 || rmodePre==1) {
    outc=mk(linearToSrgb1(outc.x),linearToSrgb1(outc.y),linearToSrgb1(outc.z));
  }
  int idx=(py*W+px)*4;
  unsigned char qr=(unsigned char)(fminf(fmaxf(outc.x,0.f),1.f)*255.f+0.5f);
  unsigned char qg=(unsigned char)(fminf(fmaxf(outc.y,0.f),1.f)*255.f+0.5f);
  unsigned char qb=(unsigned char)(fminf(fmaxf(outc.z,0.f),1.f)*255.f+0.5f);
  if (numSamples<=1 || !accum){
    out[idx+0]=qr; out[idx+1]=qg; out[idx+2]=qb; out[idx+3]=255;
    return;
  }
  // Supersampled AA: accumulate the QUANTIZED sample on the device (identical
  // math to the old host loop, so the averaged image is byte-identical) and
  // resolve on the last sample -- one readback for the whole frame instead of
  // one per sample.
  float ar=(sampleIdx==0)?(float)qr:accum[idx+0]+(float)qr;
  float ag=(sampleIdx==0)?(float)qg:accum[idx+1]+(float)qg;
  float ab=(sampleIdx==0)?(float)qb:accum[idx+2]+(float)qb;
  accum[idx+0]=ar; accum[idx+1]=ag; accum[idx+2]=ab;
  if (sampleIdx==numSamples-1){
    out[idx+0]=(unsigned char)(ar/(float)numSamples+0.5f);
    out[idx+1]=(unsigned char)(ag/(float)numSamples+0.5f);
    out[idx+2]=(unsigned char)(ab/(float)numSamples+0.5f);
    out[idx+3]=255;
  }
}
)CUDA";
