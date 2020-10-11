#include <iostream>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <pxr/base/tf/weakBase.h>
#include <pxr/base/tf/weakPtr.h>
#include <pxr/usd/usd/notice.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xformable.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif


PXR_NAMESPACE_USING_DIRECTIVE

int main(int argc, char **argv)
{
  if (argc < 2) {
    std::cout << "Need input.usd[a|c|z]\n";
    return -1;
  }

  std::string filename = argv[1];

  auto supported = UsdStage::IsSupportedFile(filename);
  if (!supported) {
    std::cerr << "Unsupported USD format. filename = " << filename << "\n";
  }

  return 0;
}

