// SPDX-License-Identifier: Apache-2.0
#include "mtlx_doc.h"
#include "mtlx_eval.h"
#include "texture.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int nearf(float a, float b) { return fabsf(a - b) <= 1.0e-6f; }

static int eval_xml(const char *xml, OpenPBRParams *p) {
  MtlxDoc *doc = mtlx_load_string(xml);
  if (!doc) {
    fprintf(stderr, "mtlx_load_string failed\n");
    return 0;
  }
  if (doc->nmat != 1 || doc->mats[0].surface_node < 0) {
    fprintf(stderr, "surface material did not resolve\n");
    mtlx_free(doc);
    return 0;
  }

  ShadeContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.doc = doc;
  ctx.tex = NULL;
  ctx.uv[0] = 0.5f;
  ctx.uv[1] = 0.5f;
  ctx.P = v3_make(0.0f, 0.0f, 0.0f);
  ctx.Ns = v3_make(0.0f, 0.0f, 1.0f);
  ctx.Ng = v3_make(0.0f, 0.0f, 1.0f);
  ctx.dpdu = v3_make(1.0f, 0.0f, 0.0f);
  ctx.dpdv = v3_make(0.0f, 1.0f, 0.0f);
  ctx.V = v3_make(0.0f, 0.0f, 1.0f);
  ctx.memo = (MtlxValue *)calloc((size_t)doc->nnode, sizeof(MtlxValue));
  ctx.memo_done = (char *)calloc((size_t)doc->nnode, 1);
  if (!ctx.memo || !ctx.memo_done) {
    fprintf(stderr, "allocation failed\n");
    free(ctx.memo);
    free(ctx.memo_done);
    mtlx_free(doc);
    return 0;
  }

  int ok = mtlx_eval_surface(&ctx, doc->mats[0].surface_node, p) == 0;
  free(ctx.memo);
  free(ctx.memo_done);
  mtlx_free(doc);
  if (!ok) {
    fprintf(stderr, "mtlx_eval_surface failed\n");
  }
  return ok;
}

static int eval_volume_xml(const char *xml, MtlxVolumeParams *p) {
  MtlxDoc *doc = mtlx_load_string(xml);
  if (!doc || doc->nmat != 1 || doc->mats[0].volume_node < 0) {
    fprintf(stderr, "volume material did not resolve\n");
    mtlx_free(doc);
    return 0;
  }
  ShadeContext ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.doc = doc;
  ctx.Ns = v3_make(0.0f, 0.0f, 1.0f);
  ctx.V = v3_make(0.0f, 0.0f, 1.0f);
  ctx.memo = (MtlxValue *)calloc((size_t)doc->nnode, sizeof(MtlxValue));
  ctx.memo_done = (char *)calloc((size_t)doc->nnode, 1);
  const int ok = ctx.memo && ctx.memo_done &&
      mtlx_eval_volume(&ctx, doc->mats[0].volume_node, p) == 0;
  free(ctx.memo);
  free(ctx.memo_done);
  mtlx_free(doc);
  return ok;
}

