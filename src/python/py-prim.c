/* SPDX-License-Identifier: Apache-2.0
 * lightusd._core — Prim, Attribute, Relationship, TimeSamples, VariantSets.
 */

#include "py-internal.h"

/* ============================================================
 * Common helpers
 * ============================================================ */

/* Validate prim + stage, returning the C handle (NULL + exception set on
 * failure). */
tusd_stage* tusd_prim_stage_checked(tusd_state* st, TusdPrim* prim) {
  if (!prim->stage) {
    PyErr_SetString(st->UsdError, "Prim has no stage");
    return NULL;
  }
  tusd_stage* stage = tusd_stage_handle(prim->stage);
  if (!stage) {
    PyErr_SetString(st->UsdError, "Stage is closed");
    return NULL;
  }
  uint64_t gen = tusd_stage_generation(stage);
  if (prim->gen != gen) {
    /* The stage was structurally modified: prim-spec pointers may have
     * moved. Re-resolve this prim by its path (self-healing handle). */
    const char* path =
        prim->path_utf8 ? PyBytes_AsString(prim->path_utf8) : NULL;
    tusd_prim fresh;
    memset(&fresh, 0, sizeof(fresh));
    if (path) fresh = tusd_stage_prim_at_path(stage, path);
    if (!tusd_prim_is_valid(fresh)) {
      PyErr_Format(st->StaleHandleError,
                   "Prim '%s' no longer exists on this stage",
                   path ? path : "<unknown>");
      return NULL;
    }
    prim->prim = fresh;
    prim->gen = gen;
  }
  return stage;
}

/* Get the prim's path as a NUL-terminated C string (borrowed from stage
 * storage). */
static const char* prim_path_cstr(TusdPrim* prim) {
  tusd_sv sv = tusd_prim_path(prim->prim);
  return sv.data ? sv.data : "";
}

/* Fetch state + validated stage handle for a Prim-derived object. */
static tusd_state* named_ref_context(TusdNamedRef* ref, TusdPrim** out_prim,
                                     tusd_stage** out_stage) {
  tusd_state* st = tusd_state_from_obj((PyObject*)ref);
  if (!st) return NULL;
  TusdPrim* prim = (TusdPrim*)ref->prim;
  tusd_stage* stage = tusd_prim_stage_checked(st, prim);
  if (!stage) return NULL;
  *out_prim = prim;
  *out_stage = stage;
  return st;
}

static const char* named_ref_name(TusdNamedRef* ref) {
  return PyBytes_AsString(ref->name_utf8);
}

/* Create an Attribute / Relationship / VariantSet instance. */
static PyObject* named_ref_new(tusd_state* st, PyObject* type_obj,
                               PyObject* prim_obj, PyObject* name) {
  (void)st;
  PyObject* name_utf8 = PyUnicode_AsUTF8String(name);
  if (!name_utf8) return NULL;
  PyObject* obj = tusd_alloc(type_obj);
  if (!obj) {
    Py_DECREF(name_utf8);
    return NULL;
  }
  TusdNamedRef* ref = (TusdNamedRef*)obj;
  ref->prim = prim_obj;
  Py_INCREF(prim_obj);
  ref->name = name;
  Py_INCREF(name);
  ref->name_utf8 = name_utf8;
  return obj;
}

static void named_ref_dealloc(PyObject* self) {
  TusdNamedRef* ref = (TusdNamedRef*)self;
  PyTypeObject* tp = Py_TYPE(self);
  Py_CLEAR(ref->prim);
  Py_CLEAR(ref->name);
  Py_CLEAR(ref->name_utf8);
  freefunc free_fn = (freefunc)PyType_GetSlot(tp, Py_tp_free);
  free_fn(self);
  Py_DECREF((PyObject*)tp);
}

/* Parse and dispatch a normalized authoring value.
 * norm is ("pod", type, is_array, bytes, count) | ("str", type, s) |
 * ("tokens", type, tuple). `time_ptr` selects the timesample path. */
static int apply_normalized(tusd_state* st, tusd_stage* stage,
                            const char* path, const char* name,
                            PyObject* norm, const double* time_ptr,
                            uint16_t flags) {
  if (!PyTuple_Check(norm) || PyTuple_Size(norm) < 3) {
    PyErr_SetString(PyExc_TypeError,
                    "value normalizer returned an invalid result");
    return -1;
  }
  PyObject* kind_obj = PyTuple_GetItem(norm, 0);
  PyObject* kind_tmp = NULL;
  const char* kind = tusd_utf8(kind_obj, &kind_tmp);
  if (!kind) return -1;
  long type_l = PyLong_AsLong(PyTuple_GetItem(norm, 1));
  if (type_l < 0 && PyErr_Occurred()) { Py_XDECREF(kind_tmp); return -1; }
  tusd_type type = (tusd_type)type_l;

  tusd_status status;
  if (strcmp(kind, "pod") == 0) {
    if (PyTuple_Size(norm) != 5) {
      PyErr_SetString(PyExc_TypeError, "pod normalization needs 5 items");
      { Py_XDECREF(kind_tmp); return -1; }
    }
    long is_array = PyLong_AsLong(PyTuple_GetItem(norm, 2));
    PyObject* data = PyTuple_GetItem(norm, 3);
    Py_ssize_t count = PyLong_AsSsize_t(PyTuple_GetItem(norm, 4));
    if (PyErr_Occurred()) { Py_XDECREF(kind_tmp); return -1; }
    char* buf = NULL;
    Py_ssize_t nbytes = 0;
    if (PyBytes_AsStringAndSize(data, &buf, &nbytes) != 0) { Py_XDECREF(kind_tmp); return -1; }
    if (time_ptr) {
      status = tusd_attr_set_timesample(stage, path, name, *time_ptr, type,
                                        (uint8_t)is_array, buf,
                                        (size_t)count);
    } else {
      status = tusd_attr_set(stage, path, name, type, (uint8_t)is_array, buf,
                             (size_t)count, flags);
    }
  } else if (strcmp(kind, "str") == 0) {
    PyObject* stmp = NULL;
    const char* s = tusd_utf8(PyTuple_GetItem(norm, 2), &stmp);
    if (!s) { Py_XDECREF(kind_tmp); return -1; }
    if (time_ptr) {
      status = tusd_attr_set_timesample(stage, path, name, *time_ptr, type, 0,
                                        s, 1);
    } else {
      status = tusd_attr_set(stage, path, name, type, 0, s, 1, flags);
    }
    Py_XDECREF(stmp);
  } else if (strcmp(kind, "tokens") == 0) {
    if (time_ptr) {
      PyErr_SetString(PyExc_TypeError,
                      "token arrays cannot be authored as time samples");
      { Py_XDECREF(kind_tmp); return -1; }
    }
    PyObject* items = PyTuple_GetItem(norm, 2);
    if (!PyTuple_Check(items)) {
      PyErr_SetString(PyExc_TypeError, "tokens normalization expects a tuple");
      { Py_XDECREF(kind_tmp); return -1; }
    }
    Py_ssize_t n = PyTuple_Size(items);
    size_t arr_n = (size_t)(n > 0 ? n : 1);
    if (arr_n > SIZE_MAX / sizeof(char*) ||
        arr_n > SIZE_MAX / sizeof(PyObject*)) {
      PyErr_NoMemory();
      { Py_XDECREF(kind_tmp); return -1; }
    }
    const char** arr = (const char**)PyMem_Malloc(
        arr_n * sizeof(char*));
    PyObject** tmps = (PyObject**)PyMem_Malloc(
        arr_n * sizeof(PyObject*));
    if (!arr || !tmps) {
      PyMem_Free(arr);
      PyMem_Free(tmps);
      PyErr_NoMemory();
      { Py_XDECREF(kind_tmp); return -1; }
    }
    Py_ssize_t filled = 0;
    int bad = 0;
    for (Py_ssize_t i = 0; i < n; ++i) {
      arr[i] = tusd_utf8(PyTuple_GetItem(items, i), &tmps[i]);
      if (!arr[i]) {
        bad = 1;
        break;
      }
      filled++;
    }
    if (bad) {
      for (Py_ssize_t i = 0; i < filled; ++i) Py_XDECREF(tmps[i]);
      PyMem_Free(arr);
      PyMem_Free(tmps);
      { Py_XDECREF(kind_tmp); return -1; }
    }
    status = tusd_attr_set_token_array(stage, path, name, type, arr,
                                       (size_t)n, flags);
    for (Py_ssize_t i = 0; i < filled; ++i) Py_XDECREF(tmps[i]);
    PyMem_Free(arr);
    PyMem_Free(tmps);
  } else {
    PyErr_Format(PyExc_TypeError, "unknown normalization kind: %s", kind);
    { Py_XDECREF(kind_tmp); return -1; }
  }

  if (status != TUSD_OK) {
    tusd_raise(st, status, name);
    { Py_XDECREF(kind_tmp); return -1; }
  }
  { Py_XDECREF(kind_tmp); return 0; }
}

/* Normalize `value` through the facade-registered normalizer and author it. */
static int set_value_common(tusd_state* st, tusd_stage* stage,
                            const char* path, const char* name,
                            PyObject* value, PyObject* type_hint,
                            const double* time_ptr, uint16_t flags) {
  if (!st->normalizer) {
    PyErr_SetString(st->UsdError,
                    "no value normalizer registered (import lightusd, not "
                    "lightusd._core directly)");
    return -1;
  }
  PyObject* norm = PyObject_CallFunctionObjArgs(
      st->normalizer, value, type_hint ? type_hint : Py_None, NULL);
  if (!norm) return -1;
  int rc = apply_normalized(st, stage, path, name, norm, time_ptr, flags);
  Py_DECREF(norm);
  return rc;
}

