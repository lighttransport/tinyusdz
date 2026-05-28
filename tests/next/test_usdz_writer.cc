/// USDZ write tests for the next library.
/// Writes a Stage to USDZ, reads it back with the USDZ reader,
/// then reads the embedded USDC with the USDC reader.

#include "next/tinyusdz-next.hh"
#include "next/writer/usdz-writer.hh"
#include "next/writer/usdc-writer.hh"
#include "next/reader/usdz-reader.hh"
#include <cstdio>
#include <string>
#include <vector>
#include <cstring>

using namespace tinyusdz::next;

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) do { printf("  %s ... ", name); test_count++; } while(0)
#define PASS() do { pass_count++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

static Stage MakeTestStage() {
  StageBuilder sb;
  sb.SetDefaultPrim("World");
  sb.SetUpAxis("Y");

  auto& layer = sb.GetLayerBuilder();

  layer.begin_prim("World", "Xform");
  layer.end_prim();

  layer.begin_prim("Cube", "Mesh");
  layer.add_property("visibility", Value::MakeToken("inherited"));
  layer.add_property("points", Value::MakeFloat3Array(
      {-1,-1,-1, 1,-1,-1, 1,1,-1, -1,1,-1,
       -1,-1,1, 1,-1,1, 1,1,1, -1,1,1}));
  layer.add_property("faceVertexCounts", Value::MakeIntArray({4,4,4,4,4,4}));
  layer.end_prim();

  layer.finalize();
  return sb.Build();
}

void test_write_usdz_to_memory() {
  TEST("WriteUSDZToMemory");
  auto stage = MakeTestStage();

  std::vector<uint8_t> buf;
  auto result = WriteUSDZToMemory(buf, stage);
  if (!result.success) { FAIL(result.error.c_str()); return; }

  // Verify ZIP signature
  if (buf.size() < 4) { FAIL("buffer too small"); return; }
  if (buf[0] != 0x50 || buf[1] != 0x4b || buf[2] != 0x03 || buf[3] != 0x04) {
    FAIL("missing ZIP local header signature"); return;
  }

  // Verify EOCD signature
  if (buf.size() < 22) { FAIL("buffer too small for EOCD"); return; }
  if (buf[buf.size()-22] != 0x50 || buf[buf.size()-21] != 0x4b ||
      buf[buf.size()-20] != 0x05 || buf[buf.size()-19] != 0x06) {
    FAIL("missing EOCD signature"); return;
  }

  PASS();
}

void test_write_usdz_to_file() {
  TEST("WriteUSDZToFile");
  auto stage = MakeTestStage();

  auto result = WriteUSDZToFile("/tmp/test_usdz_next.usdz", stage);
  if (!result.success) { FAIL(result.error.c_str()); return; }

  // Verify file exists
  FILE* f = fopen("/tmp/test_usdz_next.usdz", "rb");
  if (!f) { FAIL("file not created"); return; }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fclose(f);

  if (size < 100) { FAIL("file too small"); return; }

  PASS();
}

void test_read_usdz_with_reader() {
  TEST("Read USDZ with USDZReader");

  USDZReader reader;
  if (!reader.OpenFile("/tmp/test_usdz_next.usdz")) {
    FAIL("failed to open USDZ file"); return;
  }

  // Should have at least 1 entry (root.usdc)
  if (reader.NumEntries() < 1) { FAIL("no entries found"); return; }

  // Find USDC entry
  int idx = reader.FindUSDCFile();
  if (idx < 0) { FAIL("no .usdc entry found"); return; }

  // Verify entry name
  const std::string& entry_name = reader.EntryName(idx);
  if (entry_name.find(".usdc") == std::string::npos) {
    FAIL("entry is not a .usdc file"); return;
  }

  // Verify entry has data
  const uint8_t* data = reader.EntryData(idx);
  size_t data_size = reader.EntrySize(idx);
  if (!data || data_size == 0) { FAIL("entry has no data"); return; }

  PASS();
}

void test_read_usdz_stage() {
  TEST("Load and read USDZ through Stage");

  Stage loaded_stage;
  std::string warn, err;
  bool ok = LoadUSD("/tmp/test_usdz_next.usdz", &loaded_stage, &warn, &err);
  if (!ok) { FAIL(err.c_str()); return; }

  // Verify prims
  auto world = loaded_stage.GetPrimAtPath("/World");
  if (!world.IsValid()) { FAIL("World not found"); return; }

  auto cube = loaded_stage.GetPrimAtPath("/Cube");
  if (!cube.IsValid()) { FAIL("Cube not found"); return; }

  PASS();
}

void test_usdc_from_usdz() {
  TEST("Extract USDC from USDZ and read");

  USDZReader reader;
  if (!reader.OpenFile("/tmp/test_usdz_next.usdz")) {
    FAIL("failed to open USDZ file"); return;
  }

  int idx = reader.FindUSDCFile();
  if (idx < 0) { FAIL("no .usdc entry"); return; }

  const uint8_t* usdc_data = reader.EntryData(idx);
  size_t usdc_size = reader.EntrySize(idx);

  // Write USDC to temp file and read with USDC reader
  std::string tmpfile = "/tmp/test_usdz_extracted.usdc";
  FILE* f = fopen(tmpfile.c_str(), "wb");
  if (!f) { FAIL("can't create temp file"); return; }
  fwrite(usdc_data, 1, usdc_size, f);
  fclose(f);

  Stage stage2;
  std::string warn2, err2;
  bool ok = LoadUSDC(tmpfile, &stage2, &warn2, &err2);
  if (!ok) { FAIL(err2.c_str()); return; }

  auto world = stage2.GetPrimAtPath("/World");
  if (!world.IsValid()) { FAIL("World not found after extraction"); return; }

  PASS();
}

void test_write_from_usdc() {
  TEST("Write USDZ directly from USDC data");

  // First create a USDC in memory
  auto stage = MakeTestStage();
  std::vector<uint8_t> usdc_buf;
  auto usdc_result = WriteUSDCToMemory(usdc_buf, stage);
  if (!usdc_result.success) { FAIL(usdc_result.error.c_str()); return; }

  // Now write as USDZ
  std::vector<uint8_t> usdz_buf;
  auto result = WriteUSDZFromUSDCToMemory(usdz_buf, usdc_buf.data(), usdc_buf.size());
  if (!result.success) { FAIL(result.error.c_str()); return; }

  // Verify the USDZ can be read
  USDZReader reader;
  if (!reader.Open(usdz_buf.data(), usdz_buf.size())) {
    FAIL("failed to open USDZ from memory"); return;
  }

  int idx = reader.FindUSDCFile();
  if (idx < 0) { FAIL("no .usdc entry"); return; }

  // Verify the extracted USDC matches
  const uint8_t* extracted = reader.EntryData(idx);
  size_t extracted_size = reader.EntrySize(idx);
  if (extracted_size != usdc_buf.size()) { FAIL("USDC size mismatch"); return; }
  if (std::memcmp(extracted, usdc_buf.data(), extracted_size) != 0) {
    FAIL("USDC data mismatch"); return;
  }

  PASS();
}

int main() {
  printf("USDZ Writer Tests\n");
  printf("=================\n\n");

  test_write_usdz_to_memory();
  test_write_usdz_to_file();
  test_read_usdz_with_reader();
  test_read_usdz_stage();
  test_usdc_from_usdz();
  test_write_from_usdc();

  printf("\n%d/%d tests passed\n", pass_count, test_count);
  return pass_count == test_count ? 0 : 1;
}
