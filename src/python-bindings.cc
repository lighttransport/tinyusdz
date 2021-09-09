#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "prim-types.hh"

namespace py = pybind11;

using namespace pybind11::literals;  // to bring in the `_a` literal

static double test_api() {
  // TODO: Implement
  return 4.14;
}

PYBIND11_MODULE(pytinyusd, m) {
  m.doc() = "TinyUSD Python binding";

  m.def("test_api", &test_api, "Test API");

  py::class_<tinyusdz::GeomSphere>(m, "Sphere")

  py::class_<tinyusdz::GeomSphere>(m, "Sphere")
    .def(py::init<>())
    .def
}
