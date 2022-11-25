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
    TEST_CHECK(path.is_root_path() == true);
    TEST_CHECK(path.is_root_prim() == false);
  }

  {
    Path path("/bora", "");
    auto ret = path.split_at_root();
    TEST_CHECK(std::get<0>(ret).full_path_name() == "/bora");
    TEST_CHECK(std::get<1>(ret).is_empty() == true);
  }

  {
    Path path("/dora/bora", "");
    auto ret = path.split_at_root();
    TEST_CHECK(std::get<0>(ret).is_valid() == true);
    TEST_CHECK(std::get<0>(ret).full_path_name() == "/dora");
    TEST_CHECK(std::get<1>(ret).is_valid() == true);
    TEST_CHECK(std::get<1>(ret).full_path_name() == "/bora");
  }

  {
    Path path("dora", "");
    auto ret = path.split_at_root();
    TEST_CHECK(std::get<0>(ret).is_empty() == true);
    TEST_CHECK(std::get<1>(ret).is_valid() == true);
    TEST_CHECK(std::get<1>(ret).full_path_name() == "dora");
  }

  {
    Path rpath("dora", "");
    TEST_CHECK(rpath.make_relative().full_path_name() == "dora");

    Path apath("/dora", "");
    TEST_CHECK(apath.make_relative().full_path_name() == "dora");

    Path cpath("/dora", "");
    Path c;
    TEST_CHECK(c.make_relative(cpath).full_path_name() == "dora"); // std::move
  }

  {
    Path apath("/dora/bora", "");
    Path bpath("/dora", "");
    Path cpath("/doraa", "");
    Path dpath = bpath.AppendProperty("hello");

    TEST_CHECK(bpath < apath);
    TEST_CHECK(bpath < cpath);
    TEST_CHECK(bpath < dpath);
  }

}
