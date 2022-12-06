#ifdef _MSC_VER
#define NOMINMAX
#endif

#include <iostream>

#define TEST_NO_MAIN
#include "acutest.h"

#include "value-types.hh"
#include "unit-value-types.h"
#include "prim-types.hh"
#include "xform.hh"
#include "unit-common.hh"
#include "value-pprint.hh"


using namespace tinyusdz;
using namespace tinyusdz_test;

void xformOp_test(void) {

  {
    value::double3 scale = {1.0, 2.0, 3.0};

    XformOp op;
    op.op_type = XformOp::OpType::Scale;
    op.inverted = true;
    op.set_value(scale);


    Xformable x;
    x.xformOps.push_back(op);

    value::matrix4d m;
    bool resetXformStack;
    std::string err;
    double t = value::TimeCode::Default();
    value::TimeSampleInterpolationType tinterp = value::TimeSampleInterpolationType::Held;

    bool ret = x.EvaluateXformOps(t, tinterp, &m, &resetXformStack, &err);
    TEST_CHECK(ret == true);

    TEST_CHECK(float_equals(m.m[0][0], 1.0));
    TEST_CHECK(float_equals(m.m[1][1], 1.0/2.0));
    TEST_CHECK(float_equals(m.m[2][2], 1.0/3.0));

  }

  {
    value::matrix4d a;
    a.m[0][0] = 0;
    a.m[0][1] = 0;
    a.m[0][2] = 1;
    a.m[0][3] = 0;
    a.m[1][0] = 0;
    a.m[1][1] = 1;
    a.m[1][2] = 0;
    a.m[1][3] = 0;

    a.m[2][0] = -1;
    a.m[2][1] = 0;
    a.m[2][2] = 0;
    a.m[2][3] = 0;
    a.m[3][0] = 0.44200000166893005;
    a.m[3][1] = -7.5320000648498535;
    a.m[3][2] = 18.611000061035156;
    a.m[3][3] = 1;

    value::matrix4d b = value::matrix4d::identity();
    b.m[3][2] = -30.0;

    value::matrix4d c = a * b;
    std::cout << c << "\n";

    // expected: (0, 0, 1, 0), (0, 1, 0, 0), (-1, 0, 0, 0), (0.442, -7.532, -11.389, 1)
    TEST_CHECK(float_equals(c.m[0][0], 0.0));
    TEST_CHECK(float_equals(c.m[0][1], 0.0));
    TEST_CHECK(float_equals(c.m[0][2], 1.0));
    TEST_CHECK(float_equals(c.m[0][3], 0.0));

    TEST_CHECK(float_equals(c.m[1][0], 0.0));
    TEST_CHECK(float_equals(c.m[1][1], 1.0));
    TEST_CHECK(float_equals(c.m[1][2], 0.0));
    TEST_CHECK(float_equals(c.m[1][3], 0.0));

    TEST_CHECK(float_equals(c.m[2][0], -1.0));
    TEST_CHECK(float_equals(c.m[2][1], 0.0));
    TEST_CHECK(float_equals(c.m[2][2], 0.0));
    TEST_CHECK(float_equals(c.m[2][3], 0.0));

    TEST_CHECK(float_equals(c.m[3][0], 0.442, 0.001));
    TEST_CHECK(float_equals(c.m[3][1], -7.532, 0.001));
    TEST_CHECK(float_equals(c.m[3][2], -11.389, 0.001));
    TEST_CHECK(float_equals(c.m[3][3], 1.0));


  }

  // RotateXYZ 000
  {
    value::double3 rotXYZ = {90.0, 0.0, 0.0};

    XformOp op;
    op.op_type = XformOp::OpType::RotateXYZ;
    op.inverted = false;
    op.set_value(rotXYZ);

    Xformable x;
    x.xformOps.push_back(op);

    value::matrix4d m;
    bool resetXformStack;
    std::string err;
    double t = value::TimeCode::Default();
    value::TimeSampleInterpolationType tinterp = value::TimeSampleInterpolationType::Held;

    bool ret = x.EvaluateXformOps(t, tinterp, &m, &resetXformStack, &err);

    TEST_CHECK(ret);
    
    std::cout << "rotXYZ = " << m << "\n";

    // ret = ( (1, 0, 0, 0), (0, 6.12323e-17, 1, 0), (0, -1, 6.12323e-17, 0), (0, 0, 0, 1) )
    TEST_CHECK(float_equals(m.m[0][0], 1.0));
    TEST_CHECK(float_equals(m.m[0][1], 0.0));
    TEST_CHECK(float_equals(m.m[0][2], 0.0));
    TEST_CHECK(float_equals(m.m[0][3], 0.0));

    TEST_CHECK(float_equals(m.m[1][0], 0.0));
    TEST_CHECK(float_equals(m.m[1][1], 0.0, 0.000001));
    TEST_CHECK(float_equals(m.m[1][2], 1.0));
    TEST_CHECK(float_equals(m.m[1][3], 0.0));

    TEST_CHECK(float_equals(m.m[2][0], 0.0));
    TEST_CHECK(float_equals(m.m[2][1], -1.0));
    TEST_CHECK(float_equals(m.m[2][2], 0.0, 0.000001));
    TEST_CHECK(float_equals(m.m[2][3], 0.0));

    TEST_CHECK(float_equals(m.m[3][0], 0.0));
    TEST_CHECK(float_equals(m.m[3][1], 0.0));
    TEST_CHECK(float_equals(m.m[3][2], 0.0));
    TEST_CHECK(float_equals(m.m[3][3], 1.0));



  }

  // RotateXYZ 001
  {
    value::double3 rotXYZ = {0.0, 0.0, -65.66769};

    XformOp op;
    op.op_type = XformOp::OpType::RotateXYZ;
    op.inverted = false;
    op.set_value(rotXYZ);

    Xformable x;
    x.xformOps.push_back(op);

    value::matrix4d m;
    bool resetXformStack;
    std::string err;
    double t = value::TimeCode::Default();
    value::TimeSampleInterpolationType tinterp = value::TimeSampleInterpolationType::Held;

    bool ret = x.EvaluateXformOps(t, tinterp, &m, &resetXformStack, &err);

    TEST_CHECK(ret);
    
    std::cout << "rotXYZ = " << m << "\n";

    // 0.4120283041870241, -0.9111710468121587, 0, 0
    // 0.9111710468121587, 0.4120283041870241, 0, 0
    // 0, 0, 1, 0
    // 0, 0, 0, 1
    TEST_CHECK(float_equals(m.m[0][0], 0.4120283041870241, 0.0001));
    TEST_CHECK(float_equals(m.m[0][1], -0.9111710468121587, 0.0001));
    TEST_CHECK(float_equals(m.m[0][2], 0.0));
    TEST_CHECK(float_equals(m.m[0][3], 0.0));

    TEST_CHECK(float_equals(m.m[1][0], 0.9111710468121587, 0.0001));
    TEST_CHECK(float_equals(m.m[1][1], 0.4120283041870241, 0.0001));
    TEST_CHECK(float_equals(m.m[1][2], 0.0));
    TEST_CHECK(float_equals(m.m[1][3], 0.0));

    TEST_CHECK(float_equals(m.m[2][0], 0.0));
    TEST_CHECK(float_equals(m.m[2][1], 0.0));
    TEST_CHECK(float_equals(m.m[2][2], 1.0));
    TEST_CHECK(float_equals(m.m[2][3], 0.0));

    TEST_CHECK(float_equals(m.m[3][0], 0.0));
    TEST_CHECK(float_equals(m.m[3][1], 0.0));
    TEST_CHECK(float_equals(m.m[3][2], 0.0));
    TEST_CHECK(float_equals(m.m[3][3], 1.0));

  }

  // trans x scale
  {
    value::double3 trans = {1.0, 1.0, 1.0};
    value::double3 scale = {1.5, 0.5, 2.5};

    Xformable x;
    {
      XformOp op;
      op.op_type = XformOp::OpType::Translate;
      op.inverted = false;
      op.set_value(trans);

      x.xformOps.push_back(op);
    }

    {
      XformOp op;
      op.op_type = XformOp::OpType::Scale;
      op.inverted = false;
      op.set_value(scale);

      x.xformOps.push_back(op);
    }

    value::matrix4d m;
    bool resetXformStack;
    std::string err;
    double t = value::TimeCode::Default();
    value::TimeSampleInterpolationType tinterp = value::TimeSampleInterpolationType::Held;

    bool ret = x.EvaluateXformOps(t, tinterp, &m, &resetXformStack, &err);

    TEST_CHECK(ret);
    
    std::cout << "trans x scale = " << m << "\n";

    // 1.5 0 0 0, 0 1.5 0 0, 0 0 1.5 0, 1 0 0 1
    TEST_CHECK(float_equals(m.m[0][0], 1.5, 0.0001));
    TEST_CHECK(float_equals(m.m[0][1], 0.0));
    TEST_CHECK(float_equals(m.m[0][2], 0.0));
    TEST_CHECK(float_equals(m.m[0][3], 0.0));

    TEST_CHECK(float_equals(m.m[1][0], 0.0));
    TEST_CHECK(float_equals(m.m[1][1], 0.5, 0.0001));
    TEST_CHECK(float_equals(m.m[1][2], 0.0));
    TEST_CHECK(float_equals(m.m[1][3], 0.0));

    TEST_CHECK(float_equals(m.m[2][0], 0.0));
    TEST_CHECK(float_equals(m.m[2][1], 0.0));
    TEST_CHECK(float_equals(m.m[2][2], 2.5, 0.0001));
    TEST_CHECK(float_equals(m.m[2][3], 0.0));

    TEST_CHECK(float_equals(m.m[3][0], 1.0));
    TEST_CHECK(float_equals(m.m[3][1], 1.0));
    TEST_CHECK(float_equals(m.m[3][2], 1.0));
    TEST_CHECK(float_equals(m.m[3][3], 1.0));

  }


}
