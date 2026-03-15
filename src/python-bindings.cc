#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>
#include <pybind11/numpy.h>

#include "tiny-format.hh"
#include "tinyusdz.hh"
#include "prim-pprint.hh"
#include "tydra/render-data.hh"
#include "tydra/variant-support.hh"
#include "tydra/variant-converter.hh"
//
#include "value-type-macros.inc"

//
// NOTE:
// - pybind11 does not provide binding for `array.array` module(`py::array` is for numpy type)
//   - so implement dedicated binding for array data through `vector<T>` stl binding.
//   - Converting `numpy`, `array.array` and other Python array/list types must be converted at Python layer, not here(C++ binding).
// - Memory management: TinyUSDZ does not use smart pointer, so use `return_value_policy::reference` or `return_value_policy::reference_internal` as much as posssible.
//   - For methods returning a const pointer(doe not dynamically allocate memory)(e.g. `Stage::GetPrimAtPath`) 
// - Use return_value_policy::reference_internal for a method which returns const/nonconst lvalue reference
//   - e.g. `const StageMeta &Stage::metas() const`, `StageMeta &Stage::metas()`
//   - Use py::def  or def_property with C++ lambdas(since def_readwrite cannot specity C++ method(there may be a solution, but could'nt find example code and hard to understand pybind11 templates))
// -

namespace py = pybind11;



//PYBIND11_MAKE_OPAQUE(std::vector<int>);
PYBIND11_MAKE_OPAQUE(std::vector<tinyusdz::Prim>);

// define custom types
struct float16 {
  uint16_t h;
};

namespace PYBIND11_NAMESPACE { namespace detail {
    template <> struct type_caster<float16> {
    public:
        /**
         * This macro establishes the name 'inty' in
         * function signatures and declares a local variable
         * 'value' of type inty
         */
        PYBIND11_TYPE_CASTER(float16, const_name("float16"));

        /**
         * Conversion part 1 (Python->C++): convert a PyObject into a inty
         * instance or return false upon failure. The second argument
         * indicates whether implicit conversions should be applied.
         */
        bool load(handle src, bool) {
            /* Extract PyObject from handle */
            PyObject *source = src.ptr();
            /* Try converting into a Python integer value */
            PyObject *tmp = PyNumber_Long(source);
            if (!tmp)
                return false;
            /* Now try to convert into a C++ half */
            value.h = uint16_t(PyLong_AsLong(tmp));
            Py_DECREF(tmp);
            /* Ensure return code was OK (to avoid out-of-range errors etc) */
            return true; // !PyErr_Occurred()
        }

        /**
         * Conversion part 2 (C++ -> Python): convert an inty instance into
         * a Python object. The second and third arguments are used to
         * indicate the return value policy and parent object (for
         * ``return_value_policy::reference_internal``) and are generally
         * ignored by implicit casters.
         */
        static handle cast(float16 src, return_value_policy /* policy */, handle /* parent */) {
            return PyLong_FromLong(src.h);
        }
    };
}} // namespace PYBIND11_NAMESPACE::detail


//PYBIND11_NUMPY_DTYPE(float16, h);

// using namespace py::literals;  // to bring in the `_a` literal

static double test_api() {
  return 4.14;
}

class PyTest {
 public:
  std::vector<int> intv;
  const std::vector<int> &intvfun() const {
    return intv;
  }
  std::vector<int> &intvfun() {
    return intv;
  }
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
      .def_readwrite("intv", &PyTest::intv)
      .def_property(
          //"intv", static_cast<const std::vector<int> &(PyTest::*)(void) const>(&PyTest::intvfun)
          // In C++14, We can use py::overload_cast to simplify type cast
          "intv", py::overload_cast<>(&PyTest::intvfun, py::const_)
          , nullptr,
          py::return_value_policy::reference_internal)
      ;

    // TODO: Use attr?
#define SET_VALUE(__ty) \
    .def("set", [](primvar::PrimVar &p, const __ty &v) { p.set_value(v); })

  py::class_<primvar::PrimVar>(m, "PrimVar")
    .def(py::init<>())
    .def_property("dtype", &primvar::PrimVar::type_name, nullptr)
    SET_VALUE(int32_t)
    SET_VALUE(int64_t)
    SET_VALUE(uint32_t)
    SET_VALUE(uint64_t)
    SET_VALUE(double)
    SET_VALUE(float)
    .def("set_obj", [](primvar::PrimVar &p, const py::object &obj) {
      py::print("set_obj", obj);
      //py::buffer_info info = obj.request();
      py::print("buf info", obj.get_type());

      py::object c_float = py::module::import("ctypes").attr("c_float");
      py::print("c_float", c_float.get_type());
      py::print("isnstance(c_float)", py::isinstance(obj, c_float));
      //py::print("val = ", obj.cast<float>());
    
    })
    .def("set_buf", [](primvar::PrimVar &p, const py::buffer &buf) {
      py::print("set_buf", buf);
      py::buffer_info info = buf.request();
      py::print("buf info", info.format);
    })
    .def("set_array", [](primvar::PrimVar &p, const py::array_t<int32_t> v) {
      py::print("set_arr int[]");
    })
    .def("get_array", [](primvar::PrimVar &p) -> py::array_t<float> {
      std::vector<float> v;

      auto result = py::array_t<float>(16);
      
      return result;
    })
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
      //.def_property("primChildren", static_cast<const std::vector<Prim> &(Prim::*)(void)>Prim::children,
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

