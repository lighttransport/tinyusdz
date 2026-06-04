// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
// Unit tests for MaterialX support in TinyUSDZ

#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

#if defined(_WIN32)
#include <direct.h>
#define TUSDZ_TEST_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define TUSDZ_TEST_MKDIR(p) mkdir((p), 0755)
#endif

#include "unit-materialx.h"
#include "prim-reconstruct.hh"
#include "usda-reader.hh"
#include "usdShade.hh"
#include "layer.hh"
#include "composition.hh"
#include "usdMtlx.hh"
#include "asset-resolution.hh"
#include "value-types.hh"
#include "tinyusdz.hh"
#include "math-util.inc"

using namespace tinyusdz;

namespace {

// Helpers for the <include> path-traversal regression test.
bool MtlxTestWriteFile(const std::string &path, const std::string &content) {
  std::ofstream ofs(path, std::ios::binary);
  if (!ofs) {
    return false;
  }
  ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
  return ofs.good();
}

bool MtlxTestContains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

// Test MaterialXConfigAPI structure extension
void materialx_config_api_struct_test(void) {
  MaterialXConfigAPI config;

  // Check default values by calling get_value on unset attributes
  // When not authored, get_value() returns the fallback value
  TEST_CHECK(!config.mtlx_version.authored());
  TEST_CHECK(config.mtlx_version.get_value() == "1.38");

  TEST_CHECK(!config.mtlx_namespace.authored());
  TEST_CHECK(config.mtlx_namespace.get_value() == "");

  TEST_CHECK(!config.mtlx_colorspace.authored());
  TEST_CHECK(config.mtlx_colorspace.get_value() == "lin_rec709");

  TEST_CHECK(!config.mtlx_sourceUri.authored());
  TEST_CHECK(config.mtlx_sourceUri.get_value() == "");
}

// Test MaterialXConfigAPI parsing from USD
void materialx_config_api_parsing_test(void) {
  std::string usda = R"(#usda 1.0

def Material "TestMaterial" (
    prepend apiSchemas = ["MaterialXConfigAPI"]
)
{
    uniform string config:mtlx:version = "1.39"
    uniform string config:mtlx:namespace = "test_namespace"
    uniform string config:mtlx:colorspace = "acescg"
    uniform string config:mtlx:sourceUri = "test.mtlx"

    token outputs:surface.connect = </TestMaterial/TestShader.outputs:surface>
}
)";

  Stage stage;
  std::string warn, err;

  bool ret = LoadUSDAFromMemory((const uint8_t*)usda.c_str(), usda.length(), "", &stage, &warn, &err);
  TEST_CHECK(ret == true);

  if (ret) {
    // Find the Material prim
    const Prim *material_prim = nullptr;
    ret = stage.find_prim_at_path(Path("/TestMaterial", ""), material_prim, &err);
    TEST_CHECK(ret == true);

    if (ret && material_prim) {
      // Regression: MaterialXConfigAPI must be recognized as a known (built-in)
      // API schema, not preserved as an "unknown API schema". Recognition is
      // separate from the typed reconstruct below.
      TEST_CHECK(material_prim->metas().has_apiSchemas());
      if (material_prim->metas().has_apiSchemas()) {
        const auto &schemas = material_prim->metas().get_apiSchemas();
        bool known = false;
        for (const auto &n : schemas.names) {
          if (n.first == APISchemas::APIName::MaterialXConfigAPI) {
            known = true;
          }
        }
        TEST_CHECK(known);
        TEST_CHECK(schemas.unknownSchemas.empty());
      }

      const Material *mat = material_prim->data().as<Material>();
      TEST_CHECK(mat != nullptr);

      if (mat) {
        // Check MaterialXConfigAPI was parsed
        TEST_CHECK(mat->materialXConfig.has_value() == true);

        if (mat->materialXConfig.has_value()) {
          // Check values - need to access the actual value, not fallback
          const auto& config = mat->materialXConfig.value();

          // Check if values were authored (parsed from USD)
          TEST_CHECK(config.mtlx_version.authored() == true);
          TEST_CHECK(config.mtlx_namespace.authored() == true);
          TEST_CHECK(config.mtlx_colorspace.authored() == true);
          TEST_CHECK(config.mtlx_sourceUri.authored() == true);

          // Check actual values
          if (config.mtlx_version.authored()) {
            TEST_CHECK(config.mtlx_version.get_value() == "1.39");
          }
          if (config.mtlx_namespace.authored()) {
            TEST_CHECK(config.mtlx_namespace.get_value() == "test_namespace");
          }
          if (config.mtlx_colorspace.authored()) {
            TEST_CHECK(config.mtlx_colorspace.get_value() == "acescg");
          }
          if (config.mtlx_sourceUri.authored()) {
            TEST_CHECK(config.mtlx_sourceUri.get_value() == "test.mtlx");
          }
        }
      }
    }
  }
}

