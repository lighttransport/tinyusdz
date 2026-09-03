/* SPDX-License-Identifier: Apache-2.0
 * Smoke test for the lightusd_c C API (pure C11).
 * Authors a stage, round-trips it through USDA/USDC, and reads it back.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "lightusd-c.h"

#define CHECK_OK(expr)                                                    \
  do {                                                                    \
    tusd_status st_ = (expr);                                             \
    if (st_ != TUSD_OK) {                                                 \
      fprintf(stderr, "FAIL %s:%d: %s -> %d (%s)\n", __FILE__, __LINE__,  \
              #expr, (int)st_, tusd_last_error());                        \
      return 1;                                                           \
    }                                                                     \
  } while (0)

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
      return 1;                                                           \
    }                                                                     \
  } while (0)

static int test_authoring_roundtrip(void) {
  tusd_stage* stage = NULL;
  CHECK_OK(tusd_stage_create(&stage));

  /* stage metadata */
  CHECK_OK(tusd_stage_set_metadata(stage, "upAxis", TUSD_TYPE_TOKEN, "Z", 1));
  double mpu = 1.0;
  CHECK_OK(tusd_stage_set_metadata(stage, "metersPerUnit", TUSD_TYPE_DOUBLE,
                                   &mpu, 1));

  /* prims (ancestor auto-creation) */
  tusd_prim mesh;
  CHECK_OK(tusd_stage_define_prim(stage, "/World", "Xform", 0, NULL));
  CHECK_OK(tusd_stage_define_prim(stage, "/World/Geo/Grid", "Mesh", 0, &mesh));
  CHECK(tusd_prim_is_valid(mesh));
  uint64_t gen = tusd_stage_generation(stage);
  CHECK(gen >= 2);
  CHECK_OK(tusd_stage_set_default_prim(stage, "World"));

  /* attributes: float3 array, int array, scalar double, token, timesamples */
  const float points[12] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
  CHECK_OK(tusd_attr_set(stage, "/World/Geo/Grid", "points",
                         TUSD_TYPE_POINT3F, 1, points, 4, 0));
  const int32_t counts[1] = {4};
  CHECK_OK(tusd_attr_set(stage, "/World/Geo/Grid", "faceVertexCounts",
                         TUSD_TYPE_INT, 1, counts, 1, 0));
  const int32_t indices[4] = {0, 1, 2, 3};
  CHECK_OK(tusd_attr_set(stage, "/World/Geo/Grid", "faceVertexIndices",
                         TUSD_TYPE_INT, 1, indices, 4, 0));
  double radius = 2.5;
  CHECK_OK(tusd_attr_set(stage, "/World/Geo/Grid", "radius", TUSD_TYPE_DOUBLE,
                         0, &radius, 1, TUSD_PROP_CUSTOM));
  CHECK_OK(tusd_attr_set(stage, "/World/Geo/Grid", "purpose", TUSD_TYPE_TOKEN,
                         0, "render", 1, TUSD_PROP_UNIFORM));
  const char* order[1] = {"xformOp:translate"};
  CHECK_OK(tusd_attr_set_token_array(stage, "/World/Geo/Grid", "xformOpOrder",
                                     TUSD_TYPE_TOKEN, order, 1,
                                     TUSD_PROP_UNIFORM));
  const double t0[3] = {0, 0, 0};
  const double t1[3] = {0, 5, 0};
  CHECK_OK(tusd_attr_set_timesample(stage, "/World/Geo/Grid",
                                    "xformOp:translate", 0.0,
                                    TUSD_TYPE_DOUBLE3, 0, t0, 1));
  CHECK_OK(tusd_attr_set_timesample(stage, "/World/Geo/Grid",
                                    "xformOp:translate", 24.0,
                                    TUSD_TYPE_DOUBLE3, 0, t1, 1));

  /* attribute metadata */
  CHECK_OK(tusd_attr_set_metadata(stage, "/World/Geo/Grid", "points",
                                  "interpolation", TUSD_TYPE_TOKEN, "vertex",
                                  1));

  /* relationship + arc + variant */
  CHECK_OK(tusd_stage_define_prim(stage, "/World/Looks/Red", "Material", 0,
                                  NULL));
  CHECK_OK(tusd_rel_add_target(stage, "/World/Geo/Grid", "material:binding",
                               "/World/Looks/Red"));
  CHECK_OK(tusd_prim_add_arc(stage, "/World", TUSD_ARC_REFERENCE,
                             "./library.usda", "/Proto"));
  CHECK_OK(tusd_prim_add_variant_set(stage, "/World", "lod"));
  CHECK_OK(tusd_prim_add_variant(stage, "/World", "lod", "high"));
  CHECK_OK(tusd_prim_add_variant(stage, "/World", "lod", "low"));
  CHECK_OK(tusd_prim_set_variant_selection(stage, "/World", "lod", "high"));

  /* prim metadata */
  CHECK_OK(tusd_prim_set_metadata(stage, "/World", "kind", TUSD_TYPE_TOKEN,
                                  "assembly", 1));

  /* -------- read back before export -------- */
  tusd_prim grid = tusd_stage_prim_at_path(stage, "/World/Geo/Grid");
  CHECK(tusd_prim_is_valid(grid));

  tusd_value_view view;
  CHECK_OK(tusd_attr_get(grid, "points", &view));
  CHECK(view.is_array && view.count == 4 && view.components == 3);
  CHECK(view.storage == TUSD_COMP_FLOAT32);
  CHECK(view.nbytes == 12 * sizeof(float));
  CHECK(memcmp(view.data, points, sizeof(points)) == 0);

  CHECK_OK(tusd_attr_get(grid, "radius", &view));
  CHECK(!view.is_array && view.storage == TUSD_COMP_FLOAT64);
  CHECK(fabs(*(const double*)view.data - 2.5) < 1e-12);

  tusd_sv sv;
  CHECK_OK(tusd_attr_get_string(grid, "purpose", &sv));
  CHECK(strncmp(sv.data, "render", sv.len) == 0);

  /* flags */
  CHECK(tusd_prim_property_flags(grid, "radius") & TUSD_PROP_CUSTOM);
  CHECK(tusd_prim_property_flags(grid, "purpose") & TUSD_PROP_UNIFORM);

  /* time samples */
  CHECK(tusd_attr_has_timesamples(grid, "xformOp:translate"));
  CHECK(tusd_attr_timesample_count(grid, "xformOp:translate") == 2);
  double times[2];
  CHECK(tusd_attr_timesample_times(grid, "xformOp:translate", times, 2) == 2);
  CHECK(times[0] == 0.0 && times[1] == 24.0);
  tusd_value* interp = NULL;
  CHECK_OK(tusd_attr_interpolate(grid, "xformOp:translate", 12.0, 1, &interp));
  CHECK_OK(tusd_value_get_view(interp, &view));
  CHECK(view.storage == TUSD_COMP_FLOAT64 && view.components == 3);
  CHECK(fabs(((const double*)view.data)[1] - 2.5) < 1e-12);
  tusd_value_destroy(interp);

  /* relationships */
  CHECK(tusd_prim_has_relationship(grid, "material:binding"));
  CHECK(tusd_rel_target_count(grid, "material:binding") == 1);
  sv = tusd_rel_target(grid, "material:binding", 0);
  CHECK(strncmp(sv.data, "/World/Looks/Red", sv.len) == 0);

  /* variants */
  tusd_prim world = tusd_stage_prim_at_path(stage, "/World");
  CHECK(tusd_prim_variant_set_count(world) == 1);
  CHECK(tusd_variant_count(world, "lod") == 2);
  sv = tusd_variant_selection(world, "lod");
  CHECK(strncmp(sv.data, "high", sv.len) == 0);

  /* traversal */
  CHECK(tusd_stage_root_prim_count(stage) == 1);
  tusd_prim root = tusd_stage_root_prim(stage, 0);
  CHECK(tusd_prim_child_count(root) == 2); /* Geo, Looks */
  tusd_prim geo = tusd_prim_child_by_name(root, "Geo");
  CHECK(tusd_prim_is_valid(geo));
  tusd_prim parent = tusd_prim_parent(geo);
  sv = tusd_prim_path(parent);
  CHECK(strncmp(sv.data, "/World", sv.len) == 0);

  /* prim metadata read */
  sv = tusd_prim_kind(world);
  CHECK(strncmp(sv.data, "assembly", sv.len) == 0);

  /* -------- USDA round-trip -------- */
  tusd_string* usda = NULL;
  CHECK_OK(tusd_stage_export_usda(stage, &usda));
  tusd_sv usda_sv = tusd_string_view(usda);
  CHECK(usda_sv.len > 0);
  CHECK(strstr(usda_sv.data, "Grid") != NULL);

  tusd_stage* re = NULL;
  CHECK_OK(tusd_stage_load_from_memory((const uint8_t*)usda_sv.data,
                                       usda_sv.len, NULL, &re));
  tusd_string_destroy(usda);

  tusd_prim regrid = tusd_stage_prim_at_path(re, "/World/Geo/Grid");
  CHECK(tusd_prim_is_valid(regrid));
  CHECK_OK(tusd_attr_get(regrid, "points", &view));
  CHECK(view.is_array && view.count == 4);
  CHECK(memcmp(view.data, points, sizeof(points)) == 0);
  CHECK(tusd_attr_has_timesamples(regrid, "xformOp:translate"));
  sv = tusd_variant_selection(tusd_stage_prim_at_path(re, "/World"), "lod");
  CHECK(strncmp(sv.data, "high", sv.len) == 0);
  tusd_stage_destroy(re);

  /* -------- USDC round-trip -------- */
  tusd_string* usdc = NULL;
  CHECK_OK(tusd_stage_export_usdc(stage, &usdc));
  tusd_sv usdc_sv = tusd_string_view(usdc);
  CHECK(usdc_sv.len > 8);
  CHECK(memcmp(usdc_sv.data, "PXR-USDC", 8) == 0);
  CHECK_OK(tusd_stage_load_from_memory((const uint8_t*)usdc_sv.data,
                                       usdc_sv.len, NULL, &re));
  tusd_string_destroy(usdc);
  regrid = tusd_stage_prim_at_path(re, "/World/Geo/Grid");
  CHECK(tusd_prim_is_valid(regrid));
  CHECK_OK(tusd_attr_get(regrid, "points", &view));
  CHECK(view.count == 4 && memcmp(view.data, points, sizeof(points)) == 0);
  tusd_stage_destroy(re);

  /* remove property / prim */
  CHECK_OK(tusd_attr_remove(stage, "/World/Geo/Grid", "radius"));
  CHECK(!tusd_prim_has_property(tusd_stage_prim_at_path(stage,
                                                        "/World/Geo/Grid"),
                                "radius"));
  CHECK_OK(tusd_stage_remove_prim(stage, "/World/Looks"));
  CHECK(!tusd_prim_is_valid(
      tusd_stage_prim_at_path(stage, "/World/Looks/Red")));
  CHECK(tusd_stage_generation(stage) > gen);

  tusd_stage_destroy(stage);
  return 0;
}

