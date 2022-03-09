#include "primvar.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// "src/external"
#include "external/staticstruct.hh"

#ifdef __clang__
#pragma clang diagnostic pop
#endif



namespace staticstruct {

using namespace tinyusdz;

// -- 00conv

template <>
struct Converter<quath> {
  typedef std::array<uint16_t, 4> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            quath &value) {
    memcpy(&value.real, &shadow[0], sizeof(uint16_t) * 4);

    return nullptr;  // success
  }

  static void to_shadow(const quath &value, shadow_type &shadow) {
    memcpy(&shadow[0], &value.real, sizeof(uint16_t) * 4);
  }
};

template <>
struct Converter<quatf> {
  typedef std::array<float, 4> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            quatf &value) {
    memcpy(&value.real, &shadow[0], sizeof(float) * 4);

    return nullptr;  // success
  }

  static void to_shadow(const quatf &value, shadow_type &shadow) {
    memcpy(&shadow[0], &value.real, sizeof(float) * 4);
  }
};

template <>
struct Converter<quatd> {
  typedef std::array<double, 4> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            quatd &value) {
    memcpy(&value.real, &shadow[0], sizeof(double) * 4);

    return nullptr;  // success
  }

  static void to_shadow(const quatd &value, shadow_type &shadow) {
    memcpy(&shadow[0], &value.real, sizeof(double) * 4);
  }
};

template <>
struct Converter<matrix2d> {
  typedef std::array<double, 4> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            matrix2d &value) {
    memcpy(&value.m[0][0], &shadow[0], sizeof(double) * 4);

    return nullptr;  // success
  }

  static void to_shadow(const matrix2d &value, shadow_type &shadow) {
    memcpy(&shadow[0], &value.m[0][0], sizeof(double) * 4);
  }
};

template <>
struct Converter<matrix3d> {
  typedef std::array<double, 9> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            matrix3d &value) {
    memcpy(&value.m[0][0], &shadow[0], sizeof(double) * 9);

    return nullptr;  // success
  }

  static void to_shadow(const matrix3d &value, shadow_type &shadow) {
    memcpy(&shadow[0], &value.m[0][0], sizeof(double) * 9);
  }
};

template <>
struct Converter<matrix4d> {
  typedef std::array<double, 16> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            matrix4d &value) {
    memcpy(&value.m[0][0], &shadow[0], sizeof(double) * 16);

    return nullptr;  // success
  }

  static void to_shadow(const matrix4d &value, shadow_type &shadow) {
    memcpy(&shadow[0], &value.m[0][0], sizeof(double) * 16);
  }
};

template <>
struct Converter<vector3h> {
  typedef std::array<uint16_t, 3> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            vector3h &value) {
    memcpy(&value, &shadow[0], sizeof(uint16_t) * 3);

    return nullptr;  // success
  }

  static void to_shadow(const vector3h &value, shadow_type &shadow) {
    memcpy(&shadow[0], &value, sizeof(uint16_t) * 3);
  }
};

template <>
struct Converter<vector3f> {
  typedef std::array<float, 3> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            vector3f &value) {
    value.x = shadow[0];
    value.y = shadow[1];
    value.z = shadow[2];

    return nullptr;  // success
  }

  static void to_shadow(const vector3f &value, shadow_type &shadow) {
    shadow[0] = value.x;
    shadow[1] = value.y;
    shadow[2] = value.z;
  }
};

template <>
struct Converter<vector3d> {
  typedef std::array<double, 3> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            vector3d &value) {
    value.x = shadow[0];
    value.y = shadow[1];
    value.z = shadow[2];

    return nullptr;  // success
  }

  static void to_shadow(const vector3d &value, shadow_type &shadow) {
    shadow[0] = value.x;
    shadow[1] = value.y;
    shadow[2] = value.z;
  }
};

template <>
struct Converter<normal3h> {
  typedef std::array<uint16_t, 3> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            normal3h &value) {
    memcpy(&value, &shadow[0], sizeof(uint16_t) * 3);

    return nullptr;  // success
  }

  static void to_shadow(const normal3h &value, shadow_type &shadow) {
    memcpy(&shadow[0], &value, sizeof(uint16_t) * 3);
  }
};

template <>
struct Converter<normal3f> {
  typedef std::array<float, 3> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            normal3f &value) {
    value.x = shadow[0];
    value.y = shadow[1];
    value.z = shadow[2];

    return nullptr;  // success
  }

  static void to_shadow(const normal3f &value, shadow_type &shadow) {
    shadow[0] = value.x;
    shadow[1] = value.y;
    shadow[2] = value.z;
  }
};

template <>
struct Converter<normal3d> {
  typedef std::array<double, 3> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            normal3d &value) {
    value.x = shadow[0];
    value.y = shadow[1];
    value.z = shadow[2];

    return nullptr;  // success
  }

  static void to_shadow(const normal3d &value, shadow_type &shadow) {
    shadow[0] = value.x;
    shadow[1] = value.y;
    shadow[2] = value.z;
  }
};

