#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-value-types.h"
#include "value-types.hh"
#include "math-util.inc"

using namespace tinyusdz;

void value_types_test(void) {

  value::token tok1("bora");
  value::token tok2("muda");
  value::token tok3("bora");
  TEST_CHECK(tok1 == tok1);
  TEST_CHECK(tok1 != tok2);
  TEST_CHECK(tok1 == tok3);

  TEST_CHECK(value::GetTypeName(value::TYPE_ID_TOKEN) == "token");
  TEST_CHECK(value::GetTypeName(value::TYPE_ID_TOKEN|value::TYPE_ID_1D_ARRAY_BIT) == "token[]");

  TEST_CHECK(value::GetTypeId("token") == value::TYPE_ID_TOKEN);
  TEST_CHECK(value::GetTypeId("token[]") == (value::TYPE_ID_TOKEN|value::TYPE_ID_1D_ARRAY_BIT));

  TEST_CHECK(!value::TryGetTypeName(value::TYPE_ID_ALL));

  // texCoord2f <-> float2 cast
  value::float2 uv{1.0f, 2.0f};
  value::Value value(uv);

  value::texcoord2f *tex2f = value.as<value::texcoord2f>();
  TEST_CHECK(tex2f != nullptr);
  if (tex2f) {
    TEST_CHECK(math::is_close(tex2f->s, 1.0f));
    TEST_CHECK(math::is_close(tex2f->t, 2.0f));
  }

}

