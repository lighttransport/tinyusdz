/* SPDX-License-Identifier: Apache-2.0
 * tinyusdz._core — Array type (zero-copy view) and value conversion helpers.
 */

#include "py-internal.h"

/* ============================================================
 * Small helpers
 * ============================================================ */

static float half_to_float(uint16_t h) {
  uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
  uint32_t exp = (h >> 10) & 0x1Fu;
  uint32_t mant = h & 0x3FFu;
  uint32_t bits;
  if (exp == 0) {
    if (mant == 0) {
      bits = sign;
    } else {
      /* subnormal */
      exp = 127 - 15 + 1;
      while ((mant & 0x400u) == 0) {
        mant <<= 1;
        exp--;
      }
      mant &= 0x3FFu;
      bits = sign | (exp << 23) | (mant << 13);
    }
  } else if (exp == 31) {
    bits = sign | 0x7F800000u | (mant << 13);
  } else {
    bits = sign | ((exp - 15 + 127) << 23) | (mant << 13);
  }
  float f;
  memcpy(&f, &bits, sizeof(f));
  return f;
}

typedef struct {
  char format;
  const char* typestr;
  Py_ssize_t itemsize;
} comp_info;

static int comp_info_for(uint8_t storage, comp_info* out) {
  switch (storage) {
    case TUSD_COMP_UINT8:
      *out = (comp_info){'B', "|u1", 1};
      return 0;
    case TUSD_COMP_INT32:
      *out = (comp_info){'i', "<i4", 4};
      return 0;
    case TUSD_COMP_UINT32:
      *out = (comp_info){'I', "<u4", 4};
      return 0;
    case TUSD_COMP_INT64:
      *out = (comp_info){'q', "<i8", 8};
      return 0;
    case TUSD_COMP_UINT64:
      *out = (comp_info){'Q', "<u8", 8};
      return 0;
    case TUSD_COMP_FLOAT16:
      *out = (comp_info){'e', "<f2", 2};
      return 0;
    case TUSD_COMP_FLOAT32:
      *out = (comp_info){'f', "<f4", 4};
      return 0;
    case TUSD_COMP_FLOAT64:
      *out = (comp_info){'d', "<f8", 8};
      return 0;
    case TUSD_COMP_UINT16:
      *out = (comp_info){'H', "<u2", 2};
      return 0;
    case TUSD_COMP_INT16:
      *out = (comp_info){'h', "<i2", 2};
      return 0;
    default:
      return -1;
  }
}

/* Read component `i` of the buffer as a Python scalar. */
static PyObject* component_to_python(const void* data, uint8_t storage,
                                     size_t i, int as_bool) {
  switch (storage) {
    case TUSD_COMP_UINT8: {
      uint8_t v = ((const uint8_t*)data)[i];
      if (as_bool) return PyBool_FromLong(v != 0);
      return PyLong_FromLong(v);
    }
    case TUSD_COMP_INT32:
      return PyLong_FromLong(((const int32_t*)data)[i]);
    case TUSD_COMP_UINT32:
      return PyLong_FromUnsignedLong(((const uint32_t*)data)[i]);
    case TUSD_COMP_INT64:
      return PyLong_FromLongLong(((const int64_t*)data)[i]);
    case TUSD_COMP_UINT64:
      return PyLong_FromUnsignedLongLong(((const uint64_t*)data)[i]);
    case TUSD_COMP_FLOAT16:
      return PyFloat_FromDouble(
          (double)half_to_float(((const uint16_t*)data)[i]));
    case TUSD_COMP_FLOAT32:
      return PyFloat_FromDouble((double)((const float*)data)[i]);
    case TUSD_COMP_FLOAT64:
      return PyFloat_FromDouble(((const double*)data)[i]);
    case TUSD_COMP_UINT16:
      return PyLong_FromLong(((const uint16_t*)data)[i]);
    case TUSD_COMP_INT16:
      return PyLong_FromLong(((const int16_t*)data)[i]);
    default:
      PyErr_SetString(PyExc_TypeError, "unsupported component storage");
      return NULL;
  }
}

