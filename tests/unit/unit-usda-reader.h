// SPDX-License-Identifier: Apache 2.0
// USDA reader unit tests

#pragma once

// Basic Types
void usda_reader_scalar_int_float_double_test(void);
void usda_reader_scalar_string_token_path_test(void);
void usda_reader_vector_matrix_types_test(void);

// Arrays
void usda_reader_array_int_float_test(void);
void usda_reader_array_string_token_test(void);
void usda_reader_array_vector_types_test(void);

// TimeSamples
void usda_reader_timesamples_scalar_test(void);
void usda_reader_timesamples_array_test(void);
void usda_reader_timesamples_blocked_test(void);
void usda_reader_timesamples_token_enum_test(void);

// Connections & Relationships
void usda_reader_attribute_connection_test(void);
void usda_reader_relationship_test(void);

// Metadata
void usda_reader_prim_metadata_test(void);
void usda_reader_stage_metadata_test(void);

// Variants & Composition
void usda_reader_variantset_basic_test(void);
void usda_reader_variantset_with_properties_test(void);
void usda_reader_class_inherits_test(void);
void usda_reader_internal_reference_test(void);

// Hierarchy & Specifiers
void usda_reader_nested_hierarchy_test(void);
void usda_reader_specifiers_def_over_class_test(void);

// Edge Cases
void usda_reader_trailing_comma_test(void);
void usda_reader_empty_prim_test(void);
void usda_reader_unicode_and_special_strings_test(void);

// Error Handling
void usda_reader_malformed_input_test(void);
void usda_reader_large_nesting_depth_test(void);
