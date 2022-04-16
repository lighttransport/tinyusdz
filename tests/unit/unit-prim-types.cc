#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-prim-types.h"
#include "prim-types.hh"

using namespace tinyusdz;

void prim_type_test(void) {
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
