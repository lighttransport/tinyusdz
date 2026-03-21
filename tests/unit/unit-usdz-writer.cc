// SPDX-License-Identifier: Apache 2.0
// USDZ writer and validator unit tests

#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-usdz-writer.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "tinyusdz.hh"
#include "usdGeom.hh"
#include "stage.hh"

// Test 1: Basic USDZ write -> read roundtrip
void usdz_writer_basic_roundtrip_test(void) {
  // Build a simple stage with one Xform
  tinyusdz::Stage stage;
  stage.metas().defaultPrim = tinyusdz::value::token("root");

  tinyusdz::Xform xform;
  xform.name = "root";
  xform.spec = tinyusdz::Specifier::Def;

  tinyusdz::value::Value primdata = xform;
  tinyusdz::Prim prim("root", primdata);
  prim.prim_type_name() = "Xform";
  stage.add_root_prim(std::move(prim));

  // Write to memory
  std::map<std::string, std::vector<uint8_t>> assets;  // no extra assets
  std::vector<uint8_t> usdz_data;
  std::string warn, err;

  bool ret = tinyusdz::SaveAsUSDZToMemory(stage, assets, &usdz_data, &warn, &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("SaveAsUSDZToMemory failed: %s", err.c_str());
    return;
  }
  TEST_CHECK(usdz_data.size() > 0);

  // Validate the USDZ
  warn.clear(); err.clear();
  ret = tinyusdz::ValidateUSDZ(usdz_data.data(), usdz_data.size(), &warn, &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("ValidateUSDZ failed: %s", err.c_str());
  }

  // Verify ZIP magic
  TEST_CHECK(usdz_data[0] == 0x50);
  TEST_CHECK(usdz_data[1] == 0x4b);
  TEST_CHECK(usdz_data[2] == 0x03);
  TEST_CHECK(usdz_data[3] == 0x04);

  // Read back the USDZ
  tinyusdz::Stage loaded_stage;
  warn.clear(); err.clear();
  ret = tinyusdz::LoadUSDZFromMemory(usdz_data.data(), usdz_data.size(),
                                      "test.usdz", &loaded_stage, &warn, &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("LoadUSDZFromMemory failed: %s", err.c_str());
    return;
  }

  // Verify the loaded stage has the root prim
  TEST_CHECK(loaded_stage.root_prims().size() >= 1);
}

// Test 2: USDZ with additional assets (fake PNG data)
void usdz_writer_with_assets_test(void) {
  tinyusdz::Stage stage;
  stage.metas().defaultPrim = tinyusdz::value::token("root");

  tinyusdz::Xform xform;
  xform.name = "root";
  xform.spec = tinyusdz::Specifier::Def;

  tinyusdz::value::Value primdata = xform;
  tinyusdz::Prim prim("root", primdata);
  prim.prim_type_name() = "Xform";
  stage.add_root_prim(std::move(prim));

  // Add a fake asset
  std::map<std::string, std::vector<uint8_t>> assets;
  std::vector<uint8_t> fake_png(256, 0xAB);  // 256 bytes of dummy data
  assets["textures/diffuse.png"] = fake_png;

  std::vector<uint8_t> usdz_data;
  std::string warn, err;

  bool ret = tinyusdz::SaveAsUSDZToMemory(stage, assets, &usdz_data, &warn, &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("SaveAsUSDZToMemory with assets failed: %s", err.c_str());
    return;
  }

  // Validate
  warn.clear(); err.clear();
  ret = tinyusdz::ValidateUSDZ(usdz_data.data(), usdz_data.size(), &warn, &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("ValidateUSDZ failed: %s", err.c_str());
  }

  // Read asset info
  tinyusdz::USDZAsset usdz_asset;
  warn.clear(); err.clear();
  ret = tinyusdz::ReadUSDZAssetInfoFromMemory(usdz_data.data(), usdz_data.size(),
                                                /* asset_on_memory */ true,
                                                &usdz_asset, &warn, &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("ReadUSDZAssetInfoFromMemory failed: %s", err.c_str());
    return;
  }

  // Should have at least 2 entries (root.usdc + textures/diffuse.png)
  TEST_CHECK(usdz_asset.asset_map.size() >= 2);

  // Check the texture asset exists
  TEST_CHECK(usdz_asset.asset_map.count("textures/diffuse.png") == 1);
}

// Test 3: Validate 64-byte alignment
void usdz_validator_alignment_test(void) {
  tinyusdz::Stage stage;
  stage.metas().defaultPrim = tinyusdz::value::token("root");

  tinyusdz::Xform xform;
  xform.name = "root";
  xform.spec = tinyusdz::Specifier::Def;

  tinyusdz::value::Value primdata = xform;
  tinyusdz::Prim prim("root", primdata);
  prim.prim_type_name() = "Xform";
  stage.add_root_prim(std::move(prim));

  // Add multiple assets of varying sizes to stress alignment
  std::map<std::string, std::vector<uint8_t>> assets;
  assets["a.png"] = std::vector<uint8_t>(1, 0xFF);    // 1 byte
  assets["bb.png"] = std::vector<uint8_t>(63, 0xFE);  // 63 bytes
  assets["ccc.png"] = std::vector<uint8_t>(127, 0xFD); // 127 bytes
  assets["dddd.exr"] = std::vector<uint8_t>(1024, 0xFC); // 1024 bytes

  std::vector<uint8_t> usdz_data;
  std::string warn, err;

  bool ret = tinyusdz::SaveAsUSDZToMemory(stage, assets, &usdz_data, &warn, &err);
  TEST_CHECK(ret);

  // Validate alignment
  warn.clear(); err.clear();
  ret = tinyusdz::ValidateUSDZ(usdz_data.data(), usdz_data.size(), &warn, &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("Alignment validation failed: %s", err.c_str());
  }
}

// Test 4: Validator catches bad extensions
void usdz_validator_bad_extension_test(void) {
  tinyusdz::Stage stage;
  stage.metas().defaultPrim = tinyusdz::value::token("root");

  tinyusdz::Xform xform;
  xform.name = "root";
  xform.spec = tinyusdz::Specifier::Def;

  tinyusdz::value::Value primdata = xform;
  tinyusdz::Prim prim("root", primdata);
  prim.prim_type_name() = "Xform";
  stage.add_root_prim(std::move(prim));

  // Try to add a disallowed extension -- should be skipped with warning
  std::map<std::string, std::vector<uint8_t>> assets;
  assets["script.py"] = std::vector<uint8_t>(10, 0);

  std::vector<uint8_t> usdz_data;
  std::string warn, err;

  bool ret = tinyusdz::SaveAsUSDZToMemory(stage, assets, &usdz_data, &warn, &err);
  TEST_CHECK(ret);
  // Should have a warning about the skipped extension
  TEST_CHECK(warn.find("disallowed") != std::string::npos);
}