int main(void) {
  /* Exercise the shared UDIM resolver directly. Keep the fixture local to the
   * test working directory so the texture cache's relative search semantics
   * are identical to a real MaterialX document. */
  const char *udim_1001 = "lightrt_udim_regression.1001.ppm";
  const char *udim_1002 = "lightrt_udim_regression.1002.ppm";
  const char *udim_1101 = "lightrt_udim_regression.1101.ppm";
  FILE *udim_file = fopen(udim_1001, "wb");
  if (!udim_file) return 1;
  fputs("P6\n1 1\n255\n", udim_file);
  fputc(255, udim_file); fputc(0, udim_file); fputc(0, udim_file);
  fclose(udim_file);
  udim_file = fopen(udim_1002, "wb");
  if (!udim_file) { remove(udim_1001); return 1; }
  fputs("P6\n1 1\n255\n", udim_file);
  fputc(0, udim_file); fputc(0, udim_file); fputc(255, udim_file);
  fclose(udim_file);
  udim_file = fopen(udim_1101, "wb");
  if (!udim_file) {
    remove(udim_1001); remove(udim_1002); return 1;
  }
  fputs("P6\n1 1\n255\n", udim_file);
  fputc(0, udim_file); fputc(255, udim_file); fputc(0, udim_file);
  fclose(udim_file);
  TextureCache *udim_cache = texcache_create(".");
  MtlxDoc *udim_doc = mtlx_load_string(
      "<materialx><image name=\"udim\" type=\"color3\"><input "
      "name=\"file\" type=\"filename\" value=\"lightrt_udim_regression.<UDIM>.ppm\"/>"
      "</image></materialx>");
  if (!udim_doc) {
    texcache_free(udim_cache); remove(udim_1001); remove(udim_1002);
    remove(udim_1101); return 1;
  }
  texcache_preload(udim_cache, udim_doc);
  mtlx_free(udim_doc);
  float udim_sample[4];
  const int udim_first = texcache_sample_file(
      udim_cache, "lightrt_udim_regression.<UDIM>.ppm", 0, 0.25f, 0.5f,
      udim_sample);
  const int first_red = udim_sample[0] > 0.99f && udim_sample[2] < 0.01f;
  const int udim_second = texcache_sample_file(
      udim_cache, "lightrt_udim_regression.<UDIM>.ppm", 0, 1.25f, 0.5f,
      udim_sample);
  const int second_blue = udim_sample[0] < 0.01f && udim_sample[2] > 0.99f;
  const int udim_high = texcache_sample_file(
      udim_cache, "lightrt_udim_regression.<UDIM>.ppm", 0, 0.25f, 10.5f,
      udim_sample);
  const int high_green = udim_sample[0] < 0.01f && udim_sample[1] > 0.99f;
  const int udim_missing = texcache_sample_file(
      udim_cache, "lightrt_udim_regression.<UDIM>.ppm", 0, 2.25f, 0.5f,
      udim_sample);
  texcache_free(udim_cache);
  remove(udim_1001);
  remove(udim_1002);
  remove(udim_1101);
  if (!udim_first || !udim_second || !udim_high || !first_red ||
      !second_blue || !high_green || udim_missing ||
      udim_sample[3] != 1.0f) {
    fprintf(stderr, "UDIM present/missing tile resolution failed\n");
    return 1;
  }

  /* Projection nodes also own image filenames, but their lookups happen after
   * the cache is frozen.  Verify all of those files are preloaded instead of
   * falling back to the node default during render-time evaluation. */
  const char *projection_files[] = {"lightrt_latlong.ppm",
                                    "lightrt_triplanar_x.ppm",
                                    "lightrt_triplanar_y.ppm",
                                    "lightrt_triplanar_z.ppm"};
  for (size_t i = 0; i < sizeof(projection_files) / sizeof(projection_files[0]);
       i++) {
    FILE *f = fopen(projection_files[i], "wb");
    if (!f) return 1;
    fputs("P6\n1 1\n255\n", f);
    fputc(255, f); fputc(255, f); fputc(255, f);
    fclose(f);
  }
  MtlxDoc *projection_doc = mtlx_load_string(
      "<materialx>"
      "<latlongimage name=\"lat\" type=\"color3\"><input name=\"file\" "
      "type=\"filename\" value=\"lightrt_latlong.ppm\"/></latlongimage>"
      "<triplanarprojection name=\"tri\" type=\"color3\">"
      "<input name=\"filex\" type=\"filename\" value=\"lightrt_triplanar_x.ppm\"/>"
      "<input name=\"filey\" type=\"filename\" value=\"lightrt_triplanar_y.ppm\"/>"
      "<input name=\"filez\" type=\"filename\" value=\"lightrt_triplanar_z.ppm\"/>"
      "</triplanarprojection></materialx>");
  TextureCache *projection_cache = texcache_create(".");
  int projection_ok = projection_doc && projection_cache;
  if (projection_ok) {
    texcache_preload(projection_cache, projection_doc);
    for (size_t i = 0; i < sizeof(projection_files) / sizeof(projection_files[0]);
         i++) {
      if (texcache_get(projection_cache, projection_files[i], 0) < 0)
        projection_ok = 0;
    }
  }
  texcache_free(projection_cache);
  mtlx_free(projection_doc);
  for (size_t i = 0; i < sizeof(projection_files) / sizeof(projection_files[0]);
       i++) remove(projection_files[i]);
  if (!projection_ok) {
    fprintf(stderr, "projection image preload failed\n");
    return 1;
  }

  /* Projection nodes use the same explicit filter contract as image nodes.
   * Keep a two-texel fixture so nearest sampling cannot be mistaken for the
   * default bilinear midpoint. */
  const char *projection_filter_file = "lightrt_projection_filter.ppm";
  FILE *projection_filter = fopen(projection_filter_file, "wb");
  if (!projection_filter) return 1;
  fputs("P6\n2 1\n255\n", projection_filter);
  fputc(255, projection_filter); fputc(0, projection_filter); fputc(0, projection_filter);
  fputc(0, projection_filter); fputc(0, projection_filter); fputc(255, projection_filter);
  fclose(projection_filter);
  MtlxDoc *projection_filter_doc = mtlx_load_string(
      "<materialx>"
      "<latlongimage name=\"latf\" type=\"color3\"><input name=\"file\" "
      "type=\"filename\" value=\"lightrt_projection_filter.ppm\"/>"
      "<input name=\"filtertype\" type=\"string\" value=\"nearest\"/>"
      "</latlongimage>"
      "<triplanarprojection name=\"trif\" type=\"color3\">"
      "<input name=\"filex\" type=\"filename\" value=\"lightrt_projection_filter.ppm\"/>"
      "<input name=\"filey\" type=\"filename\" value=\"lightrt_projection_filter.ppm\"/>"
      "<input name=\"filez\" type=\"filename\" value=\"lightrt_projection_filter.ppm\"/>"
      "<input name=\"filtertype\" type=\"string\" value=\"nearest\"/>"
      "</triplanarprojection></materialx>");
  TextureCache *projection_filter_cache = texcache_create(".");
  int projection_filter_ok = projection_filter_doc && projection_filter_cache;
  if (projection_filter_ok) {
    texcache_preload(projection_filter_cache, projection_filter_doc);
    ShadeContext projection_filter_ctx;
    memset(&projection_filter_ctx, 0, sizeof(projection_filter_ctx));
    projection_filter_ctx.doc = projection_filter_doc;
    projection_filter_ctx.tex = projection_filter_cache;
    projection_filter_ctx.uv[0] = 0.5f;
    projection_filter_ctx.uv[1] = 0.5f;
    projection_filter_ctx.P = v3_make(0.5f, 0.0f, 0.0f);
    projection_filter_ctx.Ns = v3_make(0.0f, 0.0f, 1.0f);
    projection_filter_ctx.memo = (MtlxValue *)calloc((size_t)projection_filter_doc->nnode, sizeof(MtlxValue));
    projection_filter_ctx.memo_done = (char *)calloc((size_t)projection_filter_doc->nnode, 1);
    int latf = mtlx_find_node(projection_filter_doc, -1, "latf");
    int trif = mtlx_find_node(projection_filter_doc, -1, "trif");
    MtlxValue latf_value;
    MtlxValue trif_value;
    memset(&latf_value, 0, sizeof(latf_value));
    memset(&trif_value, 0, sizeof(trif_value));
    if (projection_filter_ctx.memo && projection_filter_ctx.memo_done &&
        latf >= 0 && trif >= 0) {
      latf_value = mtlx_eval_node_test(&projection_filter_ctx, latf);
      memset(projection_filter_ctx.memo_done, 0,
             (size_t)projection_filter_doc->nnode);
      trif_value = mtlx_eval_node_test(&projection_filter_ctx, trif);
      projection_filter_ok = latf_value.v[2] > 0.99f &&
          latf_value.v[0] < 0.01f && trif_value.v[2] > 0.99f &&
          trif_value.v[0] < 0.01f;
    } else {
      projection_filter_ok = 0;
    }
    free(projection_filter_ctx.memo);
    free(projection_filter_ctx.memo_done);
  }
  texcache_free(projection_filter_cache);
  mtlx_free(projection_filter_doc);
  remove(projection_filter_file);
  if (!projection_filter_ok) {
    fprintf(stderr, "projection nearest filtering failed\n");
    return 1;
  }

  /* Verify the cache's color/alpha contract independently of the projection
   * evaluator: RGB is linearized only for an sRGB logical use, while alpha is
   * preserved. Use a minimal uncompressed 32-bit TGA to carry an alpha byte. */
  const char *rgba_file = "lightrt_rgba_regression.tga";
  FILE *rgba_stream = fopen(rgba_file, "wb");
  if (!rgba_stream) return 1;
  unsigned char tga_header[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                  1, 0, 1, 0, 32, 8};
  unsigned char rgba_pixel[4] = {0, 0, 128, 64}; /* B, G, R, A */
  fwrite(tga_header, 1, sizeof(tga_header), rgba_stream);
  fwrite(rgba_pixel, 1, sizeof(rgba_pixel), rgba_stream);
  fclose(rgba_stream);
  TextureCache *rgba_cache = texcache_create(".");
  int rgba_ok = rgba_cache != NULL;
  float rgba_sample[4] = {0, 0, 0, 1};
  if (rgba_ok) {
    int rgba_id = texcache_get(rgba_cache, rgba_file, 1);
    rgba_ok = rgba_id >= 0;
    if (rgba_ok) {
      texcache_sample(rgba_cache, rgba_id, 0.5f, 0.5f, rgba_sample);
      rgba_ok = rgba_sample[0] > 0.21f && rgba_sample[0] < 0.22f &&
          rgba_sample[1] < 0.01f && rgba_sample[2] < 0.01f &&
          rgba_sample[3] > 0.24f && rgba_sample[3] < 0.26f;
    }
  }
  texcache_free(rgba_cache);
  remove(rgba_file);
  if (!rgba_ok) {
    fprintf(stderr, "sRGB RGB or alpha texture handling failed\n");
    return 1;
  }

  const char *nearest_file = "lightrt_nearest_regression.ppm";
  FILE *nearest_stream = fopen(nearest_file, "wb");
  if (!nearest_stream) return 1;
  fputs("P6\n2 1\n255\n", nearest_stream);
  fputc(255, nearest_stream); fputc(0, nearest_stream); fputc(0, nearest_stream);
  fputc(0, nearest_stream); fputc(0, nearest_stream); fputc(255, nearest_stream);
  fclose(nearest_stream);
  MtlxDoc *nearest_doc = mtlx_load_string(
      "<materialx><image name=\"nearest\" type=\"color3\">"
      "<input name=\"file\" type=\"filename\" value=\"lightrt_nearest_regression.ppm\"/>"
      "<input name=\"filtertype\" type=\"string\" value=\"nearest\"/>"
      "</image></materialx>");
  TextureCache *nearest_cache = texcache_create(".");
  int nearest_ok = nearest_doc && nearest_cache;
  MtlxValue nearest_value;
  memset(&nearest_value, 0, sizeof(nearest_value));
  if (nearest_ok) {
    texcache_preload(nearest_cache, nearest_doc);
    ShadeContext nearest_ctx;
    memset(&nearest_ctx, 0, sizeof(nearest_ctx));
    nearest_ctx.doc = nearest_doc;
    nearest_ctx.tex = nearest_cache;
    nearest_ctx.uv[0] = 0.5f;
    nearest_ctx.uv[1] = 0.5f;
    nearest_ctx.memo = (MtlxValue *)calloc((size_t)nearest_doc->nnode,
                                            sizeof(MtlxValue));
    nearest_ctx.memo_done = (char *)calloc((size_t)nearest_doc->nnode, 1);
    int nearest_node = mtlx_find_node(nearest_doc, -1, "nearest");
    if (!nearest_ctx.memo || !nearest_ctx.memo_done || nearest_node < 0) {
      nearest_ok = 0;
    } else {
      nearest_value = mtlx_eval_node_test(&nearest_ctx, nearest_node);
      nearest_ok = nearest_value.v[2] > 0.99f && nearest_value.v[0] < 0.01f;
    }
    free(nearest_ctx.memo);
    free(nearest_ctx.memo_done);
  }
  texcache_free(nearest_cache);
  mtlx_free(nearest_doc);
  remove(nearest_file);
  if (!nearest_ok) {
    fprintf(stderr, "nearest image filtering failed\n");
    return 1;
  }

  const char *xml =
      "<materialx version=\"1.38\">"
      "  <open_pbr_surface name=\"Preview\" type=\"surfaceshader\">"
      "    <input name=\"base_weight\" type=\"float\" value=\"0.75\"/>"
      "    <input name=\"base_color\" type=\"color3\" value=\"0.2, 0.4, 0.6\"/>"
      "    <input name=\"base_diffuse_roughness\" type=\"float\" value=\"0.12\"/>"
      "    <input name=\"base_metalness\" type=\"float\" value=\"0.5\"/>"
      "    <input name=\"specular_weight\" type=\"float\" value=\"0.25\"/>"
      "    <input name=\"base_roughness\" type=\"float\" value=\"0.35\"/>"
      "    <input name=\"specular_ior\" type=\"float\" value=\"1.6\"/>"
      "    <input name=\"coat_weight\" type=\"float\" value=\"0.3\"/>"
      "    <input name=\"coat_roughness\" type=\"float\" value=\"0.2\"/>"
      "    <input name=\"emission_luminance\" type=\"float\" value=\"2.0\"/>"
      "    <input name=\"emission_color\" type=\"color3\" value=\"0.1, 0.2, 0.3\"/>"
      "    <input name=\"geometry_opacity\" type=\"float\" value=\"0.65\"/>"
      "  </open_pbr_surface>"
      "  <surfacematerial name=\"Mat\">"
      "    <input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\"Preview\"/>"
      "  </surfacematerial>"
      "</materialx>";

  OpenPBRParams p;
  if (!eval_xml(xml, &p)) {
    return 1;
  }

  int ok = nearf(p.base_weight, 0.75f) && nearf(p.base_color.x, 0.2f) &&
           nearf(p.base_color.y, 0.4f) && nearf(p.base_color.z, 0.6f) &&
           nearf(p.diffuse_roughness, 0.12f) &&
           nearf(p.metalness, 0.5f) && nearf(p.specular_weight, 0.25f) &&
           nearf(p.specular_roughness, 0.35f) && nearf(p.specular_ior, 1.6f) &&
           nearf(p.coat_weight, 0.3f) && nearf(p.coat_roughness, 0.2f) &&
           nearf(p.emission, 2.0f) && nearf(p.emission_color.x, 0.1f) &&
           nearf(p.emission_color.y, 0.2f) && nearf(p.emission_color.z, 0.3f) &&
           nearf(p.opacity, 0.65f);

  if (!ok) {
    fprintf(stderr, "unexpected evaluated OpenPBR params\n");
    return 1;
  }

  /* Conical EDF angles are full cone angles. At 90 degrees from the authored
   * axis, a 60/120 degree cone is outside its 60 degree outer half-angle and
   * must emit zero. This also guards the cosine-space smoothstep contract. */
  const char *conical_xml =
      "<materialx version=\"1.39\">"
      " <conical_edf name=\"Cone\" type=\"EDF\">"
      "  <input name=\"color\" type=\"color3\" value=\"0.2,0.4,0.8\"/>"
      "  <input name=\"normal\" type=\"vector3\" value=\"0,1,0\"/>"
      "  <input name=\"inner_angle\" type=\"float\" value=\"60\"/>"
      "  <input name=\"outer_angle\" type=\"float\" value=\"120\"/>"
      " </conical_edf>"
      " <surface name=\"ConeSurface\" type=\"surfaceshader\">"
      "  <input name=\"edf\" type=\"EDF\" nodename=\"Cone\"/>"
      " </surface>"
      " <surfacematerial name=\"ConeMat\">"
      "  <input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\"ConeSurface\"/>"
      " </surfacematerial></materialx>";
  if (!eval_xml(conical_xml, &p) || !nearf(p.emission_color.x, 0.0f) ||
      !nearf(p.emission_color.y, 0.0f) || !nearf(p.emission_color.z, 0.0f)) {
    fprintf(stderr, "MaterialX conical EDF full-angle falloff was not evaluated\n");
    return 1;
  }

  const char *standard_xml =
      "<materialx version=\"1.38\">"
      "  <nodegraph name=\"NG\">"
      "    <combine3 name=\"packed\" type=\"color3\">"
      "      <input name=\"in1\" type=\"float\" value=\"0.2\"/>"
      "      <input name=\"in2\" type=\"float\" value=\"0.4\"/>"
      "      <input name=\"in3\" type=\"float\" value=\"0.6\"/>"
      "    </combine3>"
      "    <extract name=\"green\" type=\"float\">"
      "      <input name=\"in\" type=\"color3\" nodename=\"packed\"/>"
      "      <input name=\"index\" type=\"integer\" value=\"1\"/>"
      "    </extract>"
      "    <convert name=\"green_color\" type=\"color3\">"
      "      <input name=\"in\" type=\"float\" nodename=\"green\"/>"
      "    </convert>"
      "    <add name=\"tint\" type=\"color3\">"
      "      <input name=\"in1\" type=\"color3\" nodename=\"green_color\"/>"
      "      <input name=\"in2\" type=\"color3\" value=\"0.1, 0.2, 0.3\"/>"
      "    </add>"
      "    <output name=\"base\" type=\"color3\" nodename=\"tint\"/>"
      "  </nodegraph>"
      "  <standard_surface name=\"Standard\" type=\"surfaceshader\">"
      "    <input name=\"base_color\" type=\"color3\" nodegraph=\"NG\" output=\"base\"/>"
      "    <input name=\"roughness\" type=\"float\" value=\"0.42\"/>"
      "    <input name=\"specular_ior\" type=\"float\" value=\"1.55\"/>"
      "    <input name=\"coat_ior\" type=\"float\" value=\"1.45\"/>"
      "    <input name=\"opacity\" type=\"float\" value=\"0.37\"/>"
      "  </standard_surface>"
      "  <surfacematerial name=\"StdMat\">"
      "    <input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\"Standard\"/>"
      "  </surfacematerial>"
      "</materialx>";
  if (!eval_xml(standard_xml, &p)) {
    return 1;
  }
  ok = nearf(p.base_color.x, 0.5f) && nearf(p.base_color.y, 0.6f) &&
       nearf(p.base_color.z, 0.7f) && nearf(p.specular_roughness, 0.42f) &&
       nearf(p.specular_ior, 1.55f) && nearf(p.coat_ior, 1.45f) &&
       nearf(p.opacity, 0.37f);
  if (!ok) {
    fprintf(stderr, "unexpected evaluated standard_surface graph params\n");
    return 1;
  }

  const char *channels_xml =
      "<materialx version=\"1.38\">"
      "  <constant name=\"Packed\" type=\"color4\">"
      "    <input name=\"value\" type=\"color4\" value=\"0.1, 0.2, 0.3, 0.4\"/>"
      "  </constant>"
      "  <swizzle name=\"Bgr\" type=\"color3\">"
      "    <input name=\"in\" type=\"color4\" nodename=\"Packed\"/>"
      "    <input name=\"channels\" type=\"string\" value=\"bgr\"/>"
      "  </swizzle>"
      "  <open_pbr_surface name=\"Channels\" type=\"surfaceshader\">"
      "    <input name=\"base_color\" type=\"color3\" nodename=\"Bgr\"/>"
      "    <input name=\"base_roughness\" type=\"float\" nodename=\"Packed\" channels=\"g\"/>"
      "    <input name=\"geometry_opacity\" type=\"float\" nodename=\"Packed\" channels=\"a\"/>"
      "  </open_pbr_surface>"
      "  <surfacematerial name=\"ChannelMat\">"
      "    <input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\"Channels\"/>"
      "  </surfacematerial>"
      "</materialx>";
  if (!eval_xml(channels_xml, &p)) {
    return 1;
  }
  ok = nearf(p.base_color.x, 0.3f) && nearf(p.base_color.y, 0.2f) &&
       nearf(p.base_color.z, 0.1f) && nearf(p.specular_roughness, 0.2f) &&
       nearf(p.opacity, 0.4f);
  if (!ok) {
    fprintf(stderr, "MaterialX channel selectors were not evaluated\n");
    return 1;
  }

  const char *unlit_xml =
      "<materialx version=\"1.39\">"
      " <surface_unlit name=\"Unlit\" type=\"surfaceshader\">"
      "  <input name=\"emission\" type=\"float\" value=\"2.5\"/>"
      "  <input name=\"emission_color\" type=\"color3\" value=\"0.2,0.4,0.8\"/>"
      "  <input name=\"transmission\" type=\"float\" value=\"0.3\"/>"
      "  <input name=\"transmission_color\" type=\"color3\" value=\"0.7,0.6,0.5\"/>"
      "  <input name=\"opacity\" type=\"float\" value=\"0.45\"/>"
      " </surface_unlit>"
      " <surfacematerial name=\"UnlitMat\"><input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\"Unlit\"/></surfacematerial>"
      "</materialx>";
  if (!eval_xml(unlit_xml, &p)) return 1;
  ok = nearf(p.base_weight, 0.0f) && nearf(p.specular_weight, 0.0f) &&
       nearf(p.emission, 2.5f) && nearf(p.emission_color.x, 0.2f) &&
       nearf(p.emission_color.y, 0.4f) && nearf(p.emission_color.z, 0.8f) &&
       nearf(p.transmission, 0.3f) && nearf(p.transmission_color.x, 0.7f) &&
       nearf(p.transmission_color.y, 0.6f) && nearf(p.transmission_color.z, 0.5f) &&
       nearf(p.opacity, 0.45f);
  if (!ok) {
    fprintf(stderr, "surface_unlit parameters were not evaluated\n");
    return 1;
  }

  const char *closure_xml =
      "<materialx version=\"1.39\">"
      " <oren_nayar_diffuse_bsdf name=\"Diffuse\" type=\"BSDF\">"
      "  <input name=\"weight\" type=\"float\" value=\"0.4\"/>"
      "  <input name=\"color\" type=\"color3\" value=\"0.2,0.3,0.4\"/>"
      "  <input name=\"roughness\" type=\"float\" value=\"0.25\"/>"
      " </oren_nayar_diffuse_bsdf>"
      " <dielectric_bsdf name=\"Glass\" type=\"BSDF\">"
      "  <input name=\"weight\" type=\"float\" value=\"0.6\"/>"
      "  <input name=\"tint\" type=\"color3\" value=\"0.8,0.9,1\"/>"
      "  <input name=\"ior\" type=\"float\" value=\"1.45\"/>"
      "  <input name=\"roughness\" type=\"vector2\" value=\"0.12,0.2\"/>"
      "  <input name=\"scatter_mode\" type=\"string\" value=\"RT\"/>"
      " </dielectric_bsdf>"
      " <add name=\"Combined\" type=\"BSDF\">"
      "  <input name=\"in1\" type=\"BSDF\" nodename=\"Diffuse\"/>"
      "  <input name=\"in2\" type=\"BSDF\" nodename=\"Glass\"/>"
      " </add>"
      " <uniform_edf name=\"Emit\" type=\"EDF\">"
      "  <input name=\"color\" type=\"color3\" value=\"0.1,0.2,0.3\"/>"
      " </uniform_edf>"
      " <surface name=\"Surface\" type=\"surfaceshader\">"
      "  <input name=\"bsdf\" type=\"BSDF\" nodename=\"Combined\"/>"
      "  <input name=\"edf\" type=\"EDF\" nodename=\"Emit\"/>"
      "  <input name=\"opacity\" type=\"float\" value=\"0.75\"/>"
      " </surface>"
      " <surfacematerial name=\"ClosureMat\" type=\"material\">"
      "  <input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\"Surface\"/>"
      " </surfacematerial>"
      "</materialx>";
  if (!eval_xml(closure_xml, &p)) return 1;
  ok = nearf(p.base_weight, 0.4f) && nearf(p.base_color.x, 0.2f) &&
       nearf(p.diffuse_roughness, 0.25f) &&
       nearf(p.specular_weight, 0.6f) && nearf(p.specular_ior, 1.45f) &&
       nearf(p.specular_roughness, 0.12f) && nearf(p.transmission, 0.6f) &&
       nearf(p.emission_color.z, 0.3f) && nearf(p.opacity, 0.75f);
  if (!ok) {
    fprintf(stderr, "MaterialX surface closure graph was not evaluated: "
            "base=%g color=%g rough=%g spec=%g ior=%g srough=%g trans=%g "
            "emit=%g opacity=%g\n", p.base_weight, p.base_color.x,
            p.diffuse_roughness, p.specular_weight, p.specular_ior,
            p.specular_roughness, p.transmission, p.emission_color.z,
            p.opacity);
    return 1;
  }

  const char *weighted_roughness_xml =
      "<materialx version=\"1.39\">"
      " <dielectric_bsdf name=\"GlassA\" type=\"BSDF\">"
      "  <input name=\"weight\" type=\"float\" value=\"0.5\"/>"
      "  <input name=\"roughness\" type=\"float\" value=\"0.1\"/>"
      " </dielectric_bsdf>"
      " <dielectric_bsdf name=\"GlassB\" type=\"BSDF\">"
      "  <input name=\"weight\" type=\"float\" value=\"0.5\"/>"
      "  <input name=\"roughness\" type=\"float\" value=\"0.5\"/>"
      " </dielectric_bsdf>"
      " <add name=\"Combined\" type=\"BSDF\">"
      "  <input name=\"in1\" type=\"BSDF\" nodename=\"GlassA\"/>"
      "  <input name=\"in2\" type=\"BSDF\" nodename=\"GlassB\"/>"
      " </add>"
      " <surface name=\"Surface\" type=\"surfaceshader\">"
      "  <input name=\"bsdf\" type=\"BSDF\" nodename=\"Combined\"/>"
      " </surface>"
      " <surfacematerial name=\"Mat\"><input name=\"surfaceshader\""
      " type=\"surfaceshader\" nodename=\"Surface\"/></surfacematerial>"
      "</materialx>";
  if (!eval_xml(weighted_roughness_xml, &p) ||
      !nearf(p.specular_weight, 1.0f) ||
      !nearf(p.specular_roughness, 0.3f)) {
    fprintf(stderr, "composed specular roughness was not weighted\n");
    return 1;
  }

  const char *scatter_mode_xml =
      "<materialx version=\"1.39\">"
      " <dielectric_bsdf name=\"Glass\" type=\"BSDF\">"
      "  <input name=\"weight\" type=\"float\" value=\"0.6\"/>"
      "  <input name=\"tint\" type=\"color3\" value=\"0.8,0.9,1\"/>"
      "  <input name=\"scatter_mode\" type=\"string\" value=\"T\"/>"
      " </dielectric_bsdf>"
      " <surface name=\"Surface\" type=\"surfaceshader\">"
      "  <input name=\"bsdf\" type=\"BSDF\" nodename=\"Glass\"/>"
      " </surface>"
      " <surfacematerial name=\"GlassMat\"><input name=\"surfaceshader\""
      " type=\"surfaceshader\" nodename=\"Surface\"/></surfacematerial>"
      "</materialx>";
  if (!eval_xml(scatter_mode_xml, &p) || !nearf(p.specular_weight, 0.0f) ||
      !nearf(p.transmission, 0.6f)) {
    fprintf(stderr, "MaterialX transmission-only scatter mode failed\n");
    return 1;
  }

  const char *invalid_scatter_mode_xml =
      "<materialx version=\"1.39\">"
      " <dielectric_bsdf name=\"Glass\" type=\"BSDF\">"
      "  <input name=\"weight\" type=\"float\" value=\"0.6\"/>"
      "  <input name=\"scatter_mode\" type=\"string\" value=\"TR\"/>"
      " </dielectric_bsdf>"
      " <surface name=\"Surface\" type=\"surfaceshader\">"
      "  <input name=\"bsdf\" type=\"BSDF\" nodename=\"Glass\"/>"
      " </surface>"
      " <surfacematerial name=\"GlassMat\"><input name=\"surfaceshader\""
      " type=\"surfaceshader\" nodename=\"Surface\"/></surfacematerial>"
      "</materialx>";
  if (!eval_xml(invalid_scatter_mode_xml, &p) ||
      !nearf(p.specular_weight, 0.0f) || !nearf(p.transmission, 0.0f)) {
    fprintf(stderr, "invalid MaterialX scatter mode was accepted\n");
    return 1;
  }

  const char *schlick_transmission_xml =
      "<materialx version=\"1.39\">"
      " <generalized_schlick_bsdf name=\"Glass\" type=\"BSDF\">"
      "  <input name=\"weight\" type=\"float\" value=\"0.6\"/>"
      "  <input name=\"color0\" type=\"color3\" value=\"0.2,0.3,0.4\"/>"
      "  <input name=\"color90\" type=\"color3\" value=\"0.4,0.5,0.6\"/>"
      "  <input name=\"scatter_mode\" type=\"string\" value=\"T\"/>"
      " </generalized_schlick_bsdf>"
      " <surface name=\"Surface\" type=\"surfaceshader\">"
      "  <input name=\"bsdf\" type=\"BSDF\" nodename=\"Glass\"/>"
      " </surface>"
      " <surfacematerial name=\"GlassMat\"><input name=\"surfaceshader\""
      " type=\"surfaceshader\" nodename=\"Surface\"/></surfacematerial>"
      "</materialx>";
  if (!eval_xml(schlick_transmission_xml, &p) ||
      !nearf(p.specular_weight, 0.0f) || !nearf(p.transmission, 0.6f) ||
      !nearf(p.transmission_color.x, 1.0f) ||
      !nearf(p.transmission_color.y, 1.0f) ||
      !nearf(p.transmission_color.z, 1.0f)) {
    fprintf(stderr, "generalized Schlick transmission tint was not neutral\n");
    return 1;
  }

  const char *volume_xml =
      "<materialx version=\"1.39\">"
      " <anisotropic_vdf name=\"Fog\" type=\"VDF\">"
      "  <input name=\"absorption\" type=\"vector3\" value=\"0.1,0.2,0.3\"/>"
      "  <input name=\"scattering\" type=\"vector3\" value=\"0.4,0.5,0.6\"/>"
      "  <input name=\"anisotropy\" type=\"float\" value=\"0.35\"/>"
      " </anisotropic_vdf>"
      " <absorption_vdf name=\"FogFill\" type=\"VDF\">"
      "  <input name=\"absorption\" type=\"vector3\" value=\"0.2,0.1,0.0\"/>"
      " </absorption_vdf>"
      " <mix name=\"MixedFog\" type=\"VDF\">"
      "  <input name=\"bg\" type=\"VDF\" nodename=\"Fog\"/>"
      "  <input name=\"fg\" type=\"VDF\" nodename=\"FogFill\"/>"
      "  <input name=\"mix\" type=\"float\" value=\"0.25\"/>"
      " </mix>"
      " <uniform_edf name=\"Glow\" type=\"EDF\">"
      "  <input name=\"color\" type=\"color3\" value=\"2,1,0.5\"/>"
      " </uniform_edf>"
      " <uniform_edf name=\"Fill\" type=\"EDF\">"
      "  <input name=\"color\" type=\"color3\" value=\"0.25,0.5,0.75\"/>"
      " </uniform_edf>"
      " <mix name=\"MixedGlow\" type=\"EDF\">"
      "  <input name=\"bg\" type=\"EDF\" nodename=\"Glow\"/>"
      "  <input name=\"fg\" type=\"EDF\" nodename=\"Fill\"/>"
      "  <input name=\"mix\" type=\"float\" value=\"0.25\"/>"
      " </mix>"
      " <volume name=\"Volume\" type=\"volumeshader\">"
      "  <input name=\"vdf\" type=\"VDF\" nodename=\"MixedFog\"/>"
      "  <input name=\"edf\" type=\"EDF\" nodename=\"MixedGlow\"/>"
      " </volume>"
      " <volumematerial name=\"VolumeMat\" type=\"material\">"
      "  <input name=\"volumeshader\" type=\"volumeshader\" nodename=\"Volume\"/>"
      " </volumematerial>"
      "</materialx>";
  MtlxVolumeParams volume;
  if (!eval_volume_xml(volume_xml, &volume)) return 1;
  ok = nearf(volume.absorption.x, 0.125f) &&
       nearf(volume.absorption.y, 0.175f) &&
       nearf(volume.absorption.z, 0.225f) &&
       nearf(volume.scattering.x, 0.3f) &&
       nearf(volume.scattering.y, 0.375f) &&
       nearf(volume.scattering.z, 0.45f) &&
       nearf(volume.anisotropy, 0.35f) &&
       nearf(volume.emission.x, 1.5625f) &&
       nearf(volume.emission.y, 0.875f) &&
       nearf(volume.emission.z, 0.5625f);
  if (!ok) {
    fprintf(stderr, "MaterialX volume/VDF/EDF graph was not evaluated\n");
    return 1;
  }

  /* Measured profiles require a renderer-specific spectral/table backend.
   * The standalone evaluator's documented fallback preserves an authored
   * color while ignoring the unavailable profile filename. */
  const char *measured_volume_xml =
      "<materialx version=\"1.39\">"
      " <measured_edf name=\"MeasuredGlow\" type=\"EDF\">"
      "  <input name=\"color\" type=\"color3\" value=\"0.15,0.35,0.65\"/>"
      "  <input name=\"file\" type=\"filename\" value=\"profile.ies\"/>"
      " </measured_edf>"
      " <volume name=\"MeasuredVolume\" type=\"volumeshader\">"
      "  <input name=\"edf\" type=\"EDF\" nodename=\"MeasuredGlow\"/>"
      " </volume>"
      " <volumematerial name=\"MeasuredMat\" type=\"material\">"
      "  <input name=\"volumeshader\" type=\"volumeshader\" nodename=\"MeasuredVolume\"/>"
      " </volumematerial></materialx>";
  if (!eval_volume_xml(measured_volume_xml, &volume) ||
      !nearf(volume.emission.x, 0.15f) ||
      !nearf(volume.emission.y, 0.35f) ||
      !nearf(volume.emission.z, 0.65f)) {
    fprintf(stderr, "MaterialX measured EDF color fallback was not evaluated\n");
    return 1;
  }

  const char *subsurface_volume_xml =
      "<materialx version=\"1.39\">"
      " <subsurface_vdf name=\"Subsurface\" type=\"VDF\">"
      "  <input name=\"scattering\" type=\"color3\" value=\"0.3,0.5,0.7\"/>"
      "  <input name=\"anisotropy\" type=\"float\" value=\"0.2\"/>"
      " </subsurface_vdf>"
      " <volume name=\"SubsurfaceVolume\" type=\"volumeshader\">"
      "  <input name=\"vdf\" type=\"VDF\" nodename=\"Subsurface\"/>"
      " </volume>"
      " <volumematerial name=\"SubsurfaceMat\" type=\"material\">"
      "  <input name=\"volumeshader\" type=\"volumeshader\" nodename=\"SubsurfaceVolume\"/>"
      " </volumematerial></materialx>";
  if (!eval_volume_xml(subsurface_volume_xml, &volume) ||
      !nearf(volume.scattering.x, 0.3f) ||
      !nearf(volume.scattering.y, 0.5f) ||
      !nearf(volume.scattering.z, 0.7f) ||
      !nearf(volume.anisotropy, 0.2f)) {
    fprintf(stderr, "MaterialX subsurface VDF was not evaluated\n");
    return 1;
  }

  return 0;
}
