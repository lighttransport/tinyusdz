#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-primvar.h"
#include "primvar.hh"

using namespace tinyusdz;

struct Mesh {
  std::vector<vector3f> vertices;
  std::vector<int32_t> indices;
};

static bool ReconstructAttribTest0() {
  Mesh mesh;
  Reconstructor r;

  r.property("vertices", &mesh.vertices)
   .property("indices", &mesh.indices);

  AttribMap amap;
  amap.attribs["vertices"] =
      std::vector<vector3f>({{1.0f, 2.0f, 3.0f}, {0.5f, 2.1f, 4.3f}});
  amap.attribs["indices"] =
      std::vector<int32_t>({0, 1, 2, 0, 3, 4});

  bool ret = r.reconstruct(amap);

  if (!ret) {
    std::cerr << r.get_error() << "\n";
  }

  return ret;
}


static bool ReconstructVertrices(const any_value &v, Mesh &mesh) {
  if (v.type_id() == (TYPE_ID_VECTOR3F | TYPE_ID_1D_ARRAY_BIT)) {
    mesh.vertices = *reinterpret_cast<const std::vector<vector3f> *>(v.value());
    return true;
  }

  return false;
}

void primvar_test(void) {

  {
    any_value f = 1.2f;
    TEST_CHECK(is_float(f));
    TEST_CHECK(is_type<float>()(f));
    float a = typecast<TYPE_ID_FLOAT>::to(f);
    std::cout << "a = " << a << "\n";

    f = double(4.5);
    TEST_CHECK(is_double(f));
    TEST_CHECK(is_type<double>()(f));
    double b = typecast<TYPE_ID_DOUBLE>::to(f);
    std::cout << "b = " << b << "\n";

    std::vector<float> v = {1.0f, 2.0f};
    f = v;
    TEST_CHECK(is_type<std::vector<float>>()(f));
    TEST_CHECK(!is_type<std::vector<double>>()(f));
    TEST_CHECK(!is_type<std::vector<std::vector<double>>>()(f));
    auto c = typecast<TYPE_ID_FLOAT | TYPE_ID_1D_ARRAY_BIT>::to(f);
    std::cout << "c = " << c << "\n";
  }

  {
    Mesh mesh;
    std::vector<vector3f> p = {{0.0f, 1.0f, 2.0f}, {3.0f, 4.0f, 5.0f}};
    any_value f = p;

    TEST_CHECK(ReconstructVertrices(f, mesh));

    std::vector<float> vf = {0.0f, 1.0f, 2.0f};
    f = vf;

    TEST_CHECK(!ReconstructVertrices(f, mesh));
  }

  {
    TEST_CHECK(ReconstructAttribTest0());
  }

}
