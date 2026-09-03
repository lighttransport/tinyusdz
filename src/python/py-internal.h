/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024-Present Light Transport Entertainment Inc.
 *
 * lightusd._core — shared internals.
 *
 * Single source, two build configurations:
 *   - abi3 wheel:  compiled with Py_LIMITED_API=0x030A0000 (Python 3.10+)
 *   - cp31Xt wheel: compiled non-limited on free-threaded CPython
 *     (Py_GIL_DISABLED comes from pyconfig.h), declares Py_mod_gil.
 *
 * Rules kept throughout:
 *   - No static PyObject globals: everything lives in module state
 *     (multi-phase init, multiple-interpreter and free-threading safe).
 *   - Heap types only (PyType_FromModuleAndSpec); instances are immutable
 *     after creation except Stage.
 *   - The limited build never memoizes derived objects; cross-thread safety
 *     of reads bottoms out in the C API's internal locking.
 */

#ifndef LIGHTUSD_PY_INTERNAL_H_
#define LIGHTUSD_PY_INTERNAL_H_

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdint.h>
#include <string.h>

#include "lightusd-c.h"

/* PyBUF_READ/WRITE live behind the 3.11 limited-API guard in pybuffer.h, but
 * PyMemoryView_FromMemory (limited API since 3.3) needs them. Values are
 * ABI-stable. */
#ifndef PyBUF_READ
#define PyBUF_READ 0x100
#endif
#ifndef PyBUF_WRITE
#define PyBUF_WRITE 0x200
#endif

/* ============================================================
 * Module state
 * ============================================================ */

typedef struct {
  /* exceptions */
  PyObject* UsdError;
  PyObject* UsdParseError;
  PyObject* UsdIoError;
  PyObject* StaleHandleError;
  /* types */
  PyObject* StageType;
  PyObject* StageIterType;
  PyObject* PrimType;
  PyObject* PrimChildIterType;
  PyObject* AttributeType;
  PyObject* RelationshipType;
  PyObject* TimeSamplesType;
  PyObject* VariantSetsType;
  PyObject* VariantSetType;
  PyObject* ArrayType;
  /* render (tydra) types */
  PyObject* RenderSceneType;
  PyObject* RenderMeshType;
  PyObject* RenderMaterialType;
  PyObject* RenderTextureType;
  PyObject* RenderImageType;
  PyObject* RenderNodeType;
  PyObject* RenderLightType;
  PyObject* RenderCameraType;
  /* sentinel for authored value blocks (`= None`) */
  PyObject* ValueBlock;
  /* optional value normalizer registered by the pure-python facade:
   * callable(value, type_hint) -> ("pod", type:int, is_array:bool,
   * data:bytes, count:int) | ("str", type:int, s:str) | ("tokens", type:int,
   * items:tuple[str]) */
  PyObject* normalizer;
} lightusd_state;

/* State lookup: module-level functions receive the module as self; methods on
 * our heap types recover the module via their defining type. */
static inline lightusd_state* lightusd_state_from_module(PyObject* module) {
  return (lightusd_state*)PyModule_GetState(module);
}

lightusd_state* lightusd_state_from_type(PyTypeObject* tp);

static inline lightusd_state* lightusd_state_from_obj(PyObject* obj) {
  return lightusd_state_from_type(Py_TYPE(obj));
}

/* ============================================================
 * Instance structs
 * ============================================================ */

typedef struct {
  PyObject_HEAD
  lightusd_stage* stage; /* owned; NULL after close() */
} LightUSDStage;

typedef struct {
  PyObject_HEAD
  PyObject* stage;     /* strong ref to LightUSDStage */
  PyObject* path_utf8; /* bytes: prim path (for re-resolution) */
  lightusd_prim prim;
  uint64_t gen; /* stage generation captured at creation */
} LightUSDPrim;

typedef struct {
  PyObject_HEAD
  PyObject* prim;      /* strong ref to LightUSDPrim */
  PyObject* name;      /* str */
  PyObject* name_utf8; /* bytes: cached UTF-8 of name (limited-API friendly) */
} LightUSDNamedRef; /* shared shape for Attribute / Relationship / VariantSet */

typedef struct {
  PyObject_HEAD
  PyObject* prim; /* strong ref to LightUSDPrim */
} LightUSDPrimRef; /* shared shape for TimeSamples(via attr)?, VariantSets */

typedef struct {
  PyObject_HEAD
  PyObject* attr; /* strong ref to Attribute (LightUSDNamedRef) */
} LightUSDTimeSamples;

/* Zero-copy array view. Data lives either in stage-owned storage (owner keeps
 * the Stage/lightusd_value wrapper alive) or in an owned lightusd_value. */