/* ============================================================
 * TimeSamples
 * ============================================================ */

static void TimeSamples_dealloc(PyObject* self) {
  TusdTimeSamples* ts = (TusdTimeSamples*)self;
  PyTypeObject* tp = Py_TYPE(self);
  Py_CLEAR(ts->attr);
  freefunc free_fn = (freefunc)PyType_GetSlot(tp, Py_tp_free);
  free_fn(self);
  Py_DECREF((PyObject*)tp);
}

static tusd_state* timesamples_context(TusdTimeSamples* ts, TusdPrim** prim,
                                       tusd_stage** stage, const char** name) {
  TusdNamedRef* attr = (TusdNamedRef*)ts->attr;
  tusd_state* st = named_ref_context(attr, prim, stage);
  if (!st) return NULL;
  *name = named_ref_name(attr);
  if (!*name) return NULL;
  return st;
}

static Py_ssize_t TimeSamples_length(PyObject* self) {
  TusdPrim* prim;
  tusd_stage* stage;
  const char* name;
  tusd_state* st =
      timesamples_context((TusdTimeSamples*)self, &prim, &stage, &name);
  if (!st) return -1;
  return (Py_ssize_t)tusd_attr_timesample_count(prim->prim, name);
}

static PyObject* TimeSamples_getitem(PyObject* self, Py_ssize_t idx) {
  TusdPrim* prim;
  tusd_stage* stage;
  const char* name;
  tusd_state* st =
      timesamples_context((TusdTimeSamples*)self, &prim, &stage, &name);
  if (!st) return NULL;
  Py_ssize_t n = (Py_ssize_t)tusd_attr_timesample_count(prim->prim, name);
  if (idx < 0) idx += n;
  if (idx < 0 || idx >= n) {
    PyErr_SetString(PyExc_IndexError, "time sample index out of range");
    return NULL;
  }
  double time = 0.0;
  tusd_value_view view;
  tusd_status status =
      tusd_attr_timesample_at(prim->prim, name, (size_t)idx, &time, &view);
  if (status != TUSD_OK) return tusd_raise(st, status, name);

  PyObject* value;
  if (!view.is_array && (view.type == TUSD_TYPE_STRING ||
                         view.type == TUSD_TYPE_TOKEN ||
                         view.type == TUSD_TYPE_ASSET_PATH)) {
    /* Rare: string-valued samples; go through interpolate-held for an owned
     * copy. */
    tusd_value* owned = NULL;
    status = tusd_attr_interpolate(prim->prim, name, time, 0, &owned);
    if (status != TUSD_OK) return tusd_raise(st, status, name);
    value = tusd_value_to_python(st, owned);
  } else {
    value = tusd_view_to_python(st, prim->stage, NULL, &view);
  }
  if (!value) return NULL;
  return Py_BuildValue("(dN)", time, value);
}

static PyObject* TimeSamples_get_times(PyObject* self, void* closure) {
  (void)closure;
  TusdPrim* prim;
  tusd_stage* stage;
  const char* name;
  tusd_state* st =
      timesamples_context((TusdTimeSamples*)self, &prim, &stage, &name);
  if (!st) return NULL;
  size_t n = tusd_attr_timesample_count(prim->prim, name);
  size_t times_n = n ? n : 1;
  if (times_n > SIZE_MAX / sizeof(double)) return PyErr_NoMemory();
  double* times = (double*)PyMem_Malloc(times_n * sizeof(double));
  if (!times) return PyErr_NoMemory();
  tusd_attr_timesample_times(prim->prim, name, times, n);
  PyObject* tup = PyTuple_New((Py_ssize_t)n);
  if (!tup) {
    PyMem_Free(times);
    return NULL;
  }
  for (size_t i = 0; i < n; ++i) {
    PyObject* f = PyFloat_FromDouble(times[i]);
    if (!f) {
      PyMem_Free(times);
      Py_DECREF(tup);
      return NULL;
    }
    PyTuple_SetItem(tup, (Py_ssize_t)i, f);
  }
  PyMem_Free(times);
  return tup;
}

static PyObject* TimeSamples_repr(PyObject* self) {
  Py_ssize_t n = TimeSamples_length(self);
  if (n < 0) {
    PyErr_Clear();
    n = 0;
  }
  return PyUnicode_FromFormat("TimeSamples(count=%zd)", n);
}

static PyGetSetDef TimeSamples_getset[] = {
    {"times", TimeSamples_get_times, NULL, "Sample times (tuple of float).",
     NULL},
    {NULL, NULL, NULL, NULL, NULL},
};

static PyType_Slot TimeSamples_slots[] = {
    {Py_tp_dealloc, (void*)TimeSamples_dealloc},
    {Py_tp_repr, (void*)TimeSamples_repr},
    {Py_sq_length, (void*)TimeSamples_length},
    {Py_sq_item, (void*)TimeSamples_getitem},
    {Py_tp_getset, (void*)TimeSamples_getset},
    {0, NULL},
};

static PyType_Spec TimeSamples_spec = {
    .name = "lightusd._core.TimeSamples",
    .basicsize = sizeof(TusdTimeSamples),
    .flags = Py_TPFLAGS_DEFAULT
#ifdef Py_TPFLAGS_IMMUTABLETYPE
             | Py_TPFLAGS_IMMUTABLETYPE
#endif
    ,
    .slots = TimeSamples_slots,
};

/* ============================================================
 * Attribute
 * ============================================================ */

static PyObject* Attr_get_name(PyObject* self, void* closure) {
  (void)closure;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  Py_INCREF(ref->name);
  return ref->name;
}

static PyObject* Attr_get_type_name(PyObject* self, void* closure) {
  (void)closure;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = named_ref_context(ref, &prim, &stage);
  if (!st) return NULL;
  const char* name = named_ref_name(ref);
  if (!name) return NULL;
  tusd_sv sv = tusd_prim_property_type_name(prim->prim, name);
  if (sv.len > 0) return tusd_sv_to_str(sv);
  /* Fall back to the stored value's type. */
  tusd_value_view view;
  if (tusd_attr_get(prim->prim, name, &view) == TUSD_OK) {
    const char* tn = tusd_type_name(view.type);
    if (tn[0]) {
      if (view.is_array) {
        return PyUnicode_FromFormat("%s[]", tn);
      }
      return PyUnicode_FromString(tn);
    }
  }
  PyErr_Clear();
  return PyUnicode_FromString("");
}

static uint16_t attr_flags(TusdNamedRef* ref) {
  TusdPrim* prim;
  tusd_stage* stage;
  if (!named_ref_context(ref, &prim, &stage)) {
    PyErr_Clear();
    return 0;
  }
  const char* name = named_ref_name(ref);
  if (!name) return 0;
  return tusd_prim_property_flags(prim->prim, name);
}

static PyObject* Attr_get_flag(PyObject* self, void* closure) {
  uint16_t mask = (uint16_t)(uintptr_t)closure;
  return PyBool_FromLong((attr_flags((TusdNamedRef*)self) & mask) != 0);
}

static PyObject* Attr_get_has_timesamples(PyObject* self, void* closure) {
  (void)closure;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  if (!named_ref_context(ref, &prim, &stage)) return NULL;
  const char* name = named_ref_name(ref);
  if (!name) return NULL;
  return PyBool_FromLong(tusd_attr_has_timesamples(prim->prim, name));
}

static PyObject* Attr_get_connections(PyObject* self, void* closure) {
  (void)closure;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  if (!named_ref_context(ref, &prim, &stage)) return NULL;
  const char* name = named_ref_name(ref);
  if (!name) return NULL;
  size_t n = tusd_attr_connection_count(prim->prim, name);
  PyObject* tup = PyTuple_New((Py_ssize_t)n);
  if (!tup) return NULL;
  for (size_t i = 0; i < n; ++i) {
    PyObject* s = tusd_sv_to_str(tusd_attr_connection(prim->prim, name, i));
    if (!s) {
      Py_DECREF(tup);
      return NULL;
    }
    PyTuple_SetItem(tup, (Py_ssize_t)i, s);
  }
  return tup;
}

static PyObject* Attr_get_timesamples(PyObject* self, void* closure) {
  (void)closure;
  tusd_state* st = tusd_state_from_obj(self);
  if (!st) return NULL;
  PyObject* obj = tusd_alloc(st->TimeSamplesType);
  if (!obj) return NULL;
  ((TusdTimeSamples*)obj)->attr = self;
  Py_INCREF(self);
  return obj;
}

static PyObject* Attr_get_interpolation(PyObject* self, void* closure) {
  (void)closure;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  if (!named_ref_context(ref, &prim, &stage)) return NULL;
  const char* name = named_ref_name(ref);
  if (!name) return NULL;
  tusd_value* val = NULL;
  if (tusd_attr_metadata(prim->prim, name, "interpolation", &val) != TUSD_OK) {
    Py_RETURN_NONE;
  }
  tusd_state* st = tusd_state_from_obj(self);
  return tusd_value_to_python(st, val);
}