// Test OpenPBRSurface shader reconstruction
void openpbr_surface_reconstruction_test(void) {
  std::string usda = R"(#usda 1.0

def Shader "OpenPBRShader"
{
    uniform token info:id = "OpenPBRSurface"

    # Base layer
    float inputs:base_weight = 0.9
    color3f inputs:base_color = (0.5, 0.6, 0.7)
    float inputs:base_roughness = 0.3
    float inputs:base_metalness = 0.2

    # Specular layer
    float inputs:specular_weight = 0.8
    color3f inputs:specular_color = (0.9, 0.9, 1.0)
    float inputs:specular_roughness = 0.15
    float inputs:specular_ior = 1.45

    token outputs:surface
}
)";

  Stage stage;
  std::string warn, err;

  bool ret = LoadUSDAFromMemory((const uint8_t*)usda.c_str(), usda.length(), "", &stage, &warn, &err);
  TEST_CHECK(ret == true);

  if (ret) {
    const Prim *shader_prim = nullptr;
    ret = stage.find_prim_at_path(Path("/OpenPBRShader", ""), shader_prim, &err);
    TEST_CHECK(ret == true);

    if (ret && shader_prim) {
      const Shader *shader = shader_prim->data().as<Shader>();
      TEST_CHECK(shader != nullptr);

      if (shader) {
        TEST_CHECK(shader->info_id == kOpenPBRSurface);

        const OpenPBRSurface *openpbr = shader->value.as<OpenPBRSurface>();
        TEST_CHECK(openpbr != nullptr);

        if (openpbr) {
          // Test base layer values
          // The get_value() returns an Animatable<T>, and we need to extract the scalar value
          if (openpbr->base_weight.authored()) {
            const auto& base_weight_anim = openpbr->base_weight.get_value();
            float val;
            if (base_weight_anim.get_scalar(&val)) {
              TEST_CHECK(math::is_close(val, 0.9f));
            }
          }
          if (openpbr->base_color.authored()) {
            const auto& base_color_anim = openpbr->base_color.get_value();
            value::color3f color;
            if (base_color_anim.get_scalar(&color)) {
              TEST_CHECK(math::is_close(color[0], 0.5f));
              TEST_CHECK(math::is_close(color[1], 0.6f));
              TEST_CHECK(math::is_close(color[2], 0.7f));
            }
          }
          if (openpbr->base_roughness.authored()) {
            const auto& base_roughness_anim = openpbr->base_roughness.get_value();
            float val;
            if (base_roughness_anim.get_scalar(&val)) {
              TEST_CHECK(math::is_close(val, 0.3f));
            }
          }
          if (openpbr->base_metalness.authored()) {
            const auto& base_metalness_anim = openpbr->base_metalness.get_value();
            float val;
            if (base_metalness_anim.get_scalar(&val)) {
              TEST_CHECK(math::is_close(val, 0.2f));
            }
          }

          // Test specular layer values
          if (openpbr->specular_weight.authored()) {
            const auto& specular_weight_anim = openpbr->specular_weight.get_value();
            float val;
            if (specular_weight_anim.get_scalar(&val)) {
              TEST_CHECK(math::is_close(val, 0.8f));
            }
          }
          if (openpbr->specular_ior.authored()) {
            const auto& specular_ior_anim = openpbr->specular_ior.get_value();
            float val;
            if (specular_ior_anim.get_scalar(&val)) {
              TEST_CHECK(math::is_close(val, 1.45f));
            }
          }
        }
      }
    }
  }
}

