#pragma once

// usdz-convert pipeline + texture op + fpnge unit tests

void usdz_convert_png_roundtrip_test(void);
void usdz_convert_resize_test(void);
void usdz_convert_pack_channels_test(void);
void usdz_convert_fit_budget_test(void);
void usdz_convert_pipeline_test(void);
void usdz_convert_repack_files_test(void);
void usdz_convert_jpeg_roundtrip_test(void);
void usdz_convert_remap_asset_paths_test(void);
void usdz_convert_error_path_test(void);
void usdz_convert_adversarial_image_test(void);
void usdz_convert_pack_channels_error_test(void);
void usdz_convert_fit_budget_error_test(void);
void usdz_convert_missing_texture_reference_test(void);
void usdz_convert_pipeline_jpeg_test(void);
void usdz_convert_cleanup_test(void);
