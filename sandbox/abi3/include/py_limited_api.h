/* SPDX-License-Identifier: Apache 2.0
 *
 * Python Limited API (Stable ABI) Headers for Python 3.10+
 *
 * This header provides the minimal Python C API declarations needed for
 * building extension modules compatible with Python 3.10 and later using
 * the stable ABI. No Python installation is required at build time.
 *
 * Based on Python's stable ABI specification:
 * https://docs.python.org/3/c-api/stable.html
 */

#ifndef PY_LIMITED_API_H
#define PY_LIMITED_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Python 3.10+ stable ABI version */
#define Py_LIMITED_API 0x030a0000

/* Platform-specific export/import macros */
#if defined(_WIN32) || defined(__CYGWIN__)
#  ifdef Py_BUILD_CORE
#    define PyAPI_FUNC(RTYPE) __declspec(dllexport) RTYPE
#  else
#    define PyAPI_FUNC(RTYPE) __declspec(dllimport) RTYPE
#  endif
#  define PyAPI_DATA(RTYPE) extern __declspec(dllimport) RTYPE
#else
#  define PyAPI_FUNC(RTYPE) __attribute__((visibility("default"))) RTYPE
#  define PyAPI_DATA(RTYPE) extern RTYPE
#endif

/* Basic Python types */
typedef ssize_t Py_ssize_t;
typedef Py_ssize_t Py_hash_t;

/* Opaque Python object */
typedef struct _object PyObject;

/* Type object */
typedef struct _typeobject PyTypeObject;

/* Module definition */
typedef struct PyModuleDef PyModuleDef;
typedef struct PyModuleDef_Base PyModuleDef_Base;

/* Method definition */
typedef struct PyMethodDef PyMethodDef;

/* Member definition */
typedef struct PyMemberDef PyMemberDef;

/* GetSet definition */
typedef struct PyGetSetDef PyGetSetDef;

/* Buffer protocol */
typedef struct bufferinfo Py_buffer;

/* Module initialization function type */
typedef PyObject* (*PyModInitFunction)(void);

/* Object protocol */
#define Py_TPFLAGS_DEFAULT (0)
#define Py_TPFLAGS_BASETYPE (1UL << 10)
#define Py_TPFLAGS_HAVE_GC (1UL << 14)
#define Py_TPFLAGS_HEAPTYPE (1UL << 9)

/* Method calling conventions */
#define METH_VARARGS 0x0001
#define METH_KEYWORDS 0x0002
#define METH_NOARGS 0x0004
#define METH_O 0x0008
#define METH_CLASS 0x0010
#define METH_STATIC 0x0020

/* Member types for PyMemberDef */
#define T_SHORT 0
#define T_INT 1
#define T_LONG 2
#define T_FLOAT 3
#define T_DOUBLE 4
#define T_STRING 5
#define T_OBJECT 6
#define T_CHAR 7
#define T_BYTE 8
#define T_UBYTE 9
#define T_USHORT 10
#define T_UINT 11
#define T_ULONG 12
#define T_STRING_INPLACE 13
#define T_BOOL 14
#define T_OBJECT_EX 16
#define T_LONGLONG 17
#define T_ULONGLONG 18
#define T_PYSSIZET 19

/* Member flags */
#define READONLY 1
#define READ_RESTRICTED 2
#define WRITE_RESTRICTED 4
#define RESTRICTED (READ_RESTRICTED | WRITE_RESTRICTED)

/* Reference counting */
#define Py_INCREF(op) _Py_INCREF((PyObject *)(op))
#define Py_DECREF(op) _Py_DECREF((PyObject *)(op))
#define Py_XINCREF(op) _Py_XINCREF((PyObject *)(op))
#define Py_XDECREF(op) _Py_XDECREF((PyObject *)(op))

PyAPI_FUNC(void) _Py_INCREF(PyObject *op);
PyAPI_FUNC(void) _Py_DECREF(PyObject *op);
PyAPI_FUNC(void) _Py_XINCREF(PyObject *op);
PyAPI_FUNC(void) _Py_XDECREF(PyObject *op);

/* Return values */
#define Py_RETURN_NONE return Py_INCREF(Py_None), Py_None
#define Py_RETURN_TRUE return Py_INCREF(Py_True), Py_True
#define Py_RETURN_FALSE return Py_INCREF(Py_False), Py_False

/* Constants */
PyAPI_DATA(PyObject *) Py_None;
PyAPI_DATA(PyObject *) Py_True;
PyAPI_DATA(PyObject *) Py_False;

