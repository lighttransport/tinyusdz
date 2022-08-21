#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-prim-types.h"
#include "prim-types.hh"

using namespace tinyusdz;

void prim_type_test(void) {
  // Path
  {
    Path path("/", "");
    TEST_CHECK(path.IsRootPath() == true);
    TEST_CHECK(path.IsRootPrim() == false);
  }

  {
    Path path("/bora", "");
    auto ret = path.SplitAtRoot();
    TEST_CHECK(std::get<0>(ret).full_path_name() == "/bora");
    TEST_CHECK(std::get<1>(ret).IsEmpty() == true);
  }

  {
    Path path("/dora/bora", "");
    auto ret = path.SplitAtRoot();
    TEST_CHECK(std::get<0>(ret).IsValid() == true);
    TEST_CHECK(std::get<0>(ret).full_path_name() == "/dora");
    TEST_CHECK(std::get<1>(ret).IsValid() == true);
    TEST_CHECK(std::get<1>(ret).full_path_name() == "/bora");
  }

  {
    Path path("dora", "");
    auto ret = path.SplitAtRoot();
    TEST_CHECK(std::get<0>(ret).IsEmpty() == true);
    TEST_CHECK(std::get<1>(ret).IsValid() == true);
    TEST_CHECK(std::get<1>(ret).full_path_name() == "dora");
  }

  {
    Path rpath("dora", "");
    TEST_CHECK(rpath.MakeRelative().full_path_name() == "dora");

    Path apath("/dora", "");
    TEST_CHECK(apath.MakeRelative().full_path_name() == "dora");

    Path cpath("/dora", "");
    Path c;
    TEST_CHECK(c.MakeRelative(cpath).full_path_name() == "dora"); // std::move
  }


#if 0
  {
    PrimValue<float> pf;
    TEST_CHECK(pf.array_dim() == 0);
    TEST_CHECK(pf.type_name() == "float");
  }

  {
    PrimValue<std::vector<float>> v;
    TEST_CHECK(v.array_dim() == 1);
    TEST_CHECK(v.type_name() == "float[]");
  }

  {
    PrimValue<std::vector<std::vector<float>>> v;
    TEST_CHECK(v.array_dim() == 2);
    TEST_CHECK(v.type_name() == "float[][]");
  }

  {
    PrimValue<std::vector<std::vector<std::vector<float>>>> v;
    TEST_CHECK(v.array_dim() == 3);
    TEST_CHECK(v.type_name() == "float[][][]");
  }
#endif

}
