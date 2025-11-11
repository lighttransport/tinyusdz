/* SPDX-License-Identifier: Apache 2.0
 *
 * TinyUSDZ Python ABI3 Binding
 *
 * This module provides Python bindings for TinyUSDZ using the Python 3.10+
 * stable ABI (Limited API). It supports numpy-friendly buffer protocol for
 * efficient array data access without copying.
 *
 * Key design principles:
 * 1. C++ side: RAII memory management (handled by c-tinyusd.h)
 * 2. Python side: Reference counting for object lifetime
 * 3. Buffer protocol: Zero-copy array access for numpy compatibility
 */

#define Py_LIMITED_API 0x030a0000
#include "../include/py_limited_api.h"
#include "../../../src/c-tinyusd.h"

#include <string.h>

/* Forward declarations */
static PyTypeObject TinyUSDStageType;
static PyTypeObject TinyUSDPrimType;
static PyTypeObject TinyUSDValueType;
static PyTypeObject TinyUSDValueArrayType;

/* ============================================================================
 * Stage Object
 * ============================================================================ */

typedef struct {
    PyObject_HEAD
    CTinyUSDStage *stage;  /* Managed by RAII on C++ side */
} TinyUSDStageObject;

static void
TinyUSDStage_dealloc(TinyUSDStageObject *self)
{
    if (self->stage) {
        c_tinyusd_stage_free(self->stage);
        self->stage = NULL;
    }
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
TinyUSDStage_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    TinyUSDStageObject *self;
    self = (TinyUSDStageObject *)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->stage = c_tinyusd_stage_new();
        if (self->stage == NULL) {
            Py_DECREF(self);
            PyErr_SetString(PyExc_MemoryError, "Failed to create stage");
            return NULL;
        }
    }
    return (PyObject *)self;
}

static PyObject *
TinyUSDStage_to_string(TinyUSDStageObject *self, PyObject *Py_UNUSED(ignored))
{
    c_tinyusd_string_t *str = c_tinyusd_string_new_empty();
    if (!str) {
        return PyErr_NoMemory();
    }

    if (!c_tinyusd_stage_to_string(self->stage, str)) {
        c_tinyusd_string_free(str);
        PyErr_SetString(PyExc_RuntimeError, "Failed to convert stage to string");
        return NULL;
    }

    const char *cstr = c_tinyusd_string_str(str);
    PyObject *result = PyUnicode_FromString(cstr);
    c_tinyusd_string_free(str);
    return result;
}

static PyObject *
TinyUSDStage_load_from_file(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    const char *filename;
    static char *kwlist[] = {"filename", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, &filename)) {
        return NULL;
    }

    TinyUSDStageObject *self = (TinyUSDStageObject *)TinyUSDStage_new(type, NULL, NULL);
    if (self == NULL) {
        return NULL;
    }

    c_tinyusd_string_t *warn = c_tinyusd_string_new_empty();
    c_tinyusd_string_t *err = c_tinyusd_string_new_empty();

    int ret = c_tinyusd_load_usd_from_file(filename, self->stage, warn, err);

    if (!ret) {
        const char *err_str = c_tinyusd_string_str(err);
        PyErr_SetString(PyExc_RuntimeError, err_str);
        c_tinyusd_string_free(warn);
        c_tinyusd_string_free(err);
        Py_DECREF(self);
        return NULL;
    }

    /* TODO: Handle warnings */
    c_tinyusd_string_free(warn);
    c_tinyusd_string_free(err);

    return (PyObject *)self;
}

static PyMethodDef TinyUSDStage_methods[] = {
    {"to_string", (PyCFunction)TinyUSDStage_to_string, METH_NOARGS,
     "Convert stage to string representation"},
    {"load_from_file", (PyCFunction)TinyUSDStage_load_from_file,
     METH_VARARGS | METH_KEYWORDS | METH_CLASS,
     "Load USD file into a new stage"},
    {NULL}
};

static PyTypeObject TinyUSDStageType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "tinyusdz_abi3.Stage",
    .tp_doc = "TinyUSDZ Stage object",
    .tp_basicsize = sizeof(TinyUSDStageObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = TinyUSDStage_new,
    .tp_dealloc = (destructor)TinyUSDStage_dealloc,
    .tp_methods = TinyUSDStage_methods,
};

