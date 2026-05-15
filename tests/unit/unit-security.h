#pragma once

void security_empty_input_test(void);
void security_truncated_header_test(void);
void security_null_bytes_test(void);
void security_deeply_nested_test(void);
void security_huge_array_test(void);
void security_malformed_utf8_test(void);
void security_recursive_reference_test(void);
void security_json_oversized_base64_rejected_test(void);
void security_unsafe_asset_path_rejected_test(void);
void security_json_array_count_mismatch_rejected_test(void);
void security_json_point3f_count_overflow_rejected_test(void);
void security_resolver_oversized_custom_asset_rejected_test(void);
void security_nested_zstd_depth_rejected_test(void);