// Test MtlxAutodeskStandardSurface shader reconstruction
void mtlx_standard_surface_reconstruction_test(void) {
  std::string usda = R"(#usda 1.0

def Shader "StandardSurfaceShader"
{
    uniform token info:id = "MtlxAutodeskStandardSurface"

    # Base properties
    float inputs:base = 0.95
    color3f inputs:base_color = (0.18, 0.18, 0.18)
    float inputs:metalness = 0.9

    # Specular properties
    float inputs:specular = 0.85
    float inputs:specular_roughness = 0.05
    float inputs:specular_IOR = 1.52

    # Coat
    float inputs:coat = 0.2
    float inputs:coat_roughness = 0.02

    token outputs:out
}
)";

  Stage stage;
  std::string warn, err;

  bool ret = LoadUSDAFromMemory((const uint8_t*)usda.c_str(), usda.length(), "", &stage, &warn, &err);
  TEST_CHECK(ret == true);

  if (ret) {
    const Prim *shader_prim = nullptr;
    ret = stage.find_prim_at_path(Path("/StandardSurfaceShader", ""), shader_prim, &err);
    TEST_CHECK(ret == true);

    if (ret && shader_prim) {
      const Shader *shader = shader_prim->data().as<Shader>();
      TEST_CHECK(shader != nullptr);

      if (shader) {
        TEST_CHECK(shader->info_id == kMtlxAutodeskStandardSurface);

        const MtlxAutodeskStandardSurface *standardSurf = shader->value.as<MtlxAutodeskStandardSurface>();
        TEST_CHECK(standardSurf != nullptr);

        if (standardSurf) {
          // Test base properties
          if (standardSurf->base.authored()) {
            const auto& base_anim = standardSurf->base.get_value();
            float val;
            if (base_anim.get_scalar(&val)) {
              TEST_CHECK(math::is_close(val, 0.95f));
            }
          }
          if (standardSurf->metalness.authored()) {
            const auto& metalness_anim = standardSurf->metalness.get_value();
            float val;
            if (metalness_anim.get_scalar(&val)) {
              TEST_CHECK(math::is_close(val, 0.9f));
            }
          }

          // Test specular properties
          if (standardSurf->specular.authored()) {
            const auto& specular_anim = standardSurf->specular.get_value();
            float val;
            if (specular_anim.get_scalar(&val)) {
              TEST_CHECK(math::is_close(val, 0.85f));
            }
          }
          if (standardSurf->specular_roughness.authored()) {
            const auto& specular_roughness_anim = standardSurf->specular_roughness.get_value();
            float val;
            if (specular_roughness_anim.get_scalar(&val)) {
              TEST_CHECK(math::is_close(val, 0.05f));
            }
          }
          if (standardSurf->specular_IOR.authored()) {
            const auto& specular_IOR_anim = standardSurf->specular_IOR.get_value();
            float val;
            if (specular_IOR_anim.get_scalar(&val)) {
              TEST_CHECK(math::is_close(val, 1.52f));
            }
          }

          // Test coat properties
          if (standardSurf->coat.authored()) {
            const auto& coat_anim = standardSurf->coat.get_value();
            float val;
            if (coat_anim.get_scalar(&val)) {
              TEST_CHECK(math::is_close(val, 0.2f));
            }
          }
          if (standardSurf->coat_roughness.authored()) {
            const auto& coat_roughness_anim = standardSurf->coat_roughness.get_value();
            float val;
            if (coat_roughness_anim.get_scalar(&val)) {
              TEST_CHECK(math::is_close(val, 0.02f));
            }
          }
        }
      }
    }
  }
}