typedef struct {
  PyObject_HEAD
  PyObject* owner;        /* strong ref keeping the memory alive (may be NULL
                             when owned_value is set) */
  lightusd_value* owned;      /* owned value destroyed in dealloc (may be NULL) */
  lightusd_value_view view;
  Py_ssize_t shape[2];
  int ndim;
  char format[4];       /* struct-style format char for the storage type */
  char typestr[8];      /* numpy __array_interface__ typestr, e.g. "<f4" */
  Py_ssize_t itemsize;
} LightUSDArray;

typedef struct {
  PyObject_HEAD
  PyObject* stage;   /* strong ref */
  lightusd_prim* stack;  /* DFS stack */
  Py_ssize_t top;    /* number of valid entries */
  Py_ssize_t cap;
  uint64_t gen;
} LightUSDStageIter;

typedef struct {
  PyObject_HEAD
  PyObject* prim; /* strong ref to LightUSDPrim */
  Py_ssize_t index;
} LightUSDChildIter;

/* ============================================================
 * Shared helpers (defined across the py-*.c files)
 * ============================================================ */

/* Raise the exception matching a lightusd_status; message from lightusd_last_error().
 * `what` is a fallback/context prefix (may be NULL). Always returns NULL. */
PyObject* lightusd_raise(lightusd_state* st, lightusd_status status, const char* what);

/* Wrap helpers (all return new references, NULL on error). */
PyObject* lightusd_wrap_prim(lightusd_state* st, PyObject* stage_obj, lightusd_prim prim);
PyObject* lightusd_wrap_array_borrowed(lightusd_state* st, PyObject* owner,
                                   const lightusd_value_view* view);
PyObject* lightusd_wrap_array_owned(lightusd_state* st, lightusd_value* owned,
                                const lightusd_value_view* view);

/* Convert a POD/array value view to a Python object.
 * - scalars -> bool/int/float/tuple
 * - arrays -> LightUSDArray (anchored to `owner`, or adopting `owned`)
 * - blocks -> ValueBlock sentinel
 * Does NOT handle string-family / dictionary views (caller's job). Steals
 * `owned` on success when non-NULL. */
PyObject* lightusd_view_to_python(lightusd_state* st, PyObject* owner,
                              lightusd_value* owned, const lightusd_value_view* view);

/* Convert an owned lightusd_value (any type incl. string family) to Python.
 * Steals `val` (destroys it unless an Array adopts it). */
PyObject* lightusd_value_to_python(lightusd_state* st, lightusd_value* val);

/* Convert a borrowed dict cursor to a new PyDict (recursive, copies). */
PyObject* lightusd_dict_to_python(lightusd_state* st, lightusd_dict_ref dict);

/* Fetch an attribute default value as Python (string family, token arrays,
 * PODs, arrays, blocks). `owner` anchors zero-copy arrays. */
PyObject* lightusd_attr_value_to_python(lightusd_state* st, PyObject* stage_obj,
                                    lightusd_prim prim, const char* name);

/* sv -> str (new ref). */
static inline PyObject* lightusd_sv_to_str(lightusd_sv sv) {
  return PyUnicode_FromStringAndSize(sv.data ? sv.data : "",
                                     (Py_ssize_t)sv.len);
}

/* PyUnicode_AsUTF8 joined the limited API only in 3.13; go through a
 * temporary bytes object instead. The returned pointer is valid while *tmp
 * lives; caller must Py_XDECREF(*tmp) afterwards (NULL-safe). */
static inline const char* lightusd_utf8(PyObject* s, PyObject** tmp) {
  *tmp = PyUnicode_AsUTF8String(s);
  if (!*tmp) return NULL;
  return PyBytes_AsString(*tmp);
}

/* Stage helpers */
static inline lightusd_stage* lightusd_stage_handle(PyObject* stage_obj) {
  return ((LightUSDStage*)stage_obj)->stage;
}

/* Returns the C stage handle after validating the Prim's generation; raises
 * StaleHandleError / UsdError and returns NULL on failure. */
lightusd_stage* lightusd_prim_stage_checked(lightusd_state* st, LightUSDPrim* prim);

/* Allocate an instance of a heap type created with PyType_FromModuleAndSpec
 * (bypasses tp_new so Python code cannot construct these directly). */
PyObject* lightusd_alloc(PyObject* type_obj);

/* Type spec registration (called from module exec). */
int lightusd_register_array_type(PyObject* module, lightusd_state* st);
int lightusd_register_render_types(PyObject* module, lightusd_state* st);
PyObject* lightusd_to_render_scene(PyObject* module, PyObject* args,
                               PyObject* kwargs);
int lightusd_register_prim_types(PyObject* module, lightusd_state* st);
int lightusd_register_stage_type(PyObject* module, lightusd_state* st);

#endif /* LIGHTUSD_PY_INTERNAL_H_ */