/* Build a Python object for element `idx` of a view (scalar or tuple). */
static PyObject* element_to_python(const tusd_value_view* view,
                                   Py_ssize_t idx) {
  const int as_bool = (view->type == TUSD_TYPE_BOOL);
  const size_t comps = view->components ? view->components : 1;
  const size_t base = (size_t)idx * comps;
  if (comps == 1) {
    return component_to_python(view->data, view->storage, base, as_bool);
  }
  PyObject* tup = PyTuple_New((Py_ssize_t)comps);
  if (!tup) return NULL;
  for (size_t c = 0; c < comps; ++c) {
    PyObject* item =
        component_to_python(view->data, view->storage, base + c, as_bool);
    if (!item) {
      Py_DECREF(tup);
      return NULL;
    }
    PyTuple_SetItem(tup, (Py_ssize_t)c, item);
  }
  return tup;
}

/* ============================================================
 * Array type
 * ============================================================ */

static void Array_dealloc(PyObject* self) {
  TusdArray* a = (TusdArray*)self;
  PyTypeObject* tp = Py_TYPE(self);
  Py_CLEAR(a->owner);
  if (a->owned) {
    tusd_value_destroy(a->owned);
    a->owned = NULL;
  }
  freefunc free_fn = (freefunc)PyType_GetSlot(tp, Py_tp_free);
  free_fn(self);
  Py_DECREF((PyObject*)tp);
}

static Py_ssize_t Array_length(PyObject* self) {
  return (Py_ssize_t)((TusdArray*)self)->view.count;
}

static PyObject* Array_getitem(PyObject* self, Py_ssize_t idx) {
  TusdArray* a = (TusdArray*)self;
  Py_ssize_t n = (Py_ssize_t)a->view.count;
  if (idx < 0) idx += n;
  if (idx < 0 || idx >= n) {
    PyErr_SetString(PyExc_IndexError, "Array index out of range");
    return NULL;
  }
  return element_to_python(&a->view, idx);
}

static PyObject* Array_get_shape(PyObject* self, void* closure) {
  TusdArray* a = (TusdArray*)self;
  (void)closure;
  if (a->ndim == 2) {
    return Py_BuildValue("(nn)", a->shape[0], a->shape[1]);
  }
  return Py_BuildValue("(n)", a->shape[0]);
}

static PyObject* Array_get_dtype(PyObject* self, void* closure) {
  TusdArray* a = (TusdArray*)self;
  (void)closure;
  switch (a->view.storage) {
    case TUSD_COMP_UINT8:
      return PyUnicode_FromString("uint8");
    case TUSD_COMP_INT32:
      return PyUnicode_FromString("int32");
    case TUSD_COMP_UINT32:
      return PyUnicode_FromString("uint32");
    case TUSD_COMP_INT64:
      return PyUnicode_FromString("int64");
    case TUSD_COMP_UINT64:
      return PyUnicode_FromString("uint64");
    case TUSD_COMP_FLOAT16:
      return PyUnicode_FromString("float16");
    case TUSD_COMP_FLOAT32:
      return PyUnicode_FromString("float32");
    case TUSD_COMP_FLOAT64:
      return PyUnicode_FromString("float64");
    case TUSD_COMP_UINT16:
      return PyUnicode_FromString("uint16");
    case TUSD_COMP_INT16:
      return PyUnicode_FromString("int16");
    default:
      return PyUnicode_FromString("void");
  }
}

static PyObject* Array_get_type_name(PyObject* self, void* closure) {
  TusdArray* a = (TusdArray*)self;
  (void)closure;
  return PyUnicode_FromString(tusd_type_name(a->view.type));
}

static PyObject* Array_get_nbytes(PyObject* self, void* closure) {
  (void)closure;
  return PyLong_FromSize_t(((TusdArray*)self)->view.nbytes);
}

/* numpy zero-copy interop that works on the abi3 (3.10) build. numpy keeps a
 * reference to this Array when wrapping, so the memory stays alive. */
static PyObject* Array_get_array_interface(PyObject* self, void* closure) {
  TusdArray* a = (TusdArray*)self;
  (void)closure;
  PyObject* shape = Array_get_shape(self, NULL);
  if (!shape) return NULL;
  PyObject* data = Py_BuildValue("(NO)",
                                 PyLong_FromVoidPtr((void*)a->view.data),
                                 Py_True /* read-only */);
  if (!data) {
    Py_DECREF(shape);
    return NULL;
  }
  PyObject* dict = Py_BuildValue("{s:i,s:N,s:s,s:N,s:O}", "version", 3,
                                 "shape", shape, "typestr", a->typestr,
                                 "data", data, "strides", Py_None);
  return dict;
}

