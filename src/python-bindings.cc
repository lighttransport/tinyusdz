#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include "nonstd/optional.hpp"
#include "tiny-format.hh"
#include "tinyusdz.hh"

namespace py = pybind11;

PYBIND11_MAKE_OPAQUE(std::vector<int>);
PYBIND11_MAKE_OPAQUE(std::vector<tinyusdz::Prim>);

// using namespace py::literals;  // to bring in the `_a` literal

static double test_api() {
  // TODO: Implement
  return 4.14;
}

class PyTest {
 public:
  std::vector<int> intv;
};

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

namespace internal {

tinyusdz::Stage load_usd(const std::string &filename) {
  tinyusdz::Stage stage;

  if (!tinyusdz::IsUSD(filename)) {
    std::string s(tinyusdz::fmt::format("{} not found or not a USD file."));
    PyErr_SetString(PyExc_FileNotFoundError, s.c_str());
    throw py::error_already_set();
  }

  std::string warn;
  std::string err;
  bool ret = tinyusdz::LoadUSDFromFile(filename, &stage, &warn, &err);

  if (warn.size()) {
    py::print("[ctinyusdz::load_usd] ", warn);
  }

  if (!ret) {
    std::string msg = "Failed to load USD";
    if (err.size()) {
      msg += ": " + err;
    }

    PyErr_SetString(PyExc_FileNotFoundError, filename.c_str());
    throw py::error_already_set();
  }

  return stage;
}

bool is_usd(const std::string &filename) { return tinyusdz::IsUSD(filename); }

std::string detect_usd_format(const std::string &filename) {
  std::string format;

  if (tinyusdz::IsUSD(filename, &format)) {
    return format;
  }
  return format;  // empty
}

}  // namespace internal

PYBIND11_MODULE(ctinyusdz, m) {
  using namespace tinyusdz;

  m.doc() = "Python binding for TinyUSDZ.";

  m.def("test_api", &test_api, "Test API");

  // auto UsdModule = m.def_submodule("Usd");

  py::class_<USDLoadOptions>(m, "USDLoadOptions")
      .def(py::init<>())
      .def_readwrite("num_threads", &USDLoadOptions::num_threads)
      .def_readwrite("load_assets", &USDLoadOptions::load_assets)
      .def_readwrite("max_memory_limit_in_mb",
                     &USDLoadOptions::max_memory_limit_in_mb)
      .def_readwrite("do_composition", &USDLoadOptions::do_composition);

  m.def(
      "format", &internal::detect_usd_format,
      "Detect USD format(USDA/USDC/USDZ) of file. Returns `\"usda\"`, "
      "`\"usdc\"` `\"usdz\"` or empty string(when a file is not a USD file).");
  m.def("is_usd", &internal::is_usd, "Load USD/USDA/USDC/USDZ from a file.");
  m.def("load_usd", &internal::load_usd,
        "Load USD/USDA/USDC/USDZ from a file.");

  py::class_<PyTest>(m, "PyTest")
      .def(py::init<>())
#if 1
      .def_readwrite("intv", &PyTest::intv)
#else
      .def_property(
          "intv",
          [](PyTest &self) -> const std::vector<int> & {
            py::print("intv get");
            return self.intv;
          },
          [](PyTest &self, const std::vector<int> &v) {
            py::print("intv setter");
            self.intv = v;
          },
          py::return_value_policy::reference_internal)
#endif
      ;

  py::class_<Prim>(m, "Prim")
      // default ctor: Create Prim with Model type.
      .def(py::init([]() { return Prim(Model()); }))
      .def_property(
          "prim_id", [](const Prim &p) -> int64_t { return p.prim_id(); },
          [](Prim &p) -> int64_t & { return p.prim_id(); })
      //.def_property("children", [](const Prim &p) -> const std::vector<Prim> &
      //{
      //  return p.children();
      //}, [](Prim &p, const Prim &c) {
      //  p.children().push_back(c);
      //}, py::return_value_policy::reference)
      .def(
          "children",
          [](Prim &p) -> std::vector<Prim> & { return p.children(); },
          py::return_value_policy::reference)
      //.def_property("primChildren", [](const Prim &p) -> const
      //std::vector<Prim> {
      //  py::print("getter");
      //  return p.children();
      //}, [](Prim &p, const std::vector<Prim> &v) {
      //  py::print("setter");
      //  p.children() = v;
      //})
      ;

  py::class_<StageMetas>(m, "StageMetas")
    .def(py::init<>())
    .def_property("metersPerUnit", 
      [](const StageMetas &m) -> const double {
        return m.metersPerUnit.get_value();
      }, [](StageMetas &m, const double v) {
        m.metersPerUnit.set_value(v);
      })
    ;

  py::class_<Stage>(m, "Stage")
      .def(py::init<>())
      .def("metas", [](Stage &s) -> StageMetas & { return s.metas(); })
      .def("GetPrimAtPath",
           [](const Stage &s, const std::string &path_str) -> py::object {
             Path path(path_str, "");

             if (auto p = s.GetPrimAtPath(path)) {
               return py::cast(*p);
             }

             return py::none();
           });
  ;

  m.def("LoadUSDFromFile", &LoadUSDAFromFile);

  py::class_<std::vector<Prim>>(m, "PrimVector")
      .def(py::init<>())
      .def("clear", &std::vector<Prim>::clear)
      .def(
          "append",
          [](std::vector<Prim> &pv, const Prim &prim) { pv.push_back(prim); },
          py::keep_alive<1, 2>())
      .def("__len__", [](const std::vector<Prim> &v) { return v.size(); })
      .def(
          "__iter__",
          [](std::vector<Prim> &v) {
            return py::make_iterator(v.begin(), v.end());
          },
          py::keep_alive<0, 1>());

  // py::class_<tinyusdz::GPrim>(UsdModule, "GPrim");

  // py::class_<tinyusdz::GeomSphere>(m, "Sphere")
  //   .def(py::init<>())
  //;

  // py::class_<tinyusdz::GeomSphere>(m, "Sphere")
  //   .def(py::init<>())
  //   .def

  py::bind_vector<std::vector<int>>(m, "VectorInt");
}