// Test NodeGraph support
void nodegraph_support_test(void) {
  // NodeGraph reconstruction is not yet implemented - this will be added in a future step
  // For now, just test that the NodeGraph struct is defined and has correct defaults
  NodeGraph ng;
  TEST_CHECK(!ng.nodedef.authored());
  TEST_CHECK(!ng.nodegraph_type.authored());

  // TypedAttribute doesn't have authored values by default
  TEST_CHECK(!ng.nodedef.has_value());
  TEST_CHECK(!ng.nodegraph_type.has_value());

  // TODO: Once NodeGraph reconstruction is implemented, add proper tests:
  /*
  std::string usda = R"(#usda 1.0

def NodeGraph "TestNodeGraph"
{
    uniform string nodedef = "test_nodedef"
    uniform string nodegraph_type = "material"

    # NodeGraph outputs are stored in props
    token outputs:result.connect = </TestNodeGraph/InternalShader.outputs:out>
}
)";

  Stage stage;
  std::string warn, err;

  bool ret = LoadUSDAFromMemory((const uint8_t*)usda.c_str(), usda.length(), "", &stage, &warn, &err);
  TEST_CHECK(ret == true);

  if (ret) {
    const Prim *nodegraph_prim = nullptr;
    ret = stage.find_prim_at_path(Path("/TestNodeGraph", ""), nodegraph_prim, &err);
    TEST_CHECK(ret == true);

    if (ret && nodegraph_prim) {
      const NodeGraph *nodegraph = nodegraph_prim->data().as<NodeGraph>();
      TEST_CHECK(nodegraph != nullptr);

      if (nodegraph) {
        // Check MaterialX-specific attributes
        if (nodegraph->nodedef.authored()) {
          TEST_CHECK(nodegraph->nodedef.get_value() == "test_nodedef");
        }
        if (nodegraph->nodegraph_type.authored()) {
          TEST_CHECK(nodegraph->nodegraph_type.get_value() == "material");
        }

        // Check that outputs are stored in props
        auto it = nodegraph->props.find("outputs:result");
        TEST_CHECK(it != nodegraph->props.end());
      }
    }
  }
  */
}

// Regression: a NodeGraph PrimSpec must survive Layer->Stage reconstruction
// (composition flatten). It was previously dropped as "TODO or unsupported prim
// type: NodeGraph", which lost the whole MaterialX network on a USDZ roundtrip.
void nodegraph_reconstruct_from_layer_test(void) {
  Layer layer;
  PrimSpec ng(Specifier::Def, "NodeGraph", "MyNodeGraph");

  // A MaterialX image shader node inside the node graph.
  PrimSpec shader(Specifier::Def, "Shader", "ImageNode");
  {
    Attribute attr;
    attr.set_value(value::token("ND_image_color3"));
    attr.set_type_name("token");
    attr.variability() = Variability::Uniform;
    shader.props()["info:id"] = Property(attr, false);
  }
  ng.children().push_back(shader);
  layer.add_primspec("MyNodeGraph", ng);

  Stage stage;
  std::string warn, err;
  bool ok = LayerToStage(std::move(layer), &stage, &warn, &err);
  TEST_CHECK(ok);
  if (!ok) {
    TEST_MSG("LayerToStage failed: %s", err.c_str());
    return;
  }

  // The NodeGraph prim (and its Shader child) must survive, not be dropped.
  Path path("/MyNodeGraph", "");
  auto result = stage.GetPrimAtPath(path);
  TEST_CHECK(result.has_value());
  if (result) {
    TEST_CHECK(result.value()->data().as<NodeGraph>() != nullptr);
    TEST_CHECK(result.value()->children().size() == 1);
    if (result.value()->children().size() == 1) {
      TEST_CHECK(result.value()->children()[0].data().as<Shader>() != nullptr);
    }
  }
}

// Test MaterialX shader type constants
void materialx_shader_constants_test(void) {
  // Check that the constants are defined and have expected values
  TEST_CHECK(std::string(kOpenPBRSurface) == "OpenPBRSurface");
  TEST_CHECK(std::string(kMtlxAutodeskStandardSurface) == "MtlxAutodeskStandardSurface");
  TEST_CHECK(std::string(kMtlxUsdPreviewSurface) == "MtlxUsdPreviewSurface");
  TEST_CHECK(std::string(kNodeGraph) == "NodeGraph");
}

