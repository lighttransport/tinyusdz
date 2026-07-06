#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-tydra-renderscene.h"

#include <cstring>

#include "tinyusdz.hh"
#include "tydra/render-data.hh"
#include "tydra/render-data-converter.hh"
#include "tydra/scene-access.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "usdLux.hh"
#include "stage.hh"

using namespace tinyusdz;

namespace {

bool SyntheticTextureLoader(
    const value::AssetPath &assetPath, const AssetInfo &assetInfo,
    const AssetResolutionResolver &assetResolver, tydra::TextureImage *imageOut,
    std::vector<uint8_t> *imageData, void *userdata, std::string *warn,
    std::string *err) {
  (void)assetInfo;
  (void)assetResolver;
  (void)userdata;
  (void)warn;

  if (!imageOut || !imageData) {
    if (err) {
      *err = "output buffer is null";
    }
    return false;
  }

  imageOut->asset_identifier = assetPath.GetAssetPath();
  imageOut->width = 1;
  imageOut->height = 1;
  imageOut->channels = 4;
  imageOut->assetTexelComponentType = tydra::ComponentType::UInt8;
  imageData->assign({255, 128, 64, 255});
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// a. Empty stage -> RenderScene should succeed with zero content
// ---------------------------------------------------------------------------
void tydra_renderscene_empty_stage_test(void) {
  Stage stage;
  stage.commit();

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  bool ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene should succeed on empty stage");

  TEST_CHECK(scene.meshes.size() == 0);
  TEST_MSG("Empty stage should produce 0 meshes");

  TEST_CHECK(scene.materials.size() == 0);
  TEST_MSG("Empty stage should produce 0 materials");

  TEST_CHECK(scene.lights.size() == 0);
  TEST_MSG("Empty stage should produce 0 lights");

  TEST_CHECK(scene.cameras.size() == 0);
  TEST_MSG("Empty stage should produce 0 cameras");
}

// ---------------------------------------------------------------------------
// b. Single triangle mesh -> parse, convert, verify mesh exists with vertices
// ---------------------------------------------------------------------------
void tydra_renderscene_single_mesh_test(void) {
  const char *usda = R"(#usda 1.0
def Xform "Root" {
  def Mesh "Tri" {
    point3f[] points = [(0,0,0),(1,0,0),(0,1,0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0,1,2]
  }
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda),
      "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());

  TEST_CHECK(scene.meshes.size() >= 1);
  TEST_MSG("Expected at least 1 mesh, got %zu", scene.meshes.size());

  TEST_CHECK(scene.meshes[0].points.size() > 0);
  TEST_MSG("Mesh should have vertices");
}

// ---------------------------------------------------------------------------
// c. Xform containing Mesh child -> verify nodes and meshes
// ---------------------------------------------------------------------------
void tydra_renderscene_xform_hierarchy_test(void) {
  const char *usda = R"(#usda 1.0
def Xform "Root" {
  def Xform "Group" {
    def Mesh "ChildMesh" {
      point3f[] points = [(0,0,0),(1,0,0),(0,1,0)]
      int[] faceVertexCounts = [3]
      int[] faceVertexIndices = [0,1,2]
    }
  }
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda),
      "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());

  TEST_CHECK(scene.nodes.size() >= 1);
  TEST_MSG("Expected at least 1 node, got %zu", scene.nodes.size());

  TEST_CHECK(scene.meshes.size() >= 1);
  TEST_MSG("Expected at least 1 mesh, got %zu", scene.meshes.size());
}

// ---------------------------------------------------------------------------
// d. Mesh bound to Material with UsdPreviewSurface -> verify material
// ---------------------------------------------------------------------------
void tydra_renderscene_material_binding_test(void) {
  const char *usda = R"(#usda 1.0
def Xform "Root" {
  def Mesh "MyMesh" (
    prepend apiSchemas = ["MaterialBindingAPI"]
  ) {
    point3f[] points = [(0,0,0),(1,0,0),(0,1,0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0,1,2]
    rel material:binding = </Root/Materials/MyMat>
  }
  def Scope "Materials" {
    def Material "MyMat" {
      token outputs:surface.connect = </Root/Materials/MyMat/PreviewSurface.outputs:surface>
      def Shader "PreviewSurface" {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor = (0.8, 0.2, 0.1)
        float inputs:roughness = 0.4
        float inputs:metallic = 0.0
        token outputs:surface
      }
    }
  }
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda),
      "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());

  TEST_CHECK(scene.materials.size() >= 1);
  TEST_MSG("Expected at least 1 material, got %zu", scene.materials.size());
}

// ---------------------------------------------------------------------------
// d2. MaterialX NodeGraph constant folding of the newly-supported ops:
//     swizzle, separate (per-channel output), smoothstep, ifgreatereq, saturate.
//     A UsdPreviewSurface's inputs connect to NodeGraph outputs that fold to
//     constants; the converter must evaluate them into the surface shader.
// ---------------------------------------------------------------------------
void tydra_renderscene_mtlx_nodegraph_ops_test(void) {
  const char *usda = R"(#usda 1.0
def Xform "Root" {
  def Mesh "MyMesh" (
    prepend apiSchemas = ["MaterialBindingAPI"]
  ) {
    point3f[] points = [(0,0,0),(1,0,0),(0,1,0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0,1,2]
    rel material:binding = </Root/Materials/MyMat>
  }
  def Scope "Materials" {
    def Material "MyMat" {
      token outputs:surface.connect = </Root/Materials/MyMat/PreviewSurface.outputs:surface>
      def Shader "PreviewSurface" {
        uniform token info:id = "ND_UsdPreviewSurface_surfaceshader"
        color3f inputs:diffuseColor.connect = </Root/Materials/MyMat/NG.outputs:diff_out>
        color3f inputs:emissiveColor.connect = </Root/Materials/MyMat/NG.outputs:emis_out>
        float inputs:roughness.connect = </Root/Materials/MyMat/NG.outputs:rough_out>
        float inputs:metallic.connect = </Root/Materials/MyMat/NG.outputs:metal_out>
        float inputs:clearcoat.connect = </Root/Materials/MyMat/NG.outputs:coat_out>
        float inputs:opacity.connect = </Root/Materials/MyMat/NG.outputs:opacity_out>
        token outputs:surface
      }
      def NodeGraph "NG" {
        color3f outputs:diff_out.connect = </Root/Materials/MyMat/NG/swizzleNode.outputs:out>
        color3f outputs:emis_out.connect = </Root/Materials/MyMat/NG/satNode.outputs:out>
        float outputs:rough_out.connect = </Root/Materials/MyMat/NG/sepNode.outputs:outg>
        float outputs:metal_out.connect = </Root/Materials/MyMat/NG/smoothNode.outputs:out>
        float outputs:coat_out.connect = </Root/Materials/MyMat/NG/condNode.outputs:out>
        float outputs:opacity_out.connect = </Root/Materials/MyMat/NG/sep4Node.outputs:outa>

        def Shader "constNode" {
          uniform token info:id = "ND_constant_color3"
          color3f inputs:value = (0.1, 0.6, 0.9)
          color3f outputs:out
        }
        def Shader "swizzleNode" {
          uniform token info:id = "ND_swizzle_color3_color3"
          color3f inputs:in.connect = </Root/Materials/MyMat/NG/constNode.outputs:out>
          string inputs:channels = "bgr"
          color3f outputs:out
        }
        def Shader "sepNode" {
          uniform token info:id = "ND_separate3_color3"
          color3f inputs:in.connect = </Root/Materials/MyMat/NG/constNode.outputs:out>
          float outputs:outr
          float outputs:outg
          float outputs:outb
        }
        def Shader "smoothNode" {
          uniform token info:id = "ND_smoothstep_float"
          float inputs:in = 0.8
          float inputs:low = 0.0
          float inputs:high = 1.0
          float outputs:out
        }
        def Shader "condNode" {
          uniform token info:id = "ND_ifgreatereq_float"
          float inputs:value1 = 0.5
          float inputs:value2 = 0.5
          float inputs:in1 = 0.7
          float inputs:in2 = 0.2
          float outputs:out
        }
        def Shader "satNode" {
          uniform token info:id = "ND_saturate_color3"
          color3f inputs:in.connect = </Root/Materials/MyMat/NG/constNode.outputs:out>
          float inputs:amount = 0.0
          color3f outputs:out
        }
        def Shader "combine4Node" {
          uniform token info:id = "ND_combine4_color4"
          float inputs:in1 = 0.2
          float inputs:in2 = 0.4
          float inputs:in3 = 0.6
          float inputs:in4 = 0.8
          color4f outputs:out
        }
        def Shader "sep4Node" {
          uniform token info:id = "ND_separate4_color4"
          color4f inputs:in.connect = </Root/Materials/MyMat/NG/combine4Node.outputs:out>
          float outputs:outr
          float outputs:outg
          float outputs:outb
          float outputs:outa
        }
      }
    }
  }
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda),
      "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());

  TEST_CHECK(scene.materials.size() >= 1);
  if (scene.materials.empty()) return;
  const tydra::RenderMaterial &mat = scene.materials[0];
  TEST_CHECK(mat.surfaceShader.has_value());
  if (!mat.surfaceShader) return;
  const tydra::PreviewSurfaceShader &s = *mat.surfaceShader;

  auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };

  // swizzle "bgr" of (0.1,0.6,0.9) -> (0.9,0.6,0.1)
  TEST_CHECK(near(s.diffuseColor.value[0], 0.9f));
  TEST_CHECK(near(s.diffuseColor.value[1], 0.6f));
  TEST_CHECK(near(s.diffuseColor.value[2], 0.1f));
  TEST_MSG("swizzle diffuseColor = (%f,%f,%f)", s.diffuseColor.value[0],
           s.diffuseColor.value[1], s.diffuseColor.value[2]);

  // separate3(.outg) of (0.1,0.6,0.9) -> 0.6
  TEST_CHECK(near(s.roughness.value, 0.6f));
  TEST_MSG("separate outg roughness = %f", s.roughness.value);

  // smoothstep(0.8, 0, 1) = 0.8^2*(3-2*0.8) = 0.896
  TEST_CHECK(near(s.metallic.value, 0.896f));
  TEST_MSG("smoothstep metallic = %f", s.metallic.value);

  // ifgreatereq(0.5 >= 0.5) ? 0.7 : 0.2 -> 0.7
  TEST_CHECK(near(s.clearcoat.value, 0.7f));
  TEST_MSG("ifgreatereq clearcoat = %f", s.clearcoat.value);

  // combine4 -> separate4(.outa) = 0.8
  TEST_CHECK(near(s.opacity.value, 0.8f));
  TEST_MSG("combine4/separate4 opacity = %f", s.opacity.value);

  // saturate(amount=0) collapses to luminance:
  // 0.2126*0.1 + 0.7152*0.6 + 0.0722*0.9 = 0.51536
  TEST_CHECK(near(s.emissiveColor.value[0], 0.51536f));
  TEST_CHECK(near(s.emissiveColor.value[1], 0.51536f));
  TEST_CHECK(near(s.emissiveColor.value[2], 0.51536f));
  TEST_MSG("saturate emissiveColor = (%f,%f,%f)", s.emissiveColor.value[0],
           s.emissiveColor.value[1], s.emissiveColor.value[2]);
}

// ---------------------------------------------------------------------------
// d3. MaterialX NodeGraph constant folding when the graph is shared outside the
//     Material prim and forwards through a nested NodeGraph. Production assets
//     commonly keep reusable NodeGraphs next to a material library instead of
//     as children of a single Material.
// ---------------------------------------------------------------------------
void tydra_renderscene_mtlx_nonlocal_nodegraph_test(void) {
  const char *usda = R"(#usda 1.0
def Xform "Root" {
  def Mesh "MyMesh" (
    prepend apiSchemas = ["MaterialBindingAPI"]
  ) {
    point3f[] points = [(0,0,0),(1,0,0),(0,1,0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0,1,2]
    rel material:binding = </Root/Materials/MyMat>
  }
  def Scope "Materials" {
    def Material "MyMat" {
      token outputs:surface.connect = </Root/Materials/MyMat/PreviewSurface.outputs:surface>
      def Shader "PreviewSurface" {
        uniform token info:id = "ND_UsdPreviewSurface_surfaceshader"
        color3f inputs:diffuseColor.connect = </Root/SharedNG.outputs:diff_out>
        float inputs:roughness.connect = </Root/SharedNG.outputs:rough_out>
        token outputs:surface
      }
    }
  }
  def NodeGraph "SharedNG" {
    color3f outputs:diff_out.connect = </Root/SharedNG/Nested.outputs:diff_out>
    float outputs:rough_out.connect = </Root/SharedNG/Nested.outputs:rough_out>
    def NodeGraph "Nested" {
      color3f outputs:diff_out.connect = </Root/SharedNG/Nested/colorNode.outputs:out>
      float outputs:rough_out.connect = </Root/SharedNG/Nested/roughNode.outputs:out>
      def Shader "colorNode" {
        uniform token info:id = "ND_constant_color3"
        color3f inputs:value = (0.25, 0.5, 0.75)
        color3f outputs:out
      }
      def Shader "roughNode" {
        uniform token info:id = "ND_constant_float"
        float inputs:value = 0.35
        float outputs:out
      }
    }
  }
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda),
      "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());

  TEST_CHECK(scene.materials.size() >= 1);
  if (scene.materials.empty()) return;
  const tydra::RenderMaterial &mat = scene.materials[0];
  TEST_CHECK(mat.surfaceShader.has_value());
  if (!mat.surfaceShader) return;
  const tydra::PreviewSurfaceShader &s = *mat.surfaceShader;

  auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
  TEST_CHECK(near(s.diffuseColor.value[0], 0.25f));
  TEST_CHECK(near(s.diffuseColor.value[1], 0.5f));
  TEST_CHECK(near(s.diffuseColor.value[2], 0.75f));
  TEST_CHECK(near(s.roughness.value, 0.35f));
  TEST_MSG("nonlocal NG diffuse=(%f,%f,%f) roughness=%f",
           s.diffuseColor.value[0], s.diffuseColor.value[1],
           s.diffuseColor.value[2], s.roughness.value);
}

// ---------------------------------------------------------------------------
// d4. MaterialX interface inputs. Flattened MaterialX commonly connects
//     surface-shader inputs to constants authored on the enclosing Material
//     interface instead of to texture or compute nodes.
// ---------------------------------------------------------------------------
void tydra_renderscene_mtlx_interface_inputs_test(void) {
  const char *usda = R"(#usda 1.0
def Xform "Root" {
  def Mesh "MyMesh" (
    prepend apiSchemas = ["MaterialBindingAPI"]
  ) {
    point3f[] points = [(0,0,0),(1,0,0),(0,1,0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0,1,2]
    rel material:binding = </Root/Materials/MyMat>
  }
  def Scope "Materials" {
    def Material "MyMat" {
      color3f inputs:ifaceColor = (0.25, 0.5, 0.75)
      float inputs:ifaceScalar = 0.4
      token outputs:surface.connect = </Root/Materials/MyMat/PreviewSurface.outputs:surface>
      def Shader "PreviewSurface" {
        uniform token info:id = "ND_UsdPreviewSurface_surfaceshader"
        color3f inputs:diffuseColor.connect = </Root/Materials/MyMat.inputs:ifaceColor>
        color3f inputs:emissiveColor.connect = </Root/Materials/MyMat.inputs:ifaceScalar>
        float inputs:roughness.connect = </Root/Materials/MyMat.inputs:ifaceColor>
        float inputs:metallic.connect = </Root/Materials/MyMat.inputs:ifaceScalar>
        token outputs:surface
      }
    }
  }
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda),
      "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());
  TEST_CHECK(converter.GetWarning().find("MaterialX connection") ==
             std::string::npos);
  TEST_MSG("Unexpected MaterialX interface warning: %s",
           converter.GetWarning().c_str());

  TEST_CHECK(scene.materials.size() >= 1);
  if (scene.materials.empty()) return;
  const tydra::RenderMaterial &mat = scene.materials[0];
  TEST_CHECK(mat.surfaceShader.has_value());
  if (!mat.surfaceShader) return;
  const tydra::PreviewSurfaceShader &s = *mat.surfaceShader;

  auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
  TEST_CHECK(near(s.diffuseColor.value[0], 0.25f));
  TEST_CHECK(near(s.diffuseColor.value[1], 0.5f));
  TEST_CHECK(near(s.diffuseColor.value[2], 0.75f));
  TEST_CHECK(near(s.emissiveColor.value[0], 0.4f));
  TEST_CHECK(near(s.emissiveColor.value[1], 0.4f));
  TEST_CHECK(near(s.emissiveColor.value[2], 0.4f));
  TEST_CHECK(near(s.roughness.value, 0.25f));
  TEST_CHECK(near(s.metallic.value, 0.4f));
}

// ---------------------------------------------------------------------------
// d5. MaterialX geompropvalue texture coordinates. ND_image texcoord chains
//     that pass through ND_geompropvalue must preserve the authored primvar
//     name on the renderer-facing UVTexture.
// ---------------------------------------------------------------------------
void tydra_renderscene_mtlx_geomprop_texture_test(void) {
  const char *usda = R"(#usda 1.0
def Xform "Root" {
  def Mesh "MyMesh" (
    prepend apiSchemas = ["MaterialBindingAPI"]
  ) {
    point3f[] points = [(0,0,0),(1,0,0),(0,1,0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0,1,2]
    texCoord2f[] primvars:uv_custom = [(0,0),(1,0),(0,1)] (
      interpolation = "vertex"
    )
    rel material:binding = </Root/Materials/MyMat>
  }
  def Scope "Materials" {
    def Material "MyMat" {
      token outputs:surface.connect = </Root/Materials/MyMat/PreviewSurface.outputs:surface>
      def Shader "PreviewSurface" {
        uniform token info:id = "ND_UsdPreviewSurface_surfaceshader"
        color3f inputs:diffuseColor.connect = </Root/Materials/MyMat/NG.outputs:diff_out>
        token outputs:surface
      }
      def NodeGraph "NG" {
        color3f outputs:diff_out.connect = </Root/Materials/MyMat/NG/Image.outputs:out>
        def Shader "Image" {
          uniform token info:id = "ND_image_color3"
          asset inputs:file = @synthetic.png@
          float2 inputs:texcoord.connect = </Root/Materials/MyMat/NG/GeomProp.outputs:out>
          color3f outputs:out
        }
        def Shader "GeomProp" {
          uniform token info:id = "ND_geompropvalue_vector2"
          string inputs:geomprop = "uv_custom"
          float2 outputs:out
        }
      }
    }
  }
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda),
      "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  tydra::RenderSceneConverterEnv env(stage);
  env.material_config.texture_image_loader_function = SyntheticTextureLoader;
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());

  TEST_CHECK(scene.textures.size() >= 1);
  TEST_MSG("Expected at least 1 texture, got %zu", scene.textures.size());
  if (scene.textures.empty()) return;
  TEST_CHECK(scene.textures[0].varname_uv == "uv_custom");
  TEST_MSG("MaterialX geomprop varname_uv = '%s'",
           scene.textures[0].varname_uv.c_str());
  TEST_CHECK(scene.images.size() >= 1);
  if (!scene.images.empty()) {
    TEST_CHECK(scene.images[0].asset_identifier == "synthetic.png");
  }
}

// ---------------------------------------------------------------------------
// d6. MaterialX texture color-space semantics. Color parameters should synthesize
//     sRGB sourceColorSpace, while scalar/data parameters should synthesize Raw.
// ---------------------------------------------------------------------------
void tydra_renderscene_mtlx_texture_colorspace_test(void) {
  const char *usda = R"(#usda 1.0
def Xform "Root" {
  def Mesh "MyMesh" (
    prepend apiSchemas = ["MaterialBindingAPI"]
  ) {
    point3f[] points = [(0,0,0),(1,0,0),(0,1,0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0,1,2]
    rel material:binding = </Root/Materials/MyMat>
  }
  def Scope "Materials" {
    def Material "MyMat" {
      token outputs:surface.connect = </Root/Materials/MyMat/PreviewSurface.outputs:surface>
      def Shader "PreviewSurface" {
        uniform token info:id = "ND_UsdPreviewSurface_surfaceshader"
        color3f inputs:diffuseColor.connect = </Root/Materials/MyMat/NG.outputs:diff_out>
        float inputs:roughness.connect = </Root/Materials/MyMat/NG.outputs:rough_out>
        token outputs:surface
      }
      def NodeGraph "NG" {
        color3f outputs:diff_out.connect = </Root/Materials/MyMat/NG/ColorImage.outputs:out>
        float outputs:rough_out.connect = </Root/Materials/MyMat/NG/RoughImage.outputs:out>
        def Shader "ColorImage" {
          uniform token info:id = "ND_image_color3"
          asset inputs:file = @color.png@
          color3f outputs:out
        }
        def Shader "RoughImage" {
          uniform token info:id = "ND_image_float"
          asset inputs:file = @roughness.png@
          float outputs:out
        }
      }
    }
  }
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda),
      "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  tydra::RenderSceneConverterEnv env(stage);
  env.material_config.texture_image_loader_function = SyntheticTextureLoader;
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());

  TEST_CHECK(scene.materials.size() >= 1);
  if (scene.materials.empty()) return;
  const tydra::RenderMaterial &mat = scene.materials[0];
  TEST_CHECK(mat.surfaceShader.has_value());
  if (!mat.surfaceShader) return;
  const tydra::PreviewSurfaceShader &s = *mat.surfaceShader;
  TEST_CHECK(s.diffuseColor.texture_id >= 0);
  TEST_CHECK(s.roughness.texture_id >= 0);
  if (s.diffuseColor.texture_id < 0 || s.roughness.texture_id < 0) return;

  const auto textureImageColorSpace = [&](int32_t texture_id) {
    const tydra::UVTexture &tex = scene.textures[static_cast<size_t>(texture_id)];
    return scene.images[static_cast<size_t>(tex.texture_image_id)].usdColorSpace;
  };

  TEST_CHECK(textureImageColorSpace(s.diffuseColor.texture_id) ==
             tydra::ColorSpace::sRGB);
  TEST_CHECK(textureImageColorSpace(s.roughness.texture_id) ==
             tydra::ColorSpace::Raw);
}

// ---------------------------------------------------------------------------
// e. SphereLight -> verify light in converted scene
// ---------------------------------------------------------------------------
void tydra_renderscene_sphere_light_test(void) {
  const char *usda = R"(#usda 1.0
def SphereLight "MyLight" {
  float inputs:intensity = 100
  color3f inputs:color = (1, 1, 1)
  float inputs:radius = 0.5
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda),
      "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());

  TEST_CHECK(scene.lights.size() >= 1);
  TEST_MSG("Expected at least 1 light, got %zu", scene.lights.size());
}

// ---------------------------------------------------------------------------
// f. Camera -> verify camera in converted scene
// ---------------------------------------------------------------------------
void tydra_renderscene_camera_test(void) {
  const char *usda = R"(#usda 1.0
def Camera "MainCam" {
  float focalLength = 50
  float horizontalAperture = 36
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda),
      "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());

  TEST_CHECK(scene.cameras.size() >= 1);
  TEST_MSG("Expected at least 1 camera, got %zu", scene.cameras.size());
}

// ---------------------------------------------------------------------------
// g. Memory estimation -> convert scene, check estimate_memory_usage() > 0
// ---------------------------------------------------------------------------
void tydra_renderscene_memory_estimation_test(void) {
  const char *usda = R"(#usda 1.0
def Xform "Root" {
  def Mesh "Quad" {
    point3f[] points = [(0,0,0),(1,0,0),(1,1,0),(0,1,0)]
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
  }
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda),
      "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());

  size_t mem = scene.estimate_memory_usage();
  TEST_CHECK(mem > 0);
  TEST_MSG("estimate_memory_usage() should be > 0, got %zu", mem);

  // Also verify that at least the arrays are non-empty as a sanity check
  bool has_content = !scene.nodes.empty() || !scene.meshes.empty();
  TEST_CHECK(has_content);
  TEST_MSG("Converted scene should have non-empty arrays");
}

// ---------------------------------------------------------------------------
// Shared fixture for the streaming tests: two meshes (one with a bound
// material), a camera and a light under an Xform.
// ---------------------------------------------------------------------------
static const char *kStreamingUSDA = R"(#usda 1.0
(
  upAxis = "Y"
)
def Xform "Root" {
  def Mesh "MeshA" (
    prepend apiSchemas = ["MaterialBindingAPI"]
  ) {
    point3f[] points = [(0,0,0),(1,0,0),(0,1,0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0,1,2]
    rel material:binding = </Root/Materials/MatA>
  }
  def Mesh "MeshB" {
    point3f[] points = [(0,0,0),(1,0,0),(1,1,0),(0,1,0)]
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
  }
  def Scope "Materials" {
    def Material "MatA" {
      token outputs:surface.connect = </Root/Materials/MatA/PreviewSurface.outputs:surface>
      def Shader "PreviewSurface" {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor = (0.8, 0.2, 0.1)
        token outputs:surface
      }
    }
  }
  def Camera "Cam" {
    float focalLength = 50
  }
  def SphereLight "Light" {
    float inputs:intensity = 100
  }
}
)";

// ---------------------------------------------------------------------------
// h. Streaming conversion equivalence: ConvertToRenderSceneStreaming must
//    deliver every element via the sink (counts == final array sizes), in the
//    documented order (material before the mesh that binds it; no node before
//    the last mesh), fire on_complete exactly once, and produce a RenderScene
//    equal to the monolithic ConvertToRenderScene.
// ---------------------------------------------------------------------------
void tydra_renderscene_streaming_equivalence_test(void) {
  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(kStreamingUSDA),
      std::strlen(kStreamingUSDA), "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  // Monolithic reference.
  tydra::RenderScene sceneA;
  {
    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderSceneConverter converter;
    ok = converter.ConvertToRenderScene(env, &sceneA);
    TEST_CHECK(ok);
    TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());
  }

  // Streaming.
  struct StreamLog {
    size_t images = 0, buffers = 0, textures = 0, materials = 0, meshes = 0;
    size_t lights = 0, cameras = 0, nodes = 0;
    size_t skeletons = 0, animations = 0, instances = 0;
    size_t completes = 0;
    bool material_before_mesh_ok = true;  // each mesh.material_id < materials seen
    bool node_seen = false;
    bool mesh_after_node = false;  // a mesh arrived after a node (ordering bug)
  } log;

  tydra::RenderSceneSink sink;
  sink.on_image = [&](const tydra::TextureImage &, size_t, void *) {
    log.images++; return true; };
  sink.on_buffer = [&](const tydra::BufferData &, size_t, void *) {
    log.buffers++; return true; };
  sink.on_texture = [&](const tydra::UVTexture &, size_t, const std::string &, void *) {
    log.textures++; return true; };
  sink.on_material = [&](const tydra::RenderMaterial &, size_t, const std::string &, void *) {
    log.materials++; return true; };
  sink.on_mesh = [&](const tydra::RenderMesh &m, size_t, const std::string &, void *) {
    if (m.material_id >= 0 && size_t(m.material_id) >= log.materials) {
      log.material_before_mesh_ok = false;
    }
    if (log.node_seen) log.mesh_after_node = true;
    log.meshes++;
    return true;
  };
  sink.on_light = [&](const tydra::RenderLight &, size_t, const std::string &, void *) {
    log.lights++; return true; };
  sink.on_camera = [&](const tydra::RenderCamera &, size_t, const std::string &, void *) {
    log.cameras++; return true; };
  sink.on_root_node = [&](const tydra::Node &, size_t, void *) {
    log.node_seen = true; log.nodes++; return true; };
  sink.on_skeleton = [&](const tydra::SkelHierarchy &, size_t, const std::string &, void *) {
    log.skeletons++; return true; };
  sink.on_animation = [&](const tydra::AnimationClip &, size_t, const std::string &, void *) {
    log.animations++; return true; };
  sink.on_instance = [&](const tydra::RenderInstance &, size_t, const std::string &, void *) {
    log.instances++; return true; };
  sink.on_complete = [&](const tydra::RenderScene &, void *) {
    log.completes++; return true; };

  tydra::RenderScene sceneB;
  {
    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderSceneConverter converter;
    ok = converter.ConvertToRenderSceneStreaming(env, sink, &sceneB);
    TEST_CHECK(ok);
    TEST_MSG("ConvertToRenderSceneStreaming failed: %s",
             converter.GetError().c_str());
  }

  // Streamed counts match the streamed scene's arrays.
  TEST_CHECK(log.meshes == sceneB.meshes.size());
  TEST_MSG("on_mesh count %zu != meshes %zu", log.meshes, sceneB.meshes.size());
  TEST_CHECK(log.materials == sceneB.materials.size());
  TEST_MSG("on_material count %zu != materials %zu", log.materials,
           sceneB.materials.size());
  TEST_CHECK(log.textures == sceneB.textures.size());
  TEST_CHECK(log.images == sceneB.images.size());
  TEST_CHECK(log.buffers == sceneB.buffers.size());
  TEST_CHECK(log.lights == sceneB.lights.size());
  TEST_CHECK(log.cameras == sceneB.cameras.size());
  TEST_CHECK(log.nodes == sceneB.nodes.size());
  TEST_CHECK(log.skeletons == sceneB.skeletons.size());
  TEST_CHECK(log.animations == sceneB.animations.size());
  TEST_CHECK(log.instances == sceneB.instances.size());

  // Ordering guarantees.
  TEST_CHECK(log.material_before_mesh_ok);
  TEST_MSG("a mesh referenced a material that was not emitted before it");
  TEST_CHECK(!log.mesh_after_node);
  TEST_MSG("a mesh was streamed after a node (meshes must precede the hierarchy)");

  // on_complete fires exactly once.
  TEST_CHECK(log.completes == 1);
  TEST_MSG("on_complete fired %zu times (expected 1)", log.completes);

  // Streaming result equals the monolithic result.
  TEST_CHECK(sceneA.meshes.size() == sceneB.meshes.size());
  TEST_CHECK(sceneA.materials.size() == sceneB.materials.size());
  TEST_CHECK(sceneA.nodes.size() == sceneB.nodes.size());
  TEST_CHECK(sceneA.lights.size() == sceneB.lights.size());
  TEST_CHECK(sceneA.cameras.size() == sceneB.cameras.size());
  bool per_mesh_ok = true;
  for (size_t i = 0; i < sceneA.meshes.size() && i < sceneB.meshes.size(); i++) {
    if (sceneA.meshes[i].points.size() != sceneB.meshes[i].points.size() ||
        sceneA.meshes[i].material_id != sceneB.meshes[i].material_id) {
      per_mesh_ok = false;
    }
  }
  TEST_CHECK(per_mesh_ok);
  TEST_MSG("streamed meshes differ from monolithic meshes");

  // Sanity: the fixture really exercises meshes + a material.
  TEST_CHECK(sceneB.meshes.size() == 2);
  TEST_CHECK(sceneB.materials.size() >= 1);
}

// ---------------------------------------------------------------------------
// i. Streaming cancellation: a sink that returns false from on_mesh aborts the
//    conversion; the consumer keeps exactly the meshes delivered so far.
// ---------------------------------------------------------------------------
void tydra_renderscene_streaming_cancel_test(void) {
  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(kStreamingUSDA),
      std::strlen(kStreamingUSDA), "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  size_t meshes_seen = 0;
  tydra::RenderSceneSink sink;
  sink.on_mesh = [&](const tydra::RenderMesh &, size_t, const std::string &, void *) {
    meshes_seen++;
    return false;  // cancel on the very first mesh
  };

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;
  ok = converter.ConvertToRenderSceneStreaming(env, sink, &scene);

  TEST_CHECK(!ok);
  TEST_MSG("streaming conversion should report failure when cancelled");
  TEST_CHECK(meshes_seen == 1);
  TEST_MSG("expected exactly 1 mesh delivered before cancel, got %zu",
           meshes_seen);
}
