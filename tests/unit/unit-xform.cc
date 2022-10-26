#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-value-types.h"
#include "prim-types.hh"
#include "xform.hh"
#include "unit-common.hh"


using namespace tinyusdz;
using namespace tinyusdz_test;

void xformOp_test(void) {

  {
    value::double3 scale = {1.0, 2.0, 3.0};

    XformOp op;
    op.op_type = XformOp::OpType::Scale;
    op.inverted = true;
    op.set_scalar(scale);

    
    Xformable x;
    x.xformOps.push_back(op);

    value::matrix4d m;
    bool resetXformStack;
    std::string err;
    bool ret = x.EvaluateXformOps(&m, &resetXformStack, &err);
    TEST_CHECK(ret == true);

    TEST_CHECK(float_equals(m.m[0][0], 1.0));
    TEST_CHECK(float_equals(m.m[1][1], 1.0/2.0));
    TEST_CHECK(float_equals(m.m[2][2], 1.0/3.0));

  }



}
