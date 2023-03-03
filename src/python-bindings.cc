#include <pybind11/pybind11.h>

#include "tinyusdz.hh"

#include "nonstd/optional.hpp"

namespace py = pybind11;

//using namespace py::literals;  // to bring in the `_a` literal

static double test_api() {
  // TODO: Implement
  return 4.14;
}

// stub classes
struct PyStage
{
  std::string filepath;
  tinyusdz::Stage _stage;
  std::string warn;
  std::string err;
  bool status{false};

  static PyStage Open(const std::string &_filepath) {
    // TODO
    PyStage stage;
    stage.filepath = _filepath;

    stage.status = tinyusdz::LoadUSDFromFile(stage.filepath, &stage._stage, &stage.warn, &stage.err);
    
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

PYBIND11_MODULE(ctinyusdz, m) {

  m.def("test_api", &test_api, "Test API");

  auto UsdModule = m.def_submodule("Usd");

  py::class_<PyStage>(UsdModule, "Stage")
    .def(py::init<>())
    .def_static("Open", &PyStage::Open)
    .def("Export", &PyStage::Export)
    .def("GetPrimAtPath", [](const PyStage &s, const std::string &path) -> py::object {
      if (auto p = s.GetPrimAtPath(path)) {
        return py::cast(*p);
      }
      return py::none();
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