static PyObject* Attr_metadata(PyObject* self, PyObject* args) {
  const char* key;
  if (!PyArg_ParseTuple(args, "s:metadata", &key)) return NULL;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = named_ref_context(ref, &prim, &stage);
  if (!st) return NULL;
  const char* name = named_ref_name(ref);
  if (!name) return NULL;
  tusd_value* val = NULL;
  tusd_status status = tusd_attr_metadata(prim->prim, name, key, &val);
  if (status == TUSD_ERR_NOT_FOUND) Py_RETURN_NONE;
  if (status != TUSD_OK) return tusd_raise(st, status, key);
  return tusd_value_to_python(st, val);
}

static PyObject* Attr_get_custom_data(PyObject* self, void* closure) {
  (void)closure;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = named_ref_context(ref, &prim, &stage);
  if (!st) return NULL;
  const char* name = named_ref_name(ref);
  if (!name) return NULL;
  tusd_dict_ref dict;
  if (tusd_attr_custom_data(prim->prim, name, &dict) != TUSD_OK) {
    dict._dict = NULL;
  }
  return tusd_dict_to_python(st, dict);
}

static PyObject* Attr_get_impl(PyObject* self, PyObject* time_obj,
                               int interp_linear) {
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = named_ref_context(ref, &prim, &stage);
  if (!st) return NULL;
  const char* name = named_ref_name(ref);
  if (!name) return NULL;

  if (time_obj && time_obj != Py_None) {
    double time = PyFloat_AsDouble(time_obj);
    if (time == -1.0 && PyErr_Occurred()) return NULL;
    tusd_value* val = NULL;
    tusd_status status = tusd_attr_interpolate(prim->prim, name, time,
                                               interp_linear ? 1 : 0, &val);
    if (status == TUSD_ERR_NOT_FOUND) {
      /* No samples: fall back to the default value. */
      return tusd_attr_value_to_python(st, prim->stage, prim->prim, name);
    }
    if (status != TUSD_OK) return tusd_raise(st, status, name);
    return tusd_value_to_python(st, val);
  }

  PyObject* value =
      tusd_attr_value_to_python(st, prim->stage, prim->prim, name);
  if (value) return value;
  /* No default: fall back to the earliest time sample. */
  if (PyErr_ExceptionMatches(PyExc_KeyError) &&
      tusd_attr_has_timesamples(prim->prim, name)) {
    PyErr_Clear();
    double t0 = 0.0;
    tusd_attr_timesample_times(prim->prim, name, &t0, 1);
    tusd_value* val = NULL;
    tusd_status status = tusd_attr_interpolate(prim->prim, name, t0, 0, &val);
    if (status != TUSD_OK) return tusd_raise(st, status, name);
    return tusd_value_to_python(st, val);
  }
  return NULL;
}

static PyObject* Attr_get(PyObject* self, PyObject* args, PyObject* kwargs) {
  static char* kwlist[] = {"time", "interpolation", NULL};
  PyObject* time_obj = Py_None;
  const char* interp = "linear";
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|Os:get", kwlist, &time_obj,
                                   &interp)) {
    return NULL;
  }
  return Attr_get_impl(self, time_obj, strcmp(interp, "held") != 0);
}

static PyObject* Attr_eval(PyObject* self, PyObject* args, PyObject* kwargs) {
  static char* kwlist[] = {"time", NULL};
  double time = 0.0;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|d:eval", kwlist, &time)) {
    return NULL;
  }
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = named_ref_context(ref, &prim, &stage);
  if (!st) return NULL;
  const char* name = named_ref_name(ref);
  if (!name) return NULL;
  tusd_value* val = NULL;
  tusd_status status = tusd_attr_eval(stage, prim->prim, name, time, &val);
  if (status != TUSD_OK) return tusd_raise(st, status, name);
  return tusd_value_to_python(st, val);
}

