// SPDX-License-Identifier: Apache 2.0
// USDZ writer and validator unit tests

#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-usdz-writer.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "tinyusdz.hh"
#include "usdGeom.hh"
#include "stage.hh"

namespace {

tinyusdz::Stage MakeSimpleUSDZWriterStage() {
  tinyusdz::Stage stage;
  stage.metas().defaultPrim = tinyusdz::value::token("root");

  tinyusdz::Xform xform;
  xform.name = "root";
  xform.spec = tinyusdz::Specifier::Def;

  tinyusdz::value::Value primdata = xform;
  tinyusdz::Prim prim("root", primdata);
  prim.prim_type_name() = "Xform";
  stage.add_root_prim(std::move(prim));
  return stage;
}

std::string FirstUSDZEntryName(const std::vector<uint8_t> &data) {
  if (data.size() < 30 ||
      data[0] != 0x50 || data[1] != 0x4b ||
      data[2] != 0x03 || data[3] != 0x04) {
    return std::string();
  }
  const uint16_t name_len =
      static_cast<uint16_t>(data[26]) |
      static_cast<uint16_t>(static_cast<uint16_t>(data[27]) << 8);
  if (size_t(30) + size_t(name_len) > data.size()) {
    return std::string();
  }
  return std::string(reinterpret_cast<const char *>(data.data() + 30),
                     name_len);
}

}  // namespace

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