template <>
struct Converter<point3h> {
  typedef std::array<uint16_t, 3> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            point3h &value) {
    memcpy(&value, &shadow[0], sizeof(uint16_t) * 3);

    return nullptr;  // success
  }

  static void to_shadow(const point3h &value, shadow_type &shadow) {
    memcpy(&shadow[0], &value, sizeof(uint16_t) * 3);
  }
};

template <>
struct Converter<point3f> {
  typedef std::array<float, 3> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            point3f &value) {
    value.x = shadow[0];
    value.y = shadow[1];
    value.z = shadow[2];

    return nullptr;  // success
  }

  static void to_shadow(const point3f &value, shadow_type &shadow) {
    shadow[0] = value.x;
    shadow[1] = value.y;
    shadow[2] = value.z;
  }
};

template <>
struct Converter<point3d> {
  typedef std::array<double, 3> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            point3d &value) {
    value.x = shadow[0];
    value.y = shadow[1];
    value.z = shadow[2];

    return nullptr;  // success
  }

  static void to_shadow(const point3d &value, shadow_type &shadow) {
    shadow[0] = value.x;
    shadow[1] = value.y;
    shadow[2] = value.z;
  }
};

template <>
struct Converter<color3f> {
  typedef std::array<float, 3> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            color3f &value) {
    value.r = shadow[0];
    value.g = shadow[1];
    value.b = shadow[2];

    return nullptr;  // success
  }

  static void to_shadow(const color3f &value, shadow_type &shadow) {
    shadow[0] = value.r;
    shadow[1] = value.g;
    shadow[2] = value.b;
  }
};

template <>
struct Converter<color3d> {
  typedef std::array<double, 3> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            color3d &value) {
    value.r = shadow[0];
    value.g = shadow[1];
    value.b = shadow[2];

    return nullptr;  // success
  }

  static void to_shadow(const color3d &value, shadow_type &shadow) {
    shadow[0] = value.r;
    shadow[1] = value.g;
    shadow[2] = value.b;
  }
};

template <>
struct Converter<color4f> {
  typedef std::array<float, 4> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            color4f &value) {
    value.r = shadow[0];
    value.g = shadow[1];
    value.b = shadow[2];
    value.a = shadow[3];

    return nullptr;  // success
  }

  static void to_shadow(const color4f &value, shadow_type &shadow) {
    shadow[0] = value.r;
    shadow[1] = value.g;
    shadow[2] = value.b;
    shadow[3] = value.a;
  }
};

template <>
struct Converter<color4d> {
  typedef std::array<double, 4> shadow_type;

  static std::unique_ptr<Error> from_shadow(const shadow_type &shadow,
                                            color4d &value) {
    value.r = shadow[0];
    value.g = shadow[1];
    value.b = shadow[2];
    value.a = shadow[2];

    return nullptr;  // success
  }

  static void to_shadow(const color4d &value, shadow_type &shadow) {
    shadow[0] = value.r;
    shadow[1] = value.g;
    shadow[2] = value.b;
    shadow[3] = value.a;
  }
};

}  // namespace staticstruct

namespace tinyusdz {

// reconstruct

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

bool ReconstructAttribTest0() {
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

bool ReconstructAttribTest() {
  AttribMap amap;
  amap.attribs["vertices"] =
      std::vector<vector3f>({{1.0f, 2.0f, 3.0f}, {0.5f, 2.1f, 4.3f}});

  Mesh mesh;

  std::cout << "mesh.vertices typename = "
            << TypeTrait<decltype(mesh.vertices)>::type_name() << "\n";

  staticstruct::ObjectHandler h;
  h.add_property("vertices", &mesh.vertices, 0,
                 TypeTrait<decltype(mesh.vertices)>::type_id);

  staticstruct::Reader r;
  std::string err;
  bool ret = r.ParseStruct(
      &h,
      [&amap](std::string key, uint32_t flags, uint32_t user_type_id,
              staticstruct::BaseHandler &handler) -> bool {
        std::cout << "key = " << key << ", count = " << amap.attribs.count(key)
                  << "\n";

        if (!amap.attribs.count(key)) {
          if (flags & staticstruct::Flags::Optional) {
            return true;
          } else {
            return false;
          }
        }

        auto &value = amap.attribs[key];
        if (amap.attribs[key].type_id() == user_type_id) {
          if (user_type_id == (TYPE_ID_VECTOR3F | TYPE_ID_1D_ARRAY_BIT)) {
            std::vector<float3> *p =
                reinterpret_cast<std::vector<float3> *>(value.value());
            staticstruct::Handler<std::vector<float3>> _h(p);
            return _h.write(&handler);
          } else {
            std::cerr << "Unsupported type: " << GetTypeName(user_type_id)
                      << "\n";
            return false;
          }
        } else {
          std::cerr << "type: " << amap.attribs[key].type_name() << "(a.k.a "
                    << amap.attribs[key].underlying_type_name()
                    << ") expected but got " << GetTypeName(user_type_id)
                    << " for attribute \"" << key << "\"\n";
          return false;
        }
      },
      &err);

  if (!ret) {
    if (!err.empty()) {
      std::cerr << "Attrib reconstruction failed. ERR: " << err << "\n";
    }
  }

  std::cout << mesh.vertices << "\n";

  return ret;
}

} // namespace tinyusdz

