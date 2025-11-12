// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment Inc.

#pragma once

// Basic construction
void typedarray_default_constructor_test(void);
void typedarray_size_constructor_test(void);
void typedarray_value_constructor_test(void);
void typedarray_initializer_list_test(void);
void typedarray_raw_data_constructor_test(void);

// Copy and move
void typedarray_copy_constructor_test(void);
void typedarray_copy_assignment_test(void);
void typedarray_move_constructor_test(void);
void typedarray_move_assignment_test(void);

// Modifiers
void typedarray_push_back_test(void);
void typedarray_pop_back_test(void);
void typedarray_resize_test(void);
void typedarray_resize_with_value_test(void);
void typedarray_clear_test(void);
void typedarray_reserve_test(void);

// Element access
void typedarray_element_access_test(void);
void typedarray_const_element_access_test(void);

// Iterators
void typedarray_iterator_test(void);
void typedarray_const_iterator_test(void);

// Value integration
void typedarray_to_value_test(void);
void typedarray_value_copy_test(void);
void typedarray_value_move_test(void);
void typedarray_value_different_types_test(void);
void typedarray_value_type_mismatch_test(void);

// USD types
void typedarray_usd_types_test(void);

// Buffer integration
void typedarray_buffer_access_test(void);
void typedarray_packed_value_test(void);

// Edge cases
void typedarray_empty_operations_test(void);
void typedarray_single_element_test(void);
void typedarray_large_array_test(void);

// TypedArrayView
void typedarray_view_test(void);
void typedarray_const_view_test(void);

// Assignment
void typedarray_assign_test(void);
void typedarray_swap_test(void);
