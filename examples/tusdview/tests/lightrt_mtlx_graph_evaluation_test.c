// SPDX-License-Identifier: Apache-2.0
#include "mtlx_doc.h"
#include "mtlx_eval.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int nearf_eps(float a, float b) { return fabsf(a - b) <= 1.0e-5f; }

static int check1(const char *name, MtlxValue value, float expected) {
  if (!nearf_eps(value.v[0], expected)) {
    fprintf(stderr, "%s: got %.9f, expected %.9f\n", name, value.v[0],
            expected);
    return 0;
  }
  return 1;
}

static int check3(const char *name, MtlxValue value, float x, float y, float z) {
  if (!nearf_eps(value.v[0], x) || !nearf_eps(value.v[1], y) ||
      !nearf_eps(value.v[2], z)) {
    fprintf(stderr, "%s: got %.6f %.6f %.6f, expected %.6f %.6f %.6f\n",
            name, value.v[0], value.v[1], value.v[2], x, y, z);
    return 0;
  }
  return 1;
}

static int check4(const char *name, MtlxValue value, float x, float y, float z,
                  float w) {
  return check3(name, value, x, y, z) && nearf_eps(value.v[3], w);
}

static MtlxValue eval_named(ShadeContext *ctx, const char *name) {
  int id = mtlx_find_node(ctx->doc, -1, name);
  for (int graph = 0; id < 0 && graph < ctx->doc->ngraph; ++graph)
    id = mtlx_find_node(ctx->doc, graph, name);
  return mtlx_eval_node_test(ctx, id);
}