static PyObject* Attr_set(PyObject* self, PyObject* args, PyObject* kwargs) {
  static char* kwlist[] = {"value", "type", "time", NULL};
  PyObject* value;
  PyObject* type_hint = Py_None;
  PyObject* time_obj = Py_None;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|OO:set", kwlist, &value,
                                   &type_hint, &time_obj)) {
    return NULL;
  }
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = named_ref_context(ref, &prim, &stage);
  if (!st) return NULL;
  const char* name = named_ref_name(ref);
  if (!name) return NULL;

  double time = 0.0;
  const double* time_ptr = NULL;
  if (time_obj != Py_None) {
    time = PyFloat_AsDouble(time_obj);
    if (time == -1.0 && PyErr_Occurred()) return NULL;
    time_ptr = &time;
  }
  if (set_value_common(st, stage, prim_path_cstr(prim), name, value,
                       type_hint == Py_None ? NULL : type_hint, time_ptr,
                       0) != 0) {
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* Attr_block(PyObject* self, PyObject* noargs) {
  (void)noargs;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = named_ref_context(ref, &prim, &stage);
  if (!st) return NULL;
  const char* name = named_ref_name(ref);
  if (!name) return NULL;
  tusd_status status = tusd_attr_block(stage, prim_path_cstr(prim), name);
  if (status != TUSD_OK) return tusd_raise(st, status, name);
  Py_RETURN_NONE;
}

static PyObject* Attr_connect(PyObject* self, PyObject* args) {
  const char* target;
  if (!PyArg_ParseTuple(args, "s:connect", &target)) return NULL;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = named_ref_context(ref, &prim, &stage);
  if (!st) return NULL;
  const char* name = named_ref_name(ref);
  if (!name) return NULL;
  tusd_status status =
      tusd_attr_add_connection(stage, prim_path_cstr(prim), name, target);
  if (status != TUSD_OK) return tusd_raise(st, status, name);
  Py_RETURN_NONE;
}

static PyObject* Attr_set_metadata(PyObject* self, PyObject* args) {
  const char* key;
  PyObject* value;
  if (!PyArg_ParseTuple(args, "sO:set_metadata", &key, &value)) return NULL;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = named_ref_context(ref, &prim, &stage);
  if (!st) return NULL;
  const char* name = named_ref_name(ref);
  if (!name) return NULL;

  tusd_status status;
  if (PyBool_Check(value)) {
    uint8_t b = value == Py_True ? 1 : 0;
    status = tusd_attr_set_metadata(stage, prim_path_cstr(prim), name, key,
                                    TUSD_TYPE_BOOL, &b, 1);
  } else if (PyLong_Check(value)) {
    long v = PyLong_AsLong(value);
    if (PyErr_Occurred()) return NULL;
    if (v < INT32_MIN || v > INT32_MAX) {
      PyErr_SetString(PyExc_OverflowError, "value exceeds int32 range for metadata");
      return NULL;
    }
    int32_t i = (int32_t)v;
    status = tusd_attr_set_metadata(stage, prim_path_cstr(prim), name, key,
                                    TUSD_TYPE_INT, &i, 1);
  } else if (PyFloat_Check(value)) {
    double d = PyFloat_AsDouble(value);
    status = tusd_attr_set_metadata(stage, prim_path_cstr(prim), name, key,
                                    TUSD_TYPE_DOUBLE, &d, 1);
  } else if (PyUnicode_Check(value)) {
    PyObject* tmp = NULL;
    const char* s = tusd_utf8(value, &tmp);
    if (!s) return NULL;
    status = tusd_attr_set_metadata(stage, prim_path_cstr(prim), name, key,
                                    TUSD_TYPE_TOKEN, s, 1);
    Py_XDECREF(tmp);
  } else {
    PyErr_SetString(PyExc_TypeError,
                    "metadata value must be bool, int, float or str");
    return NULL;
  }
  if (status != TUSD_OK) return tusd_raise(st, status, key);
  Py_RETURN_NONE;
}

static PyObject* Attr_repr(PyObject* self) {
  TusdNamedRef* ref = (TusdNamedRef*)self;
  PyObject* tn = Attr_get_type_name(self, NULL);
  if (!tn) {
    PyErr_Clear();
    tn = PyUnicode_FromString("?");
    if (!tn) return NULL;
  }
  PyObject* res =
      PyUnicode_FromFormat("Attribute(%R, type='%U')", ref->name, tn);
  Py_DECREF(tn);
  return res;
}

static PyMethodDef Attr_methods[] = {
    {"get", (PyCFunction)(void (*)(void))Attr_get,
     METH_VARARGS | METH_KEYWORDS,
     "get(time=None, interpolation='linear') -> value\n"
     "Default value, or the (interpolated) value at `time`."},
    {"eval", (PyCFunction)(void (*)(void))Attr_eval,
     METH_VARARGS | METH_KEYWORDS,
     "eval(time=0.0) -> value\n"
     "Evaluate following connections."},
    {"set", (PyCFunction)(void (*)(void))Attr_set,
     METH_VARARGS | METH_KEYWORDS,
     "set(value, *, type=None, time=None)\nAuthor the value."},
    {"block", Attr_block, METH_NOARGS, "Author a value block (`= None`)."},
    {"connect", Attr_connect, METH_VARARGS,
     "connect(target_path)\nAdd a connection target."},
    {"metadata", Attr_metadata, METH_VARARGS,
     "metadata(key) -> value | None"},
    {"set_metadata", Attr_set_metadata, METH_VARARGS,
     "set_metadata(key, value)"},
    {NULL, NULL, 0, NULL},
};

static PyGetSetDef Attr_getset[] = {
    {"name", Attr_get_name, NULL, "Property name.", NULL},
    {"type_name", Attr_get_type_name, NULL, "Declared USD type name.", NULL},
    {"is_array", Attr_get_flag, NULL, "Array-valued.",
     (void*)(uintptr_t)TUSD_PROP_ARRAY},
    {"is_custom", Attr_get_flag, NULL, "Declared `custom`.",
     (void*)(uintptr_t)TUSD_PROP_CUSTOM},
    {"is_uniform", Attr_get_flag, NULL, "Declared `uniform`.",
     (void*)(uintptr_t)TUSD_PROP_UNIFORM},
    {"is_connection", Attr_get_flag, NULL, "Has connection(s).",
     (void*)(uintptr_t)TUSD_PROP_CONNECTION},
    {"has_timesamples", Attr_get_has_timesamples, NULL,
     "True if time samples are authored.", NULL},
    {"connections", Attr_get_connections, NULL,
     "Connection target paths (tuple of str).", NULL},
    {"timesamples", Attr_get_timesamples, NULL, "TimeSamples view.", NULL},
    {"interpolation", Attr_get_interpolation, NULL,
     "Authored interpolation token or None.", NULL},
    {"custom_data", Attr_get_custom_data, NULL,
     "customData dictionary (copied).", NULL},
    {NULL, NULL, NULL, NULL, NULL},
};

static PyType_Slot Attr_slots[] = {
    {Py_tp_dealloc, (void*)named_ref_dealloc},
    {Py_tp_repr, (void*)Attr_repr},
    {Py_tp_methods, (void*)Attr_methods},
    {Py_tp_getset, (void*)Attr_getset},
    {0, NULL},
};

static PyType_Spec Attr_spec = {
    .name = "lightusd._core.Attribute",
    .basicsize = sizeof(TusdNamedRef),
    .flags = Py_TPFLAGS_DEFAULT
#ifdef Py_TPFLAGS_IMMUTABLETYPE
             | Py_TPFLAGS_IMMUTABLETYPE
#endif
    ,
    .slots = Attr_slots,
};

/* ============================================================
 * Relationship
 * ============================================================ */

static PyObject* Rel_get_name(PyObject* self, void* closure) {
  (void)closure;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  Py_INCREF(ref->name);
  return ref->name;
}

static PyObject* Rel_get_targets(PyObject* self, void* closure) {
  (void)closure;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  if (!named_ref_context(ref, &prim, &stage)) return NULL;
  const char* name = named_ref_name(ref);
  if (!name) return NULL;
  size_t n = tusd_rel_target_count(prim->prim, name);
  PyObject* tup = PyTuple_New((Py_ssize_t)n);
  if (!tup) return NULL;
  for (size_t i = 0; i < n; ++i) {
    PyObject* s = tusd_sv_to_str(tusd_rel_target(prim->prim, name, i));
    if (!s) {
      Py_DECREF(tup);
      return NULL;
    }
    PyTuple_SetItem(tup, (Py_ssize_t)i, s);
  }
  return tup;
}

static Py_ssize_t Rel_length(PyObject* self) {
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  if (!named_ref_context(ref, &prim, &stage)) return -1;
  const char* name = named_ref_name(ref);
  if (!name) return -1;
  return (Py_ssize_t)tusd_rel_target_count(prim->prim, name);
}

static PyObject* Rel_getitem(PyObject* self, Py_ssize_t idx) {
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  if (!named_ref_context(ref, &prim, &stage)) return NULL;
  const char* name = named_ref_name(ref);
  if (!name) return NULL;
  Py_ssize_t n = (Py_ssize_t)tusd_rel_target_count(prim->prim, name);
  if (idx < 0) idx += n;
  if (idx < 0 || idx >= n) {
    PyErr_SetString(PyExc_IndexError, "relationship target out of range");
    return NULL;
  }
  return tusd_sv_to_str(tusd_rel_target(prim->prim, name, (size_t)idx));
}

static PyObject* Rel_add_target(PyObject* self, PyObject* args) {
  const char* target;
  if (!PyArg_ParseTuple(args, "s:add_target", &target)) return NULL;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = named_ref_context(ref, &prim, &stage);
  if (!st) return NULL;
  const char* name = named_ref_name(ref);
  if (!name) return NULL;
  tusd_status status =
      tusd_rel_add_target(stage, prim_path_cstr(prim), name, target);
  if (status != TUSD_OK) return tusd_raise(st, status, name);
  Py_RETURN_NONE;
}

static PyObject* Rel_set_targets(PyObject* self, PyObject* args) {
  PyObject* seq;
  if (!PyArg_ParseTuple(args, "O:set_targets", &seq)) return NULL;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = named_ref_context(ref, &prim, &stage);
  if (!st) return NULL;
  const char* name = named_ref_name(ref);
  if (!name) return NULL;

  PyObject* fast = PySequence_Fast(seq, "targets must be a sequence of str");
  if (!fast) return NULL;
  Py_ssize_t n = PySequence_Size(fast);
  size_t arr_n = (size_t)(n > 0 ? n : 1);
  if (arr_n > SIZE_MAX / sizeof(char*) ||
      arr_n > SIZE_MAX / sizeof(PyObject*)) {
    Py_DECREF(fast);
    return PyErr_NoMemory();
  }
  const char** arr =
      (const char**)PyMem_Malloc(arr_n * sizeof(char*));
  PyObject** tmps =
      (PyObject**)PyMem_Malloc(arr_n * sizeof(PyObject*));
  if (!arr || !tmps) {
    PyMem_Free(arr);
    PyMem_Free(tmps);
    Py_DECREF(fast);
    return PyErr_NoMemory();
  }
  Py_ssize_t filled = 0;
  int bad = 0;
  for (Py_ssize_t i = 0; i < n; ++i) {
    PyObject* item = PySequence_GetItem(fast, i);
    if (!item) {
      bad = 1;
      break;
    }
    arr[i] = tusd_utf8(item, &tmps[i]);
    Py_DECREF(item);
    if (!arr[i]) {
      bad = 1;
      break;
    }
    filled++;
  }
  tusd_status status = TUSD_OK;
  if (!bad) {
    status = tusd_rel_set_targets(stage, prim_path_cstr(prim), name, arr,
                                  (size_t)n);
  }
  for (Py_ssize_t i = 0; i < filled; ++i) Py_XDECREF(tmps[i]);
  PyMem_Free(arr);
  PyMem_Free(tmps);
  Py_DECREF(fast);
  if (bad) return NULL;
  if (status != TUSD_OK) return tusd_raise(st, status, name);
  Py_RETURN_NONE;
}

static PyObject* Rel_remove(PyObject* self, PyObject* noargs) {
  (void)noargs;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = named_ref_context(ref, &prim, &stage);
  if (!st) return NULL;
  const char* name = named_ref_name(ref);
  if (!name) return NULL;
  tusd_status status = tusd_rel_remove(stage, prim_path_cstr(prim), name);
  if (status != TUSD_OK) return tusd_raise(st, status, name);
  Py_RETURN_NONE;
}

static PyObject* Rel_repr(PyObject* self) {
  TusdNamedRef* ref = (TusdNamedRef*)self;
  Py_ssize_t n = Rel_length(self);
  if (n < 0) {
    PyErr_Clear();
    n = 0;
  }
  return PyUnicode_FromFormat("Relationship(%R, targets=%zd)", ref->name, n);
}

static PyMethodDef Rel_methods[] = {
    {"add_target", Rel_add_target, METH_VARARGS, "Append a target path."},
    {"set_targets", Rel_set_targets, METH_VARARGS,
     "Replace all target paths."},
    {"remove", Rel_remove, METH_NOARGS, "Remove this relationship."},
    {NULL, NULL, 0, NULL},
};

static PyGetSetDef Rel_getset[] = {
    {"name", Rel_get_name, NULL, "Relationship name.", NULL},
    {"targets", Rel_get_targets, NULL, "Target paths (tuple of str).", NULL},
    {NULL, NULL, NULL, NULL, NULL},
};

static PyType_Slot Rel_slots[] = {
    {Py_tp_dealloc, (void*)named_ref_dealloc},
    {Py_tp_repr, (void*)Rel_repr},
    {Py_sq_length, (void*)Rel_length},
    {Py_sq_item, (void*)Rel_getitem},
    {Py_tp_methods, (void*)Rel_methods},
    {Py_tp_getset, (void*)Rel_getset},
    {0, NULL},
};

static PyType_Spec Rel_spec = {
    .name = "lightusd._core.Relationship",
    .basicsize = sizeof(TusdNamedRef),
    .flags = Py_TPFLAGS_DEFAULT
#ifdef Py_TPFLAGS_IMMUTABLETYPE
             | Py_TPFLAGS_IMMUTABLETYPE
#endif
    ,
    .slots = Rel_slots,
};

/* ============================================================
 * VariantSet / VariantSets
 * ============================================================ */

static PyObject* VSet_get_name(PyObject* self, void* closure) {
  (void)closure;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  Py_INCREF(ref->name);
  return ref->name;
}

static PyObject* VSet_get_names(PyObject* self, void* closure) {
  (void)closure;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  if (!named_ref_context(ref, &prim, &stage)) return NULL;
  const char* set_name = named_ref_name(ref);
  if (!set_name) return NULL;
  size_t n = tusd_variant_count(prim->prim, set_name);
  PyObject* tup = PyTuple_New((Py_ssize_t)n);
  if (!tup) return NULL;
  for (size_t i = 0; i < n; ++i) {
    PyObject* s = tusd_sv_to_str(tusd_variant_name(prim->prim, set_name, i));
    if (!s) {
      Py_DECREF(tup);
      return NULL;
    }
    PyTuple_SetItem(tup, (Py_ssize_t)i, s);
  }
  return tup;
}

static PyObject* VSet_get_selection(PyObject* self, void* closure) {
  (void)closure;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  if (!named_ref_context(ref, &prim, &stage)) return NULL;
  const char* set_name = named_ref_name(ref);
  if (!set_name) return NULL;
  tusd_sv sv = tusd_variant_selection(prim->prim, set_name);
  if (sv.len == 0) Py_RETURN_NONE;
  return tusd_sv_to_str(sv);
}

static int VSet_set_selection(PyObject* self, PyObject* value, void* closure) {
  (void)closure;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = named_ref_context(ref, &prim, &stage);
  if (!st) return -1;
  const char* set_name = named_ref_name(ref);
  if (!set_name) return -1;
  const char* variant = "";
  PyObject* tmp = NULL;
  if (value && value != Py_None) {
    variant = tusd_utf8(value, &tmp);
    if (!variant) return -1;
  }
  tusd_status status = tusd_prim_set_variant_selection(
      stage, prim_path_cstr(prim), set_name, variant);
  Py_XDECREF(tmp);
  if (status != TUSD_OK) {
    tusd_raise(st, status, set_name);
    return -1;
  }
  return 0;
}

static PyObject* VSet_add_variant(PyObject* self, PyObject* args) {
  const char* variant;
  if (!PyArg_ParseTuple(args, "s:add_variant", &variant)) return NULL;
  TusdNamedRef* ref = (TusdNamedRef*)self;
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = named_ref_context(ref, &prim, &stage);
  if (!st) return NULL;
  const char* set_name = named_ref_name(ref);
  if (!set_name) return NULL;
  tusd_status status = tusd_prim_add_variant(stage, prim_path_cstr(prim),
                                             set_name, variant);
  if (status != TUSD_OK) return tusd_raise(st, status, set_name);
  Py_RETURN_NONE;
}

static PyObject* VSet_repr(PyObject* self) {
  TusdNamedRef* ref = (TusdNamedRef*)self;
  return PyUnicode_FromFormat("VariantSet(%R)", ref->name);
}

static PyMethodDef VSet_methods[] = {
    {"add_variant", VSet_add_variant, METH_VARARGS,
     "Add a variant option to this set."},
    {NULL, NULL, 0, NULL},
};

static PyGetSetDef VSet_getset[] = {
    {"name", VSet_get_name, NULL, "Variant set name.", NULL},
    {"names", VSet_get_names, NULL, "Variant option names (tuple).", NULL},
    {"selection", VSet_get_selection, VSet_set_selection,
     "Selected variant name (or None). Assignable.", NULL},
    {NULL, NULL, NULL, NULL, NULL},
};

static PyType_Slot VSet_slots[] = {
    {Py_tp_dealloc, (void*)named_ref_dealloc},
    {Py_tp_repr, (void*)VSet_repr},
    {Py_tp_methods, (void*)VSet_methods},
    {Py_tp_getset, (void*)VSet_getset},
    {0, NULL},
};

static PyType_Spec VSet_spec = {
    .name = "lightusd._core.VariantSet",
    .basicsize = sizeof(TusdNamedRef),
    .flags = Py_TPFLAGS_DEFAULT
#ifdef Py_TPFLAGS_IMMUTABLETYPE
             | Py_TPFLAGS_IMMUTABLETYPE
#endif
    ,
    .slots = VSet_slots,
};

/* VariantSets: mapping of set name -> VariantSet */

static void VSets_dealloc(PyObject* self) {
  TusdPrimRef* r = (TusdPrimRef*)self;
  PyTypeObject* tp = Py_TYPE(self);
  Py_CLEAR(r->prim);
  freefunc free_fn = (freefunc)PyType_GetSlot(tp, Py_tp_free);
  free_fn(self);
  Py_DECREF((PyObject*)tp);
}

static PyObject* VSets_names(PyObject* self) {
  TusdPrimRef* r = (TusdPrimRef*)self;
  tusd_state* st = tusd_state_from_obj(self);
  if (!st) return NULL;
  TusdPrim* prim = (TusdPrim*)r->prim;
  if (!tusd_prim_stage_checked(st, prim)) return NULL;
  size_t n = tusd_prim_variant_set_count(prim->prim);
  PyObject* tup = PyTuple_New((Py_ssize_t)n);
  if (!tup) return NULL;
  for (size_t i = 0; i < n; ++i) {
    PyObject* s = tusd_sv_to_str(tusd_prim_variant_set_name(prim->prim, i));
    if (!s) {
      Py_DECREF(tup);
      return NULL;
    }
    PyTuple_SetItem(tup, (Py_ssize_t)i, s);
  }
  return tup;
}

static Py_ssize_t VSets_length(PyObject* self) {
  TusdPrimRef* r = (TusdPrimRef*)self;
  tusd_state* st = tusd_state_from_obj(self);
  if (!st) return -1;
  TusdPrim* prim = (TusdPrim*)r->prim;
  if (!tusd_prim_stage_checked(st, prim)) return -1;
  return (Py_ssize_t)tusd_prim_variant_set_count(prim->prim);
}

static PyObject* VSets_subscript(PyObject* self, PyObject* key) {
  tusd_state* st = tusd_state_from_obj(self);
  if (!st) return NULL;
  TusdPrimRef* r = (TusdPrimRef*)self;
  TusdPrim* prim = (TusdPrim*)r->prim;
  if (!tusd_prim_stage_checked(st, prim)) return NULL;
  PyObject* tmp = NULL;
  const char* set_name = tusd_utf8(key, &tmp);
  if (!set_name) return NULL;
  /* Validate existence. */
  size_t n = tusd_prim_variant_set_count(prim->prim);
  int found = 0;
  for (size_t i = 0; i < n; ++i) {
    tusd_sv sv = tusd_prim_variant_set_name(prim->prim, i);
    if (sv.len == strlen(set_name) &&
        strncmp(sv.data, set_name, sv.len) == 0) {
      found = 1;
      break;
    }
  }
  Py_XDECREF(tmp);
  if (!found) {
    PyErr_SetObject(PyExc_KeyError, key);
    return NULL;
  }
  return named_ref_new(st, st->VariantSetType, r->prim, key);
}

static PyObject* VSets_iter(PyObject* self) {
  PyObject* names = VSets_names(self);
  if (!names) return NULL;
  PyObject* it = PyObject_GetIter(names);
  Py_DECREF(names);
  return it;
}

static int VSets_contains(PyObject* self, PyObject* key) {
  TusdPrimRef* r = (TusdPrimRef*)self;
  tusd_state* st = tusd_state_from_obj(self);
  if (!st) return -1;
  TusdPrim* prim = (TusdPrim*)r->prim;
  if (!tusd_prim_stage_checked(st, prim)) return -1;
  PyObject* tmp = NULL;
  const char* set_name = tusd_utf8(key, &tmp);
  if (!set_name) return -1;
  size_t n = tusd_prim_variant_set_count(prim->prim);
  int found = 0;
  for (size_t i = 0; i < n; ++i) {
    tusd_sv sv = tusd_prim_variant_set_name(prim->prim, i);
    if (sv.len == strlen(set_name) &&
        strncmp(sv.data, set_name, sv.len) == 0) {
      found = 1;
      break;
    }
  }
  Py_XDECREF(tmp);
  return found;
}

static PyObject* VSets_names_method(PyObject* self, PyObject* noargs) {
  (void)noargs;
  return VSets_names(self);
}

static PyObject* VSets_repr(PyObject* self) {
  PyObject* names = VSets_names(self);
  if (!names) return NULL;
  PyObject* res = PyUnicode_FromFormat("VariantSets(%R)", names);
  Py_DECREF(names);
  return res;
}

static PyMethodDef VSets_methods[] = {
    {"names", VSets_names_method, METH_NOARGS, "Variant set names (tuple)."},
    {NULL, NULL, 0, NULL},
};

static PyType_Slot VSets_slots[] = {
    {Py_tp_dealloc, (void*)VSets_dealloc},
    {Py_tp_repr, (void*)VSets_repr},
    {Py_mp_length, (void*)VSets_length},
    {Py_mp_subscript, (void*)VSets_subscript},
    {Py_sq_contains, (void*)VSets_contains},
    {Py_tp_iter, (void*)VSets_iter},
    {Py_tp_methods, (void*)VSets_methods},
    {0, NULL},
};

static PyType_Spec VSets_spec = {
    .name = "lightusd._core.VariantSets",
    .basicsize = sizeof(TusdPrimRef),
    .flags = Py_TPFLAGS_DEFAULT
#ifdef Py_TPFLAGS_IMMUTABLETYPE
             | Py_TPFLAGS_IMMUTABLETYPE
#endif
    ,
    .slots = VSets_slots,
};

/* ============================================================
 * Prim child iterator
 * ============================================================ */

static void ChildIter_dealloc(PyObject* self) {
  TusdChildIter* it = (TusdChildIter*)self;
  PyTypeObject* tp = Py_TYPE(self);
  Py_CLEAR(it->prim);
  freefunc free_fn = (freefunc)PyType_GetSlot(tp, Py_tp_free);
  free_fn(self);
  Py_DECREF((PyObject*)tp);
}

static PyObject* ChildIter_next(PyObject* self) {
  TusdChildIter* it = (TusdChildIter*)self;
  tusd_state* st = tusd_state_from_obj(self);
  if (!st) return NULL;
  TusdPrim* prim = (TusdPrim*)it->prim;
  tusd_stage* stage = tusd_prim_stage_checked(st, prim);
  if (!stage) return NULL;
  size_t n = tusd_prim_child_count(prim->prim);
  if ((size_t)it->index >= n) {
    return NULL; /* StopIteration */
  }
  tusd_prim child = tusd_prim_child(prim->prim, (size_t)it->index);
  it->index++;
  if (!tusd_prim_is_valid(child)) {
    return NULL;
  }
  return tusd_wrap_prim(st, prim->stage, child);
}

static PyObject* ChildIter_iter(PyObject* self) {
  Py_INCREF(self);
  return self;
}

static PyType_Slot ChildIter_slots[] = {
    {Py_tp_dealloc, (void*)ChildIter_dealloc},
    {Py_tp_iter, (void*)ChildIter_iter},
    {Py_tp_iternext, (void*)ChildIter_next},
    {0, NULL},
};

static PyType_Spec ChildIter_spec = {
    .name = "lightusd._core._PrimChildIterator",
    .basicsize = sizeof(TusdChildIter),
    .flags = Py_TPFLAGS_DEFAULT
#ifdef Py_TPFLAGS_IMMUTABLETYPE
             | Py_TPFLAGS_IMMUTABLETYPE
#endif
    ,
    .slots = ChildIter_slots,
};

/* ============================================================
 * Prim
 * ============================================================ */

static void Prim_dealloc(PyObject* self) {
  TusdPrim* p = (TusdPrim*)self;
  PyTypeObject* tp = Py_TYPE(self);
  Py_CLEAR(p->stage);
  Py_CLEAR(p->path_utf8);
  freefunc free_fn = (freefunc)PyType_GetSlot(tp, Py_tp_free);
  free_fn(self);
  Py_DECREF((PyObject*)tp);
}

PyObject* tusd_wrap_prim(tusd_state* st, PyObject* stage_obj, tusd_prim prim) {
  if (!tusd_prim_is_valid(prim)) {
    Py_RETURN_NONE;
  }
  tusd_sv path = tusd_prim_path(prim);
  PyObject* path_utf8 =
      PyBytes_FromStringAndSize(path.data ? path.data : "",
                                (Py_ssize_t)path.len);
  if (!path_utf8) return NULL;
  PyObject* obj = tusd_alloc(st->PrimType);
  if (!obj) {
    Py_DECREF(path_utf8);
    return NULL;
  }
  TusdPrim* p = (TusdPrim*)obj;
  p->stage = stage_obj;
  Py_INCREF(stage_obj);
  p->path_utf8 = path_utf8;
  p->prim = prim;
  p->gen = tusd_stage_generation(tusd_stage_handle(stage_obj));
  return obj;
}

static tusd_state* prim_context(PyObject* self, TusdPrim** out,
                                tusd_stage** stage_out) {
  tusd_state* st = tusd_state_from_obj(self);
  if (!st) return NULL;
  TusdPrim* prim = (TusdPrim*)self;
  tusd_stage* stage = tusd_prim_stage_checked(st, prim);
  if (!stage) return NULL;
  *out = prim;
  if (stage_out) *stage_out = stage;
  return st;
}

static PyObject* Prim_get_sv(PyObject* self, void* closure) {
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return NULL;
  switch ((uintptr_t)closure) {
    case 0:
      return tusd_sv_to_str(tusd_prim_name(prim->prim));
    case 1:
      return tusd_sv_to_str(tusd_prim_path(prim->prim));
    case 2:
      return tusd_sv_to_str(tusd_prim_type_name(prim->prim));
    default:
      Py_RETURN_NONE;
  }
}

static PyObject* Prim_get_specifier(PyObject* self, void* closure) {
  (void)closure;
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return NULL;
  static const char* names[] = {"def", "over", "class"};
  uint8_t s = tusd_prim_specifier(prim->prim);
  return PyUnicode_FromString(s <= 2 ? names[s] : "def");
}

static PyObject* Prim_get_active(PyObject* self, void* closure) {
  (void)closure;
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return NULL;
  return PyBool_FromLong(tusd_prim_is_active(prim->prim));
}

static PyObject* Prim_get_kind(PyObject* self, void* closure) {
  (void)closure;
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return NULL;
  tusd_sv sv = tusd_prim_kind(prim->prim);
  if (sv.len == 0) Py_RETURN_NONE;
  return tusd_sv_to_str(sv);
}

static PyObject* Prim_get_parent(PyObject* self, void* closure) {
  (void)closure;
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return NULL;
  return tusd_wrap_prim(st, prim->stage, tusd_prim_parent(prim->prim));
}

static PyObject* Prim_get_children(PyObject* self, void* closure) {
  (void)closure;
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return NULL;
  size_t n = tusd_prim_child_count(prim->prim);
  PyObject* tup = PyTuple_New((Py_ssize_t)n);
  if (!tup) return NULL;
  for (size_t i = 0; i < n; ++i) {
    PyObject* child =
        tusd_wrap_prim(st, prim->stage, tusd_prim_child(prim->prim, i));
    if (!child) {
      Py_DECREF(tup);
      return NULL;
    }
    PyTuple_SetItem(tup, (Py_ssize_t)i, child);
  }
  return tup;
}

/* closure: 0 = attributes (non-relationship properties), 1 = relationships */
static PyObject* Prim_get_prop_names(PyObject* self, void* closure) {
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return NULL;
  const int want_rel = (uintptr_t)closure == 1;

  if (want_rel) {
    tusd_strlist* names = NULL;
    if (tusd_prim_relationship_names(prim->prim, &names) != TUSD_OK) {
      return tusd_raise(st, TUSD_ERR_INTERNAL, "relationship names");
    }
    size_t n = tusd_strlist_size(names);
    PyObject* tup = PyTuple_New((Py_ssize_t)n);
    if (!tup) {
      tusd_strlist_destroy(names);
      return NULL;
    }
    for (size_t i = 0; i < n; ++i) {
      PyObject* s = tusd_sv_to_str(tusd_strlist_get(names, i));
      if (!s) {
        Py_DECREF(tup);
        tusd_strlist_destroy(names);
        return NULL;
      }
      PyTuple_SetItem(tup, (Py_ssize_t)i, s);
    }
    tusd_strlist_destroy(names);
    return tup;
  }

  size_t total = tusd_prim_property_count(prim->prim);
  PyObject* list = PyList_New(0);
  if (!list) return NULL;
  for (size_t i = 0; i < total; ++i) {
    uint16_t flags = tusd_prim_property_flags_at(prim->prim, i);
    if (flags & TUSD_PROP_RELATIONSHIP) continue;
    PyObject* s = tusd_sv_to_str(tusd_prim_property_name(prim->prim, i));
    if (!s || PyList_Append(list, s) != 0) {
      Py_XDECREF(s);
      Py_DECREF(list);
      return NULL;
    }
    Py_DECREF(s);
  }
  PyObject* tup = PyList_AsTuple(list);
  Py_DECREF(list);
  return tup;
}

static PyObject* Prim_get_variant_sets(PyObject* self, void* closure) {
  (void)closure;
  tusd_state* st = tusd_state_from_obj(self);
  if (!st) return NULL;
  PyObject* obj = tusd_alloc(st->VariantSetsType);
  if (!obj) return NULL;
  ((TusdPrimRef*)obj)->prim = self;
  Py_INCREF(self);
  return obj;
}

static PyObject* Prim_get_custom_data(PyObject* self, void* closure) {
  (void)closure;
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return NULL;
  tusd_dict_ref dict;
  if (tusd_prim_custom_data(prim->prim, &dict) != TUSD_OK) dict._dict = NULL;
  return tusd_dict_to_python(st, dict);
}

static PyObject* Prim_get_asset_info(PyObject* self, void* closure) {
  (void)closure;
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return NULL;
  tusd_dict_ref dict;
  if (tusd_prim_asset_info(prim->prim, &dict) != TUSD_OK) dict._dict = NULL;
  return tusd_dict_to_python(st, dict);
}

static Py_ssize_t Prim_length(PyObject* self) {
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return -1;
  return (Py_ssize_t)tusd_prim_child_count(prim->prim);
}

static int Prim_contains(PyObject* self, PyObject* key) {
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return -1;
  PyObject* tmp = NULL;
  const char* name = tusd_utf8(key, &tmp);
  if (!name) return -1;
  int has = tusd_prim_has_property(prim->prim, name) ? 1 : 0;
  Py_XDECREF(tmp);
  return has;
}

static PyObject* Prim_subscript(PyObject* self, PyObject* key) {
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return NULL;
  PyObject* tmp = NULL;
  const char* name = tusd_utf8(key, &tmp);
  if (!name) return NULL;
  PyObject* res = tusd_attr_value_to_python(st, prim->stage, prim->prim, name);
  Py_XDECREF(tmp);
  return res;
}

static PyObject* Prim_iter(PyObject* self) {
  tusd_state* st = tusd_state_from_obj(self);
  if (!st) return NULL;
  PyObject* obj = tusd_alloc(st->PrimChildIterType);
  if (!obj) return NULL;
  TusdChildIter* it = (TusdChildIter*)obj;
  it->prim = self;
  Py_INCREF(self);
  it->index = 0;
  return obj;
}

static PyObject* Prim_get_method(PyObject* self, PyObject* args,
                                 PyObject* kwargs) {
  static char* kwlist[] = {"name", "default", "time", NULL};
  const char* name;
  PyObject* def = Py_None;
  PyObject* time_obj = Py_None;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|OO:get", kwlist, &name,
                                   &def, &time_obj)) {
    return NULL;
  }
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return NULL;

  PyObject* value = NULL;
  if (time_obj != Py_None) {
    double time = PyFloat_AsDouble(time_obj);
    if (time == -1.0 && PyErr_Occurred()) return NULL;
    tusd_value* val = NULL;
    tusd_status status = tusd_attr_interpolate(prim->prim, name, time, 1,
                                               &val);
    if (status == TUSD_OK) {
      return tusd_value_to_python(st, val);
    }
    /* fall through to the default value */
  }
  value = tusd_attr_value_to_python(st, prim->stage, prim->prim, name);
  if (value) return value;
  if (PyErr_ExceptionMatches(PyExc_KeyError)) {
    PyErr_Clear();
    Py_INCREF(def);
    return def;
  }
  return NULL;
}