/* ============================================================================
 * Value Array Object with Buffer Protocol
 * ============================================================================ */

typedef struct {
    PyObject_HEAD
    void *data;           /* Pointer to array data */
    Py_ssize_t length;    /* Number of elements */
    Py_ssize_t itemsize;  /* Size of each element in bytes */
    int readonly;         /* Is the buffer readonly? */
    char *format;         /* Format string for buffer protocol */
    CTinyUSDValueType value_type;  /* TinyUSDZ value type */
    PyObject *owner;      /* Owner object to keep alive */
} TinyUSDValueArrayObject;

static void
TinyUSDValueArray_dealloc(TinyUSDValueArrayObject *self)
{
    Py_XDECREF(self->owner);
    if (self->format) {
        PyMem_Free(self->format);
    }
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* Get format string for buffer protocol based on value type */
static const char *
get_format_string(CTinyUSDValueType value_type)
{
    switch (value_type) {
        case C_TINYUSD_VALUE_BOOL: return "?";
        case C_TINYUSD_VALUE_INT: return "i";
        case C_TINYUSD_VALUE_UINT: return "I";
        case C_TINYUSD_VALUE_INT64: return "q";
        case C_TINYUSD_VALUE_UINT64: return "Q";
        case C_TINYUSD_VALUE_FLOAT: return "f";
        case C_TINYUSD_VALUE_DOUBLE: return "d";
        case C_TINYUSD_VALUE_HALF: return "e";  /* half-precision float */

        /* Vector types - expose as structured arrays */
        case C_TINYUSD_VALUE_INT2: return "ii";
        case C_TINYUSD_VALUE_INT3: return "iii";
        case C_TINYUSD_VALUE_INT4: return "iiii";
        case C_TINYUSD_VALUE_FLOAT2: return "ff";
        case C_TINYUSD_VALUE_FLOAT3: return "fff";
        case C_TINYUSD_VALUE_FLOAT4: return "ffff";
        case C_TINYUSD_VALUE_DOUBLE2: return "dd";
        case C_TINYUSD_VALUE_DOUBLE3: return "ddd";
        case C_TINYUSD_VALUE_DOUBLE4: return "dddd";

        default: return "B";  /* Raw bytes as fallback */
    }
}

/* Buffer protocol implementation */
static int
TinyUSDValueArray_getbuffer(TinyUSDValueArrayObject *self, Py_buffer *view, int flags)
{
    if (view == NULL) {
        PyErr_SetString(PyExc_ValueError, "NULL view in getbuffer");
        return -1;
    }

    if ((flags & PyBUF_WRITABLE) && self->readonly) {
        PyErr_SetString(PyExc_BufferError, "Array is readonly");
        return -1;
    }

    const char *format = get_format_string(self->value_type);

    /* Fill in the buffer info */
    view->obj = (PyObject *)self;
    view->buf = self->data;
    view->len = self->length * self->itemsize;
    view->readonly = self->readonly;
    view->itemsize = self->itemsize;
    view->format = (flags & PyBUF_FORMAT) ? (char *)format : NULL;
    view->ndim = 1;
    view->shape = (flags & PyBUF_ND) ? &self->length : NULL;
    view->strides = (flags & PyBUF_STRIDES) ? &self->itemsize : NULL;
    view->suboffsets = NULL;
    view->internal = NULL;

    Py_INCREF(self);
    return 0;
}

static void
TinyUSDValueArray_releasebuffer(TinyUSDValueArrayObject *self, Py_buffer *view)
{
    /* Nothing to do - data is managed by owner object */
}

static PyBufferProcs TinyUSDValueArray_as_buffer = {
    .bf_getbuffer = (getbufferproc)TinyUSDValueArray_getbuffer,
    .bf_releasebuffer = (releasebufferproc)TinyUSDValueArray_releasebuffer,
};

static PyObject *
TinyUSDValueArray_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    TinyUSDValueArrayObject *self;
    self = (TinyUSDValueArrayObject *)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->data = NULL;
        self->length = 0;
        self->itemsize = 0;
        self->readonly = 1;
        self->format = NULL;
        self->value_type = C_TINYUSD_VALUE_UNKNOWN;
        self->owner = NULL;
    }
    return (PyObject *)self;
}

