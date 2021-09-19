#include "pxr-compat.hh"

#include "tinyusdz.hh"

namespace pxr {

UsdStageRefPtr UsdStage::Open(const std::string &filepath, InitialLoadSet loadset) {
  (void)filepath;
  (void)loadset;

  // TODO:
  return std::shared_ptr<UsdStage>(nullptr);
};

UsdStageRefPtr UsdStage::CreateNew(const std::string &filepath, InitialLoadSet loadset) {
  (void)filepath;
  (void)loadset;

  // TODO:
  return std::shared_ptr<UsdStage>(nullptr);
};

UsdPrim UsdStage::GetPrimAtPath(const SdfPath &path) {
  (void)path;

  return UsdPrim();
};

} // namespace pxr
