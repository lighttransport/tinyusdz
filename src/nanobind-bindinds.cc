#include <nanobind/nanobind.h>

#include "core/prim.hh"
#include "core/prim-spec.hh"
#include "usda-reader.hh"
//#include "ascii-writer.hh"

#include "nonstd/optional.hpp"

namespace nb = nanobind;

using namespace nb::literals;  // to bring in the `_a` literal

#if defined(__clang__) || defined(__GNUC__)
#pragma message("DEPRECATED: nanobind-bindinds.cc binding is deprecated. Use the main Python binding implementation instead.")
#elif defined(_MSC_VER)
#pragma message("DEPRECATED: nanobind-bindinds.cc binding is deprecated. Use the main Python binding implementation instead.")
#endif

[[deprecated(
    "deprecated: nanobind-bindinds.cc binding is retained for compatibility only.")]]
static double test_api() {
  // TODO: Implement
  return 4.14;
}

// stub classes
[[deprecated("deprecated: placeholder Stage type in deprecated nanobind binding.")]]
struct Stage
{
  std::string filepath;

  static Stage Open(const std::string &_filepath) {
    Stage stage;
    stage.filepath = _filepath;

    return stage;
  }

  bool Export(const std::string &_filepath) {
    return false;
  }

  nonstd::optional<lightusd::GPrim> GetPrimAtPath(const std::string &_path) const {
    lightusd::GPrim prim;

    if (_path == "/bora") {
      return nonstd::nullopt;
    }

    return prim;

  }

  static lightusd::GPrim DefinePrim(const std::string &_path, const std::string &type) {
    lightusd::GPrim prim;

    if (type == "Xform") {
      // TODO:...
      prim.prim_type = "Xform";
      return prim;
    }

    return prim;
  }

};

NB_MODULE(pylightusd, m) {

  m.def("test_api", &test_api, "Test API");

  auto UsdModule = m.def_submodule("Usd");

  nb::class_<Stage>(UsdModule, "Stage")
    .def(nb::init<>())
    .def_static("Open", &Stage::Open)
    .def("Export", &Stage::Export)
    .def("GetPrimAtPath", [](const Stage &s, const std::string &path) -> nb::object {
      if (auto p = s.GetPrimAtPath(path)) {
        return nb::cast(*p);
      }
      return nb::none();
    });
  ;

  nb::class_<lightusd::GPrim>(UsdModule, "GPrim");


  nb::class_<lightusd::GeomSphere>(m, "Sphere")
    .def(nb::init<>())
  ;

  //py::class_<lightusd::GeomSphere>(m, "Sphere")
  //  .def(py::init<>())
  //  .def
}