/* Module definition structure */
struct PyModuleDef_Base {
    PyObject *m_base;
    PyObject *(*m_init)(void);
    Py_ssize_t m_index;
    PyObject *m_copy;
};

#define PyModuleDef_HEAD_INIT {NULL, NULL, 0, NULL}

struct PyModuleDef {
    PyModuleDef_Base m_base;
    const char *m_name;
    const char *m_doc;
    Py_ssize_t m_size;
    PyMethodDef *m_methods;
    void *m_slots;
    void *m_traverse;
    void *m_clear;
    void *m_free;
};

/* Method definition structure */
typedef PyObject *(*PyCFunction)(PyObject *, PyObject *);
typedef PyObject *(*PyCFunctionWithKeywords)(PyObject *, PyObject *, PyObject *);

struct PyMethodDef {
    const char *ml_name;
    PyCFunction ml_meth;
    int ml_flags;
    const char *ml_doc;
};

/* Member definition structure */
struct PyMemberDef {
    const char *name;
    int type;
    Py_ssize_t offset;
    int flags;
    const char *doc;
};

/* GetSet definition structure */
typedef PyObject *(*getter)(PyObject *, void *);
typedef int (*setter)(PyObject *, PyObject *, void *);

struct PyGetSetDef {
    const char *name;
    getter get;
    setter set;
    const char *doc;
    void *closure;
};

/* Buffer protocol structures */
#define PyBUF_SIMPLE 0
#define PyBUF_WRITABLE 0x0001
#define PyBUF_FORMAT 0x0004
#define PyBUF_ND 0x0008
#define PyBUF_STRIDES (0x0010 | PyBUF_ND)
#define PyBUF_C_CONTIGUOUS (0x0020 | PyBUF_STRIDES)
#define PyBUF_F_CONTIGUOUS (0x0040 | PyBUF_STRIDES)
#define PyBUF_ANY_CONTIGUOUS (0x0080 | PyBUF_STRIDES)
#define PyBUF_INDIRECT (0x0100 | PyBUF_STRIDES)
#define PyBUF_CONTIG (PyBUF_ND | PyBUF_WRITABLE)
#define PyBUF_CONTIG_RO (PyBUF_ND)
#define PyBUF_STRIDED (PyBUF_STRIDES | PyBUF_WRITABLE)
#define PyBUF_STRIDED_RO (PyBUF_STRIDES)
#define PyBUF_RECORDS (PyBUF_STRIDES | PyBUF_WRITABLE | PyBUF_FORMAT)
#define PyBUF_RECORDS_RO (PyBUF_STRIDES | PyBUF_FORMAT)
#define PyBUF_FULL (PyBUF_INDIRECT | PyBUF_WRITABLE | PyBUF_FORMAT)
#define PyBUF_FULL_RO (PyBUF_INDIRECT | PyBUF_FORMAT)

struct bufferinfo {
    void *buf;
    PyObject *obj;
    Py_ssize_t len;
    Py_ssize_t itemsize;
    int readonly;
    int ndim;
    char *format;
    Py_ssize_t *shape;
    Py_ssize_t *strides;
    Py_ssize_t *suboffsets;
    void *internal;
};

/* Module API */
PyAPI_FUNC(PyObject *) PyModule_Create2(PyModuleDef *module, int module_api_version);
#define PyModule_Create(module) PyModule_Create2(module, 1013)
PyAPI_FUNC(int) PyModule_AddObject(PyObject *module, const char *name, PyObject *value);
PyAPI_FUNC(int) PyModule_AddIntConstant(PyObject *module, const char *name, long value);
PyAPI_FUNC(int) PyModule_AddStringConstant(PyObject *module, const char *name, const char *value);
PyAPI_FUNC(PyObject *) PyModule_GetDict(PyObject *module);

/* Type API */
PyAPI_FUNC(int) PyType_Ready(PyTypeObject *type);
PyAPI_FUNC(PyObject *) PyType_GenericNew(PyTypeObject *type, PyObject *args, PyObject *kwds);
PyAPI_FUNC(PyObject *) PyType_GenericAlloc(PyTypeObject *type, Py_ssize_t nitems);
PyAPI_FUNC(int) PyType_IsSubtype(PyTypeObject *a, PyTypeObject *b);