static PyObject *
TinyUSDValueArray_repr(TinyUSDValueArrayObject *self)
{
    return PyUnicode_FromFormat("<ValueArray type=%s length=%zd itemsize=%zd>",
                                c_tinyusd_value_type_name(self->value_type),
                                self->length,
                                self->itemsize);
}

static Py_ssize_t
TinyUSDValueArray_length(TinyUSDValueArrayObject *self)
{
    return self->length;
}

static PySequenceMethods TinyUSDValueArray_as_sequence = {
    .sq_length = (lenfunc)TinyUSDValueArray_length,
};

static PyTypeObject TinyUSDValueArrayType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "tinyusdz_abi3.ValueArray",
    .tp_doc = "TinyUSDZ value array with buffer protocol support",
    .tp_basicsize = sizeof(TinyUSDValueArrayObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = TinyUSDValueArray_new,
    .tp_dealloc = (destructor)TinyUSDValueArray_dealloc,
    .tp_repr = (reprfunc)TinyUSDValueArray_repr,
    .tp_as_buffer = &TinyUSDValueArray_as_buffer,
    .tp_as_sequence = &TinyUSDValueArray_as_sequence,
};

/* ============================================================================
 * Value Object
 * ============================================================================ */

typedef struct {
    PyObject_HEAD
    CTinyUSDValue *value;  /* Managed by RAII on C++ side */
} TinyUSDValueObject;

static void
TinyUSDValue_dealloc(TinyUSDValueObject *self)
{
    if (self->value) {
        c_tinyusd_value_free(self->value);
        self->value = NULL;
    }
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
TinyUSDValue_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    TinyUSDValueObject *self;
    self = (TinyUSDValueObject *)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->value = c_tinyusd_value_new_null();
        if (self->value == NULL) {
            Py_DECREF(self);
            PyErr_SetString(PyExc_MemoryError, "Failed to create value");
            return NULL;
        }
    }
    return (PyObject *)self;
}

static PyObject *
TinyUSDValue_get_type(TinyUSDValueObject *self, void *closure)
{
    CTinyUSDValueType vtype = c_tinyusd_value_type(self->value);
    const char *type_name = c_tinyusd_value_type_name(vtype);
    return PyUnicode_FromString(type_name);
}

static PyObject *
TinyUSDValue_to_string(TinyUSDValueObject *self, PyObject *Py_UNUSED(ignored))
{
    c_tinyusd_string_t *str = c_tinyusd_string_new_empty();
    if (!str) {
        return PyErr_NoMemory();
    }

    if (!c_tinyusd_value_to_string(self->value, str)) {
        c_tinyusd_string_free(str);
        PyErr_SetString(PyExc_RuntimeError, "Failed to convert value to string");
        return NULL;
    }

    const char *cstr = c_tinyusd_string_str(str);
    PyObject *result = PyUnicode_FromString(cstr);
    c_tinyusd_string_free(str);
    return result;
}

static PyObject *
TinyUSDValue_as_int(TinyUSDValueObject *self, PyObject *Py_UNUSED(ignored))
{
    int val;
    if (!c_tinyusd_value_as_int(self->value, &val)) {
        PyErr_SetString(PyExc_TypeError, "Value is not an integer");
        return NULL;
    }
    return PyLong_FromLong(val);
}

static PyObject *
TinyUSDValue_as_float(TinyUSDValueObject *self, PyObject *Py_UNUSED(ignored))
{
    float val;
    if (!c_tinyusd_value_as_float(self->value, &val)) {
        PyErr_SetString(PyExc_TypeError, "Value is not a float");
        return NULL;
    }
    return PyFloat_FromDouble((double)val);
}

static PyObject *
TinyUSDValue_from_int(PyTypeObject *type, PyObject *args)
{
    int val;
    if (!PyArg_ParseTuple(args, "i", &val)) {
        return NULL;
    }

    TinyUSDValueObject *self = (TinyUSDValueObject *)type->tp_alloc(type, 0);
    if (self == NULL) {
        return NULL;
    }

    self->value = c_tinyusd_value_new_int(val);
    if (self->value == NULL) {
        Py_DECREF(self);
        return PyErr_NoMemory();
    }

    return (PyObject *)self;
}

