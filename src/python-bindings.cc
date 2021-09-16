#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "prim-types.hh"
#include "usda-parser.hh"
#include "usda-writer.hh"

#include "nonstd/optional.hpp"

namespace py = pybind11;

using namespace pybind11::literals;  // to bring in the `_a` literal

static double test_api() {
  // TODO: Implement
  return 4.14;
}

// stub classes
struct Stage
{
  std::string filepath;

  static Stage Open(const std::string &_filepath) {
    // TODO
    Stage stage;
    stage.filepath = _filepath;

    return stage;
  }

  bool Export(const std::string &_filepath) {
    // TODO
    return false;
  }

  nonstd::optional<tinyusdz::GPrim> GetPrimAtPath(const std::string &_path) const {
    // TODO
    tinyusdz::GPrim prim;

    if (_path == "/bora") {
      return nonstd::nullopt;
    }

    return prim;

  }

  static tinyusdz::GPrim DefinePrim(const std::string &_path, const std::string &type) {
    tinyusdz::GPrim prim;

    if (type == "Xform") {
      // TODO:...
      prim.prim_type = "Xform";
      return prim;
    }

    return prim;
  }

};

PYBIND11_MODULE(pytinyusd, m) {
  m.doc() = "TinyUSD Python binding";

  m.def("test_api", &test_api, "Test API");

  auto UsdModule = m.def_submodule("Usd");

  py::class_<Stage>(UsdModule, "Stage")
    .def(py::init<>())
    .def_static("Open", &Stage::Open)
    .def("Export", &Stage::Export)
    .def("GetPrimAtPath", [](const Stage &s, const std::string &path) -> py::object {
      if (auto p = s.GetPrimAtPath(path)) {
        return py::cast(*p);
      }
      return py::cast<py::none>(Py_None);
    });
  ;

  py::class_<tinyusdz::GPrim>(UsdModule, "GPrim");


  py::class_<tinyusdz::GeomSphere>(m, "Sphere")
    .def(py::init<>())
  ;

  //py::class_<tinyusdz::GeomSphere>(m, "Sphere")
  //  .def(py::init<>())
  //  .def
}