static PyObject* Array_memoryview(PyObject* self, PyObject* noargs) {
  TusdArray* a = (TusdArray*)self;
  (void)noargs;
  /* Raw read-only byte view (format 'B'); pair with .dtype/.shape or use
   * numpy's __array_interface__ path for typed access. The caller must keep
   * this Array alive while the memoryview is in use (the facade wraps this).
   */
  return PyMemoryView_FromMemory((char*)a->view.data,
                                 (Py_ssize_t)a->view.nbytes, PyBUF_READ);
}

static PyObject* Array_tolist(PyObject* self, PyObject* noargs) {
  TusdArray* a = (TusdArray*)self;
  (void)noargs;
  Py_ssize_t n = (Py_ssize_t)a->view.count;
  PyObject* list = PyList_New(n);
  if (!list) return NULL;
  for (Py_ssize_t i = 0; i < n; ++i) {
    PyObject* item = element_to_python(&a->view, i);
    if (!item) {
      Py_DECREF(list);
      return NULL;
    }
    PyList_SetItem(list, i, item);
  }
  return list;
}

static PyObject* Array_repr(PyObject* self) {
  TusdArray* a = (TusdArray*)self;
  PyObject* dtype = Array_get_dtype(self, NULL);
  if (!dtype) return NULL;
  PyObject* res;
  if (a->ndim == 2) {
    res = PyUnicode_FromFormat("Array(%U, shape=(%zd, %zd))", dtype,
                               a->shape[0], a->shape[1]);
  } else {
    res = PyUnicode_FromFormat("Array(%U, shape=(%zd,))", dtype, a->shape[0]);
  }
  Py_DECREF(dtype);
  return res;
}

#ifndef Py_LIMITED_API
/* Real buffer protocol on the non-limited (free-threaded) build: np.asarray
 * and memoryview(arr) work natively with dtype + 2-D shape. All fields are
 * immutable after creation, so no locking is needed. */
static int Array_getbuffer(PyObject* self, Py_buffer* buf, int flags) {
  TusdArray* a = (TusdArray*)self;
  if ((flags & PyBUF_WRITABLE) == PyBUF_WRITABLE) {
    PyErr_SetString(PyExc_BufferError, "Array is read-only");
    return -1;
  }
  memset(buf, 0, sizeof(*buf));
  buf->buf = (void*)a->view.data;
  buf->obj = self;
  Py_INCREF(self);
  buf->len = (Py_ssize_t)a->view.nbytes;
  buf->readonly = 1;
  buf->itemsize = a->itemsize;
  if ((flags & PyBUF_FORMAT) == PyBUF_FORMAT) {
    buf->format = a->format;
  }
  if ((flags & PyBUF_ND) == PyBUF_ND) {
    buf->ndim = a->ndim;
    buf->shape = a->shape;
  } else {
    buf->ndim = 1;
  }
  /* contiguous, strides implied */
  return 0;
}
#endif

static PyMethodDef Array_methods[] = {
    {"memoryview", Array_memoryview, METH_NOARGS,
     "Raw read-only bytes view of the underlying storage."},
    {"tolist", Array_tolist, METH_NOARGS,
     "Copy the contents into a list of Python scalars/tuples."},
    {NULL, NULL, 0, NULL},
};

static PyGetSetDef Array_getset[] = {
    {"shape", Array_get_shape, NULL, "Logical shape.", NULL},
    {"dtype", Array_get_dtype, NULL, "numpy-style dtype name.", NULL},
    {"type_name", Array_get_type_name, NULL, "USD element type name.", NULL},
    {"nbytes", Array_get_nbytes, NULL, "Total byte size.", NULL},
    {"__array_interface__", Array_get_array_interface, NULL,
     "numpy array interface (zero-copy).", NULL},
    {NULL, NULL, NULL, NULL, NULL},
};

static PyType_Slot Array_slots[] = {
    {Py_tp_dealloc, (void*)Array_dealloc},
    {Py_tp_repr, (void*)Array_repr},
    {Py_sq_length, (void*)Array_length},
    {Py_sq_item, (void*)Array_getitem},
    {Py_tp_methods, (void*)Array_methods},
    {Py_tp_getset, (void*)Array_getset},
#ifndef Py_LIMITED_API
    {Py_bf_getbuffer, (void*)Array_getbuffer},
#endif
    {0, NULL},
};