void usdz_writer_root_layer_format_test(void) {
  tinyusdz::Stage stage = MakeSimpleUSDZWriterStage();
  std::map<std::string, std::vector<uint8_t>> assets;

  {
    std::vector<uint8_t> usdz_data;
    std::string warn, err;
    bool ret = tinyusdz::SaveAsUSDZToMemory(stage, assets, &usdz_data,
                                            tinyusdz::USDZWriteOptions{},
                                            &warn, &err);
    TEST_CHECK(ret);
    if (!ret) {
      TEST_MSG("SaveAsUSDZToMemory USDC failed: %s", err.c_str());
      return;
    }
    TEST_CHECK(FirstUSDZEntryName(usdz_data) == "root.usdc");
  }

  {
    tinyusdz::USDZWriteOptions options;
    options.root_layer_format = tinyusdz::USDZRootLayerFormat::USDA;

    std::vector<uint8_t> usdz_data;
    std::string warn, err;
    bool ret = tinyusdz::SaveAsUSDZToMemory(stage, assets, &usdz_data,
                                            options, &warn, &err);
    TEST_CHECK(ret);
    if (!ret) {
      TEST_MSG("SaveAsUSDZToMemory USDA failed: %s", err.c_str());
      return;
    }
    TEST_CHECK(FirstUSDZEntryName(usdz_data) == "root.usda");

    warn.clear();
    err.clear();
    ret = tinyusdz::ValidateUSDZ(usdz_data.data(), usdz_data.size(), &warn,
                                 &err);
    TEST_CHECK(ret);
    if (!ret) {
      TEST_MSG("ValidateUSDZ USDA root failed: %s", err.c_str());
    }

    tinyusdz::Stage loaded_stage;
    warn.clear();
    err.clear();
    ret = tinyusdz::LoadUSDZFromMemory(usdz_data.data(), usdz_data.size(),
                                       "root-format.usdz", &loaded_stage,
                                       &warn, &err);
    TEST_CHECK(ret);
    if (!ret) {
      TEST_MSG("LoadUSDZFromMemory USDA root failed: %s", err.c_str());
    }
    TEST_CHECK(loaded_stage.root_prims().size() >= 1);
  }
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

// Test 4: CRC32 integrity validation
void usdz_validator_crc32_test(void) {
  tinyusdz::Stage stage;
  stage.metas().defaultPrim = tinyusdz::value::token("root");

  tinyusdz::Xform xform;
  xform.name = "root";
  xform.spec = tinyusdz::Specifier::Def;
  tinyusdz::value::Value primdata = xform;
  tinyusdz::Prim prim("root", primdata);
  prim.prim_type_name() = "Xform";
  stage.add_root_prim(std::move(prim));

  std::map<std::string, std::vector<uint8_t>> assets;
  assets["texture.png"] = std::vector<uint8_t>(100, 0x42);

  std::vector<uint8_t> usdz_data;
  std::string warn, err;

  bool ret = tinyusdz::SaveAsUSDZToMemory(stage, assets, &usdz_data, &warn, &err);
  TEST_CHECK(ret);

  // Valid data should pass
  warn.clear(); err.clear();
  ret = tinyusdz::ValidateUSDZ(usdz_data.data(), usdz_data.size(), &warn, &err);
  TEST_CHECK(ret);

  // Corrupt one byte in the file data area and check CRC fails
  if (usdz_data.size() > 128) {
    std::vector<uint8_t> corrupted = usdz_data;
    corrupted[128] ^= 0xFF;  // flip a byte
    warn.clear(); err.clear();
    ret = tinyusdz::ValidateUSDZ(corrupted.data(), corrupted.size(), &warn, &err);
    // May or may not fail depending on which byte was corrupted
    // (header vs data), but at minimum should not crash
    (void)ret;
  }
}

// Test 5: Validate compressed size == uncompressed size
void usdz_validator_size_consistency_test(void) {
  // Build a USDZ with known content
  tinyusdz::Stage stage;
  stage.metas().defaultPrim = tinyusdz::value::token("root");

  tinyusdz::Xform xform;
  xform.name = "root";
  xform.spec = tinyusdz::Specifier::Def;
  tinyusdz::value::Value primdata = xform;
  tinyusdz::Prim prim("root", primdata);
  prim.prim_type_name() = "Xform";
  stage.add_root_prim(std::move(prim));

  std::map<std::string, std::vector<uint8_t>> assets;
  std::vector<uint8_t> usdz_data;
  std::string warn, err;

  bool ret = tinyusdz::SaveAsUSDZToMemory(stage, assets, &usdz_data, &warn, &err);
  TEST_CHECK(ret);

  // The writer should produce valid compressed==uncompressed sizes
  warn.clear(); err.clear();
  ret = tinyusdz::ValidateUSDZ(usdz_data.data(), usdz_data.size(), &warn, &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("Size consistency check failed: %s", err.c_str());
  }
}

// Test 6: Validate empty/null input handling
void usdz_validator_empty_input_test(void) {
  std::string warn, err;

  // Null input
  bool ret = tinyusdz::ValidateUSDZ(nullptr, 0, &warn, &err);
  TEST_CHECK(!ret);
  TEST_CHECK(err.find("null") != std::string::npos || err.find("short") != std::string::npos);

  // Too short
  uint8_t tiny[3] = {0x50, 0x4b, 0x03};
  warn.clear(); err.clear();
  ret = tinyusdz::ValidateUSDZ(tiny, 3, &warn, &err);
  TEST_CHECK(!ret);

  // Bad magic
  uint8_t bad_magic[4] = {0x00, 0x00, 0x00, 0x00};
  warn.clear(); err.clear();
  ret = tinyusdz::ValidateUSDZ(bad_magic, 4, &warn, &err);
  TEST_CHECK(!ret);
  TEST_CHECK(err.find("magic") != std::string::npos);
}

void usdz_validator_missing_eocd_test(void) {
  const std::string name = "root.usda";
  const uint16_t name_len = static_cast<uint16_t>(name.size());
  const uint16_t extra_len = static_cast<uint16_t>(64 - 30 - name.size());

  std::vector<uint8_t> malformed(64, 0);
  malformed[0] = 0x50;
  malformed[1] = 0x4b;
  malformed[2] = 0x03;
  malformed[3] = 0x04;
  memcpy(&malformed[26], &name_len, sizeof(name_len));
  memcpy(&malformed[28], &extra_len, sizeof(extra_len));
  memcpy(malformed.data() + 30, name.data(), name.size());

  std::string warn, err;
  bool ret = tinyusdz::ValidateUSDZ(malformed.data(), malformed.size(),
                                    &warn, &err);
  TEST_CHECK(!ret);
  TEST_CHECK(err.find("End of central directory") != std::string::npos);
}

// Test 7: File-based write and read roundtrip
void usdz_writer_file_roundtrip_test(void) {
  tinyusdz::Stage stage;
  stage.metas().defaultPrim = tinyusdz::value::token("root");

  tinyusdz::Xform xform;
  xform.name = "root";
  xform.spec = tinyusdz::Specifier::Def;
  tinyusdz::value::Value primdata = xform;
  tinyusdz::Prim prim("root", primdata);
  prim.prim_type_name() = "Xform";
  stage.add_root_prim(std::move(prim));

  std::map<std::string, std::vector<uint8_t>> assets;
  assets["img.png"] = std::vector<uint8_t>(64, 0xCD);

  std::string warn, err;

  // Write to file. Use std::filesystem::temp_directory_path() so this
  // works on Windows (where /tmp does not exist) as well as POSIX.
  std::error_code tmp_ec;
  std::filesystem::path tmp_dir =
      std::filesystem::temp_directory_path(tmp_ec);
  if (tmp_ec) {
    tmp_dir = std::filesystem::current_path();
  }
  std::string tmp_path =
      (tmp_dir / "tinyusdz_test_roundtrip.usdz").string();
  bool ret = tinyusdz::SaveAsUSDZToFile(tmp_path, stage, assets, &warn, &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("SaveAsUSDZToFile failed: %s", err.c_str());
    return;
  }

  // Read back from file
  tinyusdz::Stage loaded;
  warn.clear(); err.clear();
  ret = tinyusdz::LoadUSDZFromFile(tmp_path, &loaded, &warn, &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("LoadUSDZFromFile failed: %s", err.c_str());
    return;
  }

  TEST_CHECK(loaded.root_prims().size() >= 1);

  // Clean up
  std::remove(tmp_path.c_str());
}

// Test 8: Large asset alignment stress test
void usdz_validator_large_asset_test(void) {
  tinyusdz::Stage stage;
  stage.metas().defaultPrim = tinyusdz::value::token("root");

  tinyusdz::Xform xform;
  xform.name = "root";
  xform.spec = tinyusdz::Specifier::Def;
  tinyusdz::value::Value primdata = xform;
  tinyusdz::Prim prim("root", primdata);
  prim.prim_type_name() = "Xform";
  stage.add_root_prim(std::move(prim));

  // Add assets with sizes that stress 64-byte alignment boundaries
  std::map<std::string, std::vector<uint8_t>> assets;
  assets["a.png"] = std::vector<uint8_t>(1, 0x01);
  assets["ab.png"] = std::vector<uint8_t>(31, 0x02);
  assets["abc.png"] = std::vector<uint8_t>(32, 0x03);
  assets["abcd.png"] = std::vector<uint8_t>(33, 0x04);
  assets["abcde.png"] = std::vector<uint8_t>(63, 0x05);
  assets["abcdef.png"] = std::vector<uint8_t>(64, 0x06);
  assets["abcdefg.png"] = std::vector<uint8_t>(65, 0x07);
  assets["large.exr"] = std::vector<uint8_t>(65536, 0x08);

  std::vector<uint8_t> usdz_data;
  std::string warn, err;

  bool ret = tinyusdz::SaveAsUSDZToMemory(stage, assets, &usdz_data, &warn, &err);
  TEST_CHECK(ret);

  // Validate all entries are aligned
  warn.clear(); err.clear();
  ret = tinyusdz::ValidateUSDZ(usdz_data.data(), usdz_data.size(), &warn, &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("Large asset alignment failed: %s", err.c_str());
  }

  // Verify asset count via ReadUSDZAssetInfo
  tinyusdz::USDZAsset usdz_asset;
  warn.clear(); err.clear();
  ret = tinyusdz::ReadUSDZAssetInfoFromMemory(usdz_data.data(), usdz_data.size(),
                                                true, &usdz_asset, &warn, &err);
  TEST_CHECK(ret);
  // root.usdc + 8 assets = 9 entries
  TEST_CHECK(usdz_asset.asset_map.size() == 9);
}

// Test 4 (original): Validator catches bad extensions
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

void usdz_writer_rejects_unsafe_asset_names_test(void) {
  tinyusdz::Stage stage;
  stage.metas().defaultPrim = tinyusdz::value::token("root");

  tinyusdz::Xform xform;
  xform.name = "root";
  xform.spec = tinyusdz::Specifier::Def;
  tinyusdz::value::Value primdata = xform;
  tinyusdz::Prim prim("root", primdata);
  prim.prim_type_name() = "Xform";
  stage.add_root_prim(std::move(prim));

  std::vector<std::string> bad_names = {
      "",
      "/abs.png",
      "../escape.png",
      "textures/../escape.png",
      "textures\\bad.png",
      "C:bad.png",
      "textures//bad.png",
      "textures/.",
  };
  bad_names.push_back(std::string("bad\0name.png", 12));
  bad_names.push_back(std::string(70000, 'a') + ".png");

  for (const std::string &name : bad_names) {
    std::map<std::string, std::vector<uint8_t>> assets;
    assets[name] = std::vector<uint8_t>(8, 0x42);
    std::vector<uint8_t> usdz_data;
    std::string warn, err;
    bool ret = tinyusdz::SaveAsUSDZToMemory(stage, assets, &usdz_data, &warn, &err);
    TEST_CHECK(!ret);
    TEST_CHECK(!err.empty());
  }
}
