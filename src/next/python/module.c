// SPDX-License-Identifier: Apache-2.0
// TinyUSDZ Next - CPython abi3 extension
// Wraps c-tinyusd-next.h into Python types.

//#define Py_LIMITED_API 0x030B0000

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
// Stage object
// ============================================================

typedef struct {
    PyObject_HEAD
    TinyUSDZNextStage* stage;
} NextStage;

static int NextStage_init(NextStage* self, PyObject* args, PyObject* kwds);
static void NextStage_dealloc(NextStage* self);
static PyObject* NextStage_repr(NextStage* self);

// Stage methods
static PyObject* NextStage_load(NextStage* self, PyObject* args);
static PyObject* NextStage_get_prim_at_path(NextStage* self, PyObject* args);
static PyObject* NextStage_default_prim(NextStage* self, void* closure);
static PyObject* NextStage_traverse(NextStage* self, PyObject* args);

static PyMethodDef NextStage_methods[] = {
    {"load", (PyCFunction)NextStage_load, METH_VARARGS, "Load a USD file (auto-detect)"},
    {"get_prim_at_path", (PyCFunction)NextStage_get_prim_at_path, METH_VARARGS, "Get a prim by path"},
    {"traverse", (PyCFunction)NextStage_traverse, METH_VARARGS, "Traverse all prims"},
    {NULL}
};

static PyGetSetDef NextStage_getset[] = {
    {"default_prim", (getter)NextStage_default_prim, NULL, "Default prim name", NULL},
    {NULL}
};

// (Prim methods are exposed via property dict from make_value_from_prim)

// ============================================================
// Value object (simplified - returns native Python types)
// ============================================================

// ============================================================
// Helper: create a Value from C API prim property
// ============================================================

static PyObject* make_value_from_prim(const TinyUSDZNextPrim* prim, const char* prop_name) {
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
        case TINYUSDZ_NEXT_VALUE_STRING:
        case TINYUSDZ_NEXT_VALUE_TOKEN: {
            const char* s = tinyusdz_next_prim_get_string(prim, prop_name);
            if (s) return PyUnicode_FromString(s);
            break;
        }
        case TINYUSDZ_NEXT_VALUE_FLOAT3: {
            float f3[3];
            if (tinyusdz_next_prim_get_float3(prim, prop_name, f3)) {
                PyObject* t = PyTuple_New(3);
                PyTuple_SetItem(t, 0, PyFloat_FromDouble(f3[0]));
                PyTuple_SetItem(t, 1, PyFloat_FromDouble(f3[1]));
                PyTuple_SetItem(t, 2, PyFloat_FromDouble(f3[2]));
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
        default:
            break;
    }
    Py_RETURN_NONE;
}

// ============================================================
// Stage implementation
// ============================================================

static int NextStage_init(NextStage* self, PyObject* args, PyObject* kwds) {
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
    if (dp) return PyUnicode_FromFormat("<NextStage default_prim='%s'>", dp);
    return PyUnicode_FromString("<NextStage>");
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

    // Replace stage
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
    if (!prim) {
        Py_RETURN_NONE;
    }

    PyObject* d = PyDict_New();
    const char* name = tinyusdz_next_prim_get_name(prim);
    const char* path2 = tinyusdz_next_prim_get_path(prim);
    const char* type_name = tinyusdz_next_prim_get_type_name(prim);
    if (name) PyDict_SetItemString(d, "name", PyUnicode_FromString(name));
    if (path2) PyDict_SetItemString(d, "path", PyUnicode_FromString(path2));
    if (type_name) PyDict_SetItemString(d, "type_name", PyUnicode_FromString(type_name));
    return d;
}

static PyObject* NextStage_traverse(NextStage* self, PyObject* args) {
    (void)args;
    // C API traverse requires callback - not yet wrapped
    Py_RETURN_NONE;
}

// Forward declaration
static PyObject* create_stage(TinyUSDZNextStage* handle);

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

    return create_stage(stage);
}

static PyMethodDef module_methods[] = {
    {"load", module_load, METH_VARARGS, "Load a USD file into a NextStage"},
    {NULL, NULL}
};

// ============================================================
// Module definition
// ============================================================

// Stage type spec
static PyType_Slot NextStage_slots[] = {
    {Py_tp_dealloc, (void*)NextStage_dealloc},
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

static PyObject* NextStage_type = NULL;

// Helper to create a Stage object
static PyObject* create_stage(TinyUSDZNextStage* handle) {
    NextStage* obj = (NextStage*)PyObject_New(NextStage, (PyTypeObject*)NextStage_type);
    if (!obj) return NULL;
    obj->stage = handle;
    return (PyObject*)obj;
}

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "_next_core",
    "TinyUSDZ Next - CPython abi3 extension",
    -1,
    module_methods
};

PyMODINIT_FUNC PyInit__next_core(void) {
    PyObject* m = PyModule_Create(&moduledef);
    if (!m) return NULL;

    // Create Stage type
    NextStage_type = PyType_FromSpec(&NextStage_spec);
    if (!NextStage_type) return NULL;
    Py_INCREF(NextStage_type);
    PyModule_AddObject(m, "Stage", NextStage_type);

    // Create exception types
    NextUsdError = PyErr_NewException("tinyusdz_next.UsdError", PyExc_RuntimeError, NULL);
    NextUsdParseError = PyErr_NewException("tinyusdz_next.UsdParseError", NextUsdError, NULL);
    NextUsdIoError = PyErr_NewException("tinyusdz_next.UsdIoError", NextUsdError, NULL);
    Py_INCREF(NextUsdError); PyModule_AddObject(m, "UsdError", NextUsdError);
    Py_INCREF(NextUsdParseError); PyModule_AddObject(m, "UsdParseError", NextUsdParseError);
    Py_INCREF(NextUsdIoError); PyModule_AddObject(m, "UsdIoError", NextUsdIoError);

    // Update module_load to use create_stage
    // (module_load was defined earlier and still uses PyObject_New directly)
    // We'll fix the load function to use the proper type

    return m;
}