static int test_error_handling(void) {
  tusd_stage* stage = NULL;
  tusd_status st = tusd_stage_load("/nonexistent/file.usda", NULL, &stage);
  CHECK(st != TUSD_OK && stage == NULL);
  CHECK(tusd_last_error()[0] != '\0');

  const uint8_t garbage[16] = {0xff, 0xfe, 1, 2, 3, 4, 5, 6,
                               7,    8,    9, 10, 11, 12, 13, 14};
  st = tusd_stage_load_from_memory(garbage, sizeof(garbage), NULL, &stage);
  CHECK(st == TUSD_ERR_PARSE && stage == NULL);

  CHECK_OK(tusd_stage_create(&stage));
  tusd_prim invalid = tusd_stage_prim_at_path(stage, "/Nope");
  CHECK(!tusd_prim_is_valid(invalid));
  tusd_value_view view;
  CHECK(tusd_attr_get(invalid, "x", &view) == TUSD_ERR_INVALID_ARG);
  CHECK(tusd_stage_remove_prim(stage, "/Nope") == TUSD_ERR_NOT_FOUND);
  tusd_stage_destroy(stage);
  return 0;
}

static int test_name_validation(void) {
  /* Non-identifier names are rejected at creation (pxr and the parser both
   * reject them), so they can't be authored into an unround-trippable scene. */
  const double one = 1.0;
  tusd_stage* stage = NULL;
  CHECK_OK(tusd_stage_create(&stage));
  CHECK_OK(tusd_stage_define_prim(stage, "/World", "Xform", 0, NULL));
  CHECK(tusd_attr_set(stage, "/World", "0abc", TUSD_TYPE_DOUBLE, 0, &one, 1, 0)
         == TUSD_ERR_INVALID_ARG);
  CHECK(tusd_attr_set(stage, "/World", "bad name", TUSD_TYPE_DOUBLE, 0, &one, 1, 0)
         == TUSD_ERR_INVALID_ARG);
  CHECK(tusd_stage_define_prim(stage, "/World/0abc", "Mesh", 0, NULL)
         == TUSD_ERR_INVALID_ARG);
  CHECK(tusd_rel_add_target(stage, "/World", "0rel", "/World")
         == TUSD_ERR_INVALID_ARG);
  /* Valid (possibly namespaced) names still work. */
  CHECK_OK(tusd_attr_set(stage, "/World", "good", TUSD_TYPE_DOUBLE, 0, &one, 1, 0));
  CHECK_OK(tusd_attr_set(stage, "/World", "xformOp:translate", TUSD_TYPE_DOUBLE,
                         0, &one, 1, 0));
  tusd_stage_destroy(stage);
  return 0;
}

