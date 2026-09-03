// SPDX-License-Identifier: Apache-2.0
// Unit test for lusdrender's per-instance RT LOD selection (lusdr_rt_lod):
// near->Full, mid->Proxy, sub-pixel->Cull, behind/off-screen->Cull (frustum),
// frustum-off keeps off-screen, proxy-off => Full-or-Cull, degenerate/disabled
// => Full, and BoxFitO2W maps the unit cube onto a prototype AABB.
#include <cstdio>
#include "lusdr_rt_lod.hh"
#include "lusdr_math.hh"
using namespace lusdr;
static int fails=0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s (line %d)\n",m,__LINE__); ++fails;} }while(0)

static void ident(float o[12]){ float m[12]={1,0,0,0,0,1,0,0,0,0,1,0}; for(int i=0;i<12;i++)o[i]=m[i]; }
static void trans(float o[12],float x,float y,float z){ ident(o); o[3]=x;o[7]=y;o[11]=z; }

int main(){
  CameraFrame cam;  // default: origin 0, forward -Z, up +Y, right +X, yfov 45deg
  cam.origin = Vec3{0,0,10};
  cam.forward = Vec3{0,0,-1};
  cam.right = Vec3{1,0,0};
  cam.up = Vec3{0,1,0};
  cam.yfov = 45.0f*3.14159265f/180.0f;
  cam.znear = 0.05f; cam.zfar = 1e9f;
  RtLodView view = MakeRtLodView(cam, 1080);

  RtLodConfig cfg; cfg.enabled=true; cfg.proxy=true; cfg.frustum_cull=true;
  cfg.full_px=64; cfg.cull_px=2;
  Bounds unit; unit.lo=Vec3{-1,-1,-1}; unit.hi=Vec3{1,1,1}; unit.valid=true;

  float o[12];
  // Near, in front (z=0, depth ~10): big -> Full
  trans(o,0,0,0);   CHECK(ClassifyInstance(view,cfg,o,unit)==RtLod::Full,"near->Full");
  // Mid (z=-200, depth ~210): smaller -> Proxy
  trans(o,0,0,-200);CHECK(ClassifyInstance(view,cfg,o,unit)==RtLod::Proxy,"mid->Proxy");
  // Very far (z=-50000): sub-pixel -> Cull
  trans(o,0,0,-50000);CHECK(ClassifyInstance(view,cfg,o,unit)==RtLod::Cull,"far->Cull");
  // Behind camera (z=+200): frustum near-plane cull
  trans(o,0,0,200); CHECK(ClassifyInstance(view,cfg,o,unit)==RtLod::Cull,"behind->Cull");
  // Far off to the side at mid-depth: frustum side cull
  trans(o,100000,0,-200); CHECK(ClassifyInstance(view,cfg,o,unit)==RtLod::Cull,"offscreen->Cull");

  // frustum_cull OFF: behind-camera no longer culled by frustum, but sub-pixel still
  cfg.frustum_cull=false;
  trans(o,0,0,200);  // behind: depth negative -> clamped to near -> px huge -> Full
  CHECK(ClassifyInstance(view,cfg,o,unit)==RtLod::Full,"behind no-frustum->Full(kept)");

  // proxy OFF: mid becomes Full (only Full-or-Cull)
  cfg.proxy=false; cfg.frustum_cull=true;
  trans(o,0,0,-200); CHECK(ClassifyInstance(view,cfg,o,unit)==RtLod::Full,"mid no-proxy->Full");

  // degenerate AABB -> always Full
  cfg.proxy=true; Bounds deg; deg.lo=Vec3{0,0,0}; deg.hi=Vec3{0,0,0};
  trans(o,0,0,-50000); CHECK(ClassifyInstance(view,cfg,o,deg)==RtLod::Full,"degenerate->Full");

  // disabled -> Full
  cfg.enabled=false; trans(o,0,0,-50000);
  CHECK(ClassifyInstance(view,cfg,o,unit)==RtLod::Full,"disabled->Full");

  // BoxFitO2W: unit cube [0,1] onto [mn,mx] at translate -> places corner at mn+translate
  float bf[12]; trans(o,5,6,7);
  BoxFitO2W(o, Vec3{-2,-2,-2}, Vec3{2,2,2}, bf);
  // corner (0,0,0)->mn(-2,-2,-2)+translate(5,6,7)=(3,4,5); corner(1,1,1)->mx+translate=(7,8,9)
  Vec3 c0 = TransformPointO2W(bf, Vec3{0,0,0});
  Vec3 c1 = TransformPointO2W(bf, Vec3{1,1,1});
  CHECK(std::abs(c0.x-3)<1e-3 && std::abs(c0.y-4)<1e-3 && std::abs(c0.z-5)<1e-3,"boxfit c0");
  CHECK(std::abs(c1.x-7)<1e-3 && std::abs(c1.y-8)<1e-3 && std::abs(c1.z-9)<1e-3,"boxfit c1");

  if(!fails){ printf("test_lusdr_rt_lod: ALL PASS\n"); return 0; }
  printf("test_lusdr_rt_lod: %d FAIL(s)\n",fails); return 1;
}
