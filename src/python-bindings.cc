#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include "nonstd/optional.hpp"
#include "tiny-format.hh"
#include "tinyusdz.hh"
#include "prim-pprint.hh"
#include "tydra/render-data.hh"

//
// NOTE:
// - Memory management: TinyUSDZ does not use smart pointer, so use `return_value_policy::reference` or `return_value_policy::reference_internal` as much as posssible.
//   - For methods returning a const pointer(doe not dynamically allocate memory)(e.g. `Stage::GetPrimAtPath`) 
// - Use return_value_policy::reference_internal for a method which returns const/nonconst lvalue reference
//   - e.g. `const StageMeta &Stage::metas() const`, `StageMeta &Stage::metas()`
//   - Use py::def  or def_property with C++ lambdas(since def_readwrite cannot specity C++ method(there may be a solution, but could'nt find example code and hard to understand pybind11 templates))
// -

namespace py = pybind11;

PYBIND11_MAKE_OPAQUE(std::vector<int>);
PYBIND11_MAKE_OPAQUE(std::vector<tinyusdz::Prim>);

// using namespace py::literals;  // to bring in the `_a` literal

static double test_api() {
  return 4.14;
}

class PyTest {
 public:
  std::vector<int> intv;
};

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
      .def(py::init([](const std::string &prim_name) {
        return Prim(Model());
       }))
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
      .def("__str__", [](const Prim &p) {
        return to_string(p);       
      })
      ;

  py::class_<StageMetas>(m, "StageMetas")
    .def(py::init<>())
    .def_property("metersPerUnit", 
      [](const StageMetas &m) -> const double {
        py::print("metersPerUnit get");
        return m.metersPerUnit.get_value();
      }, [](StageMetas &m, const double v) {
        py::print("metersPerUnit set");
        m.metersPerUnit.set_value(v);
        py::print("metersPerUnit ", m.metersPerUnit.get_value());
      }, py::return_value_policy::reference_internal)
    ;

  py::class_<Stage>(m, "Stage")
      .def(py::init<>())
      // Use rvp::reference for lvalue C++ reference.
      .def("metas", [](Stage &s) -> StageMetas & {
        py::print("metas method"); return s.metas(); }, py::return_value_policy::reference)
      .def("commit", &Stage::commit)
      .def(
          "root_prims",
          [](Stage &stage) -> std::vector<Prim> & { return stage.root_prims(); },
          py::return_value_policy::reference)
      .def("GetPrimAtPath",
           [](const Stage &s, const std::string &path_str) -> py::object {
             Path path(path_str, "");

             if (auto p = s.GetPrimAtPath(path)) {
               return py::cast(*p);
             }

             return py::none();
           })
      .def("ExportToString", &Stage::ExportToString)
      .def("dump_prim_tree", &Stage::dump_prim_tree)
      .def("find_prim_by_prim_id",
           [](Stage &s, uint64_t prim_id) -> py::object {

             Prim *prim{nullptr};
             if (auto p = s.find_prim_by_prim_id(prim_id, prim)) {
               return py::cast(prim);
             }

             return py::none();
           }, py::return_value_policy::reference);
  

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

  // Tydra
  {
    auto m_tydra = m.def_submodule("tydra");

    py::class_<tydra::RenderSceneConverterConfig>(m_tydra, "RenderSceneConverterConfig")
      .def(py::init<>())
      .def_readwrite("load_texture_assets", &tydra::RenderSceneConverterConfig::load_texture_assets)
    ;

    m_tydra.def("to_render_scene", [](const Stage &stage) {
      py::print("TODO");
    }, py::arg("config") = tydra::RenderSceneConverterConfig());
  }
}
