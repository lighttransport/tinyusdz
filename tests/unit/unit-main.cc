#ifdef _MSC_VER
#define NOMINMAX
#endif

#include "acutest.h"

#include "unit-ascii-parse.h"
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
#include "unit-fp-parse-print.h"
#include "unit-pprint.h"
#include "unit-materialx.h"
#include "unit-task-queue.h"
#include "unit-stage.h"
#include "unit-tiny-container.h"

#if defined(TINYUSDZ_WITH_PXR_COMPAT_API)
#include "unit-pxr-compat-api.h"
#endif



TEST_LIST = {
  { "ascii_parse_int64_valid_test", ascii_parse_int64_valid_test },
  { "ascii_parse_int64_excessive_digits_test", ascii_parse_int64_excessive_digits_test },
  { "ascii_parse_uint64_valid_test", ascii_parse_uint64_valid_test },
  { "ascii_parse_uint64_excessive_digits_test", ascii_parse_uint64_excessive_digits_test },
  { "ascii_parse_string_array_test", ascii_parse_string_array_test },
  { "prim_type_test", prim_type_test },
  { "prim_add_test", prim_add_test },
  { "primvar_test", primvar_test },
  { "value_types_test", value_types_test },
  { "role_type_cast_test", role_type_cast_test },
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
  { "fp_string_conversion_test", fp_string_conversion_test },
  { "timesamples_test", timesamples_test },
  { "fp_roundtrip_basic_test", fp_roundtrip_basic_test },
  { "fp_roundtrip_edge_cases_test", fp_roundtrip_edge_cases_test },
  { "fp_roundtrip_special_values_test", fp_roundtrip_special_values_test },
  { "fp_roundtrip_precision_test", fp_roundtrip_precision_test },
  { "fp_roundtrip_buffer_test", fp_roundtrip_buffer_test },
  { "fp_shortest_representation_test", fp_shortest_representation_test },
  { "materialx_config_api_struct_test", materialx_config_api_struct_test },
  { "materialx_config_api_parsing_test", materialx_config_api_parsing_test },
  { "openpbr_surface_reconstruction_test", openpbr_surface_reconstruction_test },
  { "mtlx_standard_surface_reconstruction_test", mtlx_standard_surface_reconstruction_test },
  { "nodegraph_support_test", nodegraph_support_test },
  { "materialx_shader_constants_test", materialx_shader_constants_test },
  { "materialx_shader_fallback_values_test", materialx_shader_fallback_values_test },
  { "task_queue_basic_test", task_queue_basic_test },
  { "task_queue_func_test", task_queue_func_test },
  { "task_queue_full_test", task_queue_full_test },
  { "task_queue_multithreaded_test", task_queue_multithreaded_test },
  { "task_queue_clear_test", task_queue_clear_test },
  { "stage_get_prim_at_path_test", stage_get_prim_at_path_test },
  { "stage_find_prim_by_id_test", stage_find_prim_by_id_test },
  { "stack_vector_basic_test", stack_vector_basic_test },
  { "stack_vector_overflow_test", stack_vector_overflow_test },
  { "stack_vector_copy_test", stack_vector_copy_test },
  { "stack_vector_move_test", stack_vector_move_test },
  { "stack_vector_iterator_test", stack_vector_iterator_test },
  { "stack_vector_complex_type_test", stack_vector_complex_type_test },
#if defined(TINYUSDZ_WITH_PXR_COMPAT_API)
  { "pxr_compat_api_test", pxr_compat_api_test },
#endif
  { nullptr, nullptr }
};