int main(void) {
  const char *xml =
      "<materialx version=\"1.39\"><nodegraph name=\"NG\">"
      " <constant name=\"src\" type=\"color3\"><input name=\"value\" type=\"color3\" value=\"0.2,0.4,0.8\"/></constant>"
      " <add name=\"add\" type=\"color3\"><input name=\"in1\" type=\"color3\" nodename=\"src\"/><input name=\"in2\" type=\"color3\" value=\"0.1,0.2,0.3\"/></add>"
      " <multiply name=\"mul\" type=\"color3\"><input name=\"in1\" type=\"color3\" nodename=\"add\"/><input name=\"in2\" type=\"float\" value=\"2\"/></multiply>"
      " <clamp name=\"clamped\" type=\"color3\"><input name=\"in\" nodename=\"mul\"/><input name=\"low\" value=\"0.0\"/><input name=\"high\" value=\"1.0\"/></clamp>"
      " <texcoord name=\"st\" type=\"vector2\"/>"
      " <ramplr name=\"ramp\" type=\"color3\"><input name=\"valuel\" type=\"color3\" value=\"1,0,0\"/><input name=\"valuer\" type=\"color3\" value=\"0,0,1\"/><input name=\"texcoord\" type=\"vector2\" nodename=\"st\"/></ramplr>"
      " <splitlr name=\"split\" type=\"color3\"><input name=\"valuel\" type=\"color3\" value=\"0,1,0\"/><input name=\"valuer\" type=\"color3\" value=\"1,0,1\"/><input name=\"center\" type=\"float\" value=\"0.6\"/><input name=\"texcoord\" type=\"vector2\" nodename=\"st\"/></splitlr>"
      " <ramptb name=\"ramp_tb\" type=\"color3\"><input name=\"valuet\" type=\"color3\" value=\"1,1,0\"/><input name=\"valueb\" type=\"color3\" value=\"0,1,1\"/><input name=\"texcoord\" type=\"vector2\" nodename=\"st\"/></ramptb>"
      " <splittb name=\"split_tb\" type=\"color3\"><input name=\"valuet\" type=\"color3\" value=\"1,0,0\"/><input name=\"valueb\" type=\"color3\" value=\"0,0,1\"/><input name=\"center\" type=\"float\" value=\"0.8\"/><input name=\"texcoord\" type=\"vector2\" nodename=\"st\"/></splittb>"
      " <screen name=\"screen\" type=\"color3\"><input name=\"fg\" type=\"color3\" value=\"0.2,0.5,0.8\"/><input name=\"bg\" type=\"color3\" value=\"0.1,0.4,0.7\"/><input name=\"mix\" type=\"float\" value=\"0.5\"/></screen>"
      " <overlay name=\"overlay\" type=\"color3\"><input name=\"fg\" type=\"color3\" value=\"0.2,0.5,0.8\"/><input name=\"bg\" type=\"color3\" value=\"0.1,0.4,0.7\"/><input name=\"mix\" type=\"float\" value=\"0.5\"/></overlay>"
      " <burn name=\"burn\" type=\"float\"><input name=\"fg\" type=\"float\" value=\"0.5\"/><input name=\"bg\" type=\"float\" value=\"0.25\"/><input name=\"mix\" type=\"float\" value=\"0.5\"/></burn>"
      " <dodge name=\"dodge\" type=\"float\"><input name=\"fg\" type=\"float\" value=\"0.5\"/><input name=\"bg\" type=\"float\" value=\"0.25\"/><input name=\"mix\" type=\"float\" value=\"0.5\"/></dodge>"
      " <saturate name=\"desaturate\" type=\"color3\"><input name=\"in\" type=\"color3\" value=\"0.2,0.4,0.8\"/><input name=\"amount\" type=\"float\" value=\"0\"/></saturate>"
      " <dotproduct name=\"dot\" type=\"float\"><input name=\"in1\" type=\"vector3\" value=\"1,2,3\"/><input name=\"in2\" type=\"vector3\" value=\"4,5,6\"/></dotproduct>"
      " <normalize name=\"norm\" type=\"vector3\"><input name=\"in\" type=\"vector3\" value=\"0,3,4\"/></normalize>"
      " <fraction name=\"fraction\" type=\"color3\"><input name=\"in\" type=\"color3\" value=\"-1.25,1.5,2.75\"/></fraction>"
      " <step name=\"step\" type=\"color3\"><input name=\"in\" type=\"color3\" value=\"0.2,0.5,0.8\"/><input name=\"edge\" type=\"float\" value=\"0.5\"/></step>"
      " <rgbtohsv name=\"hsv\" type=\"color3\"><input name=\"in\" type=\"color3\" value=\"0.2,0.4,0.8\"/></rgbtohsv>"
      " <hsvtorgb name=\"rgb\" type=\"color3\"><input name=\"in\" type=\"color3\" nodename=\"hsv\"/></hsvtorgb>"
      " <rotate2d name=\"rotate\" type=\"vector2\"><input name=\"in\" type=\"vector2\" value=\"1,0\"/><input name=\"amount\" type=\"float\" value=\"90\"/></rotate2d>"
      " <ramp4 name=\"quad\" type=\"color3\"><input name=\"valuetl\" type=\"color3\" value=\"1,0,0\"/><input name=\"valuetr\" type=\"color3\" value=\"0,1,0\"/><input name=\"valuebl\" type=\"color3\" value=\"0,0,1\"/><input name=\"valuebr\" type=\"color3\" value=\"1,1,1\"/><input name=\"texcoord\" type=\"vector2\" value=\"0.25,0.75\"/></ramp4>"
      " <switch name=\"pick\" type=\"color3\"><input name=\"which\" type=\"integer\" value=\"1\"/><input name=\"in1\" type=\"color3\" value=\"1,0,0\"/><input name=\"in2\" type=\"color3\" nodename=\"quad\"/><input name=\"in3\" type=\"color3\" value=\"0,0,1\"/></switch>"
      " <distance name=\"distance\" type=\"float\"><input name=\"in1\" type=\"vector3\" value=\"1,2,3\"/><input name=\"in2\" type=\"vector3\" value=\"1,5,7\"/></distance>"
      " <reflect name=\"reflect\" type=\"vector3\"><input name=\"in\" type=\"vector3\" value=\"1,-1,0\"/><input name=\"normal\" type=\"vector3\" value=\"0,1,0\"/></reflect>"
      " <premult name=\"premult\" type=\"color4\"><input name=\"in\" type=\"color4\" value=\"0.8,0.4,0.2,0.5\"/></premult>"
      " <unpremult name=\"unpremult\" type=\"color4\"><input name=\"in\" type=\"color4\" nodename=\"premult\"/></unpremult>"
      " <mincomponent name=\"mincomp\" type=\"float\"><input name=\"in\" type=\"color3\" value=\"0.8,0.2,0.5\"/></mincomponent>"
      " <xor name=\"xor\" type=\"boolean\"><input name=\"in1\" type=\"boolean\" value=\"true\"/><input name=\"in2\" type=\"boolean\" value=\"false\"/></xor>"
      " <inside name=\"inside\" type=\"color3\"><input name=\"in\" type=\"color3\" value=\"0.2,0.4,0.8\"/><input name=\"mask\" type=\"float\" value=\"0.25\"/></inside>"
      " <trianglewave name=\"tri_wave\" type=\"float\"><input name=\"in\" type=\"float\" value=\"1.25\"/></trianglewave>"
      " <checkerboard name=\"checker\" type=\"color3\"><input name=\"color1\" type=\"color3\" value=\"1,0,0\"/><input name=\"color2\" type=\"color3\" value=\"0,0,1\"/><input name=\"uvtiling\" type=\"vector2\" value=\"2,2\"/><input name=\"texcoord\" type=\"vector2\" value=\"0.6,0.1\"/></checkerboard>"
      " <circle name=\"circle\" type=\"float\"><input name=\"center\" type=\"vector2\" value=\"0.5,0.5\"/><input name=\"radius\" type=\"float\" value=\"0.25\"/><input name=\"texcoord\" type=\"vector2\" value=\"0.6,0.6\"/></circle>"
      " <line name=\"line\" type=\"float\"><input name=\"point1\" type=\"vector2\" value=\"0.25,0.25\"/><input name=\"point2\" type=\"vector2\" value=\"0.75,0.75\"/><input name=\"radius\" type=\"float\" value=\"0.1\"/><input name=\"texcoord\" type=\"vector2\" value=\"0.55,0.5\"/></line>"
      " <difference name=\"difference\" type=\"color3\"><input name=\"fg\" type=\"color3\" value=\"0.8,0.1,0.4\"/><input name=\"bg\" type=\"color3\" value=\"0.2,0.5,0.1\"/><input name=\"mix\" type=\"float\" value=\"0.5\"/></difference>"
      " <over name=\"over\" type=\"color4\"><input name=\"fg\" type=\"color4\" value=\"0.4,0.1,0.2,0.5\"/><input name=\"bg\" type=\"color4\" value=\"0.2,0.4,0.6,0.25\"/></over>"
      " <matte name=\"matte\" type=\"color4\"><input name=\"fg\" type=\"color4\" value=\"0.4,0.1,0.2,0.5\"/><input name=\"bg\" type=\"color4\" value=\"0.2,0.4,0.6,0.25\"/></matte>"
      " <disjointover name=\"disjoint\" type=\"color4\"><input name=\"fg\" type=\"color4\" value=\"0.4,0.1,0.2,0.8\"/><input name=\"bg\" type=\"color4\" value=\"0.2,0.4,0.6,0.6\"/></disjointover>"
      " <colorcorrect name=\"colorcorrect\" type=\"color4\"><input name=\"in\" type=\"color4\" value=\"0.5,0.25,0.75,0.3\"/><input name=\"gamma\" type=\"float\" value=\"2\"/><input name=\"lift\" type=\"float\" value=\"0.1\"/><input name=\"gain\" type=\"float\" value=\"0.8\"/><input name=\"exposure\" type=\"float\" value=\"1\"/></colorcorrect>"
      " <cellnoise2d name=\"cell2a\" type=\"float\"><input name=\"texcoord\" type=\"vector2\" value=\"1.2,2.8\"/></cellnoise2d>"
      " <cellnoise2d name=\"cell2b\" type=\"float\"><input name=\"texcoord\" type=\"vector2\" value=\"1.9,2.1\"/></cellnoise2d>"
      " <cellnoise2d name=\"cell2next\" type=\"float\"><input name=\"texcoord\" type=\"vector2\" value=\"0.9,2.1\"/></cellnoise2d>"
      " <cellnoise3d name=\"cell3\" type=\"float\"><input name=\"position\" type=\"vector3\" value=\"1.2,2.8,3.4\"/></cellnoise3d>"
      " <randomfloat name=\"random\" type=\"float\"><input name=\"in\" type=\"float\" value=\"0.25\"/><input name=\"min\" type=\"float\" value=\"2\"/><input name=\"max\" type=\"float\" value=\"4\"/><input name=\"seed\" type=\"integer\" value=\"7\"/></randomfloat>"
      " <randomcolor name=\"random_color\" type=\"color3\"><input name=\"in\" type=\"float\" value=\"0.25\"/><input name=\"seed\" type=\"integer\" value=\"7\"/><input name=\"huelow\" type=\"float\" value=\"0.1\"/><input name=\"huehigh\" type=\"float\" value=\"0.2\"/><input name=\"saturationlow\" type=\"float\" value=\"0.5\"/><input name=\"saturationhigh\" type=\"float\" value=\"0.6\"/><input name=\"brightnesslow\" type=\"float\" value=\"0.7\"/><input name=\"brightnesshigh\" type=\"float\" value=\"0.8\"/></randomcolor>"
      " <fractal2d name=\"fractal_scalar\" type=\"float\"><input name=\"amplitude\" type=\"float\" value=\"1.5\"/><input name=\"octaves\" type=\"integer\" value=\"3\"/><input name=\"lacunarity\" type=\"float\" value=\"2\"/><input name=\"diminish\" type=\"float\" value=\"0.5\"/><input name=\"texcoord\" type=\"vector2\" value=\"0.2,0.4\"/></fractal2d>"
      " <fractal2d name=\"fractal_color\" type=\"color3\"><input name=\"amplitude\" type=\"color3\" value=\"1,2,3\"/><input name=\"octaves\" type=\"integer\" value=\"3\"/><input name=\"lacunarity\" type=\"float\" value=\"2\"/><input name=\"diminish\" type=\"float\" value=\"0.5\"/><input name=\"texcoord\" type=\"vector2\" value=\"0.2,0.4\"/></fractal2d>"
      " <worleynoise2d name=\"worley2_distance\" type=\"vector3\"><input name=\"texcoord\" type=\"vector2\" value=\"0.2,0.4\"/><input name=\"jitter\" type=\"float\" value=\"1\"/><input name=\"style\" type=\"integer\" value=\"0\"/></worleynoise2d>"
      " <worleynoise2d name=\"worley2_solid\" type=\"vector3\"><input name=\"texcoord\" type=\"vector2\" value=\"0.2,0.4\"/><input name=\"jitter\" type=\"float\" value=\"1\"/><input name=\"style\" type=\"integer\" value=\"1\"/></worleynoise2d>"
      " <worleynoise3d name=\"worley3_distance\" type=\"vector2\"><input name=\"position\" type=\"vector3\" value=\"0.2,0.4,0.7\"/><input name=\"jitter\" type=\"float\" value=\"0.75\"/><input name=\"style\" type=\"integer\" value=\"0\"/></worleynoise3d>"
      " <noise2d name=\"noise2_color\" type=\"color3\"><input name=\"texcoord\" type=\"vector2\" value=\"0.2,0.4\"/><input name=\"amplitude\" type=\"color3\" value=\"1,2,3\"/><input name=\"pivot\" type=\"color3\" value=\"0.1,0.2,0.3\"/></noise2d>"
      " <noise3d name=\"noise3_color\" type=\"color3\"><input name=\"position\" type=\"vector3\" value=\"0.2,0.4,0.7\"/><input name=\"amplitude\" type=\"color3\" value=\"1,2,3\"/><input name=\"pivot\" type=\"color3\" value=\"0.1,0.2,0.3\"/></noise3d>"
      " <unifiednoise2d name=\"unified_perlin\" type=\"float\"><input name=\"texcoord\" type=\"vector2\" value=\"0.2,0.4\"/><input name=\"freq\" type=\"vector2\" value=\"2,3\"/><input name=\"offset\" type=\"vector2\" value=\"0.1,0.2\"/><input name=\"jitter\" type=\"float\" value=\"0.8\"/><input name=\"outmin\" type=\"float\" value=\"-1\"/><input name=\"outmax\" type=\"float\" value=\"2\"/><input name=\"type\" type=\"integer\" value=\"0\"/></unifiednoise2d>"
      " <unifiednoise2d name=\"unified_cell\" type=\"float\"><input name=\"texcoord\" type=\"vector2\" value=\"0.2,0.4\"/><input name=\"jitter\" type=\"float\" value=\"0.8\"/><input name=\"type\" type=\"integer\" value=\"1\"/></unifiednoise2d>"
      " <unifiednoise2d name=\"unified_worley\" type=\"float\"><input name=\"texcoord\" type=\"vector2\" value=\"0.2,0.4\"/><input name=\"jitter\" type=\"float\" value=\"0.8\"/><input name=\"type\" type=\"integer\" value=\"2\"/></unifiednoise2d>"
      " <unifiednoise2d name=\"unified_fractal\" type=\"float\"><input name=\"texcoord\" type=\"vector2\" value=\"0.2,0.4\"/><input name=\"jitter\" type=\"float\" value=\"0.8\"/><input name=\"type\" type=\"integer\" value=\"3\"/></unifiednoise2d>"
      " <unifiednoise3d name=\"unified3_fractal\" type=\"float\"><input name=\"position\" type=\"vector3\" value=\"0.2,0.4,0.7\"/><input name=\"jitter\" type=\"float\" value=\"0.8\"/><input name=\"type\" type=\"integer\" value=\"3\"/></unifiednoise3d>"
      " <cloverleaf name=\"clover_inside\" type=\"float\"><input name=\"texcoord\" type=\"vector2\" value=\"0.5,0.65\"/><input name=\"center\" type=\"vector2\" value=\"0.5,0.5\"/><input name=\"radius\" type=\"float\" value=\"0.4\"/></cloverleaf>"
      " <cloverleaf name=\"clover_outside\" type=\"float\"><input name=\"texcoord\" type=\"vector2\" value=\"0.95,0.95\"/><input name=\"center\" type=\"vector2\" value=\"0.5,0.5\"/><input name=\"radius\" type=\"float\" value=\"0.4\"/></cloverleaf>"
      " <hexagon name=\"hex_inside\" type=\"float\"><input name=\"texcoord\" type=\"vector2\" value=\"0.5,0.5\"/><input name=\"center\" type=\"vector2\" value=\"0.5,0.5\"/><input name=\"radius\" type=\"float\" value=\"0.4\"/></hexagon>"
      " <hexagon name=\"hex_outside\" type=\"float\"><input name=\"texcoord\" type=\"vector2\" value=\"0.95,0.95\"/><input name=\"center\" type=\"vector2\" value=\"0.5,0.5\"/><input name=\"radius\" type=\"float\" value=\"0.4\"/></hexagon>"
      " <ifgreater name=\"choose\" type=\"color3\"><input name=\"value1\" value=\"2\"/><input name=\"value2\" value=\"1\"/><input name=\"in1\" nodename=\"ramp\"/><input name=\"in2\" nodename=\"split\"/></ifgreater>"
      " <ifgreatereq name=\"choose_eq\" type=\"color3\"><input name=\"value1\" value=\"1\"/><input name=\"value2\" value=\"1\"/><input name=\"in1\" type=\"color3\" value=\"0.1,0.2,0.3\"/><input name=\"in2\" type=\"color3\" value=\"0.8,0.7,0.6\"/></ifgreatereq>"
      " <ifequal name=\"choose_ne\" type=\"color3\"><input name=\"value1\" value=\"1\"/><input name=\"value2\" value=\"2\"/><input name=\"in1\" type=\"color3\" value=\"1,0,0\"/><input name=\"in2\" type=\"color3\" value=\"0,0,1\"/></ifequal>"
      "</nodegraph></materialx>";
  MtlxDoc *doc = mtlx_load_string(xml);
  if (!doc) return 1;
  ShadeContext ctx = {0};
  ctx.doc = doc;
  ctx.uv[0] = 0.25f; ctx.uv[1] = 0.75f;
  ctx.Ns = ctx.Ng = v3_make(0, 0, 1);
  ctx.dpdu = v3_make(1, 0, 0); ctx.dpdv = v3_make(0, 1, 0);
  ctx.memo = (MtlxValue *)calloc((size_t)doc->nnode, sizeof(MtlxValue));
  ctx.memo_done = (char *)calloc((size_t)doc->nnode, 1);
  if (!ctx.memo || !ctx.memo_done) {
    free(ctx.memo); free(ctx.memo_done); mtlx_free(doc); return 1;
  }
  int ok = check3("chain", eval_named(&ctx, "clamped"), 0.6f, 1.0f, 1.0f) &&
           check3("ramp-left", eval_named(&ctx, "ramp"), 0.75f, 0.0f, 0.25f) &&
           check3("split-left", eval_named(&ctx, "split"), 0.0f, 1.0f, 0.0f) &&
           check3("ramp-tb", eval_named(&ctx, "ramp_tb"), 0.25f, 1.0f, 0.75f) &&
           check3("split-tb-top", eval_named(&ctx, "split_tb"), 1.0f, 0.0f, 0.0f) &&
           check3("screen", eval_named(&ctx, "screen"), 0.19f, 0.55f, 0.82f) &&
           check3("overlay", eval_named(&ctx, "overlay"), 0.07f, 0.4f, 0.79f) &&
           nearf_eps(eval_named(&ctx, "burn").v[0], -0.125f) &&
           nearf_eps(eval_named(&ctx, "dodge").v[0], 0.375f) &&
           check3("saturate", eval_named(&ctx, "desaturate"), 0.38636f,
                  0.38636f, 0.38636f) &&
           check3("conditional", eval_named(&ctx, "choose"), 0.75f, 0.0f, 0.25f) &&
           check3("conditional-gte-boundary", eval_named(&ctx, "choose_eq"),
                  0.1f, 0.2f, 0.3f) &&
           check3("conditional-equal-false", eval_named(&ctx, "choose_ne"),
                  0.0f, 0.0f, 1.0f) &&
           nearf_eps(eval_named(&ctx, "dot").v[0], 32.0f) &&
           check3("normalize", eval_named(&ctx, "norm"), 0.0f, 0.6f, 0.8f);
  ok = check3("fraction-alias", eval_named(&ctx, "fraction"), 0.75f, 0.5f,
              0.75f) &&
       check3("step-boundary", eval_named(&ctx, "step"), 0.0f, 1.0f, 1.0f) && ok;
  ok = check3("hsv-roundtrip", eval_named(&ctx, "rgb"), 0.2f, 0.4f, 0.8f) &&
       nearf_eps(eval_named(&ctx, "rotate").v[0], 0.0f) &&
       nearf_eps(eval_named(&ctx, "rotate").v[1], 1.0f) && ok;
  ok = check3("ramp4", eval_named(&ctx, "quad"), 0.375f, 0.25f, 0.75f) &&
       check3("switch-connected", eval_named(&ctx, "pick"), 0.375f, 0.25f,
              0.75f) && ok;
  ok = nearf_eps(eval_named(&ctx, "distance").v[0], 5.0f) &&
       check3("reflect", eval_named(&ctx, "reflect"), 1.0f, 1.0f, 0.0f) &&
       check3("unpremult", eval_named(&ctx, "unpremult"), 0.8f, 0.4f, 0.2f) &&
       nearf_eps(eval_named(&ctx, "mincomp").v[0], 0.2f) &&
       nearf_eps(eval_named(&ctx, "xor").v[0], 1.0f) &&
       check3("inside", eval_named(&ctx, "inside"), 0.05f, 0.1f, 0.2f) && ok;
  ok = nearf_eps(eval_named(&ctx, "tri_wave").v[0], 0.25f) && ok;
  ok = check3("checkerboard", eval_named(&ctx, "checker"), 0.0f, 0.0f, 1.0f) && ok;
  ok = nearf_eps(eval_named(&ctx, "circle").v[0], 1.0f) && ok;
  ok = nearf_eps(eval_named(&ctx, "line").v[0], 1.0f) && ok;
  ok = check3("difference", eval_named(&ctx, "difference"), 0.4f, 0.45f,
              0.2f) && ok;
  ok = check4("over", eval_named(&ctx, "over"), 0.5f, 0.3f, 0.5f,
              0.625f) && ok;
  ok = check4("matte", eval_named(&ctx, "matte"), 0.3f, 0.25f, 0.4f,
              0.625f) && ok;
  ok = check4("disjointover", eval_named(&ctx, "disjoint"),
              0.46666667f, 0.23333333f, 0.4f, 1.0f) && ok;
  ok = check4("colorcorrect", eval_named(&ctx, "colorcorrect"), 1.178234f,
              0.88f, 1.407077f, 0.3f) && ok;
  ok = check1("cellnoise2d-a", eval_named(&ctx, "cell2a"), 0.06335137f) &&
       check1("cellnoise2d-same-cell", eval_named(&ctx, "cell2b"),
              0.06335137f) &&
       check1("cellnoise2d-next-cell", eval_named(&ctx, "cell2next"),
              0.97482122f) &&
       check1("cellnoise3d", eval_named(&ctx, "cell3"), 0.72194636f) && ok;
  ok = check1("randomfloat", eval_named(&ctx, "random"), 2.31866973f) && ok;
  ok = check3("randomcolor", eval_named(&ctx, "random_color"), 0.72096912f,
              0.68491890f, 0.32840088f) && ok;
  ok = check1("fractal2d-scalar", eval_named(&ctx, "fractal_scalar"),
              0.27252933f) &&
       check3("fractal2d-color", eval_named(&ctx, "fractal_color"),
              0.18168622f, 1.98365822f, -0.40455556f) && ok;
  ok = check3("worley2d-distance", eval_named(&ctx, "worley2_distance"),
              0.604036f, 0.778409f, 0.907265f) && ok;
  ok = check3("worley2d-solid", eval_named(&ctx, "worley2_solid"),
              0.623529f, 0.580392f, 0.211765f) && ok;
  { MtlxValue w = eval_named(&ctx, "worley3_distance");
    ok = check1("worley3d-distance-f1", w, 0.724764287f) && ok;
    if (!nearf_eps(w.v[1], 0.825379968f))
      fprintf(stderr, "worley3d-distance-f2: got %.9f, expected %.9f\n",
              w.v[1], 0.825379968f);
    ok = nearf_eps(w.v[1], 0.825379968f) && ok;
  }
  ok = check3("noise2d-color", eval_named(&ctx, "noise2_color"),
              0.377306f, 1.246215f, 0.188793f) && ok;
  ok = check3("noise3d-color", eval_named(&ctx, "noise3_color"),
              0.338275f, -0.837646f, 0.669710f) && ok;
  ok = check1("unified-perlin", eval_named(&ctx, "unified_perlin"),
              -0.390847325f) && ok;
  ok = check1("unified-cell", eval_named(&ctx, "unified_cell"),
              0.860312760f) && ok;
  ok = check1("unified-worley", eval_named(&ctx, "unified_worley"),
              0.623227179f) && ok;
  ok = check1("unified-fractal", eval_named(&ctx, "unified_fractal"),
              0.475442290f) && ok;
  ok = check1("unified3-fractal", eval_named(&ctx, "unified3_fractal"),
              0.079035468f) && ok;
  ok = check1("cloverleaf-inside", eval_named(&ctx, "clover_inside"), 1.0f) && ok;
  ok = check1("cloverleaf-outside", eval_named(&ctx, "clover_outside"), 0.0f) && ok;
  ok = check1("hexagon-inside", eval_named(&ctx, "hex_inside"), 1.0f) && ok;
  ok = check1("hexagon-outside", eval_named(&ctx, "hex_outside"), 0.0f) && ok;
  /* mtlx_eval_node_test must clear memoization between shade points. */
  ctx.uv[0] = 0.8f;
  ctx.uv[1] = 0.9f;
  ok = check3("ramp-right", eval_named(&ctx, "ramp"), 0.2f, 0.0f, 0.8f) &&
       check3("split-right", eval_named(&ctx, "split"), 1.0f, 0.0f, 1.0f) && ok;
  ok = check3("split-tb-bottom", eval_named(&ctx, "split_tb"), 0.0f, 0.0f,
              1.0f) && ok;
  free(ctx.memo); free(ctx.memo_done); mtlx_free(doc);
  if (!ok) fprintf(stderr, "MaterialX graph evaluation suite failed\n");
  return ok ? 0 : 1;
}
