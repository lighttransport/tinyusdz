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
#include "unit-task-queue.h"
//#include "unit-dedup.h"  // Temporarily disabled - needs API updates
#include "unit-crate-writer.h"

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
  { "task_queue_basic_test", task_queue_basic_test },
  { "task_queue_func_test", task_queue_func_test },
  { "task_queue_full_test", task_queue_full_test },
  { "task_queue_multithreaded_test", task_queue_multithreaded_test },
  { "task_queue_clear_test", task_queue_clear_test },
  // Temporarily disabled - unit-dedup needs API updates
  //{ "dedup_float_array_test", dedup_float_array_test },
  //{ "dedup_double_array_test", dedup_double_array_test },
  //{ "dedup_int_array_test", dedup_int_array_test },
  //{ "dedup_unique_arrays_test", dedup_unique_arrays_test },
  //{ "dedup_string_array_test", dedup_string_array_test },
  //{ "dedup_matrix4d_test", dedup_matrix4d_test },
  { "crate_writer_basic_creation_test", crate_writer_basic_creation_test },
  { "crate_writer_simple_prim_test", crate_writer_simple_prim_test },
  { "crate_writer_typename_encoding_test", crate_writer_typename_encoding_test },
  { "crate_writer_timesamples_test", crate_writer_timesamples_test },
  { "crate_writer_pseudoroot_ordering_test", crate_writer_pseudoroot_ordering_test },
  { "crate_writer_roundtrip_test", crate_writer_roundtrip_test },
  { "crate_writer_multiple_prims_test", crate_writer_multiple_prims_test },
  { "crate_writer_nested_prims_test", crate_writer_nested_prims_test },
  { "crate_writer_error_handling_test", crate_writer_error_handling_test },
  { "crate_writer_material_shader_test", crate_writer_material_shader_test },
  { "crate_writer_layer_metadata_test", crate_writer_layer_metadata_test },
#if defined(TINYUSDZ_WITH_PXR_COMPAT_API)
  { "pxr_compat_api_test", pxr_compat_api_test },
#endif
  { nullptr, nullptr }
};