/* Object API */
PyAPI_FUNC(PyObject *) PyObject_CallObject(PyObject *callable, PyObject *args);
PyAPI_FUNC(PyObject *) PyObject_GetAttrString(PyObject *o, const char *attr_name);
PyAPI_FUNC(int) PyObject_SetAttrString(PyObject *o, const char *attr_name, PyObject *v);
PyAPI_FUNC(int) PyObject_HasAttrString(PyObject *o, const char *attr_name);
PyAPI_FUNC(PyObject *) PyObject_GetItem(PyObject *o, PyObject *key);
PyAPI_FUNC(int) PyObject_SetItem(PyObject *o, PyObject *key, PyObject *v);
PyAPI_FUNC(PyObject *) PyObject_Str(PyObject *o);
PyAPI_FUNC(PyObject *) PyObject_Repr(PyObject *o);
PyAPI_FUNC(PyObject *) PyObject_Type(PyObject *o);
PyAPI_FUNC(int) PyObject_IsTrue(PyObject *o);
PyAPI_FUNC(Py_hash_t) PyObject_Hash(PyObject *o);
PyAPI_FUNC(int) PyCallable_Check(PyObject *o);

/* Buffer protocol API */
PyAPI_FUNC(int) PyObject_GetBuffer(PyObject *obj, Py_buffer *view, int flags);
PyAPI_FUNC(void) PyBuffer_Release(Py_buffer *view);
PyAPI_FUNC(int) PyBuffer_FillInfo(Py_buffer *view, PyObject *obj, void *buf,
                                  Py_ssize_t len, int readonly, int flags);

/* Error handling */
PyAPI_FUNC(void) PyErr_SetString(PyObject *exception, const char *string);
PyAPI_FUNC(void) PyErr_SetObject(PyObject *exception, PyObject *value);
PyAPI_FUNC(PyObject *) PyErr_Format(PyObject *exception, const char *format, ...);
PyAPI_FUNC(int) PyErr_Occurred(void);
PyAPI_FUNC(void) PyErr_Clear(void);
PyAPI_FUNC(void) PyErr_Print(void);
PyAPI_FUNC(PyObject *) PyErr_NoMemory(void);

/* Exception types */
PyAPI_DATA(PyObject *) PyExc_Exception;
PyAPI_DATA(PyObject *) PyExc_TypeError;
PyAPI_DATA(PyObject *) PyExc_ValueError;
PyAPI_DATA(PyObject *) PyExc_RuntimeError;
PyAPI_DATA(PyObject *) PyExc_MemoryError;
PyAPI_DATA(PyObject *) PyExc_AttributeError;
PyAPI_DATA(PyObject *) PyExc_KeyError;
PyAPI_DATA(PyObject *) PyExc_IndexError;
PyAPI_DATA(PyObject *) PyExc_OSError;

/* Argument parsing */
PyAPI_FUNC(int) PyArg_ParseTuple(PyObject *args, const char *format, ...);
PyAPI_FUNC(int) PyArg_ParseTupleAndKeywords(PyObject *args, PyObject *kw,
                                            const char *format, char **keywords, ...);
PyAPI_FUNC(int) PyArg_UnpackTuple(PyObject *args, const char *name,
                                  Py_ssize_t min, Py_ssize_t max, ...);

/* Building return values */
PyAPI_FUNC(PyObject *) Py_BuildValue(const char *format, ...);

/* Long (integer) API */
PyAPI_FUNC(PyObject *) PyLong_FromLong(long v);
PyAPI_FUNC(PyObject *) PyLong_FromUnsignedLong(unsigned long v);
PyAPI_FUNC(PyObject *) PyLong_FromLongLong(long long v);
PyAPI_FUNC(PyObject *) PyLong_FromUnsignedLongLong(unsigned long long v);
PyAPI_FUNC(PyObject *) PyLong_FromSize_t(size_t v);
PyAPI_FUNC(PyObject *) PyLong_FromSsize_t(Py_ssize_t v);
PyAPI_FUNC(long) PyLong_AsLong(PyObject *obj);
PyAPI_FUNC(unsigned long) PyLong_AsUnsignedLong(PyObject *obj);
PyAPI_FUNC(long long) PyLong_AsLongLong(PyObject *obj);
PyAPI_FUNC(unsigned long long) PyLong_AsUnsignedLongLong(PyObject *obj);
PyAPI_FUNC(size_t) PyLong_AsSize_t(PyObject *obj);
PyAPI_FUNC(Py_ssize_t) PyLong_AsSsize_t(PyObject *obj);

