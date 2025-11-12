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
#include "unit-pprint.h"
#include "unit-materialx.h"
#include "unit-task-queue.h"
#include "unit-typedarray.h"

#if defined(TINYUSDZ_WITH_PXR_COMPAT_API)
#include "unit-pxr-compat-api.h"
#endif



TEST_LIST = {
  { "ascii_parse_int64_valid_test", ascii_parse_int64_valid_test },
  { "ascii_parse_int64_excessive_digits_test", ascii_parse_int64_excessive_digits_test },
  { "ascii_parse_uint64_valid_test", ascii_parse_uint64_valid_test },
  { "ascii_parse_uint64_excessive_digits_test", ascii_parse_uint64_excessive_digits_test },
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
  { "task_queue_basic_test", task_queue_basic_test },
  { "task_queue_func_test", task_queue_func_test },
  { "task_queue_full_test", task_queue_full_test },
  { "task_queue_multithreaded_test", task_queue_multithreaded_test },
  { "task_queue_clear_test", task_queue_clear_test },
  // TypedArray tests
  { "typedarray_default_constructor", typedarray_default_constructor_test },
  { "typedarray_size_constructor", typedarray_size_constructor_test },
  { "typedarray_value_constructor", typedarray_value_constructor_test },
  { "typedarray_initializer_list", typedarray_initializer_list_test },
  { "typedarray_raw_data_constructor", typedarray_raw_data_constructor_test },
  { "typedarray_copy_constructor", typedarray_copy_constructor_test },
  { "typedarray_copy_assignment", typedarray_copy_assignment_test },
  { "typedarray_move_constructor", typedarray_move_constructor_test },
  { "typedarray_move_assignment", typedarray_move_assignment_test },
  { "typedarray_push_back", typedarray_push_back_test },
  { "typedarray_pop_back", typedarray_pop_back_test },
  { "typedarray_resize", typedarray_resize_test },
  { "typedarray_resize_with_value", typedarray_resize_with_value_test },
  { "typedarray_clear", typedarray_clear_test },
  { "typedarray_reserve", typedarray_reserve_test },
  { "typedarray_element_access", typedarray_element_access_test },
  { "typedarray_const_element_access", typedarray_const_element_access_test },
  { "typedarray_iterator", typedarray_iterator_test },
  { "typedarray_const_iterator", typedarray_const_iterator_test },
  { "typedarray_to_value", typedarray_to_value_test },
  { "typedarray_value_copy", typedarray_value_copy_test },
  { "typedarray_value_move", typedarray_value_move_test },
  { "typedarray_value_different_types", typedarray_value_different_types_test },
  { "typedarray_value_type_mismatch", typedarray_value_type_mismatch_test },
  { "typedarray_usd_types", typedarray_usd_types_test },
  { "typedarray_buffer_access", typedarray_buffer_access_test },
  { "typedarray_packed_value", typedarray_packed_value_test },
  { "typedarray_empty_operations", typedarray_empty_operations_test },
  { "typedarray_single_element", typedarray_single_element_test },
  { "typedarray_large_array", typedarray_large_array_test },
  { "typedarray_view", typedarray_view_test },
  { "typedarray_const_view", typedarray_const_view_test },
  { "typedarray_assign", typedarray_assign_test },
  { "typedarray_swap", typedarray_swap_test },
#if defined(TINYUSDZ_WITH_PXR_COMPAT_API)
  { "pxr_compat_api_test", pxr_compat_api_test },
#endif
  { nullptr, nullptr }
};