static int test_file_load(const char* path) {
  tusd_load_options opts;
  tusd_load_options_init(&opts);
  tusd_stage* stage = NULL;
  tusd_status st = tusd_stage_load(path, &opts, &stage);
  if (st != TUSD_OK) {
    fprintf(stderr, "FAIL: load %s: %s\n", path, tusd_last_error());
    return 1;
  }
  tusd_stage_stats stats;
  CHECK_OK(tusd_stage_get_stats(stage, &stats));
  CHECK(stats.prim_count > 0);
  printf("  loaded %s: %llu prims\n", path,
         (unsigned long long)stats.prim_count);
  tusd_stage_destroy(stage);
  return 0;
}

static int test_usda_lazy_load_options(void) {
  const size_t count = 25000;
  const char* prefix = "#usda 1.0\n\ndef Mesh \"Mesh\" {\n    int[] points = [";
  const char* suffix = "]\n}\n";
  const size_t max_chars_per_digit = 8; /* "-2147483648" */
  size_t buf_cap = 64 + strlen(prefix) + strlen(suffix) + (count * max_chars_per_digit);
  char* data = (char*)malloc(buf_cap);
  if (!data) {
    fprintf(stderr, "FAIL: OOM building lazy parse fixture\n");
    return 1;
  }

  size_t len = strlen(prefix);
  memcpy(data, prefix, len);
  for (size_t i = 0; i < count; ++i) {
    const int written = snprintf(
        data + len,
        buf_cap - len,
        i == 0 ? "%zu" : ",%zu",
        i);
    if (written <= 0 || written >= (int)(buf_cap - len)) {
      free(data);
      fprintf(stderr, "FAIL: fixture build overflow\n");
      return 1;
    }
    len += (size_t)written;
  }
  memcpy(data + len, suffix, strlen(suffix) + 1);
  len += strlen(suffix);

  printf("  built USDA fixture: %zu values (%zu bytes)\n", count, len);

  /* Force eager parsing by shrinking the lazy cap to 1 element. */
  tusd_load_options eager;
  tusd_load_options_init(&eager);
  eager.format = TUSD_FORMAT_USDA;
  eager.enable_usda_lazy_arrays = 1;
  eager.max_usda_lazy_array_elements = 1;
  eager.usda_num_threads = 1;
  tusd_stage* eager_stage = NULL;
  CHECK_OK(tusd_stage_load_from_memory((const uint8_t*)data, len, &eager, &eager_stage));
  tusd_stage_stats eager_before = {0}, eager_after = {0};
  CHECK_OK(tusd_stage_get_stats(eager_stage, &eager_before));
  {
    tusd_prim mesh = tusd_stage_prim_at_path(eager_stage, "/Mesh");
    CHECK(tusd_prim_is_valid(mesh));
    tusd_value_view view = {0};
    CHECK_OK(tusd_attr_get(mesh, "points", &view));
    CHECK(view.is_array && view.count == count);
  }
  CHECK_OK(tusd_stage_get_stats(eager_stage, &eager_after));
  tusd_stage_destroy(eager_stage);

  /* Enable lazy path with a high per-array cap; should keep the array lazy until
   * first materialization. */
  tusd_load_options lazy;
  tusd_load_options_init(&lazy);
  lazy.format = TUSD_FORMAT_USDA;
  lazy.enable_usda_lazy_arrays = 1;
  lazy.max_usda_lazy_array_elements = (1ull << 60);
  tusd_stage* lazy_stage = NULL;
  CHECK_OK(tusd_stage_load_from_memory((const uint8_t*)data, len, &lazy, &lazy_stage));
  tusd_stage_stats lazy_before = {0}, lazy_after = {0};
  CHECK_OK(tusd_stage_get_stats(lazy_stage, &lazy_before));
  {
    tusd_prim mesh = tusd_stage_prim_at_path(lazy_stage, "/Mesh");
    CHECK(tusd_prim_is_valid(mesh));
    tusd_value_view view = {0};
    CHECK_OK(tusd_attr_get(mesh, "points", &view));
    CHECK(view.is_array && view.count == count);
  }
  CHECK_OK(tusd_stage_get_stats(lazy_stage, &lazy_after));
  tusd_stage_destroy(lazy_stage);

  /* If lazy parsing is threaded to the path, materializing from memory should grow
   * peak memory after attribute read, while eager parsing allocates upfront. */
  CHECK(lazy_before.memory_bytes <= eager_before.memory_bytes);
  CHECK(lazy_after.memory_bytes >= lazy_before.memory_bytes);
  CHECK(eager_after.memory_bytes <= eager_before.memory_bytes + 1024);

  free(data);
  return 0;
}

int main(int argc, char** argv) {
  CHECK(tusd_api_version() == ((1u << 16) | (0u << 8) | 0u));
  CHECK(strcmp(tusd_type_name(TUSD_TYPE_POINT3F), "point3f") == 0);
  CHECK(tusd_type_from_name("float3") == TUSD_TYPE_FLOAT3);
  CHECK(tusd_type_component_count(TUSD_TYPE_MATRIX4D) == 16);

  if (test_authoring_roundtrip()) return 1;
  printf("  authoring round-trip: PASSED\n");
  if (test_error_handling()) return 1;
  printf("  error handling: PASSED\n");
  if (test_name_validation()) return 1;
  printf("  name validation: PASSED\n");
  if (argc > 1) {
    if (test_file_load(argv[1])) return 1;
  }
  if (test_usda_lazy_load_options()) return 1;
  printf("  usda lazy parse options: PASSED\n");
  printf("All C API tests PASSED\n");
  return 0;
}
