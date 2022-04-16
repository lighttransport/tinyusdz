#ifdef _MSC_VER
#define NOMINMAX
#endif

#include "acutest.h"

#include "unit-prim-types.h"
#include "unit-primvar.h"

#if defined(TINYUSDZ_WITH_PXR_COMPAT_API)
#include "unit-pxr-compat-api.h"
#endif



TEST_LIST = {
  { "prim_type_test", prim_type_test },
  { "primvar_test", primvar_test },
#if defined(TINYUSDZ_WITH_PXR_COMPAT_API)
  { "pxr_compat_api_test", pxr_compat_api_test },
#endif
  { nullptr, nullptr }
};
