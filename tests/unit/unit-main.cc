#ifdef _MSC_VER
#define NOMINMAX
#endif

#include "acutest.h"

#include "unit-prim-types.h"
#include "unit-primvar.h"
#include "unit-pathutil.h"
#include "unit-value-types.h"
#include "unit-xform.h"
#include "unit-customdata.h"
#include "unit-handle-allocator.h"
#include "unit-math.h"
#include "unit-ioutil.h"
#include "unit-strutil.h"
#include "unit-timesamples.h"
#include "unit-pprint.h"
#include "unit-materialx.h"

#if defined(TINYUSDZ_WITH_PXR_COMPAT_API)
#include "unit-pxr-compat-api.h"
#endif



TEST_LIST = {
  { "prim_type_test", prim_type_test },
  { "prim_add_test", prim_add_test },
  { "primvar_test", primvar_test },
  { "value_types_test", value_types_test },
  { "xformOp_test", xformOp_test },
  { "customdata_test", customdata_test },
  { "handle_allocator_test", handle_allocator_test },
  { "math_cos_pi_test", math_cos_pi_test },
  { "math_sin_pi_test", math_sin_pi_test },
  { "math_sin_cos_pi_test", math_sin_cos_pi_test },
  { "pathutil_test", pathutil_test },
  { "ioutil_test", ioutil_test },
  { "strutil_test", strutil_test },
  { "tinystring_test", tinystring_test },
  { "parse_int_test", parse_int_test },
  { "timesamples_test", timesamples_test },
  { "materialx_config_api_struct_test", materialx_config_api_struct_test },
  { "materialx_config_api_parsing_test", materialx_config_api_parsing_test },
  { "openpbr_surface_reconstruction_test", openpbr_surface_reconstruction_test },
  { "mtlx_standard_surface_reconstruction_test", mtlx_standard_surface_reconstruction_test },
  { "nodegraph_support_test", nodegraph_support_test },
  { "materialx_shader_constants_test", materialx_shader_constants_test },
  { "materialx_shader_fallback_values_test", materialx_shader_fallback_values_test },
#if defined(TINYUSDZ_WITH_PXR_COMPAT_API)
  { "pxr_compat_api_test", pxr_compat_api_test },
#endif
  { nullptr, nullptr }
};