static PyObject *
TinyUSDValue_from_float(PyTypeObject *type, PyObject *args)
{
    float val;
    if (!PyArg_ParseTuple(args, "f", &val)) {
        return NULL;
    }

    TinyUSDValueObject *self = (TinyUSDValueObject *)type->tp_alloc(type, 0);
    if (self == NULL) {
        return NULL;
    }

    self->value = c_tinyusd_value_new_float(val);
    if (self->value == NULL) {
        Py_DECREF(self);
        return PyErr_NoMemory();
    }

    return (PyObject *)self;
}

static PyGetSetDef TinyUSDValue_getset[] = {
    {"type", (getter)TinyUSDValue_get_type, NULL, "Value type", NULL},
    {NULL}
};

static PyMethodDef TinyUSDValue_methods[] = {
    {"to_string", (PyCFunction)TinyUSDValue_to_string, METH_NOARGS,
     "Convert value to string representation"},
    {"as_int", (PyCFunction)TinyUSDValue_as_int, METH_NOARGS,
     "Get value as integer"},
    {"as_float", (PyCFunction)TinyUSDValue_as_float, METH_NOARGS,
     "Get value as float"},
    {"from_int", (PyCFunction)TinyUSDValue_from_int, METH_VARARGS | METH_CLASS,
     "Create value from integer"},
    {"from_float", (PyCFunction)TinyUSDValue_from_float, METH_VARARGS | METH_CLASS,
     "Create value from float"},
    {NULL}
};

static PyTypeObject TinyUSDValueType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "tinyusdz_abi3.Value",
    .tp_doc = "TinyUSDZ value object",
    .tp_basicsize = sizeof(TinyUSDValueObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = TinyUSDValue_new,
    .tp_dealloc = (destructor)TinyUSDValue_dealloc,
    .tp_methods = TinyUSDValue_methods,
    .tp_getset = TinyUSDValue_getset,
};

/* ============================================================================
 * Prim Object
 * ============================================================================ */

typedef struct {
    PyObject_HEAD
    CTinyUSDPrim *prim;  /* Managed by RAII on C++ side */
} TinyUSDPrimObject;

static void
TinyUSDPrim_dealloc(TinyUSDPrimObject *self)
{
    if (self->prim) {
        c_tinyusd_prim_free(self->prim);
        self->prim = NULL;
    }
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
TinyUSDPrim_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    const char *prim_type = "Xform";
    static char *kwlist[] = {"prim_type", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s", kwlist, &prim_type)) {
        return NULL;
    }

    TinyUSDPrimObject *self = (TinyUSDPrimObject *)type->tp_alloc(type, 0);
    if (self == NULL) {
        return NULL;
    }

    c_tinyusd_string_t *err = c_tinyusd_string_new_empty();
    self->prim = c_tinyusd_prim_new(prim_type, err);

    if (self->prim == NULL) {
        const char *err_str = c_tinyusd_string_str(err);
        PyErr_SetString(PyExc_ValueError, err_str);
        c_tinyusd_string_free(err);
        Py_DECREF(self);
        return NULL;
    }

    c_tinyusd_string_free(err);
    return (PyObject *)self;
}

static PyObject *
TinyUSDPrim_get_type(TinyUSDPrimObject *self, void *closure)
{
    const char *prim_type = c_tinyusd_prim_type(self->prim);
    if (prim_type == NULL) {
        Py_RETURN_NONE;
    }
    return PyUnicode_FromString(prim_type);
}

static PyObject *
TinyUSDPrim_get_element_name(TinyUSDPrimObject *self, void *closure)
{
    const char *name = c_tinyusd_prim_element_name(self->prim);
    if (name == NULL) {
        Py_RETURN_NONE;
    }
    return PyUnicode_FromString(name);
}

static PyGetSetDef TinyUSDPrim_getset[] = {
    {"type", (getter)TinyUSDPrim_get_type, NULL, "Prim type", NULL},
    {"element_name", (getter)TinyUSDPrim_get_element_name, NULL, "Element name", NULL},
    {NULL}
};

