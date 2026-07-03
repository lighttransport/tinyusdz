// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USD I/O establishment test.
//
// Proves the next-core library alone (no legacy tinyusdz module) provides a
// complete USD read+write surface for all three encodings: USDA (ASCII), USDC
// (crate binary), and USDZ (zip package). It only includes the next umbrella
// header and drives every format through the high-level Load*/Write* API,
// including the extension-dispatching LoadUSD / WriteUSD entry points.

#include "next/tinyusdz-next.hh"

#include <cstdio>
#include <string>

using namespace tinyusdz::next;

static int test_count = 0;
static int fail_count = 0;
#define TEST(name) do { printf("  %-40s ... ", name); test_count++; } while (0)
#define PASS() do { printf("PASS\n"); } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); fail_count++; } while (0)

static Stage MakeTestStage() {
  StageBuilder sb;
  sb.SetDefaultPrim("World");
  sb.SetUpAxis("Y");
  auto& layer = sb.GetLayerBuilder();

  layer.begin_prim("World", "Xform");
  layer.end_prim();

  layer.begin_prim("Cube", "Mesh");
  layer.add_property("visibility", Value::MakeToken("inherited"));
  layer.add_property(
      "points", Value::MakeFloat3Array({-1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1,
                                        -1, -1, -1, 1, 1, -1, 1, 1, 1, 1, -1, 1,
                                        1}));
  layer.add_property("faceVertexCounts", Value::MakeIntArray({4, 4, 4, 4, 4, 4}));
  layer.end_prim();

  layer.finalize();
  return sb.Build();
}

static size_t CountPrims(const Stage& stage) {
  size_t n = 0;
  stage.Traverse([&n](const UsdPrim&) {
    ++n;
    return true;
  });
  return n;
}

// WriteUSD by extension -> LoadUSD auto-detect -> prim count must round-trip.
static void test_writeusd_loadusd_roundtrip(const char* path) {
  TEST(path);
  const Stage src = MakeTestStage();
  const size_t want = CountPrims(src);

  std::string err;
  if (!WriteUSD(src, path, &err)) {
    FAIL(("WriteUSD failed: " + err).c_str());
    return;
  }

  Stage loaded;
  std::string warn, lerr;
  if (!LoadUSD(path, &loaded, &warn, &lerr)) {
    FAIL(("LoadUSD failed: " + lerr).c_str());
    return;
  }
  const size_t got = CountPrims(loaded);
  if (got != want) {
    FAIL(("prim count " + std::to_string(got) + " != " + std::to_string(want))
             .c_str());
    return;
  }
  PASS();
}

// Format-specific writer + format-specific reader round-trip.
static void test_format_specific(const char* label, const char* path) {
  TEST(label);
  const Stage src = MakeTestStage();
  const size_t want = CountPrims(src);
  const std::string p = path;
  std::string err;
  bool wrote = false;
  if (p.size() >= 5 && p.substr(p.size() - 5) == ".usda")
    wrote = WriteUSDA(src, p, &err);
  else if (p.size() >= 5 && p.substr(p.size() - 5) == ".usdc")
    wrote = WriteUSDC(src, p, &err);
  else
    wrote = WriteUSDZ(src, p, &err);
  if (!wrote) {
    FAIL(("write failed: " + err).c_str());
    return;
  }

  Stage loaded;
  std::string warn, lerr;
  bool read = false;
  if (p.size() >= 5 && p.substr(p.size() - 5) == ".usda")
    read = LoadUSDA(p, &loaded, &warn, &lerr);
  else if (p.size() >= 5 && p.substr(p.size() - 5) == ".usdc")
    read = LoadUSDC(p, &loaded, &warn, &lerr);
  else
    read = LoadUSDZ(p, &loaded, &warn, &lerr);
  if (!read) {
    FAIL(("read failed: " + lerr).c_str());
    return;
  }
  if (CountPrims(loaded) != want) {
    FAIL("prim count mismatch");
    return;
  }
  PASS();
}

// Memory-only round-trip: WriteUSDToMemory -> LoadUSDFromMemory (auto-detect).
// This is the freestanding path (no filesystem).
static void test_memory_roundtrip(const char* label, USDFormat fmt) {
  TEST(label);
  const Stage src = MakeTestStage();
  const size_t want = CountPrims(src);

  std::vector<uint8_t> buf;
  std::string err;
  if (!WriteUSDToMemory(buf, src, fmt, &err)) {
    FAIL(("WriteUSDToMemory failed: " + err).c_str());
    return;
  }
  if (buf.empty()) {
    FAIL("WriteUSDToMemory produced empty buffer");
    return;
  }

  Stage loaded;
  std::string warn, lerr;
  if (!LoadUSDFromMemory(buf.data(), buf.size(), &loaded, &warn, &lerr)) {
    FAIL(("LoadUSDFromMemory failed: " + lerr).c_str());
    return;
  }
  if (CountPrims(loaded) != want) {
    FAIL("prim count mismatch");
    return;
  }
  PASS();
}

// A bare `.usd` must be written as crate (USDC) and load back via auto-detect.
static void test_bare_usd_is_crate() {
  TEST("WriteUSD(.usd) writes USDC (crate)");
  const Stage src = MakeTestStage();
  const char* path = "/tmp/next_io_test.usd";
  std::string err;
  if (!WriteUSD(src, path, &err)) {
    FAIL(("WriteUSD failed: " + err).c_str());
    return;
  }
  // Crate magic is "PXR-USDC".
  std::FILE* fp = std::fopen(path, "rb");
  if (!fp) {
    FAIL("cannot reopen written file");
    return;
  }
  char magic[8] = {};
  size_t nread = std::fread(magic, 1, 8, fp);
  std::fclose(fp);
  if (nread != 8 || std::string(magic, 8) != "PXR-USDC") {
    FAIL("bare .usd was not written as crate");
    return;
  }
  Stage loaded;
  std::string warn, lerr;
  if (!LoadUSD(path, &loaded, &warn, &lerr)) {
    FAIL(("LoadUSD failed: " + lerr).c_str());
    return;
  }
  PASS();
}

int main() {
  printf("next USD I/O establishment test (next-core only)\n");

  printf("[WriteUSD/LoadUSD extension round-trip]\n");
  test_writeusd_loadusd_roundtrip("/tmp/next_io_test.usda");
  test_writeusd_loadusd_roundtrip("/tmp/next_io_test.usdc");
  test_writeusd_loadusd_roundtrip("/tmp/next_io_test.usdz");

  printf("[format-specific Write*/Load* round-trip]\n");
  test_format_specific("USDA WriteUSDA/LoadUSDA", "/tmp/next_io_fs.usda");
  test_format_specific("USDC WriteUSDC/LoadUSDC", "/tmp/next_io_fs.usdc");
  test_format_specific("USDZ WriteUSDZ/LoadUSDZ", "/tmp/next_io_fs.usdz");

  printf("[memory-only round-trip (freestanding)]\n");
  test_memory_roundtrip("USDA WriteUSDToMemory/LoadUSDFromMemory", USDFormat::USDA);
  test_memory_roundtrip("USDC WriteUSDToMemory/LoadUSDFromMemory", USDFormat::USDC);
  test_memory_roundtrip("USDZ WriteUSDToMemory/LoadUSDFromMemory", USDFormat::USDZ);

  printf("[extension dispatch]\n");
  test_bare_usd_is_crate();

  printf("\n%d/%d passed\n", test_count - fail_count, test_count);
  return fail_count == 0 ? 0 : 1;
}