static PyObject* Prim_attribute(PyObject* self, PyObject* args) {
  PyObject* name;
  if (!PyArg_ParseTuple(args, "U:attribute", &name)) return NULL;
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return NULL;
  PyObject* tmp = NULL;
  const char* cname = tusd_utf8(name, &tmp);
  if (!cname) return NULL;
  int ok = tusd_prim_has_property(prim->prim, cname) ||
           tusd_attr_has_timesamples(prim->prim, cname);
  Py_XDECREF(tmp);
  if (!ok) {
    PyErr_SetObject(PyExc_KeyError, name);
    return NULL;
  }
  return named_ref_new(st, st->AttributeType, self, name);
}

static PyObject* Prim_relationship(PyObject* self, PyObject* args) {
  PyObject* name;
  if (!PyArg_ParseTuple(args, "U:relationship", &name)) return NULL;
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return NULL;
  PyObject* tmp = NULL;
  const char* cname = tusd_utf8(name, &tmp);
  if (!cname) return NULL;
  int ok = tusd_prim_has_relationship(prim->prim, cname);
  Py_XDECREF(tmp);
  if (!ok) {
    PyErr_SetObject(PyExc_KeyError, name);
    return NULL;
  }
  return named_ref_new(st, st->RelationshipType, self, name);
}

