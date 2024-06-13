#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-pprint.h"
#include "prim-types.hh"
#include "value-types.hh"
#include "value-pprint.hh"
#include "pprinter.hh"

using namespace tinyusdz;

void value_type_pprint_test(void) {

  {
    std::stringstream ss;
    tinyusdz::Interpolation interp = tinyusdz::Interpolation::Vertex;
    ss << interp;
    TEST_CHECK(ss.str() == "vertex");
  }

  {
    value::normal3f v{1.0f, 2.0f, 3.f};
    std::string s = to_string(v);
    TEST_CHECK(s == "(1, 2, 3)");
  }
}