/* Float API */
PyAPI_FUNC(PyObject *) PyFloat_FromDouble(double v);
PyAPI_FUNC(double) PyFloat_AsDouble(PyObject *obj);

/* String API (Unicode in Python 3) */
PyAPI_FUNC(PyObject *) PyUnicode_FromString(const char *u);
PyAPI_FUNC(PyObject *) PyUnicode_FromStringAndSize(const char *u, Py_ssize_t size);
PyAPI_FUNC(const char *) PyUnicode_AsUTF8(PyObject *unicode);
PyAPI_FUNC(const char *) PyUnicode_AsUTF8AndSize(PyObject *unicode, Py_ssize_t *size);
PyAPI_FUNC(PyObject *) PyUnicode_FromFormat(const char *format, ...);

/* Bytes API */
PyAPI_FUNC(PyObject *) PyBytes_FromString(const char *v);
PyAPI_FUNC(PyObject *) PyBytes_FromStringAndSize(const char *v, Py_ssize_t len);
PyAPI_FUNC(char *) PyBytes_AsString(PyObject *obj);
PyAPI_FUNC(Py_ssize_t) PyBytes_Size(PyObject *obj);

/* List API */
PyAPI_FUNC(PyObject *) PyList_New(Py_ssize_t size);
PyAPI_FUNC(Py_ssize_t) PyList_Size(PyObject *list);
PyAPI_FUNC(PyObject *) PyList_GetItem(PyObject *list, Py_ssize_t index);
PyAPI_FUNC(int) PyList_SetItem(PyObject *list, Py_ssize_t index, PyObject *item);
PyAPI_FUNC(int) PyList_Append(PyObject *list, PyObject *item);

/* Tuple API */
PyAPI_FUNC(PyObject *) PyTuple_New(Py_ssize_t size);
PyAPI_FUNC(Py_ssize_t) PyTuple_Size(PyObject *tuple);
PyAPI_FUNC(PyObject *) PyTuple_GetItem(PyObject *tuple, Py_ssize_t index);
PyAPI_FUNC(int) PyTuple_SetItem(PyObject *tuple, Py_ssize_t index, PyObject *item);

/* Dict API */
PyAPI_FUNC(PyObject *) PyDict_New(void);
PyAPI_FUNC(PyObject *) PyDict_GetItemString(PyObject *dict, const char *key);
PyAPI_FUNC(int) PyDict_SetItemString(PyObject *dict, const char *key, PyObject *item);
PyAPI_FUNC(int) PyDict_DelItemString(PyObject *dict, const char *key);
PyAPI_FUNC(PyObject *) PyDict_Keys(PyObject *dict);
PyAPI_FUNC(PyObject *) PyDict_Values(PyObject *dict);
PyAPI_FUNC(PyObject *) PyDict_Items(PyObject *dict);

/* Capsule API (for passing C pointers) */
PyAPI_FUNC(PyObject *) PyCapsule_New(void *pointer, const char *name,
                                     void (*destructor)(PyObject *));
PyAPI_FUNC(void *) PyCapsule_GetPointer(PyObject *capsule, const char *name);
PyAPI_FUNC(int) PyCapsule_SetPointer(PyObject *capsule, void *pointer);

/* Memory API */
PyAPI_FUNC(void *) PyMem_Malloc(size_t size);
PyAPI_FUNC(void *) PyMem_Calloc(size_t nelem, size_t elsize);
PyAPI_FUNC(void *) PyMem_Realloc(void *ptr, size_t new_size);
PyAPI_FUNC(void) PyMem_Free(void *ptr);

/* GC support */
PyAPI_FUNC(void) PyObject_GC_Track(PyObject *op);
PyAPI_FUNC(void) PyObject_GC_UnTrack(PyObject *op);
PyAPI_FUNC(void) PyObject_GC_Del(void *op);

/* Type checking */
PyAPI_FUNC(int) PyType_Check(PyObject *o);
PyAPI_FUNC(int) PyLong_Check(PyObject *o);
PyAPI_FUNC(int) PyFloat_Check(PyObject *o);
PyAPI_FUNC(int) PyUnicode_Check(PyObject *o);
PyAPI_FUNC(int) PyBytes_Check(PyObject *o);
PyAPI_FUNC(int) PyList_Check(PyObject *o);
PyAPI_FUNC(int) PyTuple_Check(PyObject *o);
PyAPI_FUNC(int) PyDict_Check(PyObject *o);

#ifdef __cplusplus
}
#endif

#endif /* PY_LIMITED_API_H */