static PyObject* Prim_child(PyObject* self, PyObject* args) {
  PyObject* name;
  if (!PyArg_ParseTuple(args, "U:child", &name)) return NULL;
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return NULL;
  PyObject* tmp = NULL;
  const char* cname = tusd_utf8(name, &tmp);
  if (!cname) return NULL;
  tusd_prim child = tusd_prim_child_by_name(prim->prim, cname);
  Py_XDECREF(tmp);
  if (!tusd_prim_is_valid(child)) {
    PyErr_SetObject(PyExc_KeyError, name);
    return NULL;
  }
  return tusd_wrap_prim(st, prim->stage, child);
}

static PyObject* Prim_metadata(PyObject* self, PyObject* args) {
  const char* key;
  if (!PyArg_ParseTuple(args, "s:metadata", &key)) return NULL;
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return NULL;
  tusd_value* val = NULL;
  tusd_status status = tusd_prim_get_metadata(prim->prim, key, &val);
  if (status == TUSD_ERR_NOT_FOUND) Py_RETURN_NONE;
  if (status != TUSD_OK) return tusd_raise(st, status, key);
  return tusd_value_to_python(st, val);
}

static PyObject* matrix16_to_python(const double m[16]) {
  PyObject* rows = PyTuple_New(4);
  if (!rows) return NULL;
  for (int r = 0; r < 4; ++r) {
    PyObject* row = Py_BuildValue("(dddd)", m[r * 4 + 0], m[r * 4 + 1],
                                  m[r * 4 + 2], m[r * 4 + 3]);
    if (!row) {
      Py_DECREF(rows);
      return NULL;
    }
    PyTuple_SetItem(rows, r, row);
  }
  return rows;
}