    // Variant support bindings
    py::class_<tydra::VariantOption>(m_tydra, "VariantOption")
      .def(py::init<>())
      .def_readwrite("name", &tydra::VariantOption::name)
      .def_readwrite("description", &tydra::VariantOption::description)
      .def_readwrite("mesh_ids", &tydra::VariantOption::mesh_ids)
      .def_readwrite("material_ids", &tydra::VariantOption::material_ids)
      .def_readwrite("node_ids", &tydra::VariantOption::node_ids)
      .def_readwrite("animation_ids", &tydra::VariantOption::animation_ids)
      .def_readwrite("property_overrides", &tydra::VariantOption::property_overrides)
    ;

    py::class_<tydra::VariantSet>(m_tydra, "VariantSet")
      .def(py::init<>())
      .def_readwrite("name", &tydra::VariantSet::name)
      .def_readwrite("options", &tydra::VariantSet::options)
      .def_readwrite("default_option_index", &tydra::VariantSet::default_option_index)
      .def_readwrite("parent_prim_id", &tydra::VariantSet::parent_prim_id)
      .def_readwrite("parent_variant_option_name", &tydra::VariantSet::parent_variant_option_name)
    ;

    py::class_<tydra::VariantGroup>(m_tydra, "VariantGroup")
      .def(py::init<>())
      .def_readwrite("prim_path", &tydra::VariantGroup::prim_path)
      .def_readwrite("variant_sets", &tydra::VariantGroup::variant_sets)
      .def_readwrite("affected_node_id", &tydra::VariantGroup::affected_node_id)
      .def_readwrite("secondary_node_ids", &tydra::VariantGroup::secondary_node_ids)
    ;

    py::class_<tydra::VariantSelection>(m_tydra, "VariantSelection")
      .def(py::init<>())
      .def_readwrite("variant_group_id", &tydra::VariantSelection::variant_group_id)
      .def_readwrite("variant_set_id", &tydra::VariantSelection::variant_set_id)
      .def_readwrite("selected_option_index", &tydra::VariantSelection::selected_option_index)
    ;

    py::class_<tydra::VariantConverter>(m_tydra, "VariantConverter")
      .def(py::init<>())
      .def("convert_variants", [](tydra::VariantConverter &converter, const Stage &stage, RenderScene &scene) -> bool {
        std::string err;
        return converter.ConvertVariants(stage, &scene, &err);
      }, py::arg("stage"), py::arg("scene"))
    ;

    // VariantStatistics
    py::class_<tydra::VariantStatistics>(m_tydra, "VariantStatistics")
      .def(py::init<>())
      .def_readwrite("num_variant_groups", &tydra::VariantStatistics::num_variant_groups)
      .def_readwrite("num_variant_sets", &tydra::VariantStatistics::num_variant_sets)
      .def_readwrite("num_variant_options", &tydra::VariantStatistics::num_variant_options)
      .def_readwrite("max_nesting_depth", &tydra::VariantStatistics::max_nesting_depth)
    ;

    py::class_<tydra::DefaultVariantManager>(m_tydra, "DefaultVariantManager")
      .def(py::init<>())
      .def("has_variants", &tydra::DefaultVariantManager::HasVariants)
      .def("find_variant_group", &tydra::DefaultVariantManager::FindVariantGroup,
           py::arg("prim_path"), py::return_value_policy::reference)
      .def("find_variant_set", [](tydra::DefaultVariantManager &mgr, int32_t group_id, const std::string &set_name) -> tydra::VariantSet* {
           return mgr.FindVariantSet(group_id, set_name);
      }, py::arg("group_id"), py::arg("set_name"), py::return_value_policy::reference)
      .def("find_variant_option", [](tydra::DefaultVariantManager &mgr, int32_t group_id, int32_t set_id, const std::string &option_name) -> tydra::VariantOption* {
           return mgr.FindVariantOption(group_id, set_id, option_name);
      }, py::arg("group_id"), py::arg("set_id"), py::arg("option_name"), py::return_value_policy::reference)
      .def("select_variant", [](tydra::DefaultVariantManager &mgr, int32_t group_id, const std::string &set_name, const std::string &option_name) -> bool {
           std::string err;
           return mgr.SelectVariant(group_id, set_name, option_name, &err);
      }, py::arg("group_id"), py::arg("set_name"), py::arg("option_name"))
      .def("select_variant_by_index", [](tydra::DefaultVariantManager &mgr, int32_t group_id, int32_t set_id, int32_t option_index) -> bool {
           std::string err;
           return mgr.SelectVariantByIndex(group_id, set_id, option_index, &err);
      }, py::arg("group_id"), py::arg("set_id"), py::arg("option_index"))
      .def("get_current_selection", &tydra::DefaultVariantManager::GetCurrentSelection,
           py::arg("group_id"), py::return_value_policy::reference)
      .def("get_all_selections", &tydra::DefaultVariantManager::GetAllSelections,
           py::return_value_policy::reference)
      .def("reset_to_defaults", [](tydra::DefaultVariantManager &mgr) -> bool {
           std::string err;
           return mgr.ResetToDefaults(&err);
      })
      .def("get_mutable_variant_groups", &tydra::DefaultVariantManager::GetMutableVariantGroups,
           py::return_value_policy::reference)
      .def("get_variant_groups", &tydra::DefaultVariantManager::GetVariantGroups,
           py::return_value_policy::reference)
      .def("set_variant_groups", &tydra::DefaultVariantManager::SetVariantGroups,
           py::arg("groups"))
      .def("get_statistics", &tydra::DefaultVariantManager::GetStatistics)
    ;
  }
}