static PyObject *
TinyUSDPrim_to_string(TinyUSDPrimObject *self, PyObject *Py_UNUSED(ignored))
{
    c_tinyusd_string_t *str = c_tinyusd_string_new_empty();
    if (!str) {
        return PyErr_NoMemory();
    }

    if (!c_tinyusd_prim_to_string(self->prim, str)) {
        c_tinyusd_string_free(str);
        PyErr_SetString(PyExc_RuntimeError, "Failed to convert prim to string");
        return NULL;
    }

    const char *cstr = c_tinyusd_string_str(str);
    PyObject *result = PyUnicode_FromString(cstr);
    c_tinyusd_string_free(str);
    return result;
}

static PyMethodDef TinyUSDPrim_methods[] = {
    {"to_string", (PyCFunction)TinyUSDPrim_to_string, METH_NOARGS,
     "Convert prim to string representation"},
    {NULL}
};

static PyTypeObject TinyUSDPrimType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "tinyusdz_abi3.Prim",
    .tp_doc = "TinyUSDZ Prim object",
    .tp_basicsize = sizeof(TinyUSDPrimObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = TinyUSDPrim_new,
    .tp_dealloc = (destructor)TinyUSDPrim_dealloc,
    .tp_methods = TinyUSDPrim_methods,
    .tp_getset = TinyUSDPrim_getset,
};

/* ============================================================================
 * Module Functions
 * ============================================================================ */

static PyObject *
tinyusdz_detect_format(PyObject *self, PyObject *args)
{
    const char *filename;
    if (!PyArg_ParseTuple(args, "s", &filename)) {
        return NULL;
    }

    CTinyUSDFormat format = c_tinyusd_detect_format(filename);

    const char *format_str;
    switch (format) {
        case C_TINYUSD_FORMAT_USDA: format_str = "USDA"; break;
        case C_TINYUSD_FORMAT_USDC: format_str = "USDC"; break;
        case C_TINYUSD_FORMAT_USDZ: format_str = "USDZ"; break;
        case C_TINYUSD_FORMAT_AUTO: format_str = "AUTO"; break;
        default: format_str = "UNKNOWN"; break;
    }

    return PyUnicode_FromString(format_str);
}

static PyMethodDef tinyusdz_methods[] = {
    {"detect_format", tinyusdz_detect_format, METH_VARARGS,
     "Detect USD file format from filename"},
    {NULL, NULL, 0, NULL}
};

static PyModuleDef tinyusdz_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "tinyusdz_abi3",
    .m_doc = "TinyUSDZ Python bindings using ABI3 (stable API)",
    .m_size = -1,
    .m_methods = tinyusdz_methods,
};

/* ============================================================================
 * Module Initialization
 * ============================================================================ */

PyMODINIT_FUNC
PyInit_tinyusdz_abi3(void)
{
    PyObject *m;

    /* Prepare types */
    if (PyType_Ready(&TinyUSDStageType) < 0)
        return NULL;
    if (PyType_Ready(&TinyUSDPrimType) < 0)
        return NULL;
    if (PyType_Ready(&TinyUSDValueType) < 0)
        return NULL;
    if (PyType_Ready(&TinyUSDValueArrayType) < 0)
        return NULL;

    /* Create module */
    m = PyModule_Create(&tinyusdz_module);
    if (m == NULL)
        return NULL;

    /* Add types to module */
    Py_INCREF(&TinyUSDStageType);
    if (PyModule_AddObject(m, "Stage", (PyObject *)&TinyUSDStageType) < 0) {
        Py_DECREF(&TinyUSDStageType);
        Py_DECREF(m);
        return NULL;
    }

    Py_INCREF(&TinyUSDPrimType);
    if (PyModule_AddObject(m, "Prim", (PyObject *)&TinyUSDPrimType) < 0) {
        Py_DECREF(&TinyUSDPrimType);
        Py_DECREF(m);
        return NULL;
    }

    Py_INCREF(&TinyUSDValueType);
    if (PyModule_AddObject(m, "Value", (PyObject *)&TinyUSDValueType) < 0) {
        Py_DECREF(&TinyUSDValueType);
        Py_DECREF(m);
        return NULL;
    }

    Py_INCREF(&TinyUSDValueArrayType);
    if (PyModule_AddObject(m, "ValueArray", (PyObject *)&TinyUSDValueArrayType) < 0) {
        Py_DECREF(&TinyUSDValueArrayType);
        Py_DECREF(m);
        return NULL;
    }

    /* Add version */
    PyModule_AddStringConstant(m, "__version__", "0.1.0");

    return m;
}