static PyObject* Prim_local_transform(PyObject* self, PyObject* args,
                                      PyObject* kwargs) {
  static char* kwlist[] = {"time", NULL};
  double time = 0.0;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|d:local_transform", kwlist,
                                   &time)) {
    return NULL;
  }
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) return NULL;
  double m[16];
  tusd_status status = tusd_prim_local_transform(prim->prim, time, m);
  if (status != TUSD_OK) return tusd_raise(st, status, "local_transform");
  return matrix16_to_python(m);
}

static PyObject* Prim_world_transform(PyObject* self, PyObject* args,
                                      PyObject* kwargs) {
  static char* kwlist[] = {"time", NULL};
  double time = 0.0;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|d:world_transform", kwlist,
                                   &time)) {
    return NULL;
  }
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = prim_context(self, &prim, &stage);
  if (!st) return NULL;
  double m[16];
  tusd_status status =
      tusd_prim_world_transform(stage, prim->prim, time, m);
  if (status != TUSD_OK) return tusd_raise(st, status, "world_transform");
  return matrix16_to_python(m);
}

/* ---- authoring ---- */

static PyObject* Prim_set(PyObject* self, PyObject* args, PyObject* kwargs) {
  static char* kwlist[] = {"name",    "value", "type",
                           "time",    "uniform", "custom", NULL};
  const char* name;
  PyObject* value;
  PyObject* type_hint = Py_None;
  PyObject* time_obj = Py_None;
  int uniform = 0;
  int custom = 0;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sO|OOpp:set", kwlist, &name,
                                   &value, &type_hint, &time_obj, &uniform,
                                   &custom)) {
    return NULL;
  }
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = prim_context(self, &prim, &stage);
  if (!st) return NULL;

  double time = 0.0;
  const double* time_ptr = NULL;
  if (time_obj != Py_None) {
    time = PyFloat_AsDouble(time_obj);
    if (time == -1.0 && PyErr_Occurred()) return NULL;
    time_ptr = &time;
  }
  uint16_t flags = 0;
  if (uniform) flags |= TUSD_PROP_UNIFORM;
  if (custom) flags |= TUSD_PROP_CUSTOM;

  if (set_value_common(st, stage, prim_path_cstr(prim), name, value,
                       type_hint == Py_None ? NULL : type_hint, time_ptr,
                       flags) != 0) {
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* Prim_set_metadata(PyObject* self, PyObject* args) {
  const char* key;
  PyObject* value;
  if (!PyArg_ParseTuple(args, "sO:set_metadata", &key, &value)) return NULL;
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = prim_context(self, &prim, &stage);
  if (!st) return NULL;
  const char* path = prim_path_cstr(prim);

  tusd_status status;
  if (PyBool_Check(value)) {
    uint8_t b = value == Py_True ? 1 : 0;
    status =
        tusd_prim_set_metadata(stage, path, key, TUSD_TYPE_BOOL, &b, 1);
  } else if (PyUnicode_Check(value)) {
    PyObject* tmp = NULL;
    const char* s = tusd_utf8(value, &tmp);
    if (!s) return NULL;
    status = tusd_prim_set_metadata(stage, path, key, TUSD_TYPE_TOKEN, s, 1);
    Py_XDECREF(tmp);
  } else if (PyList_Check(value) || PyTuple_Check(value)) {
    PyErr_SetString(PyExc_TypeError,
                    "token-array prim metadata is not supported here yet");
    return NULL;
  } else {
    PyErr_SetString(PyExc_TypeError, "metadata value must be bool or str");
    return NULL;
  }
  if (status != TUSD_OK) return tusd_raise(st, status, key);
  Py_RETURN_NONE;
}

/* closure-driven arc adders: 0=reference 1=payload 2=inherit 3=specialize */
static PyObject* Prim_add_arc_impl(PyObject* self, PyObject* args,
                                   PyObject* kwargs, uint8_t arc_type,
                                   int asset_form) {
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = prim_context(self, &prim, &stage);
  if (!st) return NULL;

  const char* asset = NULL;
  const char* target = NULL;
  if (asset_form) {
    static char* kwlist[] = {"asset_path", "prim_path", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|z", kwlist, &asset,
                                     &target)) {
      return NULL;
    }
  } else {
    static char* kwlist[] = {"prim_path", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s", kwlist, &target)) {
      return NULL;
    }
  }
  tusd_status status = tusd_prim_add_arc(stage, prim_path_cstr(prim),
                                         arc_type, asset, target);
  if (status != TUSD_OK) return tusd_raise(st, status, "add arc");
  Py_RETURN_NONE;
}

static PyObject* Prim_add_reference(PyObject* self, PyObject* args,
                                    PyObject* kwargs) {
  return Prim_add_arc_impl(self, args, kwargs, 0, 1);
}
static PyObject* Prim_add_payload(PyObject* self, PyObject* args,
                                  PyObject* kwargs) {
  return Prim_add_arc_impl(self, args, kwargs, 1, 1);
}
static PyObject* Prim_add_inherit(PyObject* self, PyObject* args,
                                  PyObject* kwargs) {
  return Prim_add_arc_impl(self, args, kwargs, 2, 0);
}
static PyObject* Prim_add_specialize(PyObject* self, PyObject* args,
                                     PyObject* kwargs) {
  return Prim_add_arc_impl(self, args, kwargs, 3, 0);
}

