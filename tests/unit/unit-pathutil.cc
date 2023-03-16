#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-pathutil.h"
#include "prim-types.hh"
#include "path-util.hh"

using namespace tinyusdz;

void pathutil_test(void) {
  {
    Path basepath("/", "");
    Path relpath("../bora", "");
    Path abspath("", "");
    bool ret = pathutil::ResolveRelativePath(basepath, relpath, &abspath);
    std::cout << "abs_path = " << abspath.full_path_name() << "\n";
    TEST_CHECK(ret == true);
    TEST_CHECK(abspath.prim_part() == "/bora");
  }

  {
    Path basepath("/root", "");
    Path relpath("../bora", "");
    Path abspath("", "");
    bool ret = pathutil::ResolveRelativePath(basepath, relpath, &abspath);
    std::cout << "abs_path = " << abspath.full_path_name() << "\n";
    TEST_CHECK(ret == true);
    TEST_CHECK(abspath.prim_part() == "/bora");
  }

  {
    Path basepath("/root/muda", "");
    Path relpath("../bora", "");
    Path abspath("", "");
    bool ret = pathutil::ResolveRelativePath(basepath, relpath, &abspath);
    std::cout << "abs_path = " << abspath.full_path_name() << "\n";
    TEST_CHECK(ret == true);
    TEST_CHECK(abspath.prim_part() == "/root/bora");
  }

  {
    Path basepath("/root", "");
    Path relpath("../../boraa", "");
    Path abspath("", "");
    bool ret = pathutil::ResolveRelativePath(basepath, relpath, &abspath);
    std::cout << "abs_path = " << abspath.full_path_name() << "\n";
    TEST_CHECK(ret == true);
    TEST_CHECK(abspath.prim_part() == "/boraa");
  }

  {
    // unixish behavior
    Path basepath("/root", "");
    Path relpath("../../../boraaa", "");
    Path abspath("", "");
    bool ret = pathutil::ResolveRelativePath(basepath, relpath, &abspath);
    std::cout << "abs_path = " << abspath.full_path_name() << "\n";
    TEST_CHECK(ret == true);
    TEST_CHECK(abspath.prim_part() == "/boraaa");
  }

  {
    Path basepath("/root", "");
    Path relpath("../bora1", "myprop");
    Path abspath("", "");
    bool ret = pathutil::ResolveRelativePath(basepath, relpath, &abspath);
    std::cout << "abs_path = " << abspath.full_path_name() << "\n";
    TEST_CHECK(ret == true);
    TEST_CHECK(abspath.full_path_name() == "/bora1.myprop");
  }

  {
    Path basepath("/root", "");
    Path relpath("../bora2.myprop", "");
    Path abspath("", "");
    bool ret = pathutil::ResolveRelativePath(basepath, relpath, &abspath);
    TEST_CHECK(ret == false);
  }

  {
    Path basepath("/root", "");
    Path relpath("./bora3", "");
    Path abspath("", "");
    bool ret = pathutil::ResolveRelativePath(basepath, relpath, &abspath);
    TEST_CHECK(ret == true);
    TEST_CHECK(abspath.prim_part() == "/root/bora3");
  }


  {
    // .. in the middle of relative path is not support yet
    Path basepath("/root", "");
    Path relpath("../bora4/../dora", "");
    Path abspath("", "");
    bool ret = pathutil::ResolveRelativePath(basepath, relpath, &abspath);
    TEST_CHECK(ret == false);
  }

}
