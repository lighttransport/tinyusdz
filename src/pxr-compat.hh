// SPDX-License-Identifier: Apache 2.0

//
// Experimental pxr USD compatible API
//
#pragma once

#include <memory>
#include <string>

#include "prim-types.hh"

#if defined(PXR_STATIC)
  #define USD_API
#else
  #if defined(USD_EXRPORTS)
    #if defined(_WIN32)
      #if defined(_MSC_VER)
        #define USD_API __declspec(dllexport)
      #else
        // assume gcc or clang
        #define USD_API __attribute__((dllexport))
      #endif
    #else
        #define USD_API
    #endif
  #else // import

    #if defined(_WIN32)
      #if defined(_MSC_VER)
        #define USD_API __declspec(dllimport)
      #else
        // assume gcc or clang
        #define USD_API __attribute__((dllimport))
      #endif
    #else
      #define USD_API
    #endif
  #endif

#endif

#define PXR_INTERNAL_NS pxr

namespace pxr {

//namespace Sdf {

struct SdfLayer;

// pxr USD uses special pointer class(shared_ptr + alpha) for Handle, but we simply use shared_ptr.
using SdfLayerHandle = std::shared_ptr<SdfLayer>;


//} // namespae Sdf

//namespace Usd {

struct UsdStage;

using UsdStagePtr = std::weak_ptr<UsdStage>;
using UsdStageRefPtr = std::shared_ptr<UsdStage>;
typedef UsdStagePtr UsdStageWeakPtr; 

// prim could be invalid(empty)
struct UsdPrim
{
  UsdPrim() : _prim(nullptr) {}
  UsdPrim(tinyusdz::GPrim *prim) : _prim(prim) {}

  bool IsValid() const {
    if (!_prim) {
      return false;
    }

    // TODO: if (IsConcrete(_type))

    return true;
  }

  explicit operator bool() {
    return IsValid();
  }

  tinyusdz::GPrim *_prim{nullptr};
};

struct SdfPath
{
  std::string path;
};

struct UsdStage
{

  enum InitialLoadSet
  {
    LoadAll,
    LoadNone
  };

  USD_API static UsdStageRefPtr CreateNew(const std::string &filepath, InitialLoadSet = LoadAll);
  USD_API static UsdStageRefPtr CreateInMemory(InitialLoadSet = LoadAll);

  USD_API static UsdStageRefPtr Open(const std::string &filepath, InitialLoadSet = LoadAll);

  USD_API void Save();
  USD_API void SaveSessionLayers();

  USD_API bool Export(const std::string &filename, bool addSourceFileComments=true) const;
  USD_API bool ExportToString(std::string *result, bool addSourceFileComments=true) const;

  // returns invalid(empty) prim if corresponding path does not exit in the stage.
  USD_API UsdPrim GetPrimAtPath(const SdfPath &path);


};

//} // namespace Usd;
} // namespace pxr
