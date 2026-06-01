#pragma once

// USDZ writer and validator unit tests

void usdz_writer_basic_roundtrip_test(void);
void usdz_writer_with_assets_test(void);
void usdz_validator_alignment_test(void);
void usdz_validator_crc32_test(void);
void usdz_validator_size_consistency_test(void);
void usdz_validator_empty_input_test(void);
void usdz_writer_file_roundtrip_test(void);
void usdz_validator_large_asset_test(void);
void usdz_validator_bad_extension_test(void);
void usdz_writer_rejects_unsafe_asset_names_test(void);