// Test fallback values for MaterialX shaders
void materialx_shader_fallback_values_test(void) {
  // Test OpenPBRSurface default values
  // When not authored, get_value() returns the fallback value (an Animatable with the fallback value set)
  {
    OpenPBRSurface surface;
    TEST_CHECK(!surface.base_weight.authored());
    const auto& base_weight_anim = surface.base_weight.get_value();
    float val;
    if (base_weight_anim.get_scalar(&val)) {
      TEST_CHECK(math::is_close(val, 1.0f));
    }

    TEST_CHECK(!surface.base_roughness.authored());
    const auto& base_roughness_anim = surface.base_roughness.get_value();
    if (base_roughness_anim.get_scalar(&val)) {
      TEST_CHECK(math::is_close(val, 0.0f));
    }

    TEST_CHECK(!surface.base_metalness.authored());
    const auto& base_metalness_anim = surface.base_metalness.get_value();
    if (base_metalness_anim.get_scalar(&val)) {
      TEST_CHECK(math::is_close(val, 0.0f));
    }

    TEST_CHECK(!surface.specular_weight.authored());
    const auto& specular_weight_anim = surface.specular_weight.get_value();
    if (specular_weight_anim.get_scalar(&val)) {
      TEST_CHECK(math::is_close(val, 1.0f));
    }

    TEST_CHECK(!surface.specular_ior.authored());
    const auto& specular_ior_anim = surface.specular_ior.get_value();
    if (specular_ior_anim.get_scalar(&val)) {
      TEST_CHECK(math::is_close(val, 1.5f));
    }
  }

  // Test MtlxAutodeskStandardSurface default values
  {
    MtlxAutodeskStandardSurface surface;
    TEST_CHECK(!surface.base.authored());
    const auto& base_anim = surface.base.get_value();
    float val;
    if (base_anim.get_scalar(&val)) {
      TEST_CHECK(math::is_close(val, 1.0f));
    }

    TEST_CHECK(!surface.metalness.authored());
    const auto& metalness_anim = surface.metalness.get_value();
    if (metalness_anim.get_scalar(&val)) {
      TEST_CHECK(math::is_close(val, 0.0f));
    }

    TEST_CHECK(!surface.specular.authored());
    const auto& specular_anim = surface.specular.get_value();
    if (specular_anim.get_scalar(&val)) {
      TEST_CHECK(math::is_close(val, 1.0f));
    }

    TEST_CHECK(!surface.specular_roughness.authored());
    const auto& specular_roughness_anim = surface.specular_roughness.get_value();
    if (specular_roughness_anim.get_scalar(&val)) {
      TEST_CHECK(math::is_close(val, 0.2f));
    }

    TEST_CHECK(!surface.specular_IOR.authored());
    const auto& specular_IOR_anim = surface.specular_IOR.get_value();
    if (specular_IOR_anim.get_scalar(&val)) {
      TEST_CHECK(math::is_close(val, 1.5f));
    }
  }
}

