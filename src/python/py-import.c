/* SPDX-License-Identifier: Apache-2.0 */
#include "lightusd-import-c.h"
#include "py-internal.h"

PyObject* lightusd_import_scene(PyObject* module, PyObject* args,
                                PyObject* kwargs) {
  static char* names[] = {"path",     "data",       "source",   "format",
                          "settings", "max_memory", "resolver", NULL};
  const char *path = NULL, *source = "", *format = "auto", *settings = "{}";
  PyObject *data = Py_None, *resolver_obj = Py_None;
  unsigned long long max_memory = 0;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|zOsssKO:_import_scene",
                                   names, &path, &data, &source, &format,
                                   &settings, &max_memory, &resolver_obj))
    return NULL;
  lightusd_state* st = lightusd_state_from_module(module);
  if (!st) return NULL;
  lightusd_import_options opts;
  lightusd_import_options_init(&opts);
  if (strcmp(format, "auto") == 0)
    opts.format = LIGHTUSD_IMPORT_AUTO;
  else if (strcmp(format, "obj") == 0)
    opts.format = LIGHTUSD_IMPORT_OBJ;
  else if (strcmp(format, "urdf") == 0)
    opts.format = LIGHTUSD_IMPORT_URDF;
  else if (strcmp(format, "mjcf") == 0)
    opts.format = LIGHTUSD_IMPORT_MJCF;
  else {
    PyErr_SetString(PyExc_ValueError,
                    "import format must be auto, obj, urdf or mjcf");
    return NULL;
  }
  opts.settings_json = settings;
  if (max_memory) {
    opts.max_memory_bytes = max_memory;
    opts.max_input_bytes = max_memory;
  }
  opts.resolver = lightusd_resolver_handle(st, resolver_obj);
  if (resolver_obj != Py_None && !opts.resolver) return NULL;
  if ((path != NULL) == (data != Py_None)) {
    PyErr_SetString(PyExc_ValueError, "provide exactly one of path or data");
    return NULL;
  }
  Py_buffer view = {0};
  if (data != Py_None && PyObject_GetBuffer(data, &view, PyBUF_CONTIG_RO) != 0)
    return NULL;
  lightusd_stage* stage = NULL;
  lightusd_status status;
  lightusd_resolver_clear_error(resolver_obj);
  Py_BEGIN_ALLOW_THREADS if (path) status =
      lightusd_import_file(path, &opts, &stage);
  else status = lightusd_import_memory((const uint8_t*)view.buf,
                                       (size_t)view.len, source, &opts, &stage);
  Py_END_ALLOW_THREADS if (data != Py_None) PyBuffer_Release(&view);
  if (lightusd_resolver_restore_error(resolver_obj)) {
    if (stage) lightusd_stage_destroy(stage);
    return NULL;
  }
  if (status != LIGHTUSD_OK)
    return lightusd_raise(st, status, path ? path : "import");
  return lightusd_wrap_stage(st, stage);
}
