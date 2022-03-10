#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-primvar.h"
#include "primvar.hh"

using namespace tinyusdz;

#if 0
struct AttribMap {
  std::map<std::string, any_value> attribs;
};


class Register {
 public:
  Register() = default;

  template <class T>
  Register &property(std::string name, T *pointer,
                     uint32_t flags = staticstruct::Flags::Default) {
    h.add_property(name, pointer, flags, TypeTrait<T>::type_id);

    return *this;
  }

  bool reconstruct(AttribMap &amap) {
    err_.clear();

    staticstruct::Reader r;

#define CONVERT_TYPE_SCALAR(__ty, __value)       \
  case TypeTrait<__ty>::type_id: {               \
    __ty *p = reinterpret_cast<__ty *>(__value); \
    staticstruct::Handler<__ty> _h(p);           \
    return _h.write(&handler);                   \
  }

#define CONVERT_TYPE_1D(__ty, __value)                                     \
  case (TypeTrait<__ty>::type_id | TYPE_ID_1D_ARRAY_BIT): {                \
    std::vector<__ty> *p = reinterpret_cast<std::vector<__ty> *>(__value); \
    staticstruct::Handler<std::vector<__ty>> _h(p);                        \
    return _h.write(&handler);                                             \
  }

#define CONVERT_TYPE_2D(__ty, __value)                               \
  case (TypeTrait<__ty>::type_id | TYPE_ID_2D_ARRAY_BIT): {          \
    std::vector<std::vector<__ty>> *p =                              \
        reinterpret_cast<std::vector<std::vector<__ty>> *>(__value); \
    staticstruct::Handler<std::vector<std::vector<__ty>>> _h(p);     \
    return _h.write(&handler);                                       \
  }

#define CONVERT_TYPE_LIST(__FUNC) \
  __FUNC(half, v)                 \
  __FUNC(half2, v)                \
  __FUNC(half3, v)                \
  __FUNC(half4, v)                \
  __FUNC(int32_t, v)              \
  __FUNC(uint32_t, v)             \
  __FUNC(int2, v)                 \
  __FUNC(int3, v)                 \
  __FUNC(int4, v)                 \
  __FUNC(uint2, v)                \
  __FUNC(uint3, v)                \
  __FUNC(uint4, v)                \
  __FUNC(int64_t, v)              \
  __FUNC(uint64_t, v)             \
  __FUNC(float, v)                \
  __FUNC(float2, v)               \
  __FUNC(float3, v)               \
  __FUNC(float4, v)               \
  __FUNC(double, v)               \
  __FUNC(double2, v)              \
  __FUNC(double3, v)              \
  __FUNC(double4, v)              \
  __FUNC(quath, v)                \
  __FUNC(quatf, v)                \
  __FUNC(quatd, v)                \
  __FUNC(vector3h, v)             \
  __FUNC(vector3f, v)             \
  __FUNC(vector3d, v)             \
  __FUNC(normal3h, v)             \
  __FUNC(normal3f, v)             \
  __FUNC(normal3d, v)             \
  __FUNC(point3h, v)              \
  __FUNC(point3f, v)              \
  __FUNC(point3d, v)              \
  __FUNC(color3f, v)              \
  __FUNC(color3d, v)              \
  __FUNC(color4f, v)              \
  __FUNC(color4d, v)              \
  __FUNC(matrix2d, v)             \
  __FUNC(matrix3d, v)             \
  __FUNC(matrix4d, v)

    bool ret = r.ParseStruct(
        &h,
        [&amap](std::string key, uint32_t flags, uint32_t user_type_id,
                staticstruct::BaseHandler &handler) -> bool {
          std::cout << "key = " << key
                    << ", count = " << amap.attribs.count(key) << "\n";

          if (!amap.attribs.count(key)) {
            if (flags & staticstruct::Flags::Optional) {
              return true;
            } else {
              return false;
            }
          }

          auto &value = amap.attribs[key];
          if (amap.attribs[key].type_id() == user_type_id) {
            void *v = value.value();

            switch (user_type_id) {
              CONVERT_TYPE_SCALAR(bool, v)

              CONVERT_TYPE_LIST(CONVERT_TYPE_SCALAR)
              CONVERT_TYPE_LIST(CONVERT_TYPE_1D)
              CONVERT_TYPE_LIST(CONVERT_TYPE_2D)

              default: {
                std::cerr << "Unsupported type: " << GetTypeName(user_type_id)
                          << "\n";
                return false;
              }
            }
          } else {
            std::cerr << "type: " << amap.attribs[key].type_name() << "(a.k.a "
                      << amap.attribs[key].underlying_type_name()
                      << ") expected but got " << GetTypeName(user_type_id)
                      << " for attribute \"" << key << "\"\n";
            return false;
          }
        },
        &err_);

    return ret;
  }

#undef CONVERT_TYPE_SCALAR
#undef CONVERT_TYPE_1D
#undef CONVERT_TYPE_2D
#undef CONVERT_TYPE_LIST

  std::string get_error() const { return err_; }

 private:
  staticstruct::ObjectHandler h;
  std::string err_;
};

struct Mesh {
  std::vector<vector3f> vertices;
  std::vector<int32_t> indices;
};

static bool ReconstructVertrices(const any_value &v, Mesh &mesh) {
  if (v.type_id() == (TYPE_ID_VECTOR3F | TYPE_ID_1D_ARRAY_BIT)) {
    mesh.vertices = *reinterpret_cast<const std::vector<vector3f> *>(v.value());
    return true;
  }

  return false;
}

static bool ReconstructAttribTest0() {
  Mesh mesh;
  Register r;

  r.property("vertices", &mesh.vertices).property("indices", &mesh.indices);

  AttribMap amap;
  amap.attribs["vertices"] =
      std::vector<vector3f>({{1.0f, 2.0f, 3.0f}, {0.5f, 2.1f, 4.3f}});
  amap.attribs["indices"] =
      std::vector<vector3f>({{1.0f, 2.0f, 3.0f}, {0.5f, 2.1f, 4.3f}});

  bool ret = r.reconstruct(amap);

  if (!ret) {
    std::cerr << r.get_error() << "\n";
  }

  return ret;
}
#endif

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

  //TEST_CHECK(ReconstructAttribTest0());

}
