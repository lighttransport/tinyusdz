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
  TextureCache *udim_cache = texcache_create(".");
  MtlxDoc *udim_doc = mtlx_load_string(
      "<materialx><image name=\"udim\" type=\"color3\"><input "
      "name=\"file\" type=\"filename\" value=\"lightrt_udim_regression.<UDIM>.ppm\"/>"
      "</image></materialx>");
  if (!udim_doc) {
    texcache_free(udim_cache); remove(udim_1001); remove(udim_1002); return 1;
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
  const int udim_missing = texcache_sample_file(
      udim_cache, "lightrt_udim_regression.<UDIM>.ppm", 0, 2.25f, 0.5f,
      udim_sample);
  texcache_free(udim_cache);
  remove(udim_1001);
  remove(udim_1002);
  if (!udim_first || !udim_second || !first_red || !second_blue || udim_missing ||
      udim_sample[3] != 1.0f) {
    fprintf(stderr, "UDIM present/missing tile resolution failed\n");
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
      "  <input name=\"measurement\" type=\"filename\" value=\"profile.mxd\"/>"
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