static PyObject* Prim_add_relationship(PyObject* self, PyObject* args) {
  const char* name;
  PyObject* targets;
  if (!PyArg_ParseTuple(args, "sO:add_relationship", &name, &targets)) {
    return NULL;
  }
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = prim_context(self, &prim, &stage);
  if (!st) return NULL;

  PyObject* fast =
      PySequence_Fast(targets, "targets must be a sequence of str");
  if (!fast) return NULL;
  Py_ssize_t n = PySequence_Size(fast);
  tusd_status status = TUSD_OK;
  for (Py_ssize_t i = 0; i < n && status == TUSD_OK; ++i) {
    PyObject* item = PySequence_GetItem(fast, i);
    if (!item) {
      Py_DECREF(fast);
      return NULL;
    }
    PyObject* tmp = NULL;
    const char* t = tusd_utf8(item, &tmp);
    Py_DECREF(item);
    if (!t) {
      Py_DECREF(fast);
      return NULL;
    }
    status = tusd_rel_add_target(stage, prim_path_cstr(prim), name, t);
    Py_XDECREF(tmp);
  }
  Py_DECREF(fast);
  if (status != TUSD_OK) return tusd_raise(st, status, name);
  Py_RETURN_NONE;
}

static PyObject* Prim_add_variant_set(PyObject* self, PyObject* args) {
  const char* set_name;
  if (!PyArg_ParseTuple(args, "s:add_variant_set", &set_name)) return NULL;
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = prim_context(self, &prim, &stage);
  if (!st) return NULL;
  tusd_status status =
      tusd_prim_add_variant_set(stage, prim_path_cstr(prim), set_name);
  if (status != TUSD_OK) return tusd_raise(st, status, set_name);
  Py_RETURN_NONE;
}

static PyObject* Prim_remove_property(PyObject* self, PyObject* args) {
  const char* name;
  if (!PyArg_ParseTuple(args, "s:remove_property", &name)) return NULL;
  TusdPrim* prim;
  tusd_stage* stage;
  tusd_state* st = prim_context(self, &prim, &stage);
  if (!st) return NULL;
  tusd_status status = tusd_attr_remove(stage, prim_path_cstr(prim), name);
  if (status != TUSD_OK) return tusd_raise(st, status, name);
  Py_RETURN_NONE;
}

static PyObject* Prim_repr(PyObject* self) {
  TusdPrim* prim;
  tusd_state* st = prim_context(self, &prim, NULL);
  if (!st) {
    PyErr_Clear();
    return PyUnicode_FromString("Prim(<stale>)");
  }
  tusd_sv path = tusd_prim_path(prim->prim);
  tusd_sv type = tusd_prim_type_name(prim->prim);
  size_t n = tusd_prim_child_count(prim->prim);
  return PyUnicode_FromFormat("Prim('%.*s', type='%.*s', children=%zu)",
                              (int)path.len, path.data ? path.data : "",
                              (int)type.len, type.data ? type.data : "",
                              n);
}

static PyMethodDef Prim_methods[] = {
    {"get", (PyCFunction)(void (*)(void))Prim_get_method,
     METH_VARARGS | METH_KEYWORDS,
     "get(name, default=None, *, time=None) -> value"},
    {"attribute", Prim_attribute, METH_VARARGS,
     "attribute(name) -> Attribute (KeyError if absent)"},
    {"relationship", Prim_relationship, METH_VARARGS,
     "relationship(name) -> Relationship (KeyError if absent)"},
    {"child", Prim_child, METH_VARARGS,
     "child(name) -> Prim (KeyError if absent)"},
    {"metadata", Prim_metadata, METH_VARARGS,
     "metadata(key) -> value | None"},
    {"local_transform",
     (PyCFunction)(void (*)(void))Prim_local_transform,
     METH_VARARGS | METH_KEYWORDS,
     "local_transform(time=0.0) -> 4x4 nested tuple (row-major)"},
    {"world_transform",
     (PyCFunction)(void (*)(void))Prim_world_transform,
     METH_VARARGS | METH_KEYWORDS,
     "world_transform(time=0.0) -> 4x4 nested tuple (row-major)"},
    /* authoring */
    {"set", (PyCFunction)(void (*)(void))Prim_set,
     METH_VARARGS | METH_KEYWORDS,
     "set(name, value, *, type=None, time=None, uniform=False, "
     "custom=False)\nAuthor an attribute value."},
    {"set_metadata", Prim_set_metadata, METH_VARARGS,
     "set_metadata(key, value)"},
    {"add_reference", (PyCFunction)(void (*)(void))Prim_add_reference,
     METH_VARARGS | METH_KEYWORDS,
     "add_reference(asset_path, prim_path=None)"},
    {"add_payload", (PyCFunction)(void (*)(void))Prim_add_payload,
     METH_VARARGS | METH_KEYWORDS,
     "add_payload(asset_path, prim_path=None)"},
    {"add_inherit", (PyCFunction)(void (*)(void))Prim_add_inherit,
     METH_VARARGS | METH_KEYWORDS, "add_inherit(prim_path)"},
    {"add_specialize", (PyCFunction)(void (*)(void))Prim_add_specialize,
     METH_VARARGS | METH_KEYWORDS, "add_specialize(prim_path)"},
    {"add_relationship", Prim_add_relationship, METH_VARARGS,
     "add_relationship(name, targets)"},
    {"add_variant_set", Prim_add_variant_set, METH_VARARGS,
     "add_variant_set(name)"},
    {"remove_property", Prim_remove_property, METH_VARARGS,
     "remove_property(name)"},
    {NULL, NULL, 0, NULL},
};

static PyGetSetDef Prim_getset[] = {
    {"name", Prim_get_sv, NULL, "Prim name.", (void*)(uintptr_t)0},
    {"path", Prim_get_sv, NULL, "Absolute prim path.", (void*)(uintptr_t)1},
    {"type_name", Prim_get_sv, NULL, "Prim type name (may be empty).",
     (void*)(uintptr_t)2},
    {"specifier", Prim_get_specifier, NULL, "'def' | 'over' | 'class'.",
     NULL},
    {"active", Prim_get_active, NULL, "Active flag.", NULL},
    {"kind", Prim_get_kind, NULL, "Model kind or None.", NULL},
    {"parent", Prim_get_parent, NULL, "Parent Prim or None.", NULL},
    {"children", Prim_get_children, NULL, "Child prims (tuple).", NULL},
    {"attributes", Prim_get_prop_names, NULL,
     "Attribute names (tuple, excludes relationships).",
     (void*)(uintptr_t)0},
    {"relationships", Prim_get_prop_names, NULL,
     "Relationship names (tuple).", (void*)(uintptr_t)1},
    {"variant_sets", Prim_get_variant_sets, NULL, "VariantSets view.", NULL},
    {"custom_data", Prim_get_custom_data, NULL,
     "customData dictionary (copied).", NULL},
    {"asset_info", Prim_get_asset_info, NULL,
     "assetInfo dictionary (copied).", NULL},
    {NULL, NULL, NULL, NULL, NULL},
};

static PyType_Slot Prim_slots[] = {
    {Py_tp_dealloc, (void*)Prim_dealloc},
    {Py_tp_repr, (void*)Prim_repr},
    {Py_tp_iter, (void*)Prim_iter},
    {Py_sq_length, (void*)Prim_length},
    {Py_sq_contains, (void*)Prim_contains},
    {Py_mp_length, (void*)Prim_length},
    {Py_mp_subscript, (void*)Prim_subscript},
    {Py_tp_methods, (void*)Prim_methods},
    {Py_tp_getset, (void*)Prim_getset},
    {0, NULL},
};

static PyType_Spec Prim_spec = {
    .name = "lightusd._core.Prim",
    .basicsize = sizeof(TusdPrim),
    .flags = Py_TPFLAGS_DEFAULT
#ifdef Py_TPFLAGS_IMMUTABLETYPE
             | Py_TPFLAGS_IMMUTABLETYPE
#endif
    ,
    .slots = Prim_slots,
};

/* ============================================================
 * Registration
 * ============================================================ */

int tusd_register_prim_types(PyObject* module, tusd_state* st) {
  st->PrimType = PyType_FromModuleAndSpec(module, &Prim_spec, NULL);
  if (!st->PrimType) return -1;
  if (PyModule_AddObjectRef(module, "Prim", st->PrimType) < 0) return -1;

  st->PrimChildIterType =
      PyType_FromModuleAndSpec(module, &ChildIter_spec, NULL);
  if (!st->PrimChildIterType) return -1;

  st->AttributeType = PyType_FromModuleAndSpec(module, &Attr_spec, NULL);
  if (!st->AttributeType) return -1;
  if (PyModule_AddObjectRef(module, "Attribute", st->AttributeType) < 0) {
    return -1;
  }

  st->RelationshipType = PyType_FromModuleAndSpec(module, &Rel_spec, NULL);
  if (!st->RelationshipType) return -1;
  if (PyModule_AddObjectRef(module, "Relationship", st->RelationshipType) <
      0) {
    return -1;
  }

  st->TimeSamplesType =
      PyType_FromModuleAndSpec(module, &TimeSamples_spec, NULL);
  if (!st->TimeSamplesType) return -1;
  if (PyModule_AddObjectRef(module, "TimeSamples", st->TimeSamplesType) < 0) {
    return -1;
  }

  st->VariantSetType = PyType_FromModuleAndSpec(module, &VSet_spec, NULL);
  if (!st->VariantSetType) return -1;
  if (PyModule_AddObjectRef(module, "VariantSet", st->VariantSetType) < 0) {
    return -1;
  }

  st->VariantSetsType = PyType_FromModuleAndSpec(module, &VSets_spec, NULL);
  if (!st->VariantSetsType) return -1;
  if (PyModule_AddObjectRef(module, "VariantSets", st->VariantSetsType) < 0) {
    return -1;
  }
  return 0;
}
