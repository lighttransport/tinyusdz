#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-minijson.h"

#include "minijson.hh"

#include <cmath>
#include <limits>
#include <string>

using tinyusdz::minijson::Error;
using tinyusdz::minijson::Parse;
using tinyusdz::minijson::ParseOptions;
using tinyusdz::minijson::Serialize;
using tinyusdz::minijson::Value;

void minijson_parse_basic_test(void) {
  const std::string src =
      R"({"name":"mesh","count":3,"ok":true,"items":[1,2.5,null]})";
  Value v;
  Error err;
  bool ok = Parse(src, &v, &err);
  TEST_CHECK(ok);
  TEST_CHECK(v.is_object());
  TEST_CHECK(v["name"].get<std::string>() == "mesh");
  TEST_CHECK(v["count"].get<size_t>() == 3);
  TEST_CHECK(v["ok"].get<bool>());
  TEST_CHECK(v["items"].is_array());
  TEST_CHECK(v["items"].size() == 3);
  TEST_CHECK(v["items"][1].get<double>() == 2.5);
  TEST_CHECK(v["items"][2].is_null());
}

void minijson_unicode_escape_test(void) {
  Value v;
  Error err;
  bool ok = Parse(R"({"s":"A\u3042\ud83d\ude00"})", &v, &err);
  TEST_CHECK(ok);
  TEST_CHECK(v["s"].get<std::string>() == std::string("A") + "\xe3\x81\x82" +
                                            "\xf0\x9f\x98\x80");

  std::string out;
  ok = Serialize(v, &out, &err);
  TEST_CHECK(ok);
  TEST_CHECK(out.find("\xe3\x81\x82") != std::string::npos);
}

void minijson_reject_invalid_utf8_test(void) {
  std::string src = "{\"s\":\"";
  src.push_back(static_cast<char>(0xc0));
  src.push_back(static_cast<char>(0x80));
  src += "\"}";

  Value v;
  Error err;
  bool ok = Parse(src, &v, &err);
  TEST_CHECK(!ok);
  TEST_CHECK(err.message.find("UTF-8") != std::string::npos);
}

void minijson_reject_duplicate_key_test(void) {
  Value v;
  Error err;
  bool ok = Parse(R"({"a":1,"a":2})", &v, &err);
  TEST_CHECK(!ok);
  TEST_CHECK(err.message.find("duplicate") != std::string::npos);
}

void minijson_reject_invalid_number_test(void) {
  const char *bad_numbers[] = {
      R"({"n":01})",
      R"({"n":1.})",
      R"({"n":1e})",
      R"({"n":+1})",
      R"({"n":NaN})",
  };

  for (const char *src : bad_numbers) {
    Value v;
    Error err;
    bool ok = Parse(src, &v, &err);
    TEST_CHECK(!ok);
  }
}

void minijson_reject_depth_limit_test(void) {
  ParseOptions options;
  options.max_depth = 8;

  std::string src;
  for (int i = 0; i < 32; i++) src.push_back('[');
  for (int i = 0; i < 32; i++) src.push_back(']');

  Value v;
  Error err;
  bool ok = Parse(src, &v, &err, options);
  TEST_CHECK(!ok);
  TEST_CHECK(err.message.find("depth") != std::string::npos);
}

void minijson_reject_nonfinite_serialize_test(void) {
  Value v;
  v["bad"] = (std::numeric_limits<double>::infinity)();

  std::string out;
  Error err;
  bool ok = Serialize(v, &out, &err);
  TEST_CHECK(!ok);
  TEST_CHECK(err.message.find("non-finite") != std::string::npos);
}
