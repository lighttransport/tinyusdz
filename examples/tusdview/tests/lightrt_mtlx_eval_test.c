// SPDX-License-Identifier: Apache-2.0
#include "mtlx_doc.h"
#include "mtlx_eval.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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
  ctx.doc = doc;
  ctx.tex = NULL;
  ctx.uv[0] = 0.5f;
  ctx.uv[1] = 0.5f;
  ctx.P = v3_make(0.0f, 0.0f, 0.0f);
  ctx.Ns = v3_make(0.0f, 0.0f, 1.0f);
  ctx.Ng = v3_make(0.0f, 0.0f, 1.0f);
  ctx.dpdu = v3_make(1.0f, 0.0f, 0.0f);
  ctx.dpdv = v3_make(0.0f, 1.0f, 0.0f);
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

int main(void) {
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

  return 0;
}
