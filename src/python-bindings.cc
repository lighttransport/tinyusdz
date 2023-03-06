#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "tinyusdz.hh"

#include "nonstd/optional.hpp"

namespace py = pybind11;

//using namespace py::literals;  // to bring in the `_a` literal

static double test_api() {
  // TODO: Implement
  return 4.14;
}

#if 0
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

#if 0
  static tinyusdz::GPrim DefinePrim(const std::string &_path, const std::string &type) {
    tinyusdz::GPrim prim;

    if (type == "Xform") {
      // TODO:...
      prim.prim_type = "Xform";
      return prim;
    }

    return prim;
  }
#endif

};
#endif

PYBIND11_MODULE(ctinyusdz, m) {

  using namespace tinyusdz;

  m.doc() = "Python binding for TinyUSDZ.";

  m.def("test_api", &test_api, "Test API");

  //auto UsdModule = m.def_submodule("Usd");

  py::class_<USDLoadOptions>(m, "USDLoadOptions")
    .def(py::init<>())
    .def_readwrite("num_threads", &USDLoadOptions::num_threads)
    .def_readwrite("load_assets", &USDLoadOptions::load_assets)
    .def_readwrite("max_memory_limit_in_mb", &USDLoadOptions::max_memory_limit_in_mb)
    .def_readwrite("do_composition", &USDLoadOptions::do_composition)
    ;

  py::class_<Prim>(m, "Prim")
    // default ctor: Create Prim with Model type.
    .def(py::init([]() { return Prim(Model()); }))
    .def_property("prim_id", [](const Prim &p) -> int64_t {
      return p.prim_id();
    }, [](Prim &p) -> int64_t & {
      return p.prim_id();
    })
    //.def_property("children", [](const Prim &p) -> const std::vector<Prim> & {
    //  return p.children();   
    //}, [](Prim &p, const Prim &c) {
    //  p.children().push_back(c);
    //}, py::return_value_policy::reference)
    .def("children", [](Prim &p) -> std::vector<Prim> & {
      return p.children(); 
    }, py::return_value_policy::reference)
    ;


  py::class_<Stage>(m, "Stage")
    .def(py::init<>())
    .def("GetPrimAtPath", [](const Stage &s, const std::string &path_str) -> py::object {

      Path path(path_str, "");

      if (auto p = s.GetPrimAtPath(path)) {
        return py::cast(*p);
      }

      return py::none();

    });
  ;

  m.def("LoadUSDFromFile", &LoadUSDAFromFile);

  //py::class_<tinyusdz::GPrim>(UsdModule, "GPrim");


  //py::class_<tinyusdz::GeomSphere>(m, "Sphere")
  //  .def(py::init<>())
  //;

  //py::class_<tinyusdz::GeomSphere>(m, "Sphere")
  //  .def(py::init<>())
  //  .def
}
