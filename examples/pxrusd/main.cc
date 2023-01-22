#include <cfloat>
#include <iostream>
#include <sstream>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <pxr/base/tf/weakBase.h>
#include <pxr/base/tf/weakPtr.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/quaternion.h>
#include <pxr/usd/usd/notice.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/basisCurves.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif


PXR_NAMESPACE_USING_DIRECTIVE

namespace {

std::string Indent(int level)
{
  std::stringstream ss;
  for (int i = 0; i < level; i++) {
    ss << "  ";
  }

  return ss.str();
}

void Traverse(const UsdPrim &prim, int depth)
{
  std::cout << Indent(depth);
  printf("path: %s (ty: %s)\n", prim.GetPath().GetString().c_str(), prim.GetTypeName().GetText());

  if (prim.GetTypeName().GetString() == "Mesh") {
    UsdGeomMesh mesh(prim);

    VtVec3fArray points;
    mesh.GetPointsAttr().Get(&points);

    std::cout << Indent(depth + 1) << "# of vertices = " << points.size() << ", data = [\n";

    for (size_t i = 0; i < points.size(); i++) {
      std::cout << points[i][0] << ", " << points[i][1] << ", " << points[i][2] << "\n";
    }

    std::cout << "]\n";

  } else if (prim.GetTypeName().GetString() == "BasisCurves") {

    UsdGeomBasisCurves curve(prim);

    VtVec3fArray points;
    curve.GetPointsAttr().Get(&points);

    std::cout << Indent(depth + 1) << "# of vertices = " << points.size() << ", data = [\n";

    for (size_t i = 0; i < points.size(); i++) {
      std::cout << points[i][0] << ", " << points[i][1] << ", " << points[i][2] << "\n";
    }

    std::cout << "]\n";

    VtIntArray curveVertexCounts;
    curve.GetCurveVertexCountsAttr().Get(&curveVertexCounts);

    std::cout << Indent(depth + 1) << "# of curveVertexCounts = " << curveVertexCounts.size() << ", data = [\n";

    for (size_t i = 0; i < curveVertexCounts.size(); i++) {
      std::cout << curveVertexCounts[i] << "\n";
    }

    std::cout << "]\n";

  }

  for (UsdPrim const& c : prim.GetChildren()) {
    Traverse(c, depth + 1);
  }

}

} // namespace

struct Dbl {
  union {
    uint64_t u;
    double f;
  };
} ;

static void pxrusd_test()
{
  GfMatrix4d m;
  double kPI = 3.141592653589793;


  
  double rot_angle = 360.0 - std::numeric_limits<double>::epsilon();
  double s = std::sin(0.5 * rot_angle * kPI / 180.0);
  double c = std::cos(0.5 * rot_angle * kPI / 180.0);

  Dbl sv, cv;
  sv.f = s;
  cv.f = c;
  

  std::cout << "s == c" << (sv.u == cv.u) << "\n";
  std::cout << "s = " << s << "\n";
  std::cout << "c = " << c << "\n";

  GfRotation rot;
  rot.SetAxisAngle(GfVec3d(0.0, 0.0, 1.0), rot_angle);

  GfQuaternion q = rot.GetQuaternion();

  std::cout << "q = " << q << "\n";

  double w = q.GetReal();
  GfVec3d imag = q.GetImaginary();
  // qx * qx - qy * qy - qz * qz + qw * qw
  // 1.0 - 2 * (qz * qz + qw * qw)

  //double qx = 1.0 - 2.0 * (imag[1] * imag[1] + imag[2] * imag[2]);
  double qx = 2.0 * (0.5 - (imag[1] * imag[1] + imag[2] * imag[2]));
  std::cout << "qx = " << qx << "\n";
  qx = (w * w + imag[0] * imag[0] - imag[1] * imag[1] - imag[2] * imag[2]);
  std::cout << "qx = " << qx << "\n";

  m.SetRotate(rot);

  std::cout << "m = " << m << "\n";
}

int main(int argc, char **argv)
{
  pxrusd_test();

  if (argc < 2) {
    std::cout << "Need input.usd[a|c|z]\n";
    return -1;
  }

  std::string filename = argv[1];

  auto supported = UsdStage::IsSupportedFile(filename);
  if (!supported) {
    std::cerr << "Unsupported USD format. filename = " << filename << "\n";
  }

  UsdStageRefPtr loadedStage = UsdStage::Open(filename);

  if (loadedStage)
  {
      auto pseudoRoot = loadedStage->GetPseudoRoot();
      Traverse(pseudoRoot, 0);

      //printf("Pseudo root path: %s\n", pseudoRoot.GetPath().GetString().c_str());
      //for (UsdPrim const& c : pseudoRoot.GetChildren())
      //{
      //    printf("\tChild path: %s\n", c.GetPath().GetString().c_str());
      //}
  }
  else
  {
      fprintf(stderr, "Stage was not loaded");
  }

  return 0;
}