static PyType_Spec Array_spec = {
    .name = "tinyusdz._core.Array",
    .basicsize = sizeof(TusdArray),
    .flags = Py_TPFLAGS_DEFAULT
#ifdef Py_TPFLAGS_IMMUTABLETYPE
             | Py_TPFLAGS_IMMUTABLETYPE
#endif
    ,
    .slots = Array_slots,
};

int tusd_register_array_type(PyObject* module, tusd_state* st) {
  st->ArrayType = PyType_FromModuleAndSpec(module, &Array_spec, NULL);
  if (!st->ArrayType) return -1;
  return PyModule_AddObjectRef(module, "Array", st->ArrayType);
}

/* ============================================================
 * Array construction
 * ============================================================ */

static PyObject* array_new_common(tusd_state* st, PyObject* owner,
                                  tusd_value* owned,
                                  const tusd_value_view* view) {
  comp_info info;
  if (comp_info_for(view->storage, &info) != 0) {
    PyErr_SetString(st->UsdError, "array has unsupported storage type");
    return NULL;
  }
  PyObject* obj = tusd_alloc(st->ArrayType);
  if (!obj) return NULL;
  TusdArray* a = (TusdArray*)obj;
  a->owner = owner;
  Py_XINCREF(owner);
  a->owned = owned;
  a->view = *view;
  a->format[0] = info.format;
  a->format[1] = '\0';
  strncpy(a->typestr, info.typestr, sizeof(a->typestr) - 1);
  a->typestr[sizeof(a->typestr) - 1] = '\0';
  a->itemsize = info.itemsize;
  if (view->components > 1) {
    a->ndim = 2;
    a->shape[0] = (Py_ssize_t)view->count;
    a->shape[1] = (Py_ssize_t)view->components;
  } else {
    a->ndim = 1;
    a->shape[0] = (Py_ssize_t)view->count;
    a->shape[1] = 0;
  }
  return obj;
}

PyObject* tusd_wrap_array_borrowed(tusd_state* st, PyObject* owner,
                                   const tusd_value_view* view) {
  return array_new_common(st, owner, NULL, view);
}

PyObject* tusd_wrap_array_owned(tusd_state* st, tusd_value* owned,
                                const tusd_value_view* view) {
  return array_new_common(st, NULL, owned, view);
}

/* ============================================================
 * View / value conversion
 * ============================================================ */

PyObject* tusd_view_to_python(tusd_state* st, PyObject* owner,
                              tusd_value* owned, const tusd_value_view* view) {
  if (view->is_block) {
    if (owned) tusd_value_destroy(owned);
    Py_INCREF(st->ValueBlock);
    return st->ValueBlock;
  }
  if (view->is_array) {
    if (!view->data) {
      if (owned) tusd_value_destroy(owned);
      PyErr_Format(st->UsdError, "array value of type '%s' is not viewable",
                   tusd_type_name(view->type));
      return NULL;
    }
    if (owned) {
      return tusd_wrap_array_owned(st, owned, view);
    }
    return tusd_wrap_array_borrowed(st, owner, view);
  }

  /* Scalar */
  if (!view->data) {
    if (owned) tusd_value_destroy(owned);
    PyErr_Format(st->UsdError, "value of type '%s' has no data",
                 tusd_type_name(view->type));
    return NULL;
  }
  PyObject* res = element_to_python(view, 0);
  if (owned) tusd_value_destroy(owned);
  return res;
}

PyObject* tusd_value_to_python(tusd_state* st, tusd_value* val) {
  /* Try string family first. */
  tusd_sv sv;
  if (tusd_value_get_string(val, &sv) == TUSD_OK) {
    PyObject* s = tusd_sv_to_str(sv);
    tusd_value_destroy(val);
    return s;
  }
  tusd_strlist* tokens = NULL;
  if (tusd_value_get_token_array(val, &tokens) == TUSD_OK) {
    size_t n = tusd_strlist_size(tokens);
    PyObject* tup = PyTuple_New((Py_ssize_t)n);
    if (!tup) {
      tusd_strlist_destroy(tokens);
      tusd_value_destroy(val);
      return NULL;
    }
    for (size_t i = 0; i < n; ++i) {
      PyObject* item = tusd_sv_to_str(tusd_strlist_get(tokens, i));
      if (!item) {
        Py_DECREF(tup);
        tusd_strlist_destroy(tokens);
        tusd_value_destroy(val);
        return NULL;
      }
      PyTuple_SetItem(tup, (Py_ssize_t)i, item);
    }
    tusd_strlist_destroy(tokens);
    tusd_value_destroy(val);
    return tup;
  }

  tusd_value_view view;
  tusd_status status = tusd_value_get_view(val, &view);
  if (status != TUSD_OK) {
    tusd_value_destroy(val);
    return tusd_raise(st, status, "value view");
  }
  return tusd_view_to_python(st, NULL, val, &view);
}

PyObject* tusd_dict_to_python(tusd_state* st, tusd_dict_ref dict) {
  PyObject* out = PyDict_New();
  if (!out) return NULL;
  if (!tusd_dict_is_valid(dict)) return out;

  size_t n = tusd_dict_size(dict);
  for (size_t i = 0; i < n; ++i) {
    tusd_sv key, sval;
    tusd_value_view view;
    tusd_dict_ref sub;
    if (tusd_dict_entry(dict, i, &key, &view, &sval, &sub) != TUSD_OK) {
      continue;
    }
    PyObject* pykey = tusd_sv_to_str(key);
    if (!pykey) goto fail;
    PyObject* pyval = NULL;
    if (tusd_dict_is_valid(sub)) {
      pyval = tusd_dict_to_python(st, sub);
    } else if (sval.len > 0 || (view.data == NULL && !view.is_array &&
                                (view.type == TUSD_TYPE_STRING ||
                                 view.type == TUSD_TYPE_TOKEN ||
                                 view.type == TUSD_TYPE_ASSET_PATH))) {
      pyval = tusd_sv_to_str(sval);
    } else if (view.data) {
      /* Copy PODs out of the dict (dict values are small metadata). Arrays
       * become lists here rather than zero-copy views. */
      if (view.is_array) {
        PyObject* list = PyList_New((Py_ssize_t)view.count);
        if (list) {
          for (Py_ssize_t k = 0; k < (Py_ssize_t)view.count; ++k) {
            PyObject* item = element_to_python(&view, k);
            if (!item) {
              Py_CLEAR(list);
              break;
            }
            PyList_SetItem(list, k, item);
          }
        }
        pyval = list;
      } else {
        pyval = element_to_python(&view, 0);
      }
    } else {
      pyval = Py_None;
      Py_INCREF(Py_None);
    }
    if (!pyval) {
      Py_DECREF(pykey);
      goto fail;
    }
    int rc = PyDict_SetItem(out, pykey, pyval);
    Py_DECREF(pykey);
    Py_DECREF(pyval);
    if (rc != 0) goto fail;
  }
  return out;

fail:
  Py_DECREF(out);
  return NULL;
}

PyObject* tusd_attr_value_to_python(tusd_state* st, PyObject* stage_obj,
                                    tusd_prim prim, const char* name) {
  tusd_value_view view;
  tusd_status status = tusd_attr_get(prim, name, &view);
  if (status != TUSD_OK) {
    if (status == TUSD_ERR_NOT_FOUND) {
      PyErr_Format(PyExc_KeyError, "%s", name);
      return NULL;
    }
    return tusd_raise(st, status, name);
  }

  if (!view.is_array && (view.type == TUSD_TYPE_STRING ||
                         view.type == TUSD_TYPE_TOKEN ||
                         view.type == TUSD_TYPE_ASSET_PATH)) {
    tusd_sv sv;
    status = tusd_attr_get_string(prim, name, &sv);
    if (status != TUSD_OK) return tusd_raise(st, status, name);
    return tusd_sv_to_str(sv);
  }

  if (view.is_array && (view.type == TUSD_TYPE_TOKEN ||
                        view.type == TUSD_TYPE_STRING)) {
    tusd_strlist* tokens = NULL;
    status = tusd_attr_get_token_array(prim, name, &tokens);
    if (status != TUSD_OK) return tusd_raise(st, status, name);
    size_t n = tusd_strlist_size(tokens);
    PyObject* tup = PyTuple_New((Py_ssize_t)n);
    if (!tup) {
      tusd_strlist_destroy(tokens);
      return NULL;
    }
    for (size_t i = 0; i < n; ++i) {
      PyObject* item = tusd_sv_to_str(tusd_strlist_get(tokens, i));
      if (!item) {
        Py_DECREF(tup);
        tusd_strlist_destroy(tokens);
        return NULL;
      }
      PyTuple_SetItem(tup, (Py_ssize_t)i, item);
    }
    tusd_strlist_destroy(tokens);
    return tup;
  }

  return tusd_view_to_python(st, stage_obj, NULL, &view);
}