// Security regression test: MaterialX <include filename="..."/> must not be
// usable for path traversal / arbitrary file read.
//
// ProcessIncludes() in src/usdMtlx.cc resolves the attacker-controlled
// `filename` attribute against the document's base directory. Previously the
// value was used verbatim, so an absolute path ("/etc/passwd") or a "../"
// traversal escaped the base directory. The fix routes the include filename
// through security_policy::ValidateAndNormalizeAssetPath(), which rejects
// absolute paths, Windows drive letters, and any ".." segment.
//
// This test confirms (1) "../" traversal is rejected, (2) an absolute path is
// rejected, and (3) a contained relative include still loads.
void materialx_include_path_traversal_test(void) {
  // Message fragment emitted by the path-validation guard in ProcessIncludes().
  const std::string kRejectMsg = "safe relative path";
  const std::string kTmpRoot = "unit_mtlx_traversal_tmp";
  const std::string kBaseDir = "unit_mtlx_traversal_tmp/base";

  // Fixture setup (cwd is the build dir per CTest WORKING_DIRECTORY).
  TUSDZ_TEST_MKDIR(kTmpRoot.c_str());
  TUSDZ_TEST_MKDIR(kBaseDir.c_str());  // EEXIST from a prior run is harmless.

  // A "secret" file living OUTSIDE the base dir (in the parent) — the target an
  // attacker would try to reach via "../".
  TEST_CHECK(MtlxTestWriteFile(kTmpRoot + "/secret_outside.txt",
                               "TOP-SECRET-MARKER\n"));

  const char *kIncludeFragment = R"(<?xml version="1.0"?>
<materialx version="1.38">
  <surfacematerial name="TestMaterial" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="TestMaterial_shader" />
  </surfacematerial>
  <open_pbr_surface name="TestMaterial_shader" type="surfaceshader">
    <input name="base_color" type="color3" value="0.8, 0.2, 0.2" />
    <input name="base_weight" type="float" value="1.0" />
  </open_pbr_surface>
</materialx>
)";

  TEST_CHECK(MtlxTestWriteFile(kBaseDir + "/main_traversal.mtlx",
                               R"(<?xml version="1.0"?>
<materialx version="1.38">
  <include filename="../secret_outside.txt"/>
</materialx>
)"));
  TEST_CHECK(MtlxTestWriteFile(kBaseDir + "/main_absolute.mtlx",
                               R"(<?xml version="1.0"?>
<materialx version="1.38">
  <include filename="/etc/passwd"/>
</materialx>
)"));
  TEST_CHECK(MtlxTestWriteFile(kBaseDir + "/inc_ok.mtlx", kIncludeFragment));
  TEST_CHECK(MtlxTestWriteFile(kBaseDir + "/main_ok.mtlx",
                               R"(<?xml version="1.0"?>
<materialx version="1.38">
  <include filename="inc_ok.mtlx"/>
</materialx>
)"));

  AssetResolutionResolver resolver;
  resolver.set_search_paths({kBaseDir});

  // (1) "../" traversal must be rejected by the path validator.
  {
    MtlxModel mtlx;
    std::string warn, err;
    bool ret = ReadMaterialXFromFile(resolver, "main_traversal.mtlx", &mtlx,
                                     &warn, &err);
    TEST_CHECK(ret == false);
    TEST_CHECK(MtlxTestContains(err, kRejectMsg));
    TEST_MSG("err: %s", err.c_str());
  }

  // (2) Absolute-path include must be rejected by the path validator.
  {
    MtlxModel mtlx;
    std::string warn, err;
    bool ret = ReadMaterialXFromFile(resolver, "main_absolute.mtlx", &mtlx,
                                     &warn, &err);
    TEST_CHECK(ret == false);
    TEST_CHECK(MtlxTestContains(err, kRejectMsg));
    TEST_MSG("err: %s", err.c_str());
  }

  // (3) A contained relative include must NOT be rejected and should load.
  {
    MtlxModel mtlx;
    std::string warn, err;
    bool ret =
        ReadMaterialXFromFile(resolver, "main_ok.mtlx", &mtlx, &warn, &err);
    TEST_CHECK(!MtlxTestContains(err, kRejectMsg));
    TEST_CHECK(ret == true);
    TEST_MSG("err: %s", err.c_str());
  }

  // Best-effort cleanup of fixture files.
  std::remove((kBaseDir + "/main_traversal.mtlx").c_str());
  std::remove((kBaseDir + "/main_absolute.mtlx").c_str());
  std::remove((kBaseDir + "/main_ok.mtlx").c_str());
  std::remove((kBaseDir + "/inc_ok.mtlx").c_str());
  std::remove((kTmpRoot + "/secret_outside.txt").c_str());
}

// Main test runner
void materialx_tests(void) {
  materialx_config_api_struct_test();
  materialx_config_api_parsing_test();
  openpbr_surface_reconstruction_test();
  mtlx_standard_surface_reconstruction_test();
  nodegraph_support_test();
  materialx_shader_constants_test();
  materialx_shader_fallback_values_test();
  materialx_include_path_traversal_test();
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif