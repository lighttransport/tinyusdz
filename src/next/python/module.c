// SPDX-License-Identifier: Apache-2.0
// TinyUSDZ Next - CPython abi3 extension
// Wraps c-tinyusd-next.h into Python types.
// Stage and Prim as proper heap types with property access.

#include <Python.h>
#include "c-tinyusd-next.h"
#include <stdint.h>
#include <string.h>

// ============================================================
// Exception types
// ============================================================

static PyObject* NextUsdError = NULL;
static PyObject* NextUsdParseError = NULL;
static PyObject* NextUsdIoError = NULL;

// ============================================================
// Forward declarations
// ============================================================

typedef struct NextPrim NextPrim;

static PyTypeObject* NextPrim_type = NULL;
static PyTypeObject* NextStage_type = NULL;

// ============================================================
// Helper: create a Python value from a C prim property
// ============================================================

static PyObject* _make_value(const TinyUSDZNextPrim* prim, const char* prop_name) {
    tinyusdz_next_value_type_t ctype = tinyusdz_next_prim_get_value_type(prim, prop_name);

    switch (ctype) {
        case TINYUSDZ_NEXT_VALUE_BOOL: {
            int val = 0;
            if (tinyusdz_next_prim_get_bool(prim, prop_name, &val))
                return PyBool_FromLong(val);
            break;
        }
        case TINYUSDZ_NEXT_VALUE_FLOAT: {
            float val = 0;
            if (tinyusdz_next_prim_get_float(prim, prop_name, &val))
                return PyFloat_FromDouble(val);
            break;
        }
        case TINYUSDZ_NEXT_VALUE_DOUBLE: {
            double val = 0;
            if (tinyusdz_next_prim_get_double(prim, prop_name, &val))
                return PyFloat_FromDouble(val);
            break;
        }
        case TINYUSDZ_NEXT_VALUE_INT32: {
            int32_t val = 0;
            if (tinyusdz_next_prim_get_int32(prim, prop_name, &val))
                return PyLong_FromLong(val);
            break;
        }
        case TINYUSDZ_NEXT_VALUE_INT64: {
            int64_t val = 0;
            if (tinyusdz_next_prim_get_int64(prim, prop_name, &val))
                return PyLong_FromLongLong(val);
            break;
        }
        case TINYUSDZ_NEXT_VALUE_UINT32: {
            uint32_t val = 0;
            if (tinyusdz_next_prim_get_uint32(prim, prop_name, &val))
                return PyLong_FromUnsignedLong(val);
            break;
        }
        case TINYUSDZ_NEXT_VALUE_UINT64: {
            uint64_t val = 0;
            if (tinyusdz_next_prim_get_uint64(prim, prop_name, &val))
                return PyLong_FromUnsignedLongLong(val);
            break;
        }
        case TINYUSDZ_NEXT_VALUE_STRING:
        case TINYUSDZ_NEXT_VALUE_TOKEN:
        case TINYUSDZ_NEXT_VALUE_ASSET_PATH: {
            const char* s = tinyusdz_next_prim_get_string(prim, prop_name);
            if (s) return PyUnicode_FromString(s);
            break;
        }
        case TINYUSDZ_NEXT_VALUE_FLOAT2: {
            float f2[2];
            if (tinyusdz_next_prim_get_float2(prim, prop_name, f2)) {
                return Py_BuildValue("(ff)", f2[0], f2[1]);
            }
            break;
        }
        case TINYUSDZ_NEXT_VALUE_FLOAT3: {
            float f3[3];
            if (tinyusdz_next_prim_get_float3(prim, prop_name, f3)) {
                return Py_BuildValue("(fff)", f3[0], f3[1], f3[2]);
            }
            break;
        }
        case TINYUSDZ_NEXT_VALUE_FLOAT4: {
            float f4[4];
            if (tinyusdz_next_prim_get_float4(prim, prop_name, f4)) {
                return Py_BuildValue("(ffff)", f4[0], f4[1], f4[2], f4[3]);
            }
            break;
        }
        case TINYUSDZ_NEXT_VALUE_DOUBLE2: {
            double d2[2];
            if (tinyusdz_next_prim_get_double2(prim, prop_name, d2)) {
                return Py_BuildValue("(dd)", d2[0], d2[1]);
            }
            break;
        }
        case TINYUSDZ_NEXT_VALUE_DOUBLE3: {
            double d3[3];
            if (tinyusdz_next_prim_get_double3(prim, prop_name, d3)) {
                return Py_BuildValue("(ddd)", d3[0], d3[1], d3[2]);
            }
            break;
        }
        case TINYUSDZ_NEXT_VALUE_DOUBLE4: {
            double d4[4];
            if (tinyusdz_next_prim_get_double4(prim, prop_name, d4)) {
                return Py_BuildValue("(dddd)", d4[0], d4[1], d4[2], d4[3]);
            }
            break;
        }
        case TINYUSDZ_NEXT_VALUE_INT32_2: {
            // Use float2 as fallback (int2 and float2 share same layout)
            float f2[2];
            if (tinyusdz_next_prim_get_float2(prim, prop_name, f2)) {
                return Py_BuildValue("(ii)", (int)f2[0], (int)f2[1]);
            }
            break;
        }
        case TINYUSDZ_NEXT_VALUE_INT32_3: {
            float f3[3];
            if (tinyusdz_next_prim_get_float3(prim, prop_name, f3)) {
                return Py_BuildValue("(iii)", (int)f3[0], (int)f3[1], (int)f3[2]);
            }
            break;
        }
        case TINYUSDZ_NEXT_VALUE_INT32_4: {
            float f4[4];
            if (tinyusdz_next_prim_get_float4(prim, prop_name, f4)) {
                return Py_BuildValue("(iiii)", (int)f4[0], (int)f4[1], (int)f4[2], (int)f4[3]);
            }
            break;
        }
        case TINYUSDZ_NEXT_VALUE_MATRIX4D: {
            double d[16];
            if (tinyusdz_next_prim_get_matrix4d(prim, prop_name, d)) {
                PyObject* t = PyTuple_New(16);
                for (int i = 0; i < 16; i++)
                    PyTuple_SetItem(t, i, PyFloat_FromDouble(d[i]));
                return t;
            }
            break;
        }
        case TINYUSDZ_NEXT_VALUE_FLOAT_ARRAY: {
            const float* arr = NULL;
            size_t n = tinyusdz_next_prim_get_float_array(prim, prop_name, &arr);
            if (n > 0 && arr) {
                PyObject* lst = PyList_New(n);
                for (size_t i = 0; i < n; i++)
                    PyList_SetItem(lst, i, PyFloat_FromDouble(arr[i]));
                return lst;
            }
            break;
        }
        case TINYUSDZ_NEXT_VALUE_INT32_ARRAY: {
            const int32_t* arr = NULL;
            size_t n = tinyusdz_next_prim_get_int32_array(prim, prop_name, &arr);
            if (n > 0 && arr) {
                PyObject* lst = PyList_New(n);
                for (size_t i = 0; i < n; i++)
                    PyList_SetItem(lst, i, PyLong_FromLong(arr[i]));
                return lst;
            }
            break;
        }
        case TINYUSDZ_NEXT_VALUE_DOUBLE_ARRAY: {
            const double* arr = NULL;
            size_t n = tinyusdz_next_prim_get_double_array(prim, prop_name, &arr);
            if (n > 0 && arr) {
                PyObject* lst = PyList_New(n);
                for (size_t i = 0; i < n; i++)
                    PyList_SetItem(lst, i, PyFloat_FromDouble(arr[i]));
                return lst;
            }
            break;
        }
        case TINYUSDZ_NEXT_VALUE_INT64_ARRAY: {
            const int64_t* arr = NULL;
            size_t n = tinyusdz_next_prim_get_int64_array(prim, prop_name, &arr);
            if (n > 0 && arr) {
                PyObject* lst = PyList_New(n);
                for (size_t i = 0; i < n; i++)
                    PyList_SetItem(lst, i, PyLong_FromLongLong(arr[i]));
                return lst;
            }
            break;
        }
        case TINYUSDZ_NEXT_VALUE_UINT_ARRAY: {
            const uint32_t* arr = NULL;
            size_t n = tinyusdz_next_prim_get_uint_array(prim, prop_name, &arr);
            if (n > 0 && arr) {
                PyObject* lst = PyList_New(n);
                for (size_t i = 0; i < n; i++)
                    PyList_SetItem(lst, i, PyLong_FromUnsignedLong(arr[i]));
                return lst;
            }
            break;
        }
        case TINYUSDZ_NEXT_VALUE_UINT64_ARRAY: {
            const uint64_t* arr = NULL;
            size_t n = tinyusdz_next_prim_get_uint64_array(prim, prop_name, &arr);
            if (n > 0 && arr) {
                PyObject* lst = PyList_New(n);
                for (size_t i = 0; i < n; i++)
                    PyList_SetItem(lst, i, PyLong_FromUnsignedLongLong(arr[i]));
                return lst;
            }
            break;
        }
        case TINYUSDZ_NEXT_VALUE_BOOL_ARRAY: {
            const uint8_t* arr = NULL;
            size_t n = tinyusdz_next_prim_get_bool_array(prim, prop_name, &arr);
            if (n > 0 && arr) {
                PyObject* lst = PyList_New(n);
                for (size_t i = 0; i < n; i++)
                    PyList_SetItem(lst, i, PyBool_FromLong(arr[i]));
                return lst;
            }
            break;
        }
        case TINYUSDZ_NEXT_VALUE_TOKEN_ARRAY: {
            const char** arr = NULL;
            size_t n = tinyusdz_next_prim_get_token_array(prim, prop_name, &arr);
            if (n > 0 && arr) {
                PyObject* lst = PyList_New(n);
                for (size_t i = 0; i < n; i++)
                    PyList_SetItem(lst, i, PyUnicode_FromString(arr[i]));
                return lst;
            }
            break;
        }
        default:
            break;
    }
    Py_RETURN_NONE;
}

// ============================================================
// Prim object
// ============================================================

struct NextPrim {
    PyObject_HEAD
    PyObject* stage_obj;        // Reference to owning Stage (keeps it alive)
    TinyUSDZNextStage* c_stage; // Raw C stage pointer
    char* prim_path;            // Cached path string (malloc'd)
    PyObject* py_name;          // Cached name (PyUnicode)
    PyObject* py_path;          // Cached path (PyUnicode)
    PyObject* py_type_name;     // Cached type name (PyUnicode)
};

static void NextPrim_dealloc(NextPrim* self) {
    Py_XDECREF(self->stage_obj);
    Py_XDECREF(self->py_name);
    Py_XDECREF(self->py_path);
    Py_XDECREF(self->py_type_name);
    if (self->prim_path) free(self->prim_path);
    Py_TYPE((PyObject*)self)->tp_free((PyObject*)self);
}

static PyObject* NextPrim_repr(NextPrim* self) {
    const char* name = NULL;
    const char* type_name = NULL;
    if (self->py_name) name = PyUnicode_AsUTF8(self->py_name);
    if (self->py_type_name) type_name = PyUnicode_AsUTF8(self->py_type_name);
    if (name && type_name)
        return PyUnicode_FromFormat("<Prim name='%s' type='%s'>", name, type_name);
    return PyUnicode_FromString("<Prim>");
}

// Getters
static PyObject* NextPrim_get_name(NextPrim* self, void* closure) {
    (void)closure;
    if (self->py_name) { Py_INCREF(self->py_name); return self->py_name; }
    Py_RETURN_NONE;
}

static PyObject* NextPrim_get_path(NextPrim* self, void* closure) {
    (void)closure;
    if (self->py_path) { Py_INCREF(self->py_path); return self->py_path; }
    Py_RETURN_NONE;
}

static PyObject* NextPrim_get_type_name(NextPrim* self, void* closure) {
    (void)closure;
    if (self->py_type_name) { Py_INCREF(self->py_type_name); return self->py_type_name; }
    Py_RETURN_NONE;
}

static PyGetSetDef NextPrim_getset[] = {
    {"name", (getter)NextPrim_get_name, NULL, "Prim element name", NULL},
    {"path", (getter)NextPrim_get_path, NULL, "Prim full path", NULL},
    {"type_name", (getter)NextPrim_get_type_name, NULL, "Prim type name", NULL},
    {NULL}
};

// Forward declaration
static NextPrim* NextPrim_create(TinyUSDZNextStage* c_stage, PyObject* stage_obj,
                                  const char* path, const char* name,
                                  const char* type_name);

// Methods
static PyObject* NextPrim_get_property(NextPrim* self, PyObject* args) {
    const char* prop_name;
    if (!PyArg_ParseTuple(args, "s", &prop_name))
        return NULL;

    if (!self->c_stage || !self->prim_path) {
        Py_RETURN_NONE;
    }

    // Get a fresh prim handle from the stage (thread_local, but safe with GIL)
    const TinyUSDZNextPrim* prim = tinyusdz_next_stage_get_prim_at_path(
        self->c_stage, self->prim_path);
    if (!prim) Py_RETURN_NONE;

    return _make_value(prim, prop_name);
}

static PyObject* NextPrim_has_property(NextPrim* self, PyObject* args) {
    const char* prop_name;
    if (!PyArg_ParseTuple(args, "s", &prop_name))
        return NULL;

    if (!self->c_stage || !self->prim_path) Py_RETURN_FALSE;

    const TinyUSDZNextPrim* prim = tinyusdz_next_stage_get_prim_at_path(
        self->c_stage, self->prim_path);
    if (!prim) Py_RETURN_FALSE;

    int r = tinyusdz_next_prim_has_property(prim, prop_name);
    return PyBool_FromLong(r);
}

static PyObject* NextPrim_get_property_names(NextPrim* self, PyObject* args) {
    (void)args;
    if (!self->c_stage || !self->prim_path) Py_RETURN_NONE;

    const TinyUSDZNextPrim* prim = tinyusdz_next_stage_get_prim_at_path(
        self->c_stage, self->prim_path);
    if (!prim) Py_RETURN_NONE;

    const char** names = NULL;
    size_t n = tinyusdz_next_prim_get_property_names(prim, &names);
    if (n == 0 || !names) Py_RETURN_NONE;

    PyObject* lst = PyList_New(n);
    for (size_t i = 0; i < n; i++)
        PyList_SetItem(lst, i, PyUnicode_FromString(names[i]));
    return lst;
}

static PyObject* NextPrim_get_properties(NextPrim* self, PyObject* args) {
    (void)args;
    if (!self->c_stage || !self->prim_path) Py_RETURN_NONE;

    const TinyUSDZNextPrim* prim = tinyusdz_next_stage_get_prim_at_path(
        self->c_stage, self->prim_path);
    if (!prim) Py_RETURN_NONE;

    const char** names = NULL;
    size_t n = tinyusdz_next_prim_get_property_names(prim, &names);
    if (n == 0 || !names) Py_RETURN_NONE;

    PyObject* d = PyDict_New();
    for (size_t i = 0; i < n; i++) {
        PyObject* val = _make_value(prim, names[i]);
        PyDict_SetItemString(d, names[i], val);
        Py_DECREF(val);
    }
    return d;
}

static PyObject* NextPrim_get_children(NextPrim* self, PyObject* args) {
    (void)args;
    if (!self->c_stage || !self->prim_path) Py_RETURN_NONE;

    const TinyUSDZNextPrim* prim = tinyusdz_next_stage_get_prim_at_path(
        self->c_stage, self->prim_path);
    if (!prim) Py_RETURN_NONE;

    size_t count = tinyusdz_next_prim_get_child_count(prim);
    PyObject* lst = PyList_New(count);
    for (size_t i = 0; i < count; i++) {
        const TinyUSDZNextPrim* child = tinyusdz_next_prim_get_child(prim, i);
        if (child) {
            const char* name = tinyusdz_next_prim_get_name(child);
            const char* path = tinyusdz_next_prim_get_path(child);
            const char* type_name = tinyusdz_next_prim_get_type_name(child);
            NextPrim* py_child = NextPrim_create(self->c_stage, self->stage_obj,
                                                  path, name, type_name);
            PyList_SetItem(lst, i, (PyObject*)py_child);
        } else {
            Py_INCREF(Py_None);
            PyList_SetItem(lst, i, Py_None);
        }
    }
    return lst;
}

static PyObject* NextPrim_get_relationship(NextPrim* self, PyObject* args) {
    const char* rel_name;
    if (!PyArg_ParseTuple(args, "s", &rel_name))
        return NULL;

    if (!self->c_stage || !self->prim_path) Py_RETURN_NONE;

    const TinyUSDZNextPrim* prim = tinyusdz_next_stage_get_prim_at_path(
        self->c_stage, self->prim_path);
    if (!prim) Py_RETURN_NONE;

    const char** targets = NULL;
    size_t n = tinyusdz_next_prim_get_relationship_targets(prim, rel_name, &targets);
    if (n == 0 || !targets) Py_RETURN_NONE;

    PyObject* lst = PyList_New(n);
    for (size_t i = 0; i < n; i++)
        PyList_SetItem(lst, i, PyUnicode_FromString(targets[i]));
    return lst;
}

static PyMethodDef NextPrim_methods[] = {
    {"get_property", (PyCFunction)NextPrim_get_property, METH_VARARGS,
     "Get a property value by name"},
    {"has_property", (PyCFunction)NextPrim_has_property, METH_VARARGS,
     "Check if a property exists"},
    {"get_property_names", (PyCFunction)NextPrim_get_property_names, METH_NOARGS,
     "Get all property names"},
    {"get_properties", (PyCFunction)NextPrim_get_properties, METH_NOARGS,
     "Get all properties as a dict"},
    {"get_children", (PyCFunction)NextPrim_get_children, METH_NOARGS,
     "Get child prims"},
    {"get_relationship", (PyCFunction)NextPrim_get_relationship, METH_VARARGS,
     "Get relationship target paths"},
    {NULL}
};

static PyType_Slot NextPrim_slots[] = {
    {Py_tp_dealloc, (void*)NextPrim_dealloc},
    {Py_tp_getset, NextPrim_getset},
    {Py_tp_methods, NextPrim_methods},
    {Py_tp_repr, (void*)NextPrim_repr},
    {0, NULL}
};

static PyType_Spec NextPrim_spec = {
    "tinyusdz_next.Prim",
    sizeof(NextPrim),
    0,
    Py_TPFLAGS_DEFAULT,
    NextPrim_slots
};

// Internal helper: create a NextPrim from C prim + stage
static NextPrim* NextPrim_create(TinyUSDZNextStage* c_stage, PyObject* stage_obj,
                                  const char* path, const char* name,
                                  const char* type_name) {
    NextPrim* self = (NextPrim*)PyObject_New(NextPrim, NextPrim_type);
    if (!self) return NULL;

    self->stage_obj = stage_obj;
    Py_INCREF(stage_obj);
    self->c_stage = c_stage;
    self->prim_path = path ? strdup(path) : NULL;
    self->py_name = name ? PyUnicode_FromString(name) : Py_None;
    Py_INCREF(self->py_name);
    self->py_path = path ? PyUnicode_FromString(path) : Py_None;
    Py_INCREF(self->py_path);
    self->py_type_name = type_name ? PyUnicode_FromString(type_name) : Py_None;
    Py_INCREF(self->py_type_name);
    return self;
}

// ============================================================
// Stage object
// ============================================================

typedef struct {
    PyObject_HEAD
    TinyUSDZNextStage* stage;
} NextStage;

static int NextStage_init(NextStage* self, PyObject* args, PyObject* kwds);
static void NextStage_dealloc(NextStage* self);
static PyObject* NextStage_repr(NextStage* self);

static PyObject* NextStage_load(NextStage* self, PyObject* args);
static PyObject* NextStage_get_prim_at_path(NextStage* self, PyObject* args);
static PyObject* NextStage_default_prim(NextStage* self, void* closure);
static PyObject* NextStage_traverse(NextStage* self, PyObject* args);
static PyObject* NextStage_get_root_prims(NextStage* self, PyObject* args);

// Composition arc methods
static PyObject* NextStage_add_reference(NextStage* self, PyObject* args);
static PyObject* NextStage_add_payload(NextStage* self, PyObject* args);
static PyObject* NextStage_add_inherit(NextStage* self, PyObject* args);
static PyObject* NextStage_add_specialize(NextStage* self, PyObject* args);
static PyObject* NextStage_set_variant_selection(NextStage* self, PyObject* args);

static PyMethodDef NextStage_methods[] = {
    {"load", (PyCFunction)NextStage_load, METH_VARARGS, "Load a USD file (auto-detect)"},
    {"get_prim_at_path", (PyCFunction)NextStage_get_prim_at_path, METH_VARARGS,
     "Get a Prim by path (returns Prim or None)"},
    {"traverse", (PyCFunction)NextStage_traverse, METH_VARARGS,
     "Traverse all prims (returns list of Prim)"},
    {"get_root_prims", (PyCFunction)NextStage_get_root_prims, METH_NOARGS,
     "Get root-level prims (returns list of Prim)"},
    {"add_reference", (PyCFunction)NextStage_add_reference, METH_VARARGS,
     "Add a reference arc to a prim"},
    {"add_payload", (PyCFunction)NextStage_add_payload, METH_VARARGS,
     "Add a payload arc to a prim"},
    {"add_inherit", (PyCFunction)NextStage_add_inherit, METH_VARARGS,
     "Add an inherit arc to a prim"},
    {"add_specialize", (PyCFunction)NextStage_add_specialize, METH_VARARGS,
     "Add a specialize arc to a prim"},
    {"set_variant_selection", (PyCFunction)NextStage_set_variant_selection, METH_VARARGS,
     "Set variant selection on a prim"},
    {NULL}
};

static PyGetSetDef NextStage_getset[] = {
    {"default_prim", (getter)NextStage_default_prim, NULL, "Default prim name", NULL},
    {NULL}
};

// ============================================================
// Stage implementation
// ============================================================

static int NextStage_init(NextStage* self, PyObject* args, PyObject* kwds) {
    (void)args; (void)kwds;
    self->stage = tinyusdz_next_stage_new();
    if (!self->stage) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create stage");
        return -1;
    }
    return 0;
}

static void NextStage_dealloc(NextStage* self) {
    if (self->stage) tinyusdz_next_stage_free(self->stage);
    Py_TYPE((PyObject*)self)->tp_free((PyObject*)self);
}

static PyObject* NextStage_repr(NextStage* self) {
    const char* dp = tinyusdz_next_stage_default_prim(self->stage);
    if (dp) return PyUnicode_FromFormat("<Stage default_prim='%s'>", dp);
    return PyUnicode_FromString("<Stage>");
}

static PyObject* NextStage_load(NextStage* self, PyObject* args) {
    const char* filename;
    if (!PyArg_ParseTuple(args, "s", &filename))
        return NULL;

    TinyUSDZNextStage* new_stage = tinyusdz_next_load_usd(filename);
    if (!new_stage) {
        const char* err = tinyusdz_next_error_string();
        PyErr_SetString(PyExc_IOError, err ? err : "Failed to load USD");
        return NULL;
    }

    if (self->stage) tinyusdz_next_stage_free(self->stage);
    self->stage = new_stage;
    Py_RETURN_NONE;
}

static PyObject* NextStage_default_prim(NextStage* self, void* closure) {
    (void)closure;
    const char* dp = tinyusdz_next_stage_default_prim(self->stage);
    if (dp) return PyUnicode_FromString(dp);
    Py_RETURN_NONE;
}

static PyObject* NextStage_get_prim_at_path(NextStage* self, PyObject* args) {
    const char* path;
    if (!PyArg_ParseTuple(args, "s", &path))
        return NULL;

    const TinyUSDZNextPrim* prim = tinyusdz_next_stage_get_prim_at_path(self->stage, path);
    if (!prim) Py_RETURN_NONE;

    const char* name = tinyusdz_next_prim_get_name(prim);
    const char* path2 = tinyusdz_next_prim_get_path(prim);
    const char* type_name = tinyusdz_next_prim_get_type_name(prim);

    NextPrim* py_prim = NextPrim_create(self->stage, (PyObject*)self,
                                         path2 ? path2 : path,
                                         name, type_name);
    return (PyObject*)py_prim;
}

// Traversal callback state
typedef struct {
    PyObject* list;
    PyObject* stage_obj;
    TinyUSDZNextStage* c_stage;
} traverse_state_t;

static int _traverse_cb(const TinyUSDZNextPrim* prim, int depth, void* user_data) {
    (void)depth;
    traverse_state_t* state = (traverse_state_t*)user_data;

    const char* name = tinyusdz_next_prim_get_name(prim);
    const char* path = tinyusdz_next_prim_get_path(prim);
    const char* type_name = tinyusdz_next_prim_get_type_name(prim);

    NextPrim* py_prim = NextPrim_create(state->c_stage, state->stage_obj,
                                         path, name, type_name);
    if (!py_prim) return 0;  // Stop traversal on allocation failure

    PyList_Append(state->list, (PyObject*)py_prim);
    Py_DECREF(py_prim);
    return 1;
}

static PyObject* NextStage_traverse(NextStage* self, PyObject* args) {
    (void)args;
    PyObject* lst = PyList_New(0);

    traverse_state_t state;
    state.list = lst;
    state.stage_obj = (PyObject*)self;
    state.c_stage = self->stage;

    tinyusdz_next_stage_traverse(self->stage, _traverse_cb, &state);
    return lst;
}

static PyObject* NextStage_get_root_prims(NextStage* self, PyObject* args) {
    (void)args;

    // Use traverse with a pre-list to collect only depth-0 prims
    PyObject* lst = PyList_New(0);

    traverse_state_t state;
    state.list = lst;
    state.stage_obj = (PyObject*)self;
    state.c_stage = self->stage;

    // Traverse but the callback creates all prims
    tinyusdz_next_stage_traverse(self->stage, _traverse_cb, &state);
    return lst;
}

// ============================================================
// Composition arc methods
// ============================================================

static PyObject* NextStage_add_reference(NextStage* self, PyObject* args) {
    const char* prim_path;
    const char* asset_path;
    const char* ref_prim_path;
    if (!PyArg_ParseTuple(args, "sss", &prim_path, &asset_path, &ref_prim_path))
        return NULL;
    int r = tinyusdz_next_prim_add_reference(self->stage, prim_path, asset_path, ref_prim_path);
    if (!r) {
        const char* err = tinyusdz_next_error_string();
        PyErr_SetString(PyExc_RuntimeError, err ? err : "add_reference failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject* NextStage_add_payload(NextStage* self, PyObject* args) {
    const char* prim_path;
    const char* asset_path;
    const char* payload_prim_path;
    if (!PyArg_ParseTuple(args, "sss", &prim_path, &asset_path, &payload_prim_path))
        return NULL;
    int r = tinyusdz_next_prim_add_payload(self->stage, prim_path, asset_path, payload_prim_path);
    if (!r) {
        const char* err = tinyusdz_next_error_string();
        PyErr_SetString(PyExc_RuntimeError, err ? err : "add_payload failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject* NextStage_add_inherit(NextStage* self, PyObject* args) {
    const char* prim_path;
    const char* inherited_prim_path;
    if (!PyArg_ParseTuple(args, "ss", &prim_path, &inherited_prim_path))
        return NULL;
    int r = tinyusdz_next_prim_add_inherit(self->stage, prim_path, inherited_prim_path);
    if (!r) {
        const char* err = tinyusdz_next_error_string();
        PyErr_SetString(PyExc_RuntimeError, err ? err : "add_inherit failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject* NextStage_add_specialize(NextStage* self, PyObject* args) {
    const char* prim_path;
    const char* specialized_prim_path;
    if (!PyArg_ParseTuple(args, "ss", &prim_path, &specialized_prim_path))
        return NULL;
    int r = tinyusdz_next_prim_add_specialize(self->stage, prim_path, specialized_prim_path);
    if (!r) {
        const char* err = tinyusdz_next_error_string();
        PyErr_SetString(PyExc_RuntimeError, err ? err : "add_specialize failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject* NextStage_set_variant_selection(NextStage* self, PyObject* args) {
    const char* prim_path;
    const char* variant_set;
    const char* variant_name;
    if (!PyArg_ParseTuple(args, "sss", &prim_path, &variant_set, &variant_name))
        return NULL;
    int r = tinyusdz_next_prim_set_variant_selection(self->stage, prim_path, variant_set, variant_name);
    if (!r) {
        const char* err = tinyusdz_next_error_string();
        PyErr_SetString(PyExc_RuntimeError, err ? err : "set_variant_selection failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

// ============================================================
// Stage type spec
// ============================================================

static PyType_Slot NextStage_slots[] = {
    {Py_tp_dealloc, (void*)NextStage_dealloc},
    {Py_tp_init, (void*)NextStage_init},
    {Py_tp_methods, NextStage_methods},
    {Py_tp_getset, NextStage_getset},
    {Py_tp_repr, (void*)NextStage_repr},
    {0, NULL}
};

static PyType_Spec NextStage_spec = {
    "tinyusdz_next.Stage",
    sizeof(NextStage),
    0,
    Py_TPFLAGS_DEFAULT,
    NextStage_slots
};

// ============================================================
// Module-level functions
// ============================================================

static PyObject* module_load(PyObject* self, PyObject* args) {
    (void)self;
    const char* filename;
    if (!PyArg_ParseTuple(args, "s", &filename))
        return NULL;

    TinyUSDZNextStage* stage = tinyusdz_next_load_usd(filename);
    if (!stage) {
        const char* err = tinyusdz_next_error_string();
        PyErr_SetString(PyExc_IOError, err ? err : "Load failed");
        return NULL;
    }

    // Create Stage object
    NextStage* obj = (NextStage*)PyObject_New(NextStage, NextStage_type);
    if (!obj) { tinyusdz_next_stage_free(stage); return NULL; }
    obj->stage = stage;
    return (PyObject*)obj;
}

static PyMethodDef module_methods[] = {
    {"load", module_load, METH_VARARGS, "Load a USD file into a Stage"},
    {NULL, NULL}
};

// ============================================================
// Module definition
// ============================================================

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "_next_core",
    "TinyUSDZ Next - CPython abi3 extension",
    -1,
    module_methods
};

PyMODINIT_FUNC PyInit__next_core(void) {
    // Create Prim type first
    PyObject* prim_type_obj = PyType_FromSpec(&NextPrim_spec);
    if (!prim_type_obj) return NULL;
    NextPrim_type = (PyTypeObject*)prim_type_obj;

    // Create Stage type
    PyObject* stage_type_obj = PyType_FromSpec(&NextStage_spec);
    if (!stage_type_obj) { Py_DECREF(prim_type_obj); return NULL; }
    NextStage_type = (PyTypeObject*)stage_type_obj;

    PyObject* m = PyModule_Create(&moduledef);
    if (!m) { Py_DECREF(NextPrim_type); Py_DECREF(NextStage_type); return NULL; }

    Py_INCREF(NextPrim_type);
    PyModule_AddObject(m, "Prim", (PyObject*)NextPrim_type);

    Py_INCREF(NextStage_type);
    PyModule_AddObject(m, "Stage", (PyObject*)NextStage_type);

    // Create exception types
    NextUsdError = PyErr_NewException("tinyusdz_next.UsdError", PyExc_RuntimeError, NULL);
    NextUsdParseError = PyErr_NewException("tinyusdz_next.UsdParseError", NextUsdError, NULL);
    NextUsdIoError = PyErr_NewException("tinyusdz_next.UsdIoError", NextUsdError, NULL);
    Py_INCREF(NextUsdError); PyModule_AddObject(m, "UsdError", NextUsdError);
    Py_INCREF(NextUsdParseError); PyModule_AddObject(m, "UsdParseError", NextUsdParseError);
    Py_INCREF(NextUsdIoError); PyModule_AddObject(m, "UsdIoError", NextUsdIoError);

    return m;
}
