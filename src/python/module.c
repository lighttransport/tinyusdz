/* SPDX-License-Identifier: Apache 2.0
 *
 * tinyusdz._core — CPython extension for tinyusdz.
 *
 * Target: Py_LIMITED_API >= 0x030B0000 (CPython 3.11+). Single ABI3 wheel
 * works on 3.11, 3.12, 3.13, 3.14+.
 *
 * 3.11 is the floor because the buffer protocol (Py_buffer, Py_bf_getbuffer)
 * became part of the stable ABI in that version (PEP 688 prerequisite work).
 *
 * This file uses only the stable Python C API. Types are created with
 * PyType_FromSpec; no static PyTypeObject definitions. Buffer protocol is
 * provided through PyType_Slot entries (Py_bf_getbuffer, Py_bf_releasebuffer).
 *
 * All C++ interop happens through c-tinyusd.h and c-tinyusd-helpers.h; this
 * translation unit is pure C.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* PyUnicode_AsUTF8 is part of the stable ABI from CPython 3.10+ but is
 * gated behind Py_LIMITED_API in older Python.h. Forward-declare to avoid
 * the "implicit declaration -> int" landmine. */
#if !defined(PyUnicode_AsUTF8)
PyAPI_FUNC(const char *) PyUnicode_AsUTF8(PyObject *unicode);
#endif

#include "../c-tinyusd.h"
#include "../c-tinyusd-helpers.h"
#include "../c-tinyusd-tydra.h"

/* ------------------------------------------------------------------------
 * Module-level exception types (filled at PyInit_*).
 * -------------------------------------------------------------------- */

static PyObject *UsdError = NULL;
static PyObject *UsdParseError = NULL;
static PyObject *UsdIoError = NULL;

/* Type refs (heap types). */
static PyObject *StageType = NULL;
static PyObject *PrimType = NULL;
static PyObject *AttributeType = NULL;
static PyObject *ValueType = NULL;
static PyObject *BufferViewType = NULL;
static PyObject *RenderSceneType = NULL;
static PyObject *RenderMeshType = NULL;
static PyObject *RenderMaterialType = NULL;
static PyObject *RenderCameraType = NULL;
static PyObject *RenderLightType = NULL;
static PyObject *RenderTextureType = NULL;
static PyObject *RenderImageType = NULL;
static PyObject *RenderBufferType = NULL;
static PyObject *RenderAnimationType = NULL;
static PyObject *RenderSkeletonType = NULL;
static PyObject *AnimationSamplerType = NULL;
static PyObject *RenderNodeType = NULL;

/* ------------------------------------------------------------------------
 * Stage
 *   Owns a CTinyUSDStage*. Created through load* module functions.
 * -------------------------------------------------------------------- */

typedef struct {
    PyObject_HEAD
    CTinyUSDStage *stage;
} StageObject;

static int
Stage_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    (void)args; (void)kwds;
    StageObject *s = (StageObject *)self;
    s->stage = c_tinyusd_stage_new();
    if (!s->stage) {
        PyErr_SetString(PyExc_MemoryError, "failed to allocate stage");
        return -1;
    }
    return 0;
}

static void
Stage_dealloc(PyObject *self)
{
    StageObject *s = (StageObject *)self;
    if (s->stage) {
        c_tinyusd_stage_free(s->stage);
        s->stage = NULL;
    }
    PyTypeObject *tp = Py_TYPE(self);
    freefunc tp_free = (freefunc)PyType_GetSlot(tp, Py_tp_free);
    tp_free(self);
    Py_DECREF(tp);
}

/* Build a Prim wrapper object holding a borrowed pointer and a strong ref to
 * its owning Stage to keep it alive. */
static PyObject *
make_prim(const CTinyUSDPrim *prim, PyObject *owner);

/* Coerce a Python object into a fresh CTinyUSDValue*. Returns NULL on
 * unsupported value (and sets a Python exception). The returned value must
 * be freed with c_tinyusd_value_free. `dtype` is an optional explicit USD
 * type-name hint (e.g. "token", "float[]"); pass NULL for inference. */
static CTinyUSDValue *
py_to_value(PyObject *obj, const char *dtype, char *out_type_name,
            size_t out_type_name_size);
static PyObject *
make_value_owning(CTinyUSDValue *value);
static char *
dup_normalized_asset_path(const char *s);

static PyObject *
Stage_export_to_string(PyObject *self, PyObject *args)
{
    (void)args;
    StageObject *s = (StageObject *)self;
    c_tinyusd_string_t *buf = c_tinyusd_string_new_empty();
    if (!buf) return PyErr_NoMemory();

    if (!c_tinyusd_stage_to_string(s->stage, buf)) {
        c_tinyusd_string_free(buf);
        PyErr_SetString(UsdError, "stage export_to_string failed");
        return NULL;
    }
    PyObject *r = PyUnicode_FromString(c_tinyusd_string_str(buf));
    c_tinyusd_string_free(buf);
    return r;
}

static PyObject *
Stage_save(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"path", "format", NULL};
    const char *path = NULL;
    const char *format = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|s", kwlist, &path, &format)) {
        return NULL;
    }

    CTinyUSDFormat fmt = C_TINYUSD_FORMAT_AUTO;
    if (format) {
        if (!strcmp(format, "usda")) fmt = C_TINYUSD_FORMAT_USDA;
        else if (!strcmp(format, "usdc")) fmt = C_TINYUSD_FORMAT_USDC;
        else if (!strcmp(format, "usdz")) fmt = C_TINYUSD_FORMAT_USDZ;
        else if (!strcmp(format, "auto")) fmt = C_TINYUSD_FORMAT_AUTO;
        else {
            PyErr_Format(PyExc_ValueError, "unknown format %R",
                         PyUnicode_FromString(format));
            return NULL;
        }
    }

    StageObject *s = (StageObject *)self;
    c_tinyusd_string_t *warn = c_tinyusd_string_new_empty();
    c_tinyusd_string_t *err = c_tinyusd_string_new_empty();
    int ok = c_tinyusd_stage_save_to_file(s->stage, path, fmt, warn, err);
    if (!ok) {
        const char *msg = c_tinyusd_string_str(err);
        PyErr_SetString(UsdIoError, msg && *msg ? msg : "stage save failed");
        c_tinyusd_string_free(warn);
        c_tinyusd_string_free(err);
        return NULL;
    }
    c_tinyusd_string_free(warn);
    c_tinyusd_string_free(err);
    Py_RETURN_NONE;
}

static PyObject *
Stage_get_prim_at_path(PyObject *self, PyObject *args)
{
    const char *path = NULL;
    if (!PyArg_ParseTuple(args, "s", &path)) return NULL;
    StageObject *s = (StageObject *)self;
    const CTinyUSDPrim *p = NULL;
    c_tinyusd_string_t *err = c_tinyusd_string_new_empty();
    int ok = c_tinyusd_stage_get_prim_at_path(s->stage, path, &p, err);
    c_tinyusd_string_free(err);
    if (!ok || !p) Py_RETURN_NONE;
    return make_prim(p, self);
}

static PyObject *
Stage_root_prims(PyObject *self, PyObject *args)
{
    (void)args;
    StageObject *s = (StageObject *)self;
    uint64_t n = c_tinyusd_stage_num_root_prims(s->stage);
    PyObject *list = PyList_New((Py_ssize_t)n);
    if (!list) return NULL;
    for (uint64_t i = 0; i < n; ++i) {
        const CTinyUSDPrim *p = NULL;
        if (!c_tinyusd_stage_get_root_prim(s->stage, i, &p) || !p) {
            Py_DECREF(list);
            PyErr_SetString(UsdError, "failed to read root prim");
            return NULL;
        }
        PyObject *w = make_prim(p, self);
        if (!w) { Py_DECREF(list); return NULL; }
        PyList_SetItem(list, (Py_ssize_t)i, w);  /* steals ref */
    }
    return list;
}

/* visit_prims bridge */
struct visit_ctx {
    PyObject *callback;
    PyObject *owner_stage;
    int py_err;
};

static int
py_visit_cb(const CTinyUSDPrim *prim, const CTinyUSDPath *path,
            uint32_t depth, void *ud)
{
    struct visit_ctx *c = (struct visit_ctx *)ud;
    if (c->py_err) return 0;
    PyObject *prim_obj = make_prim(prim, c->owner_stage);
    if (!prim_obj) { c->py_err = 1; return 0; }

    c_tinyusd_string_t *pstr = c_tinyusd_string_new_empty();
    const char *path_c = "";
    if (pstr && c_tinyusd_path_to_string(path, pstr)) {
        path_c = c_tinyusd_string_str(pstr);
    }
    PyObject *path_obj = PyUnicode_FromString(path_c ? path_c : "");
    if (pstr) c_tinyusd_string_free(pstr);
    if (!path_obj) { Py_DECREF(prim_obj); c->py_err = 1; return 0; }

    PyObject *res = PyObject_CallFunction(c->callback, "OOI", prim_obj,
                                          path_obj, (unsigned int)depth);
    Py_DECREF(prim_obj);
    Py_DECREF(path_obj);
    if (!res) { c->py_err = 1; return 0; }
    int keep = PyObject_IsTrue(res) || (res == Py_None);
    Py_DECREF(res);
    return keep ? 1 : 0;
}

static PyObject *
Stage_visit_prims(PyObject *self, PyObject *args)
{
    PyObject *cb;
    if (!PyArg_ParseTuple(args, "O", &cb)) return NULL;
    if (!PyCallable_Check(cb)) {
        PyErr_SetString(PyExc_TypeError, "callback must be callable");
        return NULL;
    }
    StageObject *s = (StageObject *)self;
    struct visit_ctx ctx = { cb, self, 0 };
    c_tinyusd_string_t *err = c_tinyusd_string_new_empty();
    int ok = c_tinyusd_stage_visit_prims(s->stage, py_visit_cb, &ctx, err);
    c_tinyusd_string_free(err);
    if (ctx.py_err) return NULL;
    if (!ok) {
        PyErr_SetString(UsdError, "visit_prims failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

/* Stage_add_root_prim is defined later (after PrimObject is declared). */
static PyObject *Stage_add_root_prim(PyObject *self, PyObject *args);

static PyObject *
Stage_repr(PyObject *self)
{
    StageObject *s = (StageObject *)self;
    uint64_t n = c_tinyusd_stage_num_root_prims(s->stage);

    c_tinyusd_string_t *buf = c_tinyusd_string_new_empty();
    int has_dp = 0, has_ax = 0, has_mpu = 0;
    char dp[128] = {0};
    const char *ax_str = NULL;
    double mpu = 0.0;
    if (buf) {
        if (c_tinyusd_stage_meta_get_string(s->stage, "defaultPrim", buf)) {
            const char *cs = c_tinyusd_string_str(buf);
            if (cs && *cs) {
                strncpy(dp, cs, sizeof(dp) - 1);
                has_dp = 1;
            }
        }
        c_tinyusd_string_replace(buf, "");
        if (c_tinyusd_stage_meta_get_string(s->stage, "upAxis", buf)) {
            const char *cs = c_tinyusd_string_str(buf);
            if (cs && *cs) {
                ax_str = (cs[0] == 'X') ? "X" : (cs[0] == 'Z') ? "Z" : "Y";
                has_ax = 1;
            }
        }
        c_tinyusd_string_free(buf);
    }
    has_mpu = c_tinyusd_stage_meta_get_double(s->stage, "metersPerUnit", &mpu);

    PyObject *parts = PyList_New(0);
    if (!parts) return NULL;
    PyObject *piece;
    piece = PyUnicode_FromFormat("root_prims=%llu", (unsigned long long)n);
    if (piece) { PyList_Append(parts, piece); Py_DECREF(piece); }
    if (has_dp) {
        piece = PyUnicode_FromFormat("defaultPrim=%s", dp);
        if (piece) { PyList_Append(parts, piece); Py_DECREF(piece); }
    }
    if (has_ax) {
        piece = PyUnicode_FromFormat("upAxis=%s", ax_str);
        if (piece) { PyList_Append(parts, piece); Py_DECREF(piece); }
    }
    if (has_mpu) {
        char mpu_buf[64];
        snprintf(mpu_buf, sizeof(mpu_buf), "metersPerUnit=%g", mpu);
        piece = PyUnicode_FromString(mpu_buf);
        if (piece) { PyList_Append(parts, piece); Py_DECREF(piece); }
    }
    PyObject *sep = PyUnicode_FromString(" ");
    PyObject *joined = PyUnicode_Join(sep, parts);
    Py_DECREF(sep);
    Py_DECREF(parts);
    if (!joined) return NULL;
    PyObject *out = PyUnicode_FromFormat("<tinyusdz.Stage %U>", joined);
    Py_DECREF(joined);
    return out;
}

static PyObject *
Stage_set_metadata(PyObject *self, PyObject *args)
{
    const char *key = NULL;
    PyObject *value = NULL;
    if (!PyArg_ParseTuple(args, "sO", &key, &value)) return NULL;
    StageObject *s = (StageObject *)self;
    int ok = 0;
    if (PyUnicode_Check(value)) {
        const char *sv = PyUnicode_AsUTF8(value);
        if (!sv) return NULL;
        ok = c_tinyusd_stage_meta_set_string(s->stage, key, sv);
    } else if (PyFloat_Check(value) || PyLong_Check(value)) {
        double dv = PyFloat_AsDouble(value);
        if (dv == -1.0 && PyErr_Occurred()) return NULL;
        ok = c_tinyusd_stage_meta_set_double(s->stage, key, dv);
    } else {
        PyErr_SetString(PyExc_TypeError,
                        "Stage metadata value must be str or number");
        return NULL;
    }
    if (!ok) {
        PyErr_Format(PyExc_ValueError,
                     "Unsupported stage metadata key/type: %s", key);
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Stage_get_metadata(PyObject *self, PyObject *args)
{
    const char *key = NULL;
    if (!PyArg_ParseTuple(args, "s", &key)) return NULL;
    StageObject *s = (StageObject *)self;
    /* Try string first */
    c_tinyusd_string_t *out = c_tinyusd_string_new_empty();
    if (c_tinyusd_stage_meta_get_string(s->stage, key, out)) {
        const char *cstr = c_tinyusd_string_str(out);
        PyObject *r = PyUnicode_FromString(cstr ? cstr : "");
        c_tinyusd_string_free(out);
        return r;
    }
    c_tinyusd_string_free(out);
    double dv = 0.0;
    if (c_tinyusd_stage_meta_get_double(s->stage, key, &dv)) {
        return PyFloat_FromDouble(dv);
    }
    Py_RETURN_NONE;
}

static PyObject *
Stage_set_default_prim(PyObject *self, PyObject *args)
{
    const char *name = NULL;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    StageObject *s = (StageObject *)self;
    if (!s->stage) {
        PyErr_SetString(UsdError, "Stage has no underlying handle");
        return NULL;
    }
    if (!c_tinyusd_stage_set_default_prim(s->stage, name)) {
        PyErr_SetString(UsdError, "set_default_prim failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Stage_get_default_prim(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    StageObject *s = (StageObject *)self;
    if (!s->stage) {
        PyErr_SetString(UsdError, "Stage has no underlying handle");
        return NULL;
    }
    c_tinyusd_string_t *out = c_tinyusd_string_new_empty();
    if (!c_tinyusd_stage_get_default_prim(s->stage, out)) {
        c_tinyusd_string_free(out);
        PyErr_SetString(UsdError, "get_default_prim failed");
        return NULL;
    }
    PyObject *r = PyUnicode_FromString(c_tinyusd_string_str(out));
    c_tinyusd_string_free(out);
    return r;
}

static PyObject *
Stage_set_string_meta(PyObject *self, PyObject *args, const char *key)
{
    const char *value = NULL;
    if (!PyArg_ParseTuple(args, "s", &value)) return NULL;
    StageObject *s = (StageObject *)self;
    if (!s->stage) {
        PyErr_SetString(UsdError, "Stage has no underlying handle");
        return NULL;
    }
    if (!c_tinyusd_stage_meta_set_string(s->stage, key, value)) {
        PyErr_Format(PyExc_ValueError,
                     "set_%s failed (invalid value '%s')", key, value);
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Stage_set_double_meta(PyObject *self, PyObject *args, const char *key)
{
    double value = 0.0;
    if (!PyArg_ParseTuple(args, "d", &value)) return NULL;
    StageObject *s = (StageObject *)self;
    if (!s->stage) {
        PyErr_SetString(UsdError, "Stage has no underlying handle");
        return NULL;
    }
    if (!c_tinyusd_stage_meta_set_double(s->stage, key, value)) {
        PyErr_Format(UsdError, "set_%s failed", key);
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *Stage_set_up_axis(PyObject *self, PyObject *args)
{ return Stage_set_string_meta(self, args, "upAxis"); }

static PyObject *Stage_set_meters_per_unit(PyObject *self, PyObject *args)
{ return Stage_set_double_meta(self, args, "metersPerUnit"); }

static PyObject *Stage_set_time_codes_per_second(PyObject *self, PyObject *args)
{ return Stage_set_double_meta(self, args, "timeCodesPerSecond"); }

static PyObject *Stage_set_frames_per_second(PyObject *self, PyObject *args)
{ return Stage_set_double_meta(self, args, "framesPerSecond"); }

static PyObject *Stage_set_start_time_code(PyObject *self, PyObject *args)
{ return Stage_set_double_meta(self, args, "startTimeCode"); }

static PyObject *Stage_set_end_time_code(PyObject *self, PyObject *args)
{ return Stage_set_double_meta(self, args, "endTimeCode"); }

static PyMethodDef Stage_methods[] = {
    {"export_to_string", Stage_export_to_string, METH_NOARGS,
     "Serialize stage to USDA string."},
    {"save", (PyCFunction)Stage_save, METH_VARARGS | METH_KEYWORDS,
     "save(path, format=None): write stage to a .usda/.usdc/.usdz file."},
    {"get_prim_at_path", Stage_get_prim_at_path, METH_VARARGS,
     "Return Prim at absolute path, or None."},
    {"root_prims", Stage_root_prims, METH_NOARGS,
     "List of root prims."},
    {"visit_prims", Stage_visit_prims, METH_VARARGS,
     "visit_prims(callback): DFS traversal. Callback signature (prim, path, depth)."},
    {"add_root_prim", Stage_add_root_prim, METH_VARARGS,
     "add_root_prim(prim): copy `prim` under this stage as a root prim."},
    {"set_metadata", Stage_set_metadata, METH_VARARGS,
     "set_metadata(key, value): set stage metadata (defaultPrim, upAxis, "
     "metersPerUnit, timeCodesPerSecond, framesPerSecond, startTimeCode, "
     "endTimeCode, doc, comment)."},
    {"get_metadata", Stage_get_metadata, METH_VARARGS,
     "get_metadata(key): return stage metadata value or None if unauthored."},
    {"set_default_prim", Stage_set_default_prim, METH_VARARGS,
     "set_default_prim(name): convenience for set_metadata('defaultPrim', name)."},
    {"get_default_prim", Stage_get_default_prim, METH_NOARGS,
     "get_default_prim(): return defaultPrim name, or '' if unauthored."},
    {"set_up_axis", Stage_set_up_axis, METH_VARARGS,
     "set_up_axis('X' | 'Y' | 'Z'): convenience for"
     " set_metadata('upAxis', ...)."},
    {"set_meters_per_unit", Stage_set_meters_per_unit, METH_VARARGS,
     "set_meters_per_unit(value): set the metersPerUnit stage metadatum."},
    {"set_time_codes_per_second", Stage_set_time_codes_per_second,
     METH_VARARGS,
     "set_time_codes_per_second(value): set timeCodesPerSecond."},
    {"set_frames_per_second", Stage_set_frames_per_second, METH_VARARGS,
     "set_frames_per_second(value): set framesPerSecond."},
    {"set_start_time_code", Stage_set_start_time_code, METH_VARARGS,
     "set_start_time_code(value): set startTimeCode."},
    {"set_end_time_code", Stage_set_end_time_code, METH_VARARGS,
     "set_end_time_code(value): set endTimeCode."},
    {NULL, NULL, 0, NULL}
};

static PyType_Slot Stage_slots[] = {
    {Py_tp_doc, "USD Stage (scene-graph container)."},
    {Py_tp_init, Stage_init},
    {Py_tp_dealloc, Stage_dealloc},
    {Py_tp_methods, Stage_methods},
    {Py_tp_repr, Stage_repr},
    {0, NULL}
};

static PyType_Spec Stage_spec = {
    "tinyusdz._core.Stage",
    sizeof(StageObject),
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    Stage_slots
};

/* ------------------------------------------------------------------------
 * Prim — borrowed pointer + strong ref to owner (Stage or parent Prim).
 * -------------------------------------------------------------------- */

typedef struct {
    PyObject_HEAD
    const CTinyUSDPrim *prim;
    PyObject *owner;   /* keeps the backing Stage (or parent Prim) alive,
                          or NULL when this Prim owns its C handle */
    int owns_prim;     /* 1 = we own `prim` and must free in dealloc */
} PrimObject;

static void
Prim_dealloc(PyObject *self)
{
    PrimObject *p = (PrimObject *)self;
    if (p->owns_prim && p->prim) {
        c_tinyusd_prim_free((CTinyUSDPrim *)p->prim);
    }
    p->prim = NULL;
    Py_XDECREF(p->owner);
    PyTypeObject *tp = Py_TYPE(self);
    freefunc tp_free = (freefunc)PyType_GetSlot(tp, Py_tp_free);
    tp_free(self);
    Py_DECREF(tp);
}

static PyObject *
make_prim(const CTinyUSDPrim *prim, PyObject *owner)
{
    PyTypeObject *tp = (PyTypeObject *)PrimType;
    allocfunc alloc = (allocfunc)PyType_GetSlot(tp, Py_tp_alloc);
    PrimObject *obj = (PrimObject *)alloc(tp, 0);
    if (!obj) return NULL;
    obj->prim = prim;
    obj->owns_prim = 0;
    Py_INCREF(owner);
    obj->owner = owner;
    return (PyObject *)obj;
}

/* Build a Prim wrapper that takes ownership of a fresh CTinyUSDPrim*. */
static PyObject *
make_prim_owning(CTinyUSDPrim *prim)
{
    PyTypeObject *tp = (PyTypeObject *)PrimType;
    allocfunc alloc = (allocfunc)PyType_GetSlot(tp, Py_tp_alloc);
    PrimObject *obj = (PrimObject *)alloc(tp, 0);
    if (!obj) {
        c_tinyusd_prim_free(prim);
        return NULL;
    }
    obj->prim = prim;
    obj->owns_prim = 1;
    obj->owner = NULL;
    return (PyObject *)obj;
}

/* Stage.add_root_prim implementation (forward-declared above). */
static PyObject *
Stage_add_root_prim(PyObject *self, PyObject *args)
{
    PyObject *prim_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &prim_obj)) return NULL;
    if (Py_TYPE(prim_obj) != (PyTypeObject *)PrimType) {
        PyErr_SetString(PyExc_TypeError, "expected a tinyusdz.Prim");
        return NULL;
    }
    StageObject *s = (StageObject *)self;
    PrimObject *p = (PrimObject *)prim_obj;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    c_tinyusd_string_t *err = c_tinyusd_string_new_empty();
    int ok = c_tinyusd_stage_add_root_prim(
        s->stage, (CTinyUSDPrim *)p->prim, err);
    if (!ok) {
        const char *msg = c_tinyusd_string_str(err);
        PyErr_SetString(UsdError, msg && *msg ? msg : "add_root_prim failed");
        c_tinyusd_string_free(err);
        return NULL;
    }
    c_tinyusd_string_free(err);
    Py_RETURN_NONE;
}

/* Construct an owned Prim from Python: tinyusdz.Prim(type_name, name=None). */
static int
Prim_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"type_name", "name", NULL};
    const char *type_name = NULL;
    const char *name = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|s", kwlist,
                                     &type_name, &name)) {
        return -1;
    }
    PrimObject *p = (PrimObject *)self;
    /* If this is being initialized via make_prim_owning, the prim is already
       set. But Python-level Stage()/Prim() goes through tp_alloc + tp_init,
       so prim is NULL here. */
    if (p->prim) {
        /* Already initialized (e.g. via make_prim_*); ignore re-init args. */
        return 0;
    }
    c_tinyusd_string_t *err = c_tinyusd_string_new_empty();
    CTinyUSDPrim *cp = c_tinyusd_prim_new(type_name, err);
    if (!cp) {
        const char *msg = c_tinyusd_string_str(err);
        PyErr_Format(PyExc_ValueError, "Prim(%s) failed: %s",
                     type_name, msg && *msg ? msg : "(no detail)");
        c_tinyusd_string_free(err);
        return -1;
    }
    c_tinyusd_string_free(err);
    if (name) {
        if (!c_tinyusd_prim_set_element_name(cp, name)) {
            c_tinyusd_prim_free(cp);
            PyErr_SetString(PyExc_ValueError, "set_element_name failed");
            return -1;
        }
    }
    p->prim = cp;
    p->owns_prim = 1;
    p->owner = NULL;
    return 0;
}

static PyObject *
Prim_get_type_name(PyObject *self, void *closure)
{
    (void)closure;
    PrimObject *p = (PrimObject *)self;
    const char *s = c_tinyusd_prim_type(p->prim);
    if (!s) Py_RETURN_NONE;
    return PyUnicode_FromString(s);
}

static PyObject *
Prim_get_element_name(PyObject *self, void *closure)
{
    (void)closure;
    PrimObject *p = (PrimObject *)self;
    const char *s = c_tinyusd_prim_element_name(p->prim);
    if (!s) Py_RETURN_NONE;
    return PyUnicode_FromString(s);
}

static PyObject *
Prim_name(PyObject *self, void *closure)
{
    return Prim_get_element_name(self, closure);
}

static PyObject *
Prim_children(PyObject *self, PyObject *args)
{
    (void)args;
    PrimObject *p = (PrimObject *)self;
    uint64_t n = c_tinyusd_prim_num_children(p->prim);
    PyObject *list = PyList_New((Py_ssize_t)n);
    if (!list) return NULL;
    for (uint64_t i = 0; i < n; ++i) {
        const CTinyUSDPrim *c = NULL;
        if (!c_tinyusd_prim_get_child(p->prim, i, &c) || !c) {
            Py_DECREF(list);
            PyErr_SetString(UsdError, "failed to read child prim");
            return NULL;
        }
        PyObject *w = make_prim(c, p->owner);
        if (!w) { Py_DECREF(list); return NULL; }
        PyList_SetItem(list, (Py_ssize_t)i, w);
    }
    return list;
}

static PyObject *
Prim_to_string(PyObject *self, PyObject *args)
{
    (void)args;
    PrimObject *p = (PrimObject *)self;
    c_tinyusd_string_t *s = c_tinyusd_string_new_empty();
    if (!s) return PyErr_NoMemory();
    if (!c_tinyusd_prim_to_string(p->prim, s)) {
        c_tinyusd_string_free(s);
        PyErr_SetString(UsdError, "prim to_string failed");
        return NULL;
    }
    PyObject *r = PyUnicode_FromString(c_tinyusd_string_str(s));
    c_tinyusd_string_free(s);
    return r;
}

/* Forward for attribute construction. */
static PyObject *
make_attribute_owning(CTinyUSDAttribute *attr);

static PyObject *
Prim_get_attribute(PyObject *self, PyObject *args)
{
    const char *name = NULL;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    PrimObject *p = (PrimObject *)self;
    CTinyUSDAttribute *a = c_tinyusd_prim_get_attribute(p->prim, name);
    if (!a) Py_RETURN_NONE;
    return make_attribute_owning(a);  /* takes ownership */
}

static PyObject *
Prim_property_names(PyObject *self, PyObject *args)
{
    (void)args;
    PrimObject *p = (PrimObject *)self;
    c_tinyusd_token_vector_t *tv = c_tinyusd_token_vector_new_empty();
    if (!tv) return PyErr_NoMemory();
    if (!c_tinyusd_prim_get_property_names(p->prim, tv)) {
        c_tinyusd_token_vector_free(tv);
        PyErr_SetString(UsdError, "failed to read property names");
        return NULL;
    }
    size_t n = c_tinyusd_token_vector_size(tv);
    PyObject *list = PyList_New((Py_ssize_t)n);
    if (!list) { c_tinyusd_token_vector_free(tv); return NULL; }
    for (size_t i = 0; i < n; ++i) {
        const char *s = c_tinyusd_token_vector_str(tv, i);
        PyObject *u = PyUnicode_FromString(s ? s : "");
        if (!u) { Py_DECREF(list); c_tinyusd_token_vector_free(tv); return NULL; }
        PyList_SetItem(list, (Py_ssize_t)i, u);
    }
    c_tinyusd_token_vector_free(tv);
    return list;
}

static PyObject *
Prim_add_child(PyObject *self, PyObject *args)
{
    PyObject *child_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &child_obj)) return NULL;
    if (Py_TYPE(child_obj) != (PyTypeObject *)PrimType) {
        PyErr_SetString(PyExc_TypeError, "expected a tinyusdz.Prim");
        return NULL;
    }
    PrimObject *parent = (PrimObject *)self;
    PrimObject *child = (PrimObject *)child_obj;
    if (!parent->prim || !child->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    /* C API: c_tinyusd_prim_append_child *copies* the child Prim. */
    if (!c_tinyusd_prim_append_child((CTinyUSDPrim *)parent->prim,
                                     (CTinyUSDPrim *)child->prim)) {
        PyErr_SetString(UsdError, "append_child failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Prim_set_element_name(PyObject *self, PyObject *args)
{
    const char *name = NULL;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    if (!c_tinyusd_prim_set_element_name((CTinyUSDPrim *)p->prim, name)) {
        PyErr_SetString(UsdError, "set_element_name failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Prim_set_attribute(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"name", "value", "dtype", NULL};
    const char *name = NULL;
    PyObject *py_value = NULL;
    const char *dtype = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "sO|s", kwlist,
                                     &name, &py_value, &dtype)) {
        return NULL;
    }
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    char type_name[64] = {0};
    CTinyUSDValue *val = py_to_value(py_value, dtype, type_name, sizeof(type_name));
    if (!val) return NULL;  /* exception already set */

    CTinyUSDAttribute *attr = c_tinyusd_attribute_new();
    if (!attr) {
        c_tinyusd_value_free(val);
        return PyErr_NoMemory();
    }
    c_tinyusd_attribute_set_name(attr, name);
    if (type_name[0]) {
        c_tinyusd_attribute_set_type_name(attr, type_name);
    }
    if (!c_tinyusd_attribute_set_value(attr, val)) {
        c_tinyusd_attribute_free(attr);
        c_tinyusd_value_free(val);
        PyErr_SetString(UsdError, "attribute_set_value failed");
        return NULL;
    }

    c_tinyusd_string_t *err = c_tinyusd_string_new_empty();
    int ok = c_tinyusd_prim_add_attribute((CTinyUSDPrim *)p->prim, attr, err);
    if (!ok) {
        const char *msg = c_tinyusd_string_str(err);
        PyErr_Format(UsdError, "set_attribute(%s) failed: %s", name,
                     msg && *msg ? msg : "(no detail)");
        c_tinyusd_string_free(err);
        c_tinyusd_attribute_free(attr);
        c_tinyusd_value_free(val);
        return NULL;
    }
    c_tinyusd_string_free(err);
    c_tinyusd_attribute_free(attr);
    c_tinyusd_value_free(val);
    Py_RETURN_NONE;
}

static PyObject *
Prim_set_metadata(PyObject *self, PyObject *args)
{
    const char *meta_name = NULL;
    PyObject *value = NULL;
    if (!PyArg_ParseTuple(args, "sO", &meta_name, &value)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    int ok = 0;
    if (PyBool_Check(value)) {
        ok = c_tinyusd_prim_meta_set_bool(
            (CTinyUSDPrim *)p->prim, meta_name,
            value == Py_True ? 1 : 0);
    } else if (PyUnicode_Check(value)) {
        const char *s = PyUnicode_AsUTF8(value);
        if (!s) return NULL;
        ok = c_tinyusd_prim_meta_set_string(
            (CTinyUSDPrim *)p->prim, meta_name, s);
    } else {
        PyErr_SetString(PyExc_TypeError,
                        "prim metadata value must be str or bool");
        return NULL;
    }
    if (!ok) {
        PyErr_Format(PyExc_ValueError,
                     "set_metadata failed (key=%s)", meta_name);
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Prim_apply_api_schema(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"schema_name", "instance_name", NULL};
    const char *schema = NULL;
    const char *instance = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|z", kwlist,
                                     &schema, &instance)) {
        return NULL;
    }
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    if (!c_tinyusd_prim_apply_api_schema((CTinyUSDPrim *)p->prim,
                                         schema, instance)) {
        PyErr_SetString(UsdError, "apply_api_schema failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Prim_api_schemas(PyObject *self, PyObject *args)
{
    (void)args;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) return PyList_New(0);
    c_tinyusd_string_t *buf = c_tinyusd_string_new_empty();
    if (!buf) return PyErr_NoMemory();
    int got = c_tinyusd_prim_get_api_schemas(p->prim, buf);
    if (!got) {
        c_tinyusd_string_free(buf);
        return PyList_New(0);
    }
    const char *s = c_tinyusd_string_str(buf);
    PyObject *raw = PyUnicode_FromString(s ? s : "");
    c_tinyusd_string_free(buf);
    if (!raw) return NULL;
    /* Split on comma in Python land. */
    PyObject *sep = PyUnicode_FromString(",");
    PyObject *parts = PyUnicode_Split(raw, sep, -1);
    Py_DECREF(sep);
    Py_DECREF(raw);
    return parts;
}

static PyObject *
Prim_get_metadata(PyObject *self, PyObject *args)
{
    const char *meta_name = NULL;
    if (!PyArg_ParseTuple(args, "s", &meta_name)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) Py_RETURN_NONE;
    /* Try string first */
    c_tinyusd_string_t *buf = c_tinyusd_string_new_empty();
    if (!buf) return PyErr_NoMemory();
    int got = c_tinyusd_prim_meta_get_string(p->prim, meta_name, buf);
    if (got) {
        PyObject *r = PyUnicode_FromString(c_tinyusd_string_str(buf));
        c_tinyusd_string_free(buf);
        return r;
    }
    c_tinyusd_string_free(buf);
    /* Try bool (active, hidden) */
    int b = 0;
    if (c_tinyusd_prim_meta_get_bool(p->prim, meta_name, &b)) {
        if (b) Py_RETURN_TRUE; else Py_RETURN_FALSE;
    }
    Py_RETURN_NONE;
}

static PyObject *
Prim_add_relationship(PyObject *self, PyObject *args)
{
    const char *name = NULL;
    PyObject *targets = NULL;
    if (!PyArg_ParseTuple(args, "sO", &name, &targets)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    /* Accept str (single target) or sequence of str. */
    PyObject *seq = NULL;
    int single = PyUnicode_Check(targets);
    Py_ssize_t n;
    if (single) {
        n = 1;
    } else {
        n = PySequence_Length(targets);
        if (n < 0) return NULL;
        seq = targets;
    }
    const char **paths = NULL;
    PyObject **items = NULL;
    if (n > 0) {
        paths = (const char **)PyMem_Malloc(sizeof(char *) * (size_t)n);
        if (!paths) return PyErr_NoMemory();
        items = (PyObject **)PyMem_Malloc(sizeof(PyObject *) * (size_t)n);
        if (!items) { PyMem_Free(paths); return PyErr_NoMemory(); }
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *it;
            if (single) { Py_INCREF(targets); it = targets; }
            else { it = PySequence_GetItem(seq, i); }
            if (!it) {
                for (Py_ssize_t k = 0; k < i; ++k) Py_DECREF(items[k]);
                PyMem_Free(items); PyMem_Free(paths);
                return NULL;
            }
            if (!PyUnicode_Check(it)) {
                Py_DECREF(it);
                for (Py_ssize_t k = 0; k < i; ++k) Py_DECREF(items[k]);
                PyMem_Free(items); PyMem_Free(paths);
                PyErr_SetString(PyExc_TypeError,
                                "all targets must be str");
                return NULL;
            }
            items[i] = it;
            paths[i] = PyUnicode_AsUTF8(it);
            if (!paths[i]) {
                for (Py_ssize_t k = 0; k <= i; ++k) Py_DECREF(items[k]);
                PyMem_Free(items); PyMem_Free(paths);
                return NULL;
            }
        }
    }
    c_tinyusd_string_t *err = c_tinyusd_string_new_empty();
    int ok = c_tinyusd_prim_add_relationship((CTinyUSDPrim *)p->prim, name,
                                             (uint64_t)n, paths, err);
    if (items) {
        for (Py_ssize_t k = 0; k < n; ++k) Py_DECREF(items[k]);
        PyMem_Free(items);
    }
    if (paths) PyMem_Free(paths);
    if (!ok) {
        const char *msg = c_tinyusd_string_str(err);
        PyErr_SetString(UsdError, msg && *msg ? msg : "add_relationship failed");
        c_tinyusd_string_free(err);
        return NULL;
    }
    c_tinyusd_string_free(err);
    Py_RETURN_NONE;
}

static PyObject *
Prim_get_relationship_targets(PyObject *self, PyObject *args)
{
    const char *name = NULL;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) Py_RETURN_NONE;
    c_tinyusd_string_t *buf = c_tinyusd_string_new_empty();
    if (!buf) return PyErr_NoMemory();
    int ok = c_tinyusd_prim_get_relationship_targets(p->prim, name, buf);
    if (!ok) { c_tinyusd_string_free(buf); Py_RETURN_NONE; }
    const char *cstr = c_tinyusd_string_str(buf);
    PyObject *raw = PyUnicode_FromString(cstr ? cstr : "");
    c_tinyusd_string_free(buf);
    if (!raw) return NULL;
    PyObject *sep = PyUnicode_FromString(",");
    PyObject *parts = PyUnicode_Split(raw, sep, -1);
    Py_DECREF(sep); Py_DECREF(raw);
    return parts;
}

static PyObject *
Prim_add_attribute_connection(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"name", "targets", "dtype", NULL};
    const char *name = NULL;
    PyObject *targets = NULL;
    const char *dtype = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "sO|z", kwlist,
                                     &name, &targets, &dtype)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    int single = PyUnicode_Check(targets);
    Py_ssize_t n;
    if (single) {
        n = 1;
    } else {
        n = PySequence_Length(targets);
        if (n < 0) return NULL;
    }
    if (n <= 0) {
        PyErr_SetString(PyExc_ValueError, "at least one target is required");
        return NULL;
    }
    const char **paths = (const char **)PyMem_Malloc(sizeof(char *) * (size_t)n);
    if (!paths) return PyErr_NoMemory();
    PyObject **items = (PyObject **)PyMem_Malloc(sizeof(PyObject *) * (size_t)n);
    if (!items) { PyMem_Free(paths); return PyErr_NoMemory(); }
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *it;
        if (single) { Py_INCREF(targets); it = targets; }
        else { it = PySequence_GetItem(targets, i); }
        if (!it) {
            for (Py_ssize_t k = 0; k < i; ++k) Py_DECREF(items[k]);
            PyMem_Free(items); PyMem_Free(paths); return NULL;
        }
        if (!PyUnicode_Check(it)) {
            Py_DECREF(it);
            for (Py_ssize_t k = 0; k < i; ++k) Py_DECREF(items[k]);
            PyMem_Free(items); PyMem_Free(paths);
            PyErr_SetString(PyExc_TypeError, "all targets must be str");
            return NULL;
        }
        items[i] = it;
        paths[i] = PyUnicode_AsUTF8(it);
        if (!paths[i]) {
            for (Py_ssize_t k = 0; k <= i; ++k) Py_DECREF(items[k]);
            PyMem_Free(items); PyMem_Free(paths); return NULL;
        }
    }
    c_tinyusd_string_t *err = c_tinyusd_string_new_empty();
    int ok = c_tinyusd_prim_add_attribute_connection(
        (CTinyUSDPrim *)p->prim, name, dtype,
        (uint64_t)n, paths, err);
    for (Py_ssize_t k = 0; k < n; ++k) Py_DECREF(items[k]);
    PyMem_Free(items); PyMem_Free(paths);
    if (!ok) {
        const char *msg = c_tinyusd_string_str(err);
        PyErr_SetString(UsdError,
                        msg && *msg ? msg : "add_attribute_connection failed");
        c_tinyusd_string_free(err);
        return NULL;
    }
    c_tinyusd_string_free(err);
    Py_RETURN_NONE;
}

static PyObject *
Prim_get_attribute_connections(PyObject *self, PyObject *args)
{
    const char *name = NULL;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) Py_RETURN_NONE;
    c_tinyusd_string_t *buf = c_tinyusd_string_new_empty();
    if (!buf) return PyErr_NoMemory();
    int ok = c_tinyusd_prim_get_attribute_connections(p->prim, name, buf);
    if (!ok) { c_tinyusd_string_free(buf); Py_RETURN_NONE; }
    const char *cstr = c_tinyusd_string_str(buf);
    PyObject *raw = PyUnicode_FromString(cstr ? cstr : "");
    c_tinyusd_string_free(buf);
    if (!raw) return NULL;
    PyObject *sep = PyUnicode_FromString(",");
    PyObject *parts = PyUnicode_Split(raw, sep, -1);
    Py_DECREF(sep); Py_DECREF(raw);
    return parts;
}

static PyObject *
Prim_set_attribute_metadata(PyObject *self, PyObject *args)
{
    const char *attr_name = NULL;
    const char *meta_key = NULL;
    PyObject *value = NULL;
    if (!PyArg_ParseTuple(args, "ssO", &attr_name, &meta_key, &value)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    int ok = 0;
    if (PyBool_Check(value)) {
        ok = c_tinyusd_prim_attribute_meta_set_bool(
            (CTinyUSDPrim *)p->prim, attr_name, meta_key,
            value == Py_True ? 1 : 0);
    } else if (PyUnicode_Check(value)) {
        const char *s = PyUnicode_AsUTF8(value);
        if (!s) return NULL;
        ok = c_tinyusd_prim_attribute_meta_set_string(
            (CTinyUSDPrim *)p->prim, attr_name, meta_key, s);
    } else {
        PyErr_SetString(PyExc_TypeError,
                        "attribute metadata value must be str or bool");
        return NULL;
    }
    if (!ok) {
        PyErr_Format(PyExc_ValueError,
                     "set_attribute_metadata failed (attr=%s, key=%s)",
                     attr_name, meta_key);
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Prim_get_attribute_metadata(PyObject *self, PyObject *args)
{
    const char *attr_name = NULL;
    const char *meta_key = NULL;
    if (!PyArg_ParseTuple(args, "ss", &attr_name, &meta_key)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) Py_RETURN_NONE;
    c_tinyusd_string_t *buf = c_tinyusd_string_new_empty();
    if (!buf) return PyErr_NoMemory();
    int ok = c_tinyusd_prim_attribute_meta_get_string(
        p->prim, attr_name, meta_key, buf);
    if (!ok) { c_tinyusd_string_free(buf); Py_RETURN_NONE; }
    PyObject *r = PyUnicode_FromString(c_tinyusd_string_str(buf));
    c_tinyusd_string_free(buf);
    return r;
}

static PyObject *
Prim_set_attribute_at_time(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"name", "time", "value", "dtype", NULL};
    const char *name = NULL;
    double time = 0.0;
    PyObject *py_value = NULL;
    const char *dtype = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "sdO|z", kwlist,
                                     &name, &time, &py_value, &dtype)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    char type_name[64] = {0};
    CTinyUSDValue *val = py_to_value(py_value, dtype, type_name, sizeof(type_name));
    if (!val) return NULL;
    c_tinyusd_string_t *err = c_tinyusd_string_new_empty();
    int ok = c_tinyusd_prim_set_attribute_timesample(
        (CTinyUSDPrim *)p->prim, name, time, val,
        dtype ? dtype : type_name, err);
    c_tinyusd_value_free(val);
    if (!ok) {
        const char *msg = c_tinyusd_string_str(err);
        PyErr_SetString(UsdError,
                        msg && *msg ? msg : "set_attribute_at_time failed");
        c_tinyusd_string_free(err);
        return NULL;
    }
    c_tinyusd_string_free(err);
    Py_RETURN_NONE;
}

static PyObject *
Prim_get_attribute_timesamples(PyObject *self, PyObject *args)
{
    const char *name = NULL;
    const char *attr_type_name = NULL;
    CTinyUSDAttribute *attr = NULL;
    c_tinyusd_string_t *type_name_buf = NULL;
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) Py_RETURN_NONE;
    attr = c_tinyusd_prim_get_attribute(p->prim, name);
    if (attr) {
        type_name_buf = c_tinyusd_string_new_empty();
        if (!type_name_buf) {
            c_tinyusd_attribute_free(attr);
            return PyErr_NoMemory();
        }
        if (c_tinyusd_attribute_get_type_name(attr, type_name_buf)) {
            attr_type_name = c_tinyusd_string_str(type_name_buf);
        }
    }
    uint64_t count = c_tinyusd_prim_get_attribute_timesample_count(
        p->prim, name);
    PyObject *out = PyList_New((Py_ssize_t)count);
    if (!out) {
        if (type_name_buf) c_tinyusd_string_free(type_name_buf);
        if (attr) c_tinyusd_attribute_free(attr);
        return NULL;
    }
    for (uint64_t i = 0; i < count; ++i) {
        double t = 0.0;
        CTinyUSDValue *v = NULL;
        if (!c_tinyusd_prim_get_attribute_timesample(p->prim, name, i, &t, &v)) {
            if (type_name_buf) c_tinyusd_string_free(type_name_buf);
            if (attr) c_tinyusd_attribute_free(attr);
            Py_DECREF(out);
            Py_RETURN_NONE;
        }
        PyObject *tup;
        PyObject *py_val = NULL;

        if (attr_type_name &&
            (!strcmp(attr_type_name, "half") || !strcmp(attr_type_name, "uint64"))) {
            c_tinyusd_string_t *sample_buf = c_tinyusd_string_new_empty();
            if (!sample_buf) {
                c_tinyusd_value_free(v);
                if (type_name_buf) c_tinyusd_string_free(type_name_buf);
                if (attr) c_tinyusd_attribute_free(attr);
                Py_DECREF(out);
                return PyErr_NoMemory();
            }
            if (c_tinyusd_value_to_string(v, sample_buf)) {
                const char *s = c_tinyusd_string_str(sample_buf);
                if (!strcmp(attr_type_name, "half")) {
                    py_val = PyFloat_FromDouble(s ? strtod(s, NULL) : 0.0);
                } else {
                    py_val = PyLong_FromUnsignedLongLong(
                        s ? strtoull(s, NULL, 10) : 0ULL);
                }
            }
            c_tinyusd_string_free(sample_buf);
            c_tinyusd_value_free(v);
        } else {
            py_val = make_value_owning(v);
        }

        if (!py_val) {
            if (type_name_buf) c_tinyusd_string_free(type_name_buf);
            if (attr) c_tinyusd_attribute_free(attr);
            Py_DECREF(out);
            return NULL;
        }
        tup = Py_BuildValue("(dO)", t, py_val);
        Py_DECREF(py_val);
        if (!tup) {
            if (type_name_buf) c_tinyusd_string_free(type_name_buf);
            if (attr) c_tinyusd_attribute_free(attr);
            Py_DECREF(out);
            return NULL;
        }
        PyList_SetItem(out, (Py_ssize_t)i, tup);
    }
    if (type_name_buf) c_tinyusd_string_free(type_name_buf);
    if (attr) c_tinyusd_attribute_free(attr);
    return out;
}

static PyObject *
Prim_repr(PyObject *self)
{
    PrimObject *p = (PrimObject *)self;
    const char *t = c_tinyusd_prim_type(p->prim);
    const char *n = c_tinyusd_prim_element_name(p->prim);
    uint64_t nch = p->prim ? c_tinyusd_prim_num_children(p->prim) : 0;
    /* property count via tydra GetPropertyNames is heavyweight; use the
     * lightweight num_props if available. */
    return PyUnicode_FromFormat(
        "<tinyusdz.Prim type=%s name=%s children=%llu>",
        t ? t : "?", n ? n : "?", (unsigned long long)nch);
}

static int
parse_listedit_qual(const char *s, CTinyUSDListEditQual *out)
{
    if (!s || !*s || !strcmp(s, "prepend")) {
        *out = C_TINYUSD_LISTEDITQUAL_PREPEND;
        return 1;
    }
    if (!strcmp(s, "append")) {
        *out = C_TINYUSD_LISTEDITQUAL_APPEND;
        return 1;
    }
    if (!strcmp(s, "add")) {
        *out = C_TINYUSD_LISTEDITQUAL_ADD;
        return 1;
    }
    if (!strcmp(s, "delete")) {
        *out = C_TINYUSD_LISTEDITQUAL_DELETE;
        return 1;
    }
    if (!strcmp(s, "explicit") || !strcmp(s, "reset")) {
        *out = C_TINYUSD_LISTEDITQUAL_RESETTOEXPLICIT;
        return 1;
    }
    if (!strcmp(s, "order")) {
        *out = C_TINYUSD_LISTEDITQUAL_ORDER;
        return 1;
    }
    PyErr_Format(PyExc_ValueError,
                 "unknown qualifier '%s' (expected one of:"
                 " prepend, append, add, delete, explicit, order)", s);
    return 0;
}

static PyObject *
Prim_add_arc_asset(PyObject *self, PyObject *args, PyObject *kwds, int is_payload)
{
    static char *kwlist[] = {"asset_path", "prim_path", "offset", "scale",
                             "qualifier", NULL};
    const char *asset_path = NULL;
    const char *prim_path = NULL;
    double offset = 0.0;
    double scale = 1.0;
    const char *qstr = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|zdds", kwlist,
                                     &asset_path, &prim_path, &offset, &scale,
                                     &qstr)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    CTinyUSDListEditQual q;
    if (!parse_listedit_qual(qstr, &q)) return NULL;
    int ok = is_payload
        ? c_tinyusd_prim_add_payload(p->prim, q, asset_path, prim_path,
                                     offset, scale)
        : c_tinyusd_prim_add_reference(p->prim, q, asset_path, prim_path,
                                       offset, scale);
    if (!ok) {
        PyErr_SetString(UsdError,
                        is_payload ? "add_payload failed"
                                   : "add_reference failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Prim_add_reference(PyObject *self, PyObject *args, PyObject *kwds)
{
    return Prim_add_arc_asset(self, args, kwds, 0);
}

static PyObject *
Prim_add_payload(PyObject *self, PyObject *args, PyObject *kwds)
{
    return Prim_add_arc_asset(self, args, kwds, 1);
}

static PyObject *
Prim_add_arc_path(PyObject *self, PyObject *args, PyObject *kwds, int is_specialize)
{
    static char *kwlist[] = {"prim_path", "qualifier", NULL};
    const char *prim_path = NULL;
    const char *qstr = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|s", kwlist,
                                     &prim_path, &qstr)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    CTinyUSDListEditQual q;
    if (!parse_listedit_qual(qstr, &q)) return NULL;
    int ok = is_specialize
        ? c_tinyusd_prim_add_specialize(p->prim, q, prim_path)
        : c_tinyusd_prim_add_inherit(p->prim, q, prim_path);
    if (!ok) {
        PyErr_SetString(UsdError,
                        is_specialize ? "add_specialize failed"
                                      : "add_inherit failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Prim_add_inherit(PyObject *self, PyObject *args, PyObject *kwds)
{
    return Prim_add_arc_path(self, args, kwds, 0);
}

static PyObject *
Prim_add_specialize(PyObject *self, PyObject *args, PyObject *kwds)
{
    return Prim_add_arc_path(self, args, kwds, 1);
}

#define PRIM_CLEAR_ARC(NAME, FN, LABEL)                                  \
    static PyObject *NAME(PyObject *self, PyObject *Py_UNUSED(ignored))  \
    {                                                                    \
        PrimObject *p = (PrimObject *)self;                              \
        if (!p->prim) {                                                  \
            PyErr_SetString(UsdError, "Prim has no underlying handle"); \
            return NULL;                                                 \
        }                                                                \
        if (!FN(p->prim)) {                                              \
            PyErr_SetString(UsdError, LABEL " failed");                  \
            return NULL;                                                 \
        }                                                                \
        Py_RETURN_NONE;                                                  \
    }

PRIM_CLEAR_ARC(Prim_clear_references, c_tinyusd_prim_clear_references,
               "clear_references")
PRIM_CLEAR_ARC(Prim_clear_payload, c_tinyusd_prim_clear_payload,
               "clear_payload")
PRIM_CLEAR_ARC(Prim_clear_inherits, c_tinyusd_prim_clear_inherits,
               "clear_inherits")
PRIM_CLEAR_ARC(Prim_clear_specializes, c_tinyusd_prim_clear_specializes,
               "clear_specializes")

#undef PRIM_CLEAR_ARC

static PyObject *
Prim_add_variant_set_name(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"name", "qualifier", NULL};
    const char *name = NULL;
    const char *qstr = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|s", kwlist,
                                     &name, &qstr)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    CTinyUSDListEditQual q;
    if (!parse_listedit_qual(qstr, &q)) return NULL;
    if (!c_tinyusd_prim_add_variant_set_name(p->prim, q, name)) {
        PyErr_SetString(UsdError, "add_variant_set_name failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Prim_clear_variant_set_names(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    if (!c_tinyusd_prim_clear_variant_set_names(p->prim)) {
        PyErr_SetString(UsdError, "clear_variant_set_names failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Prim_set_variant_selection(PyObject *self, PyObject *args)
{
    const char *vset = NULL;
    const char *vname = NULL;
    if (!PyArg_ParseTuple(args, "ss", &vset, &vname)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    if (!c_tinyusd_prim_set_variant_selection(p->prim, vset, vname)) {
        PyErr_SetString(UsdError, "set_variant_selection failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Prim_clear_variant_selection(PyObject *self, PyObject *args)
{
    const char *vset = NULL;
    if (!PyArg_ParseTuple(args, "|z", &vset)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    if (!c_tinyusd_prim_clear_variant_selection(p->prim, vset)) {
        PyErr_SetString(UsdError, "clear_variant_selection failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Prim_define_variant(PyObject *self, PyObject *args)
{
    const char *vset = NULL;
    const char *vname = NULL;
    if (!PyArg_ParseTuple(args, "ss", &vset, &vname)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    if (!c_tinyusd_prim_define_variant(p->prim, vset, vname)) {
        PyErr_SetString(UsdError, "define_variant failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Prim_variant_add_child(PyObject *self, PyObject *args)
{
    const char *vset = NULL;
    const char *vname = NULL;
    PyObject *child_obj = NULL;
    if (!PyArg_ParseTuple(args, "ssO", &vset, &vname, &child_obj))
        return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    if (Py_TYPE(child_obj) != (PyTypeObject *)PrimType) {
        PyErr_SetString(PyExc_TypeError, "child must be a tinyusdz.Prim");
        return NULL;
    }
    PrimObject *child = (PrimObject *)child_obj;
    if (!child->prim) {
        PyErr_SetString(UsdError, "child Prim has no underlying handle");
        return NULL;
    }
    if (!c_tinyusd_prim_variant_add_child(p->prim, vset, vname, child->prim)) {
        PyErr_SetString(UsdError, "variant_add_child failed");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Prim_variant_set_attribute(PyObject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"variant_set_name", "variant_name",
                             "attr_name", "value", "dtype", NULL};
    const char *vset = NULL;
    const char *vname = NULL;
    const char *attr_name = NULL;
    PyObject *py_value = NULL;
    const char *dtype = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "sssO|s", kwlist,
                                     &vset, &vname, &attr_name, &py_value,
                                     &dtype)) return NULL;
    PrimObject *p = (PrimObject *)self;
    if (!p->prim) {
        PyErr_SetString(UsdError, "Prim has no underlying handle");
        return NULL;
    }
    char type_name[64] = {0};
    CTinyUSDValue *val = py_to_value(py_value, dtype, type_name,
                                     sizeof(type_name));
    if (!val) return NULL;
    CTinyUSDAttribute *attr = c_tinyusd_attribute_new();
    if (!attr) {
        c_tinyusd_value_free(val);
        return PyErr_NoMemory();
    }
    c_tinyusd_attribute_set_name(attr, attr_name);
    if (type_name[0]) {
        c_tinyusd_attribute_set_type_name(attr, type_name);
    }
    if (!c_tinyusd_attribute_set_value(attr, val)) {
        c_tinyusd_attribute_free(attr);
        c_tinyusd_value_free(val);
        PyErr_SetString(UsdError, "attribute_set_value failed");
        return NULL;
    }
    c_tinyusd_string_t *err = c_tinyusd_string_new_empty();
    int ok = c_tinyusd_prim_variant_add_attribute(p->prim, vset, vname,
                                                  attr, err);
    c_tinyusd_attribute_free(attr);
    c_tinyusd_value_free(val);
    if (!ok) {
        const char *msg = c_tinyusd_string_str(err);
        PyErr_Format(UsdError,
                     "variant_set_attribute(%s) failed: %s", attr_name,
                     msg && *msg ? msg : "(no detail)");
        c_tinyusd_string_free(err);
        return NULL;
    }
    c_tinyusd_string_free(err);
    Py_RETURN_NONE;
}

static PyMethodDef Prim_methods[] = {
    {"children", Prim_children, METH_NOARGS, "List of child Prims."},
    {"to_string", Prim_to_string, METH_NOARGS, "USDA text of this prim subtree."},
    {"get_attribute", Prim_get_attribute, METH_VARARGS,
     "get_attribute(name) -> Attribute or None."},
    {"property_names", Prim_property_names, METH_NOARGS,
     "List of property (attribute + relationship) names."},
    {"add_child", Prim_add_child, METH_VARARGS,
     "add_child(prim): copy `prim` as a child of this prim."},
    {"set_element_name", Prim_set_element_name, METH_VARARGS,
     "set_element_name(name): rename this prim's leaf path component."},
    {"set_attribute", (PyCFunction)Prim_set_attribute,
     METH_VARARGS | METH_KEYWORDS,
     "set_attribute(name, value, dtype=None): author an attribute."},
    {"set_metadata", Prim_set_metadata, METH_VARARGS,
     "set_metadata(name, string_value): author a string-typed prim metadatum"
     " (kind, doc, comment, displayName)."},
    {"get_metadata", Prim_get_metadata, METH_VARARGS,
     "get_metadata(name) -> str | None: read a string-typed prim metadatum."},
    {"apply_api_schema", (PyCFunction)Prim_apply_api_schema,
     METH_VARARGS | METH_KEYWORDS,
     "apply_api_schema(schema_name, instance_name=None): append an applied"
     " API schema (e.g. 'MaterialXConfigAPI', 'SkelBindingAPI')."},
    {"api_schemas", Prim_api_schemas, METH_NOARGS,
     "api_schemas() -> list[str]: applied API schemas, with multi-apply"
     " instance names appended as 'Schema:instance'."},
    {"add_relationship", Prim_add_relationship, METH_VARARGS,
     "add_relationship(name, targets): author a Relationship property."
     " `targets` is a path string or sequence of path strings."},
    {"get_relationship_targets", Prim_get_relationship_targets, METH_VARARGS,
     "get_relationship_targets(name) -> list[str] | None: read target paths"
     " of a Relationship authored on this prim."},
    {"add_attribute_connection", (PyCFunction)Prim_add_attribute_connection,
     METH_VARARGS | METH_KEYWORDS,
     "add_attribute_connection(name, targets, dtype=None): author an attribute"
     " connection (the `.connect = </path>` form). `targets` may be a single"
     " path str or a list of paths."},
    {"get_attribute_connections", Prim_get_attribute_connections, METH_VARARGS,
     "get_attribute_connections(name) -> list[str] | None: read connection"
     " target paths for a connectable attribute."},
    {"set_attribute_metadata", Prim_set_attribute_metadata, METH_VARARGS,
     "set_attribute_metadata(attr_name, key, value): author an attribute"
     " metadatum (displayName, doc, displayGroup, interpolation, colorSpace,"
     " hidden, custom)."},
    {"get_attribute_metadata", Prim_get_attribute_metadata, METH_VARARGS,
     "get_attribute_metadata(attr_name, key) -> str | None: read an attribute"
     " metadatum as a string."},
    {"set_attribute_at_time", (PyCFunction)Prim_set_attribute_at_time,
     METH_VARARGS | METH_KEYWORDS,
     "set_attribute_at_time(name, time, value, dtype=None): author a single"
     " (time, value) sample on a time-sampled attribute. Repeated calls"
     " with different `time` build up the TimeSamples vector."},
    {"get_attribute_timesamples", Prim_get_attribute_timesamples, METH_VARARGS,
     "get_attribute_timesamples(name) -> list[(time, Value)]: read"
     " authored time samples."},
    {"add_reference", (PyCFunction)Prim_add_reference,
     METH_VARARGS | METH_KEYWORDS,
     "add_reference(asset_path, prim_path=None, offset=0.0, scale=1.0,"
     " qualifier='prepend'): author a Reference arc on this prim. Pass"
     " an empty string for asset_path for an internal reference."},
    {"add_payload", (PyCFunction)Prim_add_payload,
     METH_VARARGS | METH_KEYWORDS,
     "add_payload(asset_path, prim_path=None, offset=0.0, scale=1.0,"
     " qualifier='prepend'): author a Payload arc on this prim."},
    {"add_inherit", (PyCFunction)Prim_add_inherit,
     METH_VARARGS | METH_KEYWORDS,
     "add_inherit(prim_path, qualifier='prepend'): author an inherits arc."},
    {"add_specialize", (PyCFunction)Prim_add_specialize,
     METH_VARARGS | METH_KEYWORDS,
     "add_specialize(prim_path, qualifier='prepend'): author a specializes arc."},
    {"clear_references", Prim_clear_references, METH_NOARGS,
     "clear_references(): drop all authored references."},
    {"clear_payload", Prim_clear_payload, METH_NOARGS,
     "clear_payload(): drop all authored payloads."},
    {"clear_inherits", Prim_clear_inherits, METH_NOARGS,
     "clear_inherits(): drop all authored inherits."},
    {"clear_specializes", Prim_clear_specializes, METH_NOARGS,
     "clear_specializes(): drop all authored specializes."},
    {"add_variant_set_name", (PyCFunction)Prim_add_variant_set_name,
     METH_VARARGS | METH_KEYWORDS,
     "add_variant_set_name(name, qualifier='prepend'): declare a"
     " variantSet name (`variantSets = [...]` listop)."},
    {"clear_variant_set_names", Prim_clear_variant_set_names, METH_NOARGS,
     "clear_variant_set_names(): drop the authored variantSets list."},
    {"set_variant_selection", Prim_set_variant_selection, METH_VARARGS,
     "set_variant_selection(variant_set_name, variant_name): set/replace"
     " a single entry in the prim's `variants = {...}` map."},
    {"clear_variant_selection", Prim_clear_variant_selection, METH_VARARGS,
     "clear_variant_selection(variant_set_name=None): drop a single"
     " entry, or all entries if name is None/empty."},
    {"define_variant", Prim_define_variant, METH_VARARGS,
     "define_variant(variant_set_name, variant_name): create an empty"
     " variant entry; populate via variant_add_child /"
     " variant_set_attribute."},
    {"variant_add_child", Prim_variant_add_child, METH_VARARGS,
     "variant_add_child(variant_set_name, variant_name, child_prim):"
     " copy `child_prim` as a primChild of the named variant."},
    {"variant_set_attribute", (PyCFunction)Prim_variant_set_attribute,
     METH_VARARGS | METH_KEYWORDS,
     "variant_set_attribute(variant_set_name, variant_name, attr_name,"
     " value, dtype=None): author an attribute inside the named"
     " variant."},
    {NULL, NULL, 0, NULL}
};

static PyGetSetDef Prim_getset[] = {
    {"type_name", Prim_get_type_name, NULL,
     "Schema type name, e.g. 'Xform', 'Mesh'.", NULL},
    {"element_name", Prim_get_element_name, NULL,
     "Element name (last path segment).", NULL},
    {"name", Prim_name, NULL,
     "Alias of element_name.", NULL},
    {NULL}
};

static PyType_Slot Prim_slots[] = {
    {Py_tp_doc, "USD primitive."},
    {Py_tp_init, Prim_init},
    {Py_tp_dealloc, Prim_dealloc},
    {Py_tp_methods, Prim_methods},
    {Py_tp_getset, Prim_getset},
    {Py_tp_repr, Prim_repr},
    {0, NULL}
};

static PyType_Spec Prim_spec = {
    "tinyusdz._core.Prim",
    sizeof(PrimObject),
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    Prim_slots
};

/* ------------------------------------------------------------------------
 * Value — borrowed pointer + owner (Attribute or Stage).
 * Supports buffer protocol for array types.
 * -------------------------------------------------------------------- */

typedef struct {
    PyObject_HEAD
    const CTinyUSDValue *value;
    PyObject *owner;
    int owns_value;
} ValueObject;

static PyObject *
make_value(const CTinyUSDValue *value, PyObject *owner);
static PyObject *
make_value_owning(CTinyUSDValue *value);
static char *
dup_normalized_asset_path(const char *s);

static void
Value_dealloc(PyObject *self)
{
    ValueObject *v = (ValueObject *)self;
    if (v->owns_value && v->value) {
        c_tinyusd_value_free((CTinyUSDValue *)v->value);
    }
    Py_XDECREF(v->owner);
    PyTypeObject *tp = Py_TYPE(self);
    freefunc tp_free = (freefunc)PyType_GetSlot(tp, Py_tp_free);
    tp_free(self);
    Py_DECREF(tp);
}

static PyObject *
Value_get_type_name(PyObject *self, void *closure)
{
    (void)closure;
    ValueObject *v = (ValueObject *)self;
    CTinyUSDValueType t = c_tinyusd_value_type(v->value);
    const char *n = c_tinyusd_value_type_name(t);
    return PyUnicode_FromString(n ? n : "unknown");
}

static PyObject *
Value_is_array(PyObject *self, void *closure)
{
    (void)closure;
    ValueObject *v = (ValueObject *)self;
    return PyBool_FromLong(c_tinyusd_value_is_array(v->value));
}

static PyObject *
Value_to_string(PyObject *self, PyObject *args)
{
    (void)args;
    ValueObject *v = (ValueObject *)self;
    c_tinyusd_string_t *s = c_tinyusd_string_new_empty();
    if (!s) return PyErr_NoMemory();
    if (!c_tinyusd_value_to_string(v->value, s)) {
        c_tinyusd_string_free(s);
        PyErr_SetString(UsdError, "value to_string failed");
        return NULL;
    }
    PyObject *r = PyUnicode_FromString(c_tinyusd_string_str(s));
    c_tinyusd_string_free(s);
    return r;
}

static PyObject *
Value_as_scalar(PyObject *self, PyObject *args)
{
    (void)args;
    ValueObject *v = (ValueObject *)self;
    if (c_tinyusd_value_is_array(v->value)) Py_RETURN_NONE;

    double d;
    int64_t i64;
    int b;

    if (c_tinyusd_value_as_bool(v->value, &b)) {
        return PyBool_FromLong(b);
    }
    if (c_tinyusd_value_as_int64(v->value, &i64)) {
        return PyLong_FromLongLong((long long)i64);
    }
    if (c_tinyusd_value_as_double(v->value, &d)) {
        return PyFloat_FromDouble(d);
    }

    {
        CTinyUSDValueType t = c_tinyusd_value_type(v->value);
        const char *n = c_tinyusd_value_type_name(t);
        c_tinyusd_string_t *buf = c_tinyusd_string_new_empty();
        if (!buf) return PyErr_NoMemory();

        if (c_tinyusd_value_to_string(v->value, buf)) {
            const char *s = c_tinyusd_string_str(buf);
            if (n && s) {
                if (!strcmp(n, "uint64")) {
                    unsigned long long u = strtoull(s, NULL, 10);
                    c_tinyusd_string_free(buf);
                    return PyLong_FromUnsignedLongLong(u);
                }
                if (!strcmp(n, "half")) {
                    double hd = strtod(s, NULL);
                    c_tinyusd_string_free(buf);
                    return PyFloat_FromDouble(hd);
                }
            }
        }
        c_tinyusd_string_free(buf);
    }

    c_tinyusd_string_t *buf = c_tinyusd_string_new_empty();
    if (!buf) return PyErr_NoMemory();
    if (c_tinyusd_value_get_string(v->value, buf)) {
        PyObject *r = PyUnicode_FromString(c_tinyusd_string_str(buf));
        c_tinyusd_string_free(buf);
        return r;
    }
    c_tinyusd_string_free(buf);

    /* Fallback: string representation. */
    return Value_to_string(self, NULL);
}

static int
Value_getbuffer(PyObject *self, Py_buffer *view, int flags)
{
    ValueObject *v = (ValueObject *)self;
    if (!c_tinyusd_value_is_array(v->value)) {
        PyErr_SetString(PyExc_BufferError,
                        "Value is not an array; buffer protocol unavailable");
        return -1;
    }

    const void *ptr = NULL;
    uint64_t n_outer = 0;
    uint32_t n_inner = 0;
    uint32_t comp_size = 0;
    const char *fmt = NULL;
    if (!c_tinyusd_value_array_data(v->value, &ptr, &n_outer, &n_inner,
                                    &comp_size, &fmt)) {
        PyErr_SetString(PyExc_BufferError,
                        "Unsupported array element type for buffer protocol");
        return -1;
    }

    int ndim = (n_inner > 1) ? 2 : 1;
    Py_ssize_t total_bytes =
        (Py_ssize_t)(n_outer * (uint64_t)n_inner * (uint64_t)comp_size);

    view->obj = self;
    Py_INCREF(self);
    view->buf = (void *)ptr;
    view->len = total_bytes;
    view->readonly = 1;
    view->itemsize = (Py_ssize_t)comp_size;
    view->format = (flags & PyBUF_FORMAT) ? (char *)fmt : NULL;
    view->ndim = ndim;
    view->shape = NULL;
    view->strides = NULL;
    view->suboffsets = NULL;
    view->internal = NULL;

    /* Stash shape/strides on the heap; release in Value_releasebuffer.
     * For 2-D, shape = (n_outer, n_inner), strides = (n_inner*comp_size,
     * comp_size) — tightly packed. */
    if (flags & PyBUF_ND) {
        Py_ssize_t *shape = PyMem_Malloc(sizeof(Py_ssize_t) * (size_t)ndim);
        if (!shape) { Py_DECREF(self); return -1; }
        if (ndim == 2) { shape[0] = (Py_ssize_t)n_outer; shape[1] = (Py_ssize_t)n_inner; }
        else           { shape[0] = (Py_ssize_t)n_outer; }
        view->shape = shape;
    }
    if (flags & PyBUF_STRIDES) {
        Py_ssize_t *strides = PyMem_Malloc(sizeof(Py_ssize_t) * (size_t)ndim);
        if (!strides) {
            if (view->shape) PyMem_Free(view->shape);
            Py_DECREF(self);
            return -1;
        }
        if (ndim == 2) {
            strides[0] = (Py_ssize_t)(n_inner * comp_size);
            strides[1] = (Py_ssize_t)comp_size;
        } else {
            strides[0] = (Py_ssize_t)comp_size;
        }
        view->strides = strides;
    }
    return 0;
}

static void
Value_releasebuffer(PyObject *self, Py_buffer *view)
{
    (void)self;
    if (view->shape) PyMem_Free(view->shape);
    if (view->strides) PyMem_Free(view->strides);
    view->shape = NULL;
    view->strides = NULL;
}

static PyObject *
Value_repr(PyObject *self)
{
    ValueObject *v = (ValueObject *)self;
    CTinyUSDValueType t = c_tinyusd_value_type(v->value);
    const char *n = c_tinyusd_value_type_name(t);
    return PyUnicode_FromFormat("<tinyusdz.Value type=%s>", n ? n : "?");
}

static PyMethodDef Value_methods[] = {
    {"to_string", Value_to_string, METH_NOARGS, "Pprint value contents."},
    {"as_scalar", Value_as_scalar, METH_NOARGS,
     "Return scalar value as a Python bool/int/float/str, or None for arrays."},
    {NULL, NULL, 0, NULL}
};

static PyGetSetDef Value_getset[] = {
    {"type_name", Value_get_type_name, NULL, "USD value type name.", NULL},
    {"is_array", Value_is_array, NULL, "True if this value is a 1D array.", NULL},
    {NULL}
};

static PyType_Slot Value_slots[] = {
    {Py_tp_doc, "USD value."},
    {Py_tp_dealloc, Value_dealloc},
    {Py_tp_methods, Value_methods},
    {Py_tp_getset, Value_getset},
    {Py_tp_repr, Value_repr},
    {Py_bf_getbuffer, Value_getbuffer},
    {Py_bf_releasebuffer, Value_releasebuffer},
    {0, NULL}
};

static PyType_Spec Value_spec = {
    "tinyusdz._core.Value",
    sizeof(ValueObject),
    0,
    Py_TPFLAGS_DEFAULT,
    Value_slots
};

static PyObject *
make_value(const CTinyUSDValue *value, PyObject *owner)
{
    PyTypeObject *tp = (PyTypeObject *)ValueType;
    allocfunc alloc = (allocfunc)PyType_GetSlot(tp, Py_tp_alloc);
    ValueObject *obj = (ValueObject *)alloc(tp, 0);
    if (!obj) return NULL;
    obj->value = value;
    obj->owns_value = 0;
    Py_INCREF(owner);
    obj->owner = owner;
    return (PyObject *)obj;
}

static PyObject *
make_value_owning(CTinyUSDValue *value)
{
    PyTypeObject *tp = (PyTypeObject *)ValueType;
    allocfunc alloc = (allocfunc)PyType_GetSlot(tp, Py_tp_alloc);
    ValueObject *obj = (ValueObject *)alloc(tp, 0);
    if (!obj) {
        c_tinyusd_value_free(value);
        return NULL;
    }
    obj->value = value;
    obj->owns_value = 1;
    obj->owner = NULL;
    return (PyObject *)obj;
}

static char *
dup_normalized_asset_path(const char *s)
{
    size_t len;
    size_t start = 0;
    size_t end = 0;
    char *out;

    if (!s) return NULL;

    len = strlen(s);
    if (len >= 6 &&
        s[0] == '@' && s[1] == '@' && s[2] == '@' &&
        s[len - 3] == '@' && s[len - 2] == '@' && s[len - 1] == '@') {
        start = 3;
        end = 3;
    } else if (len >= 2 && s[0] == '@' && s[len - 1] == '@') {
        start = 1;
        end = 1;
    }

    out = (char *)PyMem_Malloc(len - start - end + 1);
    if (!out) return NULL;
    memcpy(out, s + start, len - start - end);
    out[len - start - end] = '\0';
    return out;
}

/* ------------------------------------------------------------------------
 * Python -> CTinyUSDValue coercion.
 *
 * Coverage matches the C value-constructor surface in c-tinyusd.h:
 *   - scalars: int, float, bool, str (-> string), token via dtype="token"
 *   - tuple/list of int   length 2/3/4 -> int{2,3,4}
 *   - tuple/list of float length 2/3/4 -> float{2,3,4}
 *   - other lists of int/float -> int[]/float[] arrays
 *
 * `dtype` overrides inference for ambiguous cases (e.g. dtype="token").
 *
 * `out_type_name` is filled with a human-readable USD type name used to
 * stamp the Attribute (e.g. "float", "int[]", "float3", "token"), which
 * the writer needs for unambiguous serialization.
 * -------------------------------------------------------------------- */

static int
py_seq_classify(PyObject *seq, Py_ssize_t *n_out, int *all_int, int *all_float)
{
    if (!(PyList_Check(seq) || PyTuple_Check(seq))) return 0;
    Py_ssize_t n = PySequence_Size(seq);
    if (n < 0) return 0;
    *n_out = n;
    *all_int = 1;
    *all_float = 1;
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject *e = PySequence_GetItem(seq, i);
        if (!e) return 0;
        int is_bool = PyBool_Check(e);  /* bools are ints in Python */
        int is_int = PyLong_Check(e) && !is_bool;
        int is_flt = PyFloat_Check(e);
        Py_DECREF(e);
        if (!is_int) *all_int = 0;
        if (!is_int && !is_flt) *all_float = 0;
    }
    return 1;
}

static CTinyUSDValue *
py_to_value(PyObject *obj, const char *dtype, char *out_type_name,
            size_t out_type_name_size)
{
    out_type_name[0] = '\0';

    /* Explicit dtype="token[]" or "string[]" handles a list/tuple of
     * strings. */
    if (dtype && (!strcmp(dtype, "token[]") || !strcmp(dtype, "string[]"))) {
        if (!(PyList_Check(obj) || PyTuple_Check(obj))) {
            PyErr_SetString(PyExc_TypeError,
                            "dtype expects a list/tuple of strings");
            return NULL;
        }
        Py_ssize_t n = PySequence_Size(obj);
        const char **carr = (const char **)PyMem_Malloc(
            sizeof(const char *) * (size_t)(n > 0 ? n : 1));
        if (!carr) return (PyObject *)PyErr_NoMemory();
        /* Hold ref-keepers so utf8 buffers stay alive across the C call. */
        PyObject *keepers = PyList_New(n);
        if (!keepers) { PyMem_Free(carr); return NULL; }
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *e = PySequence_GetItem(obj, i);
            if (!PyUnicode_Check(e)) {
                Py_DECREF(e); Py_DECREF(keepers); PyMem_Free(carr);
                PyErr_SetString(PyExc_TypeError,
                                "token[]/string[] requires str elements");
                return NULL;
            }
            const char *str = PyUnicode_AsUTF8(e);
            carr[i] = str ? str : "";
            PyList_SetItem(keepers, i, e);  /* steals e */
        }
        CTinyUSDValue *v;
        if (!strcmp(dtype, "token[]")) {
            v = c_tinyusd_value_new_array_token((uint64_t)n, carr);
            snprintf(out_type_name, out_type_name_size, "token[]");
        } else {
            v = c_tinyusd_value_new_array_string((uint64_t)n, carr);
            snprintf(out_type_name, out_type_name_size, "string[]");
        }
        PyMem_Free(carr);
        Py_DECREF(keepers);
        if (!v) PyErr_SetString(PyExc_RuntimeError,
                                "value_new_array_(token|string) failed");
        return v;
    }

    /* Explicit dtype="quat{h,f,d}" — 4-tuple/list (w, x, y, z). */
    if (dtype && (!strcmp(dtype, "quath") ||
                  !strcmp(dtype, "quatf") ||
                  !strcmp(dtype, "quatd"))) {
        if (!(PyList_Check(obj) || PyTuple_Check(obj)) ||
            PySequence_Size(obj) != 4) {
            PyErr_Format(PyExc_TypeError,
                         "dtype='%s' requires a 4-element (w, x, y, z) sequence",
                         dtype);
            return NULL;
        }
        double xs[4];
        for (Py_ssize_t i = 0; i < 4; ++i) {
            PyObject *e = PySequence_GetItem(obj, i);
            xs[i] = PyFloat_AsDouble(e);
            Py_DECREF(e);
            if (PyErr_Occurred()) return NULL;
        }
        /* (w, x, y, z) -> imag = (x, y, z), real = w. */
        CTinyUSDValue *v = NULL;
        if (!strcmp(dtype, "quatd")) {
            c_tinyusd_quatd_t q;
            q.imag[0] = xs[1]; q.imag[1] = xs[2]; q.imag[2] = xs[3];
            q.real = xs[0];
            v = c_tinyusd_value_new_quatd(q);
        } else if (!strcmp(dtype, "quatf")) {
            c_tinyusd_quatf_t q;
            q.imag[0] = (float)xs[1]; q.imag[1] = (float)xs[2]; q.imag[2] = (float)xs[3];
            q.real = (float)xs[0];
            v = c_tinyusd_value_new_quatf(q);
        } else {
            c_tinyusd_quath_t q;
            q.imag[0] = c_tinyusd_float_to_half((float)xs[1]);
            q.imag[1] = c_tinyusd_float_to_half((float)xs[2]);
            q.imag[2] = c_tinyusd_float_to_half((float)xs[3]);
            q.real    = c_tinyusd_float_to_half((float)xs[0]);
            v = c_tinyusd_value_new_quath(q);
        }
        snprintf(out_type_name, out_type_name_size, "%s", dtype);
        return v;
    }

    /* Explicit dtype="half|half2|half3|half4" — float input(s) -> half. */
    if (dtype && !strncmp(dtype, "half", 4) &&
        (dtype[4] == '\0' || (dtype[4] >= '2' && dtype[4] <= '4' && dtype[5] == '\0'))) {
        int dim = dtype[4] ? (dtype[4] - '0') : 1;
        if (dim == 1) {
            if (!PyFloat_Check(obj) && !PyLong_Check(obj)) {
                PyErr_SetString(PyExc_TypeError,
                                "dtype='half' requires a float/int");
                return NULL;
            }
            double d = PyFloat_AsDouble(obj);
            if (PyErr_Occurred()) return NULL;
            CTinyUSDValue *v = c_tinyusd_value_new_half(
                c_tinyusd_float_to_half((float)d));
            snprintf(out_type_name, out_type_name_size, "half");
            return v;
        }
        if (!(PyList_Check(obj) || PyTuple_Check(obj)) ||
            PySequence_Size(obj) != dim) {
            PyErr_Format(PyExc_TypeError,
                         "dtype='%s' requires a %d-element sequence",
                         dtype, dim);
            return NULL;
        }
        c_tinyusd_half_t h[4];
        for (Py_ssize_t i = 0; i < dim; ++i) {
            PyObject *e = PySequence_GetItem(obj, i);
            float fv = (float)PyFloat_AsDouble(e);
            Py_DECREF(e);
            if (PyErr_Occurred()) return NULL;
            h[i] = c_tinyusd_float_to_half(fv);
        }
        CTinyUSDValue *v = NULL;
        if (dim == 2) {
            c_tinyusd_half2_t hh = {h[0], h[1]};
            v = c_tinyusd_value_new_half2(hh);
        } else if (dim == 3) {
            c_tinyusd_half3_t hh = {h[0], h[1], h[2]};
            v = c_tinyusd_value_new_half3(hh);
        } else {
            c_tinyusd_half4_t hh = {h[0], h[1], h[2], h[3]};
            v = c_tinyusd_value_new_half4(hh);
        }
        snprintf(out_type_name, out_type_name_size, "%s", dtype);
        return v;
    }

    /* Explicit dtype="uint|uint64|int64" — single integer. */
    if (dtype && (!strcmp(dtype, "uint") ||
                  !strcmp(dtype, "uint64") ||
                  !strcmp(dtype, "int64"))) {
        if (!PyLong_Check(obj)) {
            PyErr_Format(PyExc_TypeError, "dtype='%s' requires an int", dtype);
            return NULL;
        }
        CTinyUSDValue *v;
        if (!strcmp(dtype, "uint")) {
            unsigned long ul = PyLong_AsUnsignedLong(obj);
            if (PyErr_Occurred()) return NULL;
            v = c_tinyusd_value_new_uint((uint32_t)ul);
        } else if (!strcmp(dtype, "uint64")) {
            unsigned long long ull = PyLong_AsUnsignedLongLong(obj);
            if (PyErr_Occurred()) return NULL;
            v = c_tinyusd_value_new_uint64((uint64_t)ull);
        } else {
            long long ll = PyLong_AsLongLong(obj);
            if (PyErr_Occurred()) return NULL;
            v = c_tinyusd_value_new_int64((int64_t)ll);
        }
        snprintf(out_type_name, out_type_name_size, "%s", dtype);
        return v;
    }

    /* Explicit dtype="matrix2d|matrix3d|matrix4d" — nested tuples/lists.
     * Accept any tuple or list of N rows × N cols of floats. */
    if (dtype && (!strcmp(dtype, "matrix2d") ||
                  !strcmp(dtype, "matrix3d") ||
                  !strcmp(dtype, "matrix4d"))) {
        int dim = dtype[6] - '0';  /* 2/3/4 */
        if (!(PyList_Check(obj) || PyTuple_Check(obj))) {
            PyErr_Format(PyExc_TypeError,
                         "dtype='%s' requires a nested sequence (%dx%d)",
                         dtype, dim, dim);
            return NULL;
        }
        Py_ssize_t n_rows = PySequence_Size(obj);
        if (n_rows != dim) {
            PyErr_Format(PyExc_ValueError,
                         "dtype='%s' expects %d rows, got %zd",
                         dtype, dim, n_rows);
            return NULL;
        }
        double m[16] = {0};
        for (Py_ssize_t i = 0; i < dim; ++i) {
            PyObject *row = PySequence_GetItem(obj, i);
            if (!row || !(PyList_Check(row) || PyTuple_Check(row)) ||
                PySequence_Size(row) != dim) {
                Py_XDECREF(row);
                PyErr_Format(PyExc_ValueError,
                             "dtype='%s' row %zd must have %d cols",
                             dtype, i, dim);
                return NULL;
            }
            for (Py_ssize_t j = 0; j < dim; ++j) {
                PyObject *e = PySequence_GetItem(row, j);
                m[i * dim + j] = PyFloat_AsDouble(e);
                Py_DECREF(e);
                if (PyErr_Occurred()) { Py_DECREF(row); return NULL; }
            }
            Py_DECREF(row);
        }
        CTinyUSDValue *v = NULL;
        if (dim == 2) {
            c_tinyusd_matrix2d_t mat;
            memcpy(&mat, m, sizeof(double) * 4);
            v = c_tinyusd_value_new_matrix2d_t(mat);
        } else if (dim == 3) {
            c_tinyusd_matrix3d_t mat;
            memcpy(&mat, m, sizeof(double) * 9);
            v = c_tinyusd_value_new_matrix3d_t(mat);
        } else {
            c_tinyusd_matrix4d_t mat;
            memcpy(&mat, m, sizeof(double) * 16);
            v = c_tinyusd_value_new_matrix4d_t(mat);
        }
        snprintf(out_type_name, out_type_name_size, "%s", dtype);
        return v;
    }

    /* Explicit dtype="asset" handles a single path string. */
    if (dtype && !strcmp(dtype, "asset")) {
        char *norm = NULL;
        if (!PyUnicode_Check(obj)) {
            PyErr_SetString(PyExc_TypeError, "dtype='asset' requires a string");
            return NULL;
        }
        const char *s = PyUnicode_AsUTF8(obj);
        if (!s) return NULL;
        norm = dup_normalized_asset_path(s);
        if (!norm) {
            PyErr_NoMemory();
            return NULL;
        }
        CTinyUSDValue *v = c_tinyusd_value_new_asset(norm);
        PyMem_Free(norm);
        snprintf(out_type_name, out_type_name_size, "asset");
        if (!v) PyErr_SetString(PyExc_RuntimeError, "value_new_asset failed");
        return v;
    }

    /* Explicit dtype="asset[]" handles a list/tuple of path strings. */
    if (dtype && !strcmp(dtype, "asset[]")) {
        char **norm_paths = NULL;
        if (!PyList_Check(obj) && !PyTuple_Check(obj)) {
            PyErr_SetString(PyExc_TypeError,
                            "dtype='asset[]' requires a list/tuple of str");
            return NULL;
        }
        Py_ssize_t n = PySequence_Size(obj);
        const char **paths = (const char **)PyMem_Malloc(
            sizeof(char *) * (size_t)(n > 0 ? n : 1));
        if (!paths) return PyErr_NoMemory();
        PyObject **items = (PyObject **)PyMem_Malloc(
            sizeof(PyObject *) * (size_t)(n > 0 ? n : 1));
        if (!items) { PyMem_Free(paths); return PyErr_NoMemory(); }
        norm_paths = (char **)PyMem_Malloc(sizeof(char *) * (size_t)(n > 0 ? n : 1));
        if (!norm_paths) {
            PyMem_Free(items);
            PyMem_Free(paths);
            return PyErr_NoMemory();
        }
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *e = PySequence_GetItem(obj, i);
            if (!e || !PyUnicode_Check(e)) {
                Py_XDECREF(e);
                for (Py_ssize_t k = 0; k < i; ++k) Py_DECREF(items[k]);
                for (Py_ssize_t k = 0; k < i; ++k) PyMem_Free(norm_paths[k]);
                PyMem_Free(norm_paths); PyMem_Free(items); PyMem_Free(paths);
                PyErr_SetString(PyExc_TypeError,
                                "all asset[] elements must be str");
                return NULL;
            }
            items[i] = e;
            paths[i] = PyUnicode_AsUTF8(e);
            if (!paths[i]) {
                for (Py_ssize_t k = 0; k <= i; ++k) Py_DECREF(items[k]);
                for (Py_ssize_t k = 0; k < i; ++k) PyMem_Free(norm_paths[k]);
                PyMem_Free(norm_paths); PyMem_Free(items); PyMem_Free(paths);
                return NULL;
            }
            norm_paths[i] = dup_normalized_asset_path(paths[i]);
            if (!norm_paths[i]) {
                for (Py_ssize_t k = 0; k <= i; ++k) Py_DECREF(items[k]);
                for (Py_ssize_t k = 0; k < i; ++k) PyMem_Free(norm_paths[k]);
                PyMem_Free(norm_paths); PyMem_Free(items); PyMem_Free(paths);
                PyErr_NoMemory();
                return NULL;
            }
            paths[i] = norm_paths[i];
        }
        CTinyUSDValue *v = c_tinyusd_value_new_array_asset((uint64_t)n, paths);
        for (Py_ssize_t k = 0; k < n; ++k) Py_DECREF(items[k]);
        for (Py_ssize_t k = 0; k < n; ++k) PyMem_Free(norm_paths[k]);
        PyMem_Free(norm_paths); PyMem_Free(items); PyMem_Free(paths);
        snprintf(out_type_name, out_type_name_size, "asset[]");
        if (!v) PyErr_SetString(PyExc_RuntimeError, "value_new_array_asset failed");
        return v;
    }

    /* Explicit dtype="token" handles a single string. */
    if (dtype && !strcmp(dtype, "token")) {
        if (!PyUnicode_Check(obj)) {
            PyErr_SetString(PyExc_TypeError, "dtype='token' requires a string");
            return NULL;
        }
        const char *s = PyUnicode_AsUTF8(obj);
        if (!s) return NULL;
        c_tinyusd_token_t *tok = c_tinyusd_token_new(s);
        if (!tok) return PyErr_NoMemory();
        CTinyUSDValue *v = c_tinyusd_value_new_token(tok);
        c_tinyusd_token_free(tok);
        snprintf(out_type_name, out_type_name_size, "token");
        if (!v) PyErr_SetString(PyExc_RuntimeError, "value_new_token failed");
        return v;
    }

    /* bool BEFORE int — PyLong_Check is true for bool. */
    if (PyBool_Check(obj)) {
        long b = PyObject_IsTrue(obj);
        CTinyUSDValue *v = c_tinyusd_value_new_bool((int)b);
        snprintf(out_type_name, out_type_name_size, "bool");
        return v;
    }
    if (PyLong_Check(obj)) {
        long long ll = PyLong_AsLongLong(obj);
        if (PyErr_Occurred()) return NULL;
        CTinyUSDValue *v = c_tinyusd_value_new_int((int)ll);
        snprintf(out_type_name, out_type_name_size, "int");
        return v;
    }
    if (PyFloat_Check(obj)) {
        double d = PyFloat_AsDouble(obj);
        if (PyErr_Occurred()) return NULL;
        if (dtype && strcmp(dtype, "double") == 0) {
            CTinyUSDValue *v = c_tinyusd_value_new_double(d);
            snprintf(out_type_name, out_type_name_size, "double");
            return v;
        }
        CTinyUSDValue *v = c_tinyusd_value_new_float((float)d);
        snprintf(out_type_name, out_type_name_size, "float");
        return v;
    }
    if (PyUnicode_Check(obj)) {
        const char *s = PyUnicode_AsUTF8(obj);
        if (!s) return NULL;
        c_tinyusd_string_t *cs = c_tinyusd_string_new(s);
        if (!cs) return PyErr_NoMemory();
        CTinyUSDValue *v = c_tinyusd_value_new_string(cs);
        c_tinyusd_string_free(cs);
        snprintf(out_type_name, out_type_name_size, "string");
        return v;
    }

    /* Explicit dtype="int64[]" — list/tuple of int64 values. */
    if (dtype && !strcmp(dtype, "int64[]")) {
        if (!(PyList_Check(obj) || PyTuple_Check(obj))) {
            PyErr_SetString(PyExc_TypeError,
                            "dtype='int64[]' requires a list/tuple of int");
            return NULL;
        }
        Py_ssize_t n = PySequence_Size(obj);
        int64_t *arr = (int64_t *)PyMem_Malloc(sizeof(int64_t) * (size_t)(n > 0 ? n : 1));
        if (!arr) {
            PyErr_NoMemory();
            return NULL;
        }
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *e = PySequence_GetItem(obj, i);
            if (!PyLong_Check(e)) {
                Py_DECREF(e);
                PyMem_Free(arr);
                PyErr_SetString(PyExc_TypeError,
                                "int64[] elements must be int");
                return NULL;
            }
            arr[i] = PyLong_AsLongLong(e);
            Py_DECREF(e);
            if (PyErr_Occurred()) { PyMem_Free(arr); return NULL; }
        }
        CTinyUSDValue *v = c_tinyusd_value_new_array_int64((uint64_t)n, arr);
        PyMem_Free(arr);
        snprintf(out_type_name, out_type_name_size, "int64[]");
        if (!v) PyErr_SetString(PyExc_RuntimeError, "value_new_array_int64 failed");
        return v;
    }

    /* Explicit dtype="uint64[]" — list/tuple of uint64 values. */
    if (dtype && !strcmp(dtype, "uint64[]")) {
        if (!(PyList_Check(obj) || PyTuple_Check(obj))) {
            PyErr_SetString(PyExc_TypeError,
                            "dtype='uint64[]' requires a list/tuple of int");
            return NULL;
        }
        Py_ssize_t n = PySequence_Size(obj);
        uint64_t *arr = (uint64_t *)PyMem_Malloc(sizeof(uint64_t) * (size_t)(n > 0 ? n : 1));
        if (!arr) {
            PyErr_NoMemory();
            return NULL;
        }
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *e = PySequence_GetItem(obj, i);
            if (!PyLong_Check(e)) {
                Py_DECREF(e);
                PyMem_Free(arr);
                PyErr_SetString(PyExc_TypeError,
                                "uint64[] elements must be int");
                return NULL;
            }
            arr[i] = PyLong_AsUnsignedLongLong(e);
            Py_DECREF(e);
            if (PyErr_Occurred()) { PyMem_Free(arr); return NULL; }
        }
        CTinyUSDValue *v = c_tinyusd_value_new_array_uint64((uint64_t)n, arr);
        PyMem_Free(arr);
        snprintf(out_type_name, out_type_name_size, "uint64[]");
        if (!v) PyErr_SetString(PyExc_RuntimeError, "value_new_array_uint64 failed");
        return v;
    }

    /* Explicit dtype="bool[]" — list/tuple of bool values. */
    if (dtype && !strcmp(dtype, "bool[]")) {
        if (!(PyList_Check(obj) || PyTuple_Check(obj))) {
            PyErr_SetString(PyExc_TypeError,
                            "dtype='bool[]' requires a list/tuple of bool");
            return NULL;
        }
        Py_ssize_t n = PySequence_Size(obj);
        int *arr = (int *)PyMem_Malloc(sizeof(int) * (size_t)(n > 0 ? n : 1));
        if (!arr) {
            PyErr_NoMemory();
            return NULL;
        }
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *e = PySequence_GetItem(obj, i);
            arr[i] = PyObject_IsTrue(e);
            Py_DECREF(e);
            if (arr[i] == -1) { PyMem_Free(arr); return NULL; }
        }
        CTinyUSDValue *v = c_tinyusd_value_new_array_bool((uint64_t)n, arr);
        PyMem_Free(arr);
        snprintf(out_type_name, out_type_name_size, "bool[]");
        if (!v) PyErr_SetString(PyExc_RuntimeError, "value_new_array_bool failed");
        return v;
    }

    /* Explicit dtype="half[]" — list/tuple of float values -> half array. */
    if (dtype && !strcmp(dtype, "half[]")) {
        if (!(PyList_Check(obj) || PyTuple_Check(obj))) {
            PyErr_SetString(PyExc_TypeError,
                            "dtype='half[]' requires a list/tuple of float");
            return NULL;
        }
        Py_ssize_t n = PySequence_Size(obj);
        c_tinyusd_half_t *arr = (c_tinyusd_half_t *)PyMem_Malloc(
            sizeof(c_tinyusd_half_t) * (size_t)(n > 0 ? n : 1));
        if (!arr) {
            PyErr_NoMemory();
            return NULL;
        }
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *e = PySequence_GetItem(obj, i);
            double d = PyFloat_AsDouble(e);
            Py_DECREF(e);
            if (PyErr_Occurred()) { PyMem_Free(arr); return NULL; }
            arr[i] = c_tinyusd_float_to_half((float)d);
        }
        CTinyUSDValue *v = c_tinyusd_value_new_array_half((uint64_t)n, arr);
        PyMem_Free(arr);
        snprintf(out_type_name, out_type_name_size, "half[]");
        if (!v) PyErr_SetString(PyExc_RuntimeError, "value_new_array_half failed");
        return v;
    }

    /* Explicit dtype="double[]" — list/tuple of float values -> double array. */
    if (dtype && !strcmp(dtype, "double[]")) {
        if (!(PyList_Check(obj) || PyTuple_Check(obj))) {
            PyErr_SetString(PyExc_TypeError,
                            "dtype='double[]' requires a list/tuple of float");
            return NULL;
        }
        Py_ssize_t n = PySequence_Size(obj);
        double *arr = (double *)PyMem_Malloc(sizeof(double) * (size_t)(n > 0 ? n : 1));
        if (!arr) {
            PyErr_NoMemory();
            return NULL;
        }
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *e = PySequence_GetItem(obj, i);
            arr[i] = PyFloat_AsDouble(e);
            Py_DECREF(e);
            if (PyErr_Occurred()) { PyMem_Free(arr); return NULL; }
        }
        CTinyUSDValue *v = c_tinyusd_value_new_array_double((uint64_t)n, arr);
        PyMem_Free(arr);
        snprintf(out_type_name, out_type_name_size, "double[]");
        if (!v) PyErr_SetString(PyExc_RuntimeError, "value_new_array_double failed");
        return v;
    }

    /* Explicit dtype="uint[]" — list/tuple of int values -> uint array. */
    if (dtype && !strcmp(dtype, "uint[]")) {
        if (!(PyList_Check(obj) || PyTuple_Check(obj))) {
            PyErr_SetString(PyExc_TypeError,
                            "dtype='uint[]' requires a list/tuple of int");
            return NULL;
        }
        Py_ssize_t n = PySequence_Size(obj);
        unsigned int *arr = (unsigned int *)PyMem_Malloc(sizeof(unsigned int) * (size_t)(n > 0 ? n : 1));
        if (!arr) {
            PyErr_NoMemory();
            return NULL;
        }
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *e = PySequence_GetItem(obj, i);
            arr[i] = (unsigned int)PyLong_AsUnsignedLong(e);
            Py_DECREF(e);
            if (PyErr_Occurred()) { PyMem_Free(arr); return NULL; }
        }
        CTinyUSDValue *v = c_tinyusd_value_new_array_uint((uint64_t)n, arr);
        PyMem_Free(arr);
        snprintf(out_type_name, out_type_name_size, "uint[]");
        if (!v) PyErr_SetString(PyExc_RuntimeError, "value_new_array_uint failed");
        return v;
    }

    /* Explicit dtype="quatf[]" — list of 4-element [w,x,y,z] sequences. */
    if (dtype && !strcmp(dtype, "quatf[]")) {
        if (!(PyList_Check(obj) || PyTuple_Check(obj))) {
            PyErr_SetString(PyExc_TypeError,
                            "dtype='quatf[]' requires a list/tuple of 4-element sequences");
            return NULL;
        }
        Py_ssize_t n = PySequence_Size(obj);
        c_tinyusd_quatf_t *arr = (c_tinyusd_quatf_t *)PyMem_Malloc(
            sizeof(c_tinyusd_quatf_t) * (size_t)(n > 0 ? n : 1));
        if (!arr) {
            PyErr_NoMemory();
            return NULL;
        }
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *elem = PySequence_GetItem(obj, i);
            if (!(PyList_Check(elem) || PyTuple_Check(elem)) ||
                PySequence_Size(elem) != 4) {
                Py_DECREF(elem);
                PyMem_Free(arr);
                PyErr_SetString(PyExc_TypeError,
                                "quatf[] elements must be 4-element sequences [w,x,y,z]");
                return NULL;
            }
            double xs[4];
            for (Py_ssize_t j = 0; j < 4; ++j) {
                PyObject *e = PySequence_GetItem(elem, j);
                xs[j] = PyFloat_AsDouble(e);
                Py_DECREF(e);
                if (PyErr_Occurred()) { Py_DECREF(elem); PyMem_Free(arr); return NULL; }
            }
            arr[i].real = (float)xs[0];
            arr[i].imag[0] = (float)xs[1];
            arr[i].imag[1] = (float)xs[2];
            arr[i].imag[2] = (float)xs[3];
            Py_DECREF(elem);
        }
        CTinyUSDValue *v = c_tinyusd_value_new_array_quatf((uint64_t)n, arr);
        PyMem_Free(arr);
        snprintf(out_type_name, out_type_name_size, "quatf[]");
        if (!v) PyErr_SetString(PyExc_RuntimeError, "value_new_array_quatf failed");
        return v;
    }

    /* Explicit dtype="quatd[]" — list of 4-element [w,x,y,z] sequences. */
    if (dtype && !strcmp(dtype, "quatd[]")) {
        if (!(PyList_Check(obj) || PyTuple_Check(obj))) {
            PyErr_SetString(PyExc_TypeError,
                            "dtype='quatd[]' requires a list/tuple of 4-element sequences");
            return NULL;
        }
        Py_ssize_t n = PySequence_Size(obj);
        c_tinyusd_quatd_t *arr = (c_tinyusd_quatd_t *)PyMem_Malloc(
            sizeof(c_tinyusd_quatd_t) * (size_t)(n > 0 ? n : 1));
        if (!arr) {
            PyErr_NoMemory();
            return NULL;
        }
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *elem = PySequence_GetItem(obj, i);
            if (!(PyList_Check(elem) || PyTuple_Check(elem)) ||
                PySequence_Size(elem) != 4) {
                Py_DECREF(elem);
                PyMem_Free(arr);
                PyErr_SetString(PyExc_TypeError,
                                "quatd[] elements must be 4-element sequences [w,x,y,z]");
                return NULL;
            }
            double xs[4];
            for (Py_ssize_t j = 0; j < 4; ++j) {
                PyObject *e = PySequence_GetItem(elem, j);
                xs[j] = PyFloat_AsDouble(e);
                Py_DECREF(e);
                if (PyErr_Occurred()) { Py_DECREF(elem); PyMem_Free(arr); return NULL; }
            }
            arr[i].real = xs[0];
            arr[i].imag[0] = xs[1];
            arr[i].imag[1] = xs[2];
            arr[i].imag[2] = xs[3];
            Py_DECREF(elem);
        }
        CTinyUSDValue *v = c_tinyusd_value_new_array_quatd((uint64_t)n, arr);
        PyMem_Free(arr);
        snprintf(out_type_name, out_type_name_size, "quatd[]");
        if (!v) PyErr_SetString(PyExc_RuntimeError, "value_new_array_quatd failed");
        return v;
    }

    /* Explicit dtype="quath[]" — list of 4-element [w,x,y,z] sequences. */
    if (dtype && !strcmp(dtype, "quath[]")) {
        if (!(PyList_Check(obj) || PyTuple_Check(obj))) {
            PyErr_SetString(PyExc_TypeError,
                            "dtype='quath[]' requires a list/tuple of 4-element sequences");
            return NULL;
        }
        Py_ssize_t n = PySequence_Size(obj);
        c_tinyusd_quath_t *arr = (c_tinyusd_quath_t *)PyMem_Malloc(
            sizeof(c_tinyusd_quath_t) * (size_t)(n > 0 ? n : 1));
        if (!arr) {
            PyErr_NoMemory();
            return NULL;
        }
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *elem = PySequence_GetItem(obj, i);
            if (!(PyList_Check(elem) || PyTuple_Check(elem)) ||
                PySequence_Size(elem) != 4) {
                Py_DECREF(elem);
                PyMem_Free(arr);
                PyErr_SetString(PyExc_TypeError,
                                "quath[] elements must be 4-element sequences [w,x,y,z]");
                return NULL;
            }
            double xs[4];
            for (Py_ssize_t j = 0; j < 4; ++j) {
                PyObject *e = PySequence_GetItem(elem, j);
                xs[j] = PyFloat_AsDouble(e);
                Py_DECREF(e);
                if (PyErr_Occurred()) { Py_DECREF(elem); PyMem_Free(arr); return NULL; }
            }
            arr[i].real = c_tinyusd_float_to_half((float)xs[0]);
            arr[i].imag[0] = c_tinyusd_float_to_half((float)xs[1]);
            arr[i].imag[1] = c_tinyusd_float_to_half((float)xs[2]);
            arr[i].imag[2] = c_tinyusd_float_to_half((float)xs[3]);
            Py_DECREF(elem);
        }
        CTinyUSDValue *v = c_tinyusd_value_new_array_quath((uint64_t)n, arr);
        PyMem_Free(arr);
        snprintf(out_type_name, out_type_name_size, "quath[]");
        if (!v) PyErr_SetString(PyExc_RuntimeError, "value_new_array_quath failed");
        return v;
    }

    /* Explicit dtype="frame4d" — 4x4 matrix */
    if (dtype && !strcmp(dtype, "frame4d")) {
        if (!(PyList_Check(obj) || PyTuple_Check(obj))) {
            PyErr_SetString(PyExc_TypeError,
                          "dtype='frame4d' requires a nested sequence (4x4)");
            return NULL;
        }
        Py_ssize_t n_rows = PySequence_Size(obj);
        /* Accept either 4 rows x 4 cols (nested) or flat 16-element list */
        if (n_rows == 4) {
            /* Nested 4x4 matrix */
            double m[16] = {0};
            for (Py_ssize_t i = 0; i < 4; ++i) {
                PyObject *row = PySequence_GetItem(obj, i);
                if (!row || !(PyList_Check(row) || PyTuple_Check(row)) ||
                    PySequence_Size(row) != 4) {
                    Py_XDECREF(row);
                    PyErr_SetString(PyExc_ValueError,
                                  "dtype='frame4d' row must have 4 cols");
                    return NULL;
                }
                for (Py_ssize_t j = 0; j < 4; ++j) {
                    PyObject *e = PySequence_GetItem(row, j);
                    m[i * 4 + j] = PyFloat_AsDouble(e);
                    Py_DECREF(e);
                    if (PyErr_Occurred()) { Py_DECREF(row); return NULL; }
                }
                Py_DECREF(row);
            }
            c_tinyusd_matrix4d_t mat;
            memcpy(&mat, m, sizeof(double) * 16);
            CTinyUSDValue *v = c_tinyusd_value_new_frame4d(mat);
            snprintf(out_type_name, out_type_name_size, "frame4d");
            return v;
        } else if (n_rows == 16) {
            /* Flat 16-element list */
            double m[16] = {0};
            for (Py_ssize_t i = 0; i < 16; ++i) {
                PyObject *e = PySequence_GetItem(obj, i);
                m[i] = PyFloat_AsDouble(e);
                Py_DECREF(e);
                if (PyErr_Occurred()) return NULL;
            }
            c_tinyusd_matrix4d_t mat;
            memcpy(&mat, m, sizeof(double) * 16);
            CTinyUSDValue *v = c_tinyusd_value_new_frame4d(mat);
            snprintf(out_type_name, out_type_name_size, "frame4d");
            return v;
        } else {
            PyErr_Format(PyExc_ValueError,
                       "dtype='frame4d' expects 4 rows or 16 elements, got %zd",
                       n_rows);
            return NULL;
        }
    }

    /* Explicit dtype="frame4d[]" — array of 4x4 matrices */
    if (dtype && !strcmp(dtype, "frame4d[]")) {
        if (!(PyList_Check(obj) || PyTuple_Check(obj))) {
            PyErr_SetString(PyExc_TypeError,
                          "dtype='frame4d[]' requires a list/tuple of matrices");
            return NULL;
        }
        Py_ssize_t n = PySequence_Size(obj);
        if (n == 0) {
            /* Empty array */
            CTinyUSDValue *v = c_tinyusd_value_new_array_frame4d(0, NULL);
            snprintf(out_type_name, out_type_name_size, "frame4d[]");
            return v;
        }
        c_tinyusd_matrix4d_t *matrices =
            (c_tinyusd_matrix4d_t *)PyMem_Malloc(sizeof(c_tinyusd_matrix4d_t) * (size_t)n);
        if (!matrices) {
            PyErr_NoMemory();
            return NULL;
        }
        
        for (Py_ssize_t k = 0; k < n; ++k) {
            PyObject *item = PySequence_GetItem(obj, k);
            if (!item || !(PyList_Check(item) || PyTuple_Check(item))) {
                Py_XDECREF(item);
                PyMem_Free(matrices);
                PyErr_SetString(PyExc_TypeError,
                              "frame4d[] elements must be sequences (4x4 matrices)");
                return NULL;
            }
            Py_ssize_t item_len = PySequence_Size(item);
            double m[16] = {0};
            int is_valid = 0;
            
            if (item_len == 4) {
                /* Nested 4x4 */
                is_valid = 1;
                for (Py_ssize_t i = 0; i < 4 && is_valid; ++i) {
                    PyObject *row = PySequence_GetItem(item, i);
                    if (!row || !(PyList_Check(row) || PyTuple_Check(row)) ||
                        PySequence_Size(row) != 4) {
                        Py_XDECREF(row);
                        is_valid = 0;
                        break;
                    }
                    for (Py_ssize_t j = 0; j < 4; ++j) {
                        PyObject *e = PySequence_GetItem(row, j);
                        m[i * 4 + j] = PyFloat_AsDouble(e);
                        Py_DECREF(e);
                        if (PyErr_Occurred()) { Py_DECREF(row); is_valid = 0; break; }
                    }
                    Py_DECREF(row);
                }
            } else if (item_len == 16) {
                /* Flat 16-element */
                is_valid = 1;
                for (Py_ssize_t i = 0; i < 16; ++i) {
                    PyObject *e = PySequence_GetItem(item, i);
                    m[i] = PyFloat_AsDouble(e);
                    Py_DECREF(e);
                    if (PyErr_Occurred()) { is_valid = 0; break; }
                }
            }
            
            if (!is_valid) {
                Py_DECREF(item);
                PyMem_Free(matrices);
                PyErr_SetString(PyExc_ValueError,
                              "frame4d[] element must be 4x4 matrix or 16-element list");
                return NULL;
            }
            memcpy(&matrices[k], m, sizeof(double) * 16);
            Py_DECREF(item);
        }
        
        CTinyUSDValue *v = c_tinyusd_value_new_array_frame4d((uint64_t)n, matrices);
        PyMem_Free(matrices);
        snprintf(out_type_name, out_type_name_size, "frame4d[]");
        if (!v) PyErr_SetString(PyExc_RuntimeError, "value_new_array_frame4d failed");
        return v;
    }

    /* Explicit texCoord types — must be handled before generic tuple handling */
    if (dtype && !strcmp(dtype, "texCoord2f")) {
        Py_ssize_t n = 0;
        int all_int = 0, all_float = 0;
        if (py_seq_classify(obj, &n, &all_int, &all_float) && n == 2 && all_float) {
            float xs[2] = {0.0f, 0.0f};
            for (Py_ssize_t i = 0; i < 2; ++i) {
                PyObject *e = PySequence_GetItem(obj, i);
                xs[i] = (float)PyFloat_AsDouble(e);
                Py_DECREF(e);
                if (PyErr_Occurred()) return NULL;
            }
            c_tinyusd_texcoord2f_t t = {xs[0], xs[1]};
            CTinyUSDValue *v = c_tinyusd_value_new_texcoord2f(t);
            snprintf(out_type_name, out_type_name_size, "texCoord2f");
            return v;
        } else {
            PyErr_SetString(PyExc_TypeError, "texCoord2f must be a 2-tuple of floats");
            return NULL;
        }
    }

    if (dtype && !strcmp(dtype, "texCoord2d")) {
        Py_ssize_t n = 0;
        int all_int = 0, all_float = 0;
        if (py_seq_classify(obj, &n, &all_int, &all_float) && n == 2 && all_float) {
            double xs[2] = {0.0, 0.0};
            for (Py_ssize_t i = 0; i < 2; ++i) {
                PyObject *e = PySequence_GetItem(obj, i);
                xs[i] = PyFloat_AsDouble(e);
                Py_DECREF(e);
                if (PyErr_Occurred()) return NULL;
            }
            c_tinyusd_texcoord2d_t t = {xs[0], xs[1]};
            CTinyUSDValue *v = c_tinyusd_value_new_texcoord2d(t);
            snprintf(out_type_name, out_type_name_size, "texCoord2d");
            return v;
        } else {
            PyErr_SetString(PyExc_TypeError, "texCoord2d must be a 2-tuple of floats");
            return NULL;
        }
    }

    if (dtype && !strcmp(dtype, "texCoord3f")) {
        Py_ssize_t n = 0;
        int all_int = 0, all_float = 0;
        if (py_seq_classify(obj, &n, &all_int, &all_float) && n == 3 && all_float) {
            float xs[3] = {0.0f, 0.0f, 0.0f};
            for (Py_ssize_t i = 0; i < 3; ++i) {
                PyObject *e = PySequence_GetItem(obj, i);
                xs[i] = (float)PyFloat_AsDouble(e);
                Py_DECREF(e);
                if (PyErr_Occurred()) return NULL;
            }
            c_tinyusd_float3_t t = {xs[0], xs[1], xs[2]};
            CTinyUSDValue *v = c_tinyusd_value_new_texcoord3f(t);
            snprintf(out_type_name, out_type_name_size, "texCoord3f");
            return v;
        } else {
            PyErr_SetString(PyExc_TypeError, "texCoord3f must be a 3-tuple of floats");
            return NULL;
        }
    }

    if (dtype && !strcmp(dtype, "texCoord3d")) {
        Py_ssize_t n = 0;
        int all_int = 0, all_float = 0;
        if (py_seq_classify(obj, &n, &all_int, &all_float) && n == 3 && all_float) {
            double xs[3] = {0.0, 0.0, 0.0};
            for (Py_ssize_t i = 0; i < 3; ++i) {
                PyObject *e = PySequence_GetItem(obj, i);
                xs[i] = PyFloat_AsDouble(e);
                Py_DECREF(e);
                if (PyErr_Occurred()) return NULL;
            }
            c_tinyusd_double3_t t = {xs[0], xs[1], xs[2]};
            CTinyUSDValue *v = c_tinyusd_value_new_texcoord3d(t);
            snprintf(out_type_name, out_type_name_size, "texCoord3d");
            return v;
        } else {
            PyErr_SetString(PyExc_TypeError, "texCoord3d must be a 3-tuple of floats");
            return NULL;
        }
    }

    /* Explicit dtype="texCoord2f[]" — list/tuple of 2-tuples (texCoord 2-element float) */
    if (dtype && !strcmp(dtype, "texCoord2f[]")) {
        if (!(PyList_Check(obj) || PyTuple_Check(obj))) {
            PyErr_SetString(PyExc_TypeError,
                            "dtype='texCoord2f[]' requires a list/tuple of 2-tuples");
            return NULL;
        }
        Py_ssize_t n = PySequence_Size(obj);
        c_tinyusd_texcoord2f_t *arr = (c_tinyusd_texcoord2f_t *)PyMem_Malloc(sizeof(c_tinyusd_texcoord2f_t) * (size_t)(n > 0 ? n : 1));
        if (!arr) {
            PyErr_NoMemory();
            return NULL;
        }
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *e = PySequence_GetItem(obj, i);
            Py_ssize_t elem_n = 0;
            int elem_all_int = 0, elem_all_float = 0;
            if (!py_seq_classify(e, &elem_n, &elem_all_int, &elem_all_float) || elem_n != 2 || !elem_all_float) {
                Py_DECREF(e);
                PyMem_Free(arr);
                PyErr_SetString(PyExc_TypeError,
                                "texCoord2f[] elements must be 2-tuples of floats");
                return NULL;
            }
            PyObject *x_obj = PySequence_GetItem(e, 0);
            PyObject *y_obj = PySequence_GetItem(e, 1);
            arr[i].x = (float)PyFloat_AsDouble(x_obj);
            arr[i].y = (float)PyFloat_AsDouble(y_obj);
            Py_DECREF(x_obj);
            Py_DECREF(y_obj);
            Py_DECREF(e);
            if (PyErr_Occurred()) { PyMem_Free(arr); return NULL; }
        }
        CTinyUSDValue *v = c_tinyusd_value_new_array_texcoord2f((uint64_t)n, arr);
        PyMem_Free(arr);
        snprintf(out_type_name, out_type_name_size, "texCoord2f[]");
        if (!v) PyErr_SetString(PyExc_RuntimeError, "value_new_array_texcoord2f failed");
        return v;
    }

    /* Explicit dtype="texCoord2d[]" — list/tuple of 2-tuples (texCoord 2-element double) */
    if (dtype && !strcmp(dtype, "texCoord2d[]")) {
        if (!(PyList_Check(obj) || PyTuple_Check(obj))) {
            PyErr_SetString(PyExc_TypeError,
                            "dtype='texCoord2d[]' requires a list/tuple of 2-tuples");
            return NULL;
        }
        Py_ssize_t n = PySequence_Size(obj);
        c_tinyusd_texcoord2d_t *arr = (c_tinyusd_texcoord2d_t *)PyMem_Malloc(sizeof(c_tinyusd_texcoord2d_t) * (size_t)(n > 0 ? n : 1));
        if (!arr) {
            PyErr_NoMemory();
            return NULL;
        }
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *e = PySequence_GetItem(obj, i);
            Py_ssize_t elem_n = 0;
            int elem_all_int = 0, elem_all_float = 0;
            if (!py_seq_classify(e, &elem_n, &elem_all_int, &elem_all_float) || elem_n != 2 || !elem_all_float) {
                Py_DECREF(e);
                PyMem_Free(arr);
                PyErr_SetString(PyExc_TypeError,
                                "texCoord2d[] elements must be 2-tuples of floats");
                return NULL;
            }
            PyObject *x_obj = PySequence_GetItem(e, 0);
            PyObject *y_obj = PySequence_GetItem(e, 1);
            arr[i].x = PyFloat_AsDouble(x_obj);
            arr[i].y = PyFloat_AsDouble(y_obj);
            Py_DECREF(x_obj);
            Py_DECREF(y_obj);
            Py_DECREF(e);
            if (PyErr_Occurred()) { PyMem_Free(arr); return NULL; }
        }
        CTinyUSDValue *v = c_tinyusd_value_new_array_texcoord2d((uint64_t)n, arr);
        PyMem_Free(arr);
        snprintf(out_type_name, out_type_name_size, "texCoord2d[]");
        if (!v) PyErr_SetString(PyExc_RuntimeError, "value_new_array_texcoord2d failed");
        return v;
    }

    /* Explicit dtype="texCoord3f[]" — list/tuple of 3-tuples (texCoord 3-element float) */
    if (dtype && !strcmp(dtype, "texCoord3f[]")) {
        if (!(PyList_Check(obj) || PyTuple_Check(obj))) {
            PyErr_SetString(PyExc_TypeError,
                            "dtype='texCoord3f[]' requires a list/tuple of 3-tuples");
            return NULL;
        }
        Py_ssize_t n = PySequence_Size(obj);
        c_tinyusd_float3_t *arr = (c_tinyusd_float3_t *)PyMem_Malloc(sizeof(c_tinyusd_float3_t) * (size_t)(n > 0 ? n : 1));
        if (!arr) {
            PyErr_NoMemory();
            return NULL;
        }
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *e = PySequence_GetItem(obj, i);
            Py_ssize_t elem_n = 0;
            int elem_all_int = 0, elem_all_float = 0;
            if (!py_seq_classify(e, &elem_n, &elem_all_int, &elem_all_float) || elem_n != 3 || !elem_all_float) {
                Py_DECREF(e);
                PyMem_Free(arr);
                PyErr_SetString(PyExc_TypeError,
                                "texCoord3f[] elements must be 3-tuples of floats");
                return NULL;
            }
            PyObject *x_obj = PySequence_GetItem(e, 0);
            PyObject *y_obj = PySequence_GetItem(e, 1);
            PyObject *z_obj = PySequence_GetItem(e, 2);
            arr[i].x = (float)PyFloat_AsDouble(x_obj);
            arr[i].y = (float)PyFloat_AsDouble(y_obj);
            arr[i].z = (float)PyFloat_AsDouble(z_obj);
            Py_DECREF(x_obj);
            Py_DECREF(y_obj);
            Py_DECREF(z_obj);
            Py_DECREF(e);
            if (PyErr_Occurred()) { PyMem_Free(arr); return NULL; }
        }
        CTinyUSDValue *v = c_tinyusd_value_new_array_texcoord3f((uint64_t)n, arr);
        PyMem_Free(arr);
        snprintf(out_type_name, out_type_name_size, "texCoord3f[]");
        if (!v) PyErr_SetString(PyExc_RuntimeError, "value_new_array_texcoord3f failed");
        return v;
    }

    /* Explicit dtype="texCoord3d[]" — list/tuple of 3-tuples (texCoord 3-element double) */
    if (dtype && !strcmp(dtype, "texCoord3d[]")) {
        if (!(PyList_Check(obj) || PyTuple_Check(obj))) {
            PyErr_SetString(PyExc_TypeError,
                            "dtype='texCoord3d[]' requires a list/tuple of 3-tuples");
            return NULL;
        }
        Py_ssize_t n = PySequence_Size(obj);
        c_tinyusd_double3_t *arr = (c_tinyusd_double3_t *)PyMem_Malloc(sizeof(c_tinyusd_double3_t) * (size_t)(n > 0 ? n : 1));
        if (!arr) {
            PyErr_NoMemory();
            return NULL;
        }
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *e = PySequence_GetItem(obj, i);
            Py_ssize_t elem_n = 0;
            int elem_all_int = 0, elem_all_float = 0;
            if (!py_seq_classify(e, &elem_n, &elem_all_int, &elem_all_float) || elem_n != 3 || !elem_all_float) {
                Py_DECREF(e);
                PyMem_Free(arr);
                PyErr_SetString(PyExc_TypeError,
                                "texCoord3d[] elements must be 3-tuples of floats");
                return NULL;
            }
            PyObject *x_obj = PySequence_GetItem(e, 0);
            PyObject *y_obj = PySequence_GetItem(e, 1);
            PyObject *z_obj = PySequence_GetItem(e, 2);
            arr[i].x = PyFloat_AsDouble(x_obj);
            arr[i].y = PyFloat_AsDouble(y_obj);
            arr[i].z = PyFloat_AsDouble(z_obj);
            Py_DECREF(x_obj);
            Py_DECREF(y_obj);
            Py_DECREF(z_obj);
            Py_DECREF(e);
            if (PyErr_Occurred()) { PyMem_Free(arr); return NULL; }
        }
        CTinyUSDValue *v = c_tinyusd_value_new_array_texcoord3d((uint64_t)n, arr);
        PyMem_Free(arr);
        snprintf(out_type_name, out_type_name_size, "texCoord3d[]");
        if (!v) PyErr_SetString(PyExc_RuntimeError, "value_new_array_texcoord3d failed");
        return v;
    }

    Py_ssize_t n = 0;
    int all_int = 0, all_float = 0;
    if (py_seq_classify(obj, &n, &all_int, &all_float)) {
        /* Convention: tuple of length 2/3/4 -> packed vector; list -> array.
         * dtype overrides this where supplied. */
        int is_tuple = PyTuple_Check(obj);
        if (is_tuple && (n == 2 || n == 3 || n == 4)) {
            if (all_int) {
                int xs[4] = {0,0,0,0};
                for (Py_ssize_t i = 0; i < n; ++i) {
                    PyObject *e = PySequence_GetItem(obj, i);
                    xs[i] = (int)PyLong_AsLong(e);
                    Py_DECREF(e);
                    if (PyErr_Occurred()) return NULL;
                }
                CTinyUSDValue *v;
                if (n == 2) {
                    c_tinyusd_int2_t t = {xs[0], xs[1]};
                    v = c_tinyusd_value_new_int2(t);
                    snprintf(out_type_name, out_type_name_size, "int2");
                } else if (n == 3) {
                    c_tinyusd_int3_t t = {xs[0], xs[1], xs[2]};
                    v = c_tinyusd_value_new_int3(t);
                    snprintf(out_type_name, out_type_name_size, "int3");
                } else {
                    c_tinyusd_int4_t t = {xs[0], xs[1], xs[2], xs[3]};
                    v = c_tinyusd_value_new_int4(t);
                    snprintf(out_type_name, out_type_name_size, "int4");
                }
                return v;
            }
            if (all_float) {
                /* Double-precision branch when dtype requests it. */
                int want_double = dtype && (
                    strcmp(dtype, "double2") == 0 ||
                    strcmp(dtype, "double3") == 0 ||
                    strcmp(dtype, "double4") == 0 ||
                    strcmp(dtype, "point3d") == 0 ||
                    strcmp(dtype, "vector3d") == 0 ||
                    strcmp(dtype, "normal3d") == 0 ||
                    strcmp(dtype, "color3d") == 0 ||
                    strcmp(dtype, "color4d") == 0 ||
                    strcmp(dtype, "texCoord2d") == 0 ||
                    strcmp(dtype, "texCoord3d") == 0);
                if (want_double) {
                    double xs[4] = {0,0,0,0};
                    for (Py_ssize_t i = 0; i < n; ++i) {
                        PyObject *e = PySequence_GetItem(obj, i);
                        xs[i] = PyFloat_AsDouble(e);
                        Py_DECREF(e);
                        if (PyErr_Occurred()) return NULL;
                    }
                    CTinyUSDValue *v = NULL;
                    /* Typed double3 alias dispatch. */
                    if (n == 3) {
                        c_tinyusd_double3_t t = {xs[0], xs[1], xs[2]};
                        if (!strcmp(dtype, "color3d"))  v = c_tinyusd_value_new_color3d(t);
                        else if (!strcmp(dtype, "point3d"))  v = c_tinyusd_value_new_point3d(t);
                        else if (!strcmp(dtype, "normal3d")) v = c_tinyusd_value_new_normal3d(t);
                        else if (!strcmp(dtype, "vector3d")) v = c_tinyusd_value_new_vector3d(t);
                    }
                    if (!v) {
                        if (n == 2) {
                            c_tinyusd_double2_t t = {xs[0], xs[1]};
                            v = c_tinyusd_value_new_double2(t);
                        } else if (n == 3) {
                            c_tinyusd_double3_t t = {xs[0], xs[1], xs[2]};
                            v = c_tinyusd_value_new_double3(t);
                        } else {
                            c_tinyusd_double4_t t = {xs[0], xs[1], xs[2], xs[3]};
                            v = c_tinyusd_value_new_double4(t);
                        }
                    }
                    snprintf(out_type_name, out_type_name_size, "%s", dtype);
                    return v;
                }
                float xs[4] = {0,0,0,0};
                for (Py_ssize_t i = 0; i < n; ++i) {
                    PyObject *e = PySequence_GetItem(obj, i);
                    xs[i] = (float)PyFloat_AsDouble(e);
                    Py_DECREF(e);
                    if (PyErr_Occurred()) return NULL;
                }
                CTinyUSDValue *v;
                /* Typed float3 aliases: dtype dispatch. */
                if (n == 3 && dtype) {
                    c_tinyusd_float3_t t = {xs[0], xs[1], xs[2]};
                    if (!strcmp(dtype, "color3f")) {
                        v = c_tinyusd_value_new_color3f(t);
                        snprintf(out_type_name, out_type_name_size, "color3f");
                        return v;
                    } else if (!strcmp(dtype, "point3f")) {
                        v = c_tinyusd_value_new_point3f(t);
                        snprintf(out_type_name, out_type_name_size, "point3f");
                        return v;
                    } else if (!strcmp(dtype, "normal3f")) {
                        v = c_tinyusd_value_new_normal3f(t);
                        snprintf(out_type_name, out_type_name_size, "normal3f");
                        return v;
                    } else if (!strcmp(dtype, "vector3f")) {
                        v = c_tinyusd_value_new_vector3f(t);
                        snprintf(out_type_name, out_type_name_size, "vector3f");
                        return v;
                    }
                }
                if (n == 2) {
                    c_tinyusd_float2_t t = {xs[0], xs[1]};
                    v = c_tinyusd_value_new_float2(t);
                    snprintf(out_type_name, out_type_name_size, "float2");
                } else if (n == 3) {
                    c_tinyusd_float3_t t = {xs[0], xs[1], xs[2]};
                    v = c_tinyusd_value_new_float3(t);
                    snprintf(out_type_name, out_type_name_size, "float3");
                } else {
                    c_tinyusd_float4_t t = {xs[0], xs[1], xs[2], xs[3]};
                    v = c_tinyusd_value_new_float4(t);
                    snprintf(out_type_name, out_type_name_size, "float4");
                }
                return v;
            }
        }
        /* Other lengths: treat as 1D array of int or float. */
        if (all_int && n >= 0) {
            int *arr = (int *)PyMem_Malloc(sizeof(int) * (size_t)(n > 0 ? n : 1));
            if (!arr) {
                PyErr_NoMemory();
                return NULL;
            }
            for (Py_ssize_t i = 0; i < n; ++i) {
                PyObject *e = PySequence_GetItem(obj, i);
                arr[i] = (int)PyLong_AsLong(e);
                Py_DECREF(e);
                if (PyErr_Occurred()) { PyMem_Free(arr); return NULL; }
            }
            CTinyUSDValue *v = c_tinyusd_value_new_array_int((uint64_t)n, arr);
            PyMem_Free(arr);
            snprintf(out_type_name, out_type_name_size, "int[]");
            return v;
        }
        if (all_float && n >= 0) {
            float *arr = (float *)PyMem_Malloc(sizeof(float) * (size_t)(n > 0 ? n : 1));
            if (!arr) {
                PyErr_NoMemory();
                return NULL;
            }
            for (Py_ssize_t i = 0; i < n; ++i) {
                PyObject *e = PySequence_GetItem(obj, i);
                arr[i] = (float)PyFloat_AsDouble(e);
                Py_DECREF(e);
                if (PyErr_Occurred()) { PyMem_Free(arr); return NULL; }
            }
            CTinyUSDValue *v = c_tinyusd_value_new_array_float((uint64_t)n, arr);
            PyMem_Free(arr);
            snprintf(out_type_name, out_type_name_size, "float[]");
            return v;
        }
        /* Sequence of sequences -> array of vectors (e.g. point3f[]). */
        if (n > 0) {
            PyObject *first = PySequence_GetItem(obj, 0);
            int is_seq = PyList_Check(first) || PyTuple_Check(first);
            Py_ssize_t inner_n = is_seq ? PySequence_Size(first) : 0;
            Py_DECREF(first);
            if (is_seq && (inner_n == 3 || inner_n == 2 || inner_n == 4)) {
                /* Flatten into float array; assume float components. */
                size_t total = (size_t)n * (size_t)inner_n;
                float *flat = (float *)PyMem_Malloc(sizeof(float) * (total > 0 ? total : 1));
                if (!flat) {
                    PyErr_NoMemory();
                    return NULL;
                }
                for (Py_ssize_t i = 0; i < n; ++i) {
                    PyObject *row = PySequence_GetItem(obj, i);
                    if (!row || PySequence_Size(row) != inner_n) {
                        Py_XDECREF(row);
                        PyMem_Free(flat);
                        PyErr_SetString(PyExc_ValueError,
                                        "ragged sequence in attribute value");
                        return NULL;
                    }
                    for (Py_ssize_t j = 0; j < inner_n; ++j) {
                        PyObject *e = PySequence_GetItem(row, j);
                        flat[i * inner_n + j] = (float)PyFloat_AsDouble(e);
                        Py_DECREF(e);
                        if (PyErr_Occurred()) {
                            Py_DECREF(row);
                            PyMem_Free(flat);
                            return NULL;
                        }
                    }
                    Py_DECREF(row);
                }
                CTinyUSDValue *v = NULL;
                if (inner_n == 2) {
                    v = c_tinyusd_value_new_array_float2(
                        (uint64_t)n, (const c_tinyusd_float2_t *)flat);
                    snprintf(out_type_name, out_type_name_size, "float2[]");
                } else if (inner_n == 3) {
                    v = c_tinyusd_value_new_array_float3(
                        (uint64_t)n, (const c_tinyusd_float3_t *)flat);
                    /* Default type name; caller may override via dtype. */
                    snprintf(out_type_name, out_type_name_size,
                             dtype ? dtype : "float3[]");
                } else {
                    v = c_tinyusd_value_new_array_float4(
                        (uint64_t)n, (const c_tinyusd_float4_t *)flat);
                    snprintf(out_type_name, out_type_name_size, "float4[]");
                }
                PyMem_Free(flat);
                /* Allow caller to override the role-type name via dtype
                 * (e.g. "point3f[]", "color3f[]", "normal3f[]"). */
                if (dtype && inner_n == 3) {
                    snprintf(out_type_name, out_type_name_size, "%s", dtype);
                }
                return v;
            }
        }
    }

    PyErr_SetString(PyExc_TypeError,
                    "unsupported value type for tinyusdz.Prim.set_attribute");
    return NULL;
}

/* ------------------------------------------------------------------------
 * Attribute — OWNS a heap-allocated CTinyUSDAttribute*.
 *
 * Ownership rather than borrowing keeps multiple outstanding Attribute
 * handles on the same Prim independent; each carries its own stable value
 * storage, which is what the buffer-protocol zero-copy path relies on.
 * -------------------------------------------------------------------- */

typedef struct {
    PyObject_HEAD
    CTinyUSDAttribute *attr;  /* owned */
} AttributeObject;

static void
Attribute_dealloc(PyObject *self)
{
    AttributeObject *a = (AttributeObject *)self;
    if (a->attr) {
        c_tinyusd_attribute_free(a->attr);
        a->attr = NULL;
    }
    PyTypeObject *tp = Py_TYPE(self);
    freefunc tp_free = (freefunc)PyType_GetSlot(tp, Py_tp_free);
    tp_free(self);
    Py_DECREF(tp);
}

/* Wrap an already-owned CTinyUSDAttribute*; the AttributeObject takes
 * ownership and will free it on dealloc. */
static PyObject *
make_attribute_owning(CTinyUSDAttribute *attr)
{
    PyTypeObject *tp = (PyTypeObject *)AttributeType;
    allocfunc alloc = (allocfunc)PyType_GetSlot(tp, Py_tp_alloc);
    AttributeObject *obj = (AttributeObject *)alloc(tp, 0);
    if (!obj) {
        c_tinyusd_attribute_free(attr);
        return NULL;
    }
    obj->attr = attr;
    return (PyObject *)obj;
}

static PyObject *
Attribute_get_name(PyObject *self, void *closure)
{
    (void)closure;
    AttributeObject *a = (AttributeObject *)self;
    c_tinyusd_string_t *buf = c_tinyusd_string_new_empty();
    if (!buf) return PyErr_NoMemory();
    c_tinyusd_attribute_get_name(a->attr, buf);
    PyObject *r = PyUnicode_FromString(c_tinyusd_string_str(buf));
    c_tinyusd_string_free(buf);
    return r;
}

static PyObject *
Attribute_get_type_name(PyObject *self, void *closure)
{
    (void)closure;
    AttributeObject *a = (AttributeObject *)self;
    c_tinyusd_string_t *buf = c_tinyusd_string_new_empty();
    if (!buf) return PyErr_NoMemory();
    c_tinyusd_attribute_get_type_name(a->attr, buf);
    PyObject *r = PyUnicode_FromString(c_tinyusd_string_str(buf));
    c_tinyusd_string_free(buf);
    return r;
}

static PyObject *
Attribute_get_value(PyObject *self, void *closure)
{
    (void)closure;
    AttributeObject *a = (AttributeObject *)self;
    const CTinyUSDValue *v = NULL;
    if (!c_tinyusd_attribute_get_value(a->attr, &v) || !v) {
        Py_RETURN_NONE;
    }
    /* Value borrows from the Attribute; Attribute must outlive Value. */
    return make_value(v, self);
}

static PyObject *
Attribute_repr(PyObject *self)
{
    AttributeObject *a = (AttributeObject *)self;
    c_tinyusd_string_t *nbuf = c_tinyusd_string_new_empty();
    c_tinyusd_string_t *tbuf = c_tinyusd_string_new_empty();
    c_tinyusd_attribute_get_name(a->attr, nbuf);
    c_tinyusd_attribute_get_type_name(a->attr, tbuf);
    PyObject *r = PyUnicode_FromFormat(
        "<tinyusdz.Attribute name=%s type=%s>",
        nbuf ? c_tinyusd_string_str(nbuf) : "?",
        tbuf ? c_tinyusd_string_str(tbuf) : "?");
    if (nbuf) c_tinyusd_string_free(nbuf);
    if (tbuf) c_tinyusd_string_free(tbuf);
    return r;
}

static PyGetSetDef Attribute_getset[] = {
    {"name", Attribute_get_name, NULL, "Attribute name.", NULL},
    {"type_name", Attribute_get_type_name, NULL,
     "USD type name, e.g. 'float3[]'.", NULL},
    {"value", Attribute_get_value, NULL,
     "Default Value, or None if not authored.", NULL},
    {NULL}
};

static PyType_Slot Attribute_slots[] = {
    {Py_tp_doc, "USD attribute."},
    {Py_tp_dealloc, Attribute_dealloc},
    {Py_tp_getset, Attribute_getset},
    {Py_tp_repr, Attribute_repr},
    {0, NULL}
};

static PyType_Spec Attribute_spec = {
    "tinyusdz._core.Attribute",
    sizeof(AttributeObject),
    0,
    Py_TPFLAGS_DEFAULT,
    Attribute_slots
};

/* ------------------------------------------------------------------------
 * Module-level functions.
 * -------------------------------------------------------------------- */

static PyObject *
make_stage_from_loader(int (*loader)(const char *, CTinyUSDStage *,
                                     c_tinyusd_string_t *, c_tinyusd_string_t *),
                       const char *path)
{
    PyTypeObject *tp = (PyTypeObject *)StageType;
    allocfunc alloc = (allocfunc)PyType_GetSlot(tp, Py_tp_alloc);
    StageObject *s = (StageObject *)alloc(tp, 0);
    if (!s) return NULL;
    s->stage = c_tinyusd_stage_new();
    if (!s->stage) { Py_DECREF(s); return PyErr_NoMemory(); }

    c_tinyusd_string_t *warn = c_tinyusd_string_new_empty();
    c_tinyusd_string_t *err = c_tinyusd_string_new_empty();
    int ok = loader(path, s->stage, warn, err);
    if (!ok) {
        const char *msg = c_tinyusd_string_str(err);
        PyObject *exc = UsdParseError;
        /* Treat "no such file" strictly as IO error if we can tell. */
        if (msg && strstr(msg, "not found")) exc = UsdIoError;
        PyErr_SetString(exc, msg && *msg ? msg : "USD load failed");
        c_tinyusd_string_free(warn);
        c_tinyusd_string_free(err);
        Py_DECREF(s);
        return NULL;
    }
    c_tinyusd_string_free(warn);
    c_tinyusd_string_free(err);
    return (PyObject *)s;
}

static PyObject *
mod_load(PyObject *self, PyObject *args, PyObject *kwds)
{
    (void)self;
    static char *kwlist[] = {"path", "format", NULL};
    const char *path = NULL;
    const char *format = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|s", kwlist, &path, &format)) {
        return NULL;
    }

    CTinyUSDFormat fmt = C_TINYUSD_FORMAT_AUTO;
    if (format) {
        if (!strcmp(format, "auto")) fmt = C_TINYUSD_FORMAT_AUTO;
        else if (!strcmp(format, "usda")) fmt = C_TINYUSD_FORMAT_USDA;
        else if (!strcmp(format, "usdc")) fmt = C_TINYUSD_FORMAT_USDC;
        else if (!strcmp(format, "usdz")) fmt = C_TINYUSD_FORMAT_USDZ;
        else {
            PyErr_Format(PyExc_ValueError, "unknown format: %s", format);
            return NULL;
        }
    }

    if (fmt == C_TINYUSD_FORMAT_AUTO) fmt = c_tinyusd_detect_format(path);

    int (*loader)(const char *, CTinyUSDStage *,
                  c_tinyusd_string_t *, c_tinyusd_string_t *) = NULL;
    switch (fmt) {
        case C_TINYUSD_FORMAT_USDA: loader = c_tinyusd_load_usda_from_file; break;
        case C_TINYUSD_FORMAT_USDC: loader = c_tinyusd_load_usdc_from_file; break;
        case C_TINYUSD_FORMAT_USDZ: loader = c_tinyusd_load_usdz_from_file; break;
        default:                     loader = c_tinyusd_load_usd_from_file;  break;
    }
    return make_stage_from_loader(loader, path);
}

/* Shared helper for load-from-memory module functions. */
static PyObject *
make_stage_from_memory(const uint8_t *data, Py_ssize_t len,
                       CTinyUSDFormat fmt)
{
    PyTypeObject *tp = (PyTypeObject *)StageType;
    allocfunc alloc = (allocfunc)PyType_GetSlot(tp, Py_tp_alloc);
    StageObject *s = (StageObject *)alloc(tp, 0);
    if (!s) return NULL;
    s->stage = c_tinyusd_stage_new();
    if (!s->stage) { Py_DECREF(s); return PyErr_NoMemory(); }

    c_tinyusd_string_t *warn = c_tinyusd_string_new_empty();
    c_tinyusd_string_t *err = c_tinyusd_string_new_empty();
    int ok = c_tinyusd_stage_load_from_memory(s->stage, data, (size_t)len, fmt,
                                              warn, err);
    if (!ok) {
        const char *msg = c_tinyusd_string_str(err);
        PyErr_SetString(UsdParseError, msg && *msg ? msg : "USD parse failed");
        c_tinyusd_string_free(warn);
        c_tinyusd_string_free(err);
        Py_DECREF(s);
        return NULL;
    }
    c_tinyusd_string_free(warn);
    c_tinyusd_string_free(err);
    return (PyObject *)s;
}

static PyObject *
mod_loads(PyObject *self, PyObject *args)
{
    (void)self;
    const char *text = NULL;
    Py_ssize_t len = 0;
    if (!PyArg_ParseTuple(args, "s#", &text, &len)) return NULL;
    return make_stage_from_memory((const uint8_t *)text, len,
                                  C_TINYUSD_FORMAT_USDA);
}

static PyObject *
mod_load_bytes(PyObject *self, PyObject *args, PyObject *kwds)
{
    (void)self;
    static char *kwlist[] = {"data", "format", NULL};
    Py_buffer buf;
    const char *format = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "y*|s", kwlist,
                                     &buf, &format)) {
        return NULL;
    }
    CTinyUSDFormat fmt = C_TINYUSD_FORMAT_AUTO;
    if (format) {
        if      (!strcmp(format, "auto")) fmt = C_TINYUSD_FORMAT_AUTO;
        else if (!strcmp(format, "usda")) fmt = C_TINYUSD_FORMAT_USDA;
        else if (!strcmp(format, "usdc")) fmt = C_TINYUSD_FORMAT_USDC;
        else if (!strcmp(format, "usdz")) fmt = C_TINYUSD_FORMAT_USDZ;
        else {
            PyBuffer_Release(&buf);
            PyErr_Format(PyExc_ValueError, "unknown format: %s", format);
            return NULL;
        }
    }
    PyObject *result = make_stage_from_memory(
        (const uint8_t *)buf.buf, buf.len, fmt);
    PyBuffer_Release(&buf);
    return result;
}

static PyObject *
mod_is_usd(PyObject *self, PyObject *args)
{
    (void)self;
    const char *path = NULL;
    if (!PyArg_ParseTuple(args, "s", &path)) return NULL;
    return PyBool_FromLong(c_tinyusd_is_usd_file(path));
}

static PyObject *
mod_detect_format(PyObject *self, PyObject *args)
{
    (void)self;
    const char *path = NULL;
    if (!PyArg_ParseTuple(args, "s", &path)) return NULL;
    CTinyUSDFormat f = c_tinyusd_detect_format(path);
    const char *s;
    switch (f) {
        case C_TINYUSD_FORMAT_USDA: s = "usda"; break;
        case C_TINYUSD_FORMAT_USDC: s = "usdc"; break;
        case C_TINYUSD_FORMAT_USDZ: s = "usdz"; break;
        case C_TINYUSD_FORMAT_AUTO: s = "auto"; break;
        default:                    s = "unknown"; break;
    }
    return PyUnicode_FromString(s);
}

static PyMethodDef module_methods[] = {
    {"load", (PyCFunction)mod_load, METH_VARARGS | METH_KEYWORDS,
     "load(path, format=None) -> Stage"},
    {"loads", mod_loads, METH_VARARGS,
     "loads(usda_text) -> Stage (parse USDA string)"},
    {"load_bytes", (PyCFunction)mod_load_bytes, METH_VARARGS | METH_KEYWORDS,
     "load_bytes(data, format=None) -> Stage (parse USDA/USDC/USDZ bytes)"},
    {"is_usd", mod_is_usd, METH_VARARGS,
     "is_usd(path) -> bool"},
    {"detect_format", mod_detect_format, METH_VARARGS,
     "detect_format(path) -> 'usda'|'usdc'|'usdz'|'unknown'"},
    {NULL, NULL, 0, NULL}
};

/* ------------------------------------------------------------------------
 * BufferView — generic zero-copy buffer wrapper backed by a parent owner.
 *
 * Used by RenderMesh.points / .normals / .face_vertex_indices /
 * .face_vertex_counts / .texcoord() and anywhere else the Python side wants
 * to hand a numpy-friendly view over a raw pointer that belongs to a
 * different C object (a RenderScene in this case).
 *
 * The BufferView keeps a strong Python ref to the `owner` object so that
 * the underlying memory can't be freed while a buffer view is alive.
 * -------------------------------------------------------------------- */

typedef struct {
    PyObject_HEAD
    const void *ptr;
    uint64_t    n_outer;
    uint32_t    n_inner;
    uint32_t    comp_size;
    const char *format;    /* points at static storage, never freed */
    PyObject   *owner;
} BufferViewObject;

static void
BufferView_dealloc(PyObject *self)
{
    BufferViewObject *b = (BufferViewObject *)self;
    Py_XDECREF(b->owner);
    PyTypeObject *tp = Py_TYPE(self);
    freefunc tp_free = (freefunc)PyType_GetSlot(tp, Py_tp_free);
    tp_free(self);
    Py_DECREF(tp);
}

static PyObject *
make_buffer_view(const void *ptr, uint64_t n_outer, uint32_t n_inner,
                 uint32_t comp_size, const char *format, PyObject *owner)
{
    PyTypeObject *tp = (PyTypeObject *)BufferViewType;
    allocfunc alloc = (allocfunc)PyType_GetSlot(tp, Py_tp_alloc);
    BufferViewObject *obj = (BufferViewObject *)alloc(tp, 0);
    if (!obj) return NULL;
    obj->ptr = ptr;
    obj->n_outer = n_outer;
    obj->n_inner = n_inner;
    obj->comp_size = comp_size;
    obj->format = format;
    Py_INCREF(owner);
    obj->owner = owner;
    return (PyObject *)obj;
}

static int
BufferView_getbuffer(PyObject *self, Py_buffer *view, int flags)
{
    BufferViewObject *b = (BufferViewObject *)self;
    if (!b->ptr) {
        PyErr_SetString(PyExc_BufferError, "buffer is empty");
        return -1;
    }
    int ndim = (b->n_inner > 1) ? 2 : 1;
    Py_ssize_t total_bytes =
        (Py_ssize_t)(b->n_outer * (uint64_t)b->n_inner *
                     (uint64_t)b->comp_size);

    view->obj = self;
    Py_INCREF(self);
    view->buf = (void *)b->ptr;
    view->len = total_bytes;
    view->readonly = 1;
    view->itemsize = (Py_ssize_t)b->comp_size;
    view->format = (flags & PyBUF_FORMAT) ? (char *)b->format : NULL;
    view->ndim = ndim;
    view->shape = NULL;
    view->strides = NULL;
    view->suboffsets = NULL;
    view->internal = NULL;

    if (flags & PyBUF_ND) {
        Py_ssize_t *shape = PyMem_Malloc(sizeof(Py_ssize_t) * (size_t)ndim);
        if (!shape) { Py_DECREF(self); return -1; }
        if (ndim == 2) { shape[0] = (Py_ssize_t)b->n_outer; shape[1] = (Py_ssize_t)b->n_inner; }
        else           { shape[0] = (Py_ssize_t)b->n_outer; }
        view->shape = shape;
    }
    if (flags & PyBUF_STRIDES) {
        Py_ssize_t *strides = PyMem_Malloc(sizeof(Py_ssize_t) * (size_t)ndim);
        if (!strides) {
            if (view->shape) PyMem_Free(view->shape);
            Py_DECREF(self);
            return -1;
        }
        if (ndim == 2) {
            strides[0] = (Py_ssize_t)(b->n_inner * b->comp_size);
            strides[1] = (Py_ssize_t)b->comp_size;
        } else {
            strides[0] = (Py_ssize_t)b->comp_size;
        }
        view->strides = strides;
    }
    return 0;
}

static void
BufferView_releasebuffer(PyObject *self, Py_buffer *view)
{
    (void)self;
    if (view->shape) PyMem_Free(view->shape);
    if (view->strides) PyMem_Free(view->strides);
    view->shape = NULL;
    view->strides = NULL;
}

static PyObject *
BufferView_repr(PyObject *self)
{
    BufferViewObject *b = (BufferViewObject *)self;
    if (b->n_inner > 1) {
        return PyUnicode_FromFormat(
            "<tinyusdz.BufferView shape=(%llu, %u) format=%s>",
            (unsigned long long)b->n_outer,
            (unsigned int)b->n_inner, b->format);
    }
    return PyUnicode_FromFormat(
        "<tinyusdz.BufferView shape=(%llu,) format=%s>",
        (unsigned long long)b->n_outer, b->format);
}

static PyType_Slot BufferView_slots[] = {
    {Py_tp_doc, "Zero-copy buffer-protocol view."},
    {Py_tp_dealloc, BufferView_dealloc},
    {Py_tp_repr, BufferView_repr},
    {Py_bf_getbuffer, BufferView_getbuffer},
    {Py_bf_releasebuffer, BufferView_releasebuffer},
    {0, NULL}
};

static PyType_Spec BufferView_spec = {
    "tinyusdz._core.BufferView",
    sizeof(BufferViewObject),
    0,
    Py_TPFLAGS_DEFAULT,
    BufferView_slots
};

/* ------------------------------------------------------------------------
 * RenderScene — owns CTinyUSDRenderScene*; all Render{Mesh,Material,Camera,
 * Light} handles keep a strong ref to their RenderScene parent.
 * -------------------------------------------------------------------- */

typedef struct {
    PyObject_HEAD
    CTinyUSDRenderScene *scene;  /* owned */
} RenderSceneObject;

static void
RenderScene_dealloc(PyObject *self)
{
    RenderSceneObject *s = (RenderSceneObject *)self;
    if (s->scene) {
        c_tinyusd_render_scene_free(s->scene);
        s->scene = NULL;
    }
    PyTypeObject *tp = Py_TYPE(self);
    freefunc tp_free = (freefunc)PyType_GetSlot(tp, Py_tp_free);
    tp_free(self);
    Py_DECREF(tp);
}

/* Forward for child-wrapper constructors. */
static PyObject *make_render_mesh(const CTinyUSDRenderMesh *m, PyObject *owner);
static PyObject *make_render_material(const CTinyUSDRenderMaterial *m, PyObject *owner);
static PyObject *make_render_camera(const CTinyUSDRenderCamera *c, PyObject *owner);
static PyObject *make_render_light(const CTinyUSDRenderLight *l, PyObject *owner);
static PyObject *make_render_texture(const CTinyUSDRenderTexture *t, PyObject *owner);
static PyObject *make_render_image(const CTinyUSDRenderImage *i, PyObject *owner);
static PyObject *make_render_buffer(const CTinyUSDRenderBuffer *b, PyObject *owner);
static PyObject *make_render_animation(const CTinyUSDAnimationClip *a, PyObject *owner);
static PyObject *make_render_skeleton(const CTinyUSDSkelHierarchy *s, PyObject *owner);
static PyObject *make_render_node    (const CTinyUSDRenderNode *n, PyObject *owner);

#define RS_LIST_GETTER(PYNAME, CCOUNT, CGET, MAKE)                           \
static PyObject *                                                            \
RenderScene_##PYNAME(PyObject *self, PyObject *args)                         \
{                                                                            \
    (void)args;                                                              \
    RenderSceneObject *s = (RenderSceneObject *)self;                        \
    uint64_t n = CCOUNT(s->scene);                                           \
    PyObject *list = PyList_New((Py_ssize_t)n);                              \
    if (!list) return NULL;                                                  \
    for (uint64_t i = 0; i < n; ++i) {                                       \
        const void *h = CGET(s->scene, i);                                   \
        PyObject *w = MAKE(h, self);                                         \
        if (!w) { Py_DECREF(list); return NULL; }                            \
        PyList_SetItem(list, (Py_ssize_t)i, w);                              \
    }                                                                        \
    return list;                                                             \
}

RS_LIST_GETTER(meshes,     c_tinyusd_render_scene_num_meshes,
               c_tinyusd_render_scene_get_mesh,      make_render_mesh)
RS_LIST_GETTER(materials,  c_tinyusd_render_scene_num_materials,
               c_tinyusd_render_scene_get_material,  make_render_material)
RS_LIST_GETTER(cameras,    c_tinyusd_render_scene_num_cameras,
               c_tinyusd_render_scene_get_camera,    make_render_camera)
RS_LIST_GETTER(lights,     c_tinyusd_render_scene_num_lights,
               c_tinyusd_render_scene_get_light,     make_render_light)
RS_LIST_GETTER(textures,   c_tinyusd_render_scene_num_textures,
               c_tinyusd_render_scene_get_texture,   make_render_texture)
RS_LIST_GETTER(images,     c_tinyusd_render_scene_num_images,
               c_tinyusd_render_scene_get_image,     make_render_image)
RS_LIST_GETTER(buffers,    c_tinyusd_render_scene_num_buffers,
               c_tinyusd_render_scene_get_buffer,    make_render_buffer)
RS_LIST_GETTER(animations, c_tinyusd_render_scene_num_animations,
               c_tinyusd_render_scene_get_animation, make_render_animation)
RS_LIST_GETTER(skeletons,  c_tinyusd_render_scene_num_skeletons,
               c_tinyusd_render_scene_get_skeleton,  make_render_skeleton)
RS_LIST_GETTER(nodes,      c_tinyusd_render_scene_num_nodes,
               c_tinyusd_render_scene_get_node,      make_render_node)

#undef RS_LIST_GETTER

static PyObject *
RenderScene_default_root_node(PyObject *self, PyObject *args)
{
    (void)args;
    RenderSceneObject *s = (RenderSceneObject *)self;
    return PyLong_FromUnsignedLong(
        (unsigned long)c_tinyusd_render_scene_default_root_node(s->scene));
}

static PyObject *
RenderScene_repr(PyObject *self)
{
    RenderSceneObject *s = (RenderSceneObject *)self;
    return PyUnicode_FromFormat(
        "<tinyusdz.RenderScene meshes=%llu materials=%llu cameras=%llu lights=%llu>",
        (unsigned long long)c_tinyusd_render_scene_num_meshes(s->scene),
        (unsigned long long)c_tinyusd_render_scene_num_materials(s->scene),
        (unsigned long long)c_tinyusd_render_scene_num_cameras(s->scene),
        (unsigned long long)c_tinyusd_render_scene_num_lights(s->scene));
}

static PyMethodDef RenderScene_methods[] = {
    {"meshes",     RenderScene_meshes,     METH_NOARGS, "list[RenderMesh]"},
    {"materials",  RenderScene_materials,  METH_NOARGS, "list[RenderMaterial]"},
    {"cameras",    RenderScene_cameras,    METH_NOARGS, "list[RenderCamera]"},
    {"lights",     RenderScene_lights,     METH_NOARGS, "list[RenderLight]"},
    {"textures",   RenderScene_textures,   METH_NOARGS, "list[RenderTexture]"},
    {"images",     RenderScene_images,     METH_NOARGS, "list[RenderImage]"},
    {"buffers",    RenderScene_buffers,    METH_NOARGS, "list[RenderBuffer]"},
    {"animations", RenderScene_animations, METH_NOARGS, "list[RenderAnimation]"},
    {"skeletons",  RenderScene_skeletons,  METH_NOARGS, "list[RenderSkeleton]"},
    {"nodes",      RenderScene_nodes,      METH_NOARGS, "list[RenderNode]"},
    {"default_root_node", RenderScene_default_root_node, METH_NOARGS,
     "Index of the default root node in nodes()."},
    {NULL, NULL, 0, NULL}
};

static PyType_Slot RenderScene_slots[] = {
    {Py_tp_doc, "Flattened render-friendly scene."},
    {Py_tp_dealloc, RenderScene_dealloc},
    {Py_tp_methods, RenderScene_methods},
    {Py_tp_repr, RenderScene_repr},
    {0, NULL}
};
static PyType_Spec RenderScene_spec = {
    "tinyusdz._core.RenderScene", sizeof(RenderSceneObject), 0,
    Py_TPFLAGS_DEFAULT, RenderScene_slots
};

/* ------------------------------------------------------------------------
 * RenderMesh / RenderMaterial / RenderCamera / RenderLight — borrowed.
 * Each keeps a strong ref to the owning RenderScene.
 * -------------------------------------------------------------------- */

typedef struct {
    PyObject_HEAD
    const void *handle;   /* generic: the borrowed C pointer */
    PyObject   *owner;    /* RenderScene */
} ChildObject;

static void
Child_dealloc(PyObject *self)
{
    ChildObject *c = (ChildObject *)self;
    Py_XDECREF(c->owner);
    PyTypeObject *tp = Py_TYPE(self);
    freefunc tp_free = (freefunc)PyType_GetSlot(tp, Py_tp_free);
    tp_free(self);
    Py_DECREF(tp);
}

static PyObject *
make_child(PyObject *type_ref, const void *handle, PyObject *owner)
{
    PyTypeObject *tp = (PyTypeObject *)type_ref;
    allocfunc alloc = (allocfunc)PyType_GetSlot(tp, Py_tp_alloc);
    ChildObject *obj = (ChildObject *)alloc(tp, 0);
    if (!obj) return NULL;
    obj->handle = handle;
    Py_INCREF(owner);
    obj->owner = owner;
    return (PyObject *)obj;
}

/* ---- helper: accessor → Py string ---- */
static PyObject *
child_str_getter(PyObject *self,
                 int (*fn)(const void *h, c_tinyusd_string_t *out))
{
    ChildObject *c = (ChildObject *)self;
    c_tinyusd_string_t *buf = c_tinyusd_string_new_empty();
    if (!buf) return PyErr_NoMemory();
    fn(c->handle, buf);
    PyObject *r = PyUnicode_FromString(c_tinyusd_string_str(buf));
    c_tinyusd_string_free(buf);
    return r;
}

/* Type-pun helpers to adapt the concrete-typed C signatures into the
 * type-erased form expected by child_str_getter. */
#define STR_GETTER_ADAPTER(NAME, CFN)                                   \
static int NAME(const void *h, c_tinyusd_string_t *out)                 \
{                                                                       \
    return CFN((const void *)h, out) ? 1 : 0;                           \
}

/* ---- RenderMesh ---- */

static PyObject *
RenderMesh_name(PyObject *self, void *closure) {
    (void)closure;
    return child_str_getter(self,
        (int (*)(const void *, c_tinyusd_string_t *))c_tinyusd_render_mesh_get_name);
}
static PyObject *
RenderMesh_abs_path(PyObject *self, void *closure) {
    (void)closure;
    return child_str_getter(self,
        (int (*)(const void *, c_tinyusd_string_t *))c_tinyusd_render_mesh_get_abs_path);
}
static PyObject *
RenderMesh_display_name(PyObject *self, void *closure) {
    (void)closure;
    return child_str_getter(self,
        (int (*)(const void *, c_tinyusd_string_t *))c_tinyusd_render_mesh_get_display_name);
}

static PyObject *
RenderMesh_points(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    const void *ptr = NULL; uint64_t n = 0;
    c_tinyusd_render_mesh_get_points(
        (const CTinyUSDRenderMesh *)c->handle, &ptr, &n);
    return make_buffer_view(ptr, n, 3, 4, "f", c->owner);
}

static PyObject *
RenderMesh_face_vertex_indices(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    const void *ptr = NULL; uint64_t n = 0;
    c_tinyusd_render_mesh_get_face_vertex_indices(
        (const CTinyUSDRenderMesh *)c->handle, &ptr, &n);
    return make_buffer_view(ptr, n, 1, 4, "I", c->owner);
}

static PyObject *
RenderMesh_face_vertex_counts(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    const void *ptr = NULL; uint64_t n = 0;
    c_tinyusd_render_mesh_get_face_vertex_counts(
        (const CTinyUSDRenderMesh *)c->handle, &ptr, &n);
    return make_buffer_view(ptr, n, 1, 4, "I", c->owner);
}

static PyObject *
RenderMesh_normals(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    const void *ptr = NULL; uint64_t n_outer = 0;
    uint32_t n_inner = 0, comp = 0;
    const char *fmt = NULL;
    if (!c_tinyusd_render_mesh_get_normals(
            (const CTinyUSDRenderMesh *)c->handle,
            &ptr, &n_outer, &n_inner, &comp, &fmt)) {
        Py_RETURN_NONE;
    }
    return make_buffer_view(ptr, n_outer, n_inner, comp, fmt, c->owner);
}

static PyObject *
RenderMesh_texcoord(PyObject *self, PyObject *args) {
    ChildObject *c = (ChildObject *)self;
    unsigned int slot = 0;
    if (!PyArg_ParseTuple(args, "|I", &slot)) return NULL;
    const void *ptr = NULL; uint64_t n_outer = 0;
    uint32_t n_inner = 0, comp = 0;
    const char *fmt = NULL;
    if (!c_tinyusd_render_mesh_get_texcoord(
            (const CTinyUSDRenderMesh *)c->handle, (uint32_t)slot,
            &ptr, &n_outer, &n_inner, &comp, &fmt)) {
        Py_RETURN_NONE;
    }
    return make_buffer_view(ptr, n_outer, n_inner, comp, fmt, c->owner);
}

static PyObject *
RenderMesh_texcoord_slot_ids(PyObject *self, PyObject *args) {
    (void)args;
    ChildObject *c = (ChildObject *)self;
    uint32_t n = c_tinyusd_render_mesh_num_texcoord_slots(
        (const CTinyUSDRenderMesh *)c->handle);
    uint32_t *ids = (uint32_t *)PyMem_Malloc(sizeof(uint32_t) * (n ? n : 1));
    if (!ids) return PyErr_NoMemory();
    uint32_t got = c_tinyusd_render_mesh_get_texcoord_slot_ids(
        (const CTinyUSDRenderMesh *)c->handle, ids, n);
    PyObject *list = PyList_New((Py_ssize_t)got);
    if (!list) { PyMem_Free(ids); return NULL; }
    for (uint32_t i = 0; i < got; ++i) {
        PyList_SetItem(list, (Py_ssize_t)i, PyLong_FromUnsignedLong(ids[i]));
    }
    PyMem_Free(ids);
    return list;
}

static PyObject *
RenderMesh_display_color(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    float rgb[3];
    if (!c_tinyusd_render_mesh_get_display_color(
            (const CTinyUSDRenderMesh *)c->handle, rgb)) {
        Py_RETURN_NONE;
    }
    return Py_BuildValue("(fff)", rgb[0], rgb[1], rgb[2]);
}

static PyObject *
RenderMesh_material_id(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    int32_t id = c_tinyusd_render_mesh_material_id(
        (const CTinyUSDRenderMesh *)c->handle);
    return PyLong_FromLong((long)id);
}

static PyObject *
RenderMesh_is_right_handed(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyBool_FromLong(c_tinyusd_render_mesh_is_right_handed(
        (const CTinyUSDRenderMesh *)c->handle));
}
static PyObject *
RenderMesh_is_double_sided(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyBool_FromLong(c_tinyusd_render_mesh_is_double_sided(
        (const CTinyUSDRenderMesh *)c->handle));
}

static PyObject *
RenderMesh_repr(PyObject *self) {
    return child_str_getter(self,
        (int (*)(const void *, c_tinyusd_string_t *))c_tinyusd_render_mesh_get_abs_path);
}

static PyMethodDef RenderMesh_methods[] = {
    {"texcoord", RenderMesh_texcoord, METH_VARARGS,
     "texcoord(slot_id=0) -> BufferView (float32 [N, 2]) or None."},
    {"texcoord_slot_ids", RenderMesh_texcoord_slot_ids, METH_NOARGS,
     "List available UV slot IDs."},
    {NULL, NULL, 0, NULL}
};

static PyGetSetDef RenderMesh_getset[] = {
    {"name",               RenderMesh_name,               NULL, "Prim element name.", NULL},
    {"abs_path",           RenderMesh_abs_path,           NULL, "Absolute USD path.", NULL},
    {"display_name",       RenderMesh_display_name,       NULL, "displayName metadatum.", NULL},
    {"points",             RenderMesh_points,             NULL, "BufferView [N,3] float32.", NULL},
    {"face_vertex_indices", RenderMesh_face_vertex_indices, NULL, "BufferView [M] uint32.", NULL},
    {"face_vertex_counts", RenderMesh_face_vertex_counts, NULL, "BufferView [F] uint32.", NULL},
    {"normals",            RenderMesh_normals,            NULL, "BufferView or None.", NULL},
    {"display_color",      RenderMesh_display_color,      NULL, "(r, g, b) tuple.", NULL},
    {"material_id",        RenderMesh_material_id,        NULL, "Material index, -1 if unbound.", NULL},
    {"is_right_handed",    RenderMesh_is_right_handed,    NULL, "bool", NULL},
    {"is_double_sided",    RenderMesh_is_double_sided,    NULL, "bool", NULL},
    {NULL}
};

static PyType_Slot RenderMesh_slots[] = {
    {Py_tp_doc, "Render-scene mesh."},
    {Py_tp_dealloc, Child_dealloc},
    {Py_tp_methods, RenderMesh_methods},
    {Py_tp_getset, RenderMesh_getset},
    {Py_tp_repr, RenderMesh_repr},
    {0, NULL}
};
static PyType_Spec RenderMesh_spec = {
    "tinyusdz._core.RenderMesh", sizeof(ChildObject), 0,
    Py_TPFLAGS_DEFAULT, RenderMesh_slots
};

static PyObject *
make_render_mesh(const CTinyUSDRenderMesh *m, PyObject *owner) {
    return make_child(RenderMeshType, (const void *)m, owner);
}

/* ---- RenderMaterial ---- */

static PyObject *
RenderMaterial_name(PyObject *self, void *closure) { (void)closure;
    return child_str_getter(self,
        (int (*)(const void *, c_tinyusd_string_t *))c_tinyusd_render_material_get_name); }
static PyObject *
RenderMaterial_abs_path(PyObject *self, void *closure) { (void)closure;
    return child_str_getter(self,
        (int (*)(const void *, c_tinyusd_string_t *))c_tinyusd_render_material_get_abs_path); }
static PyObject *
RenderMaterial_has_preview_surface(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyBool_FromLong(c_tinyusd_render_material_has_preview_surface(
        (const CTinyUSDRenderMaterial *)c->handle));
}
static PyObject *
RenderMaterial_has_open_pbr(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyBool_FromLong(c_tinyusd_render_material_has_open_pbr(
        (const CTinyUSDRenderMaterial *)c->handle));
}
static PyObject *
RenderMaterial_repr(PyObject *self) {
    return child_str_getter(self,
        (int (*)(const void *, c_tinyusd_string_t *))c_tinyusd_render_material_get_abs_path);
}

/* Forward: preview_surface / open_pbr / node_graph_json getters are defined
 * further below, alongside the extended Tydra types. */
static PyObject *RenderMaterial_preview_surface(PyObject *self, void *closure);
static PyObject *RenderMaterial_open_pbr        (PyObject *self, void *closure);
static PyObject *RenderMaterial_node_graph_json (PyObject *self, void *closure);

static PyGetSetDef RenderMaterial_getset[] = {
    {"name",                 RenderMaterial_name,                 NULL, NULL, NULL},
    {"abs_path",             RenderMaterial_abs_path,             NULL, NULL, NULL},
    {"has_preview_surface",  RenderMaterial_has_preview_surface,  NULL, NULL, NULL},
    {"has_open_pbr",         RenderMaterial_has_open_pbr,         NULL, NULL, NULL},
    {"preview_surface",      RenderMaterial_preview_surface,      NULL,
     "dict of UsdPreviewSurface inputs (each {value, texture_id}), or None.", NULL},
    {"open_pbr",             RenderMaterial_open_pbr,             NULL,
     "dict of MaterialX OpenPBR inputs (each {value, texture_id}), or None.", NULL},
    {"node_graph_json",      RenderMaterial_node_graph_json,      NULL,
     "MaterialX node graph as a JSON string, or None if no OpenPBR graph.", NULL},
    {NULL}
};
static PyType_Slot RenderMaterial_slots[] = {
    {Py_tp_doc, "Render-scene material."},
    {Py_tp_dealloc, Child_dealloc},
    {Py_tp_getset, RenderMaterial_getset},
    {Py_tp_repr, RenderMaterial_repr},
    {0, NULL}
};
static PyType_Spec RenderMaterial_spec = {
    "tinyusdz._core.RenderMaterial", sizeof(ChildObject), 0,
    Py_TPFLAGS_DEFAULT, RenderMaterial_slots
};

static PyObject *
make_render_material(const CTinyUSDRenderMaterial *m, PyObject *owner) {
    return make_child(RenderMaterialType, (const void *)m, owner);
}

/* ---- RenderCamera ---- */

#define CAM_FLOAT_GETTER(NAME, CFN)                                      \
static PyObject *NAME(PyObject *self, void *closure)                     \
{                                                                        \
    (void)closure;                                                       \
    ChildObject *c = (ChildObject *)self;                                \
    return PyFloat_FromDouble((double)CFN((const CTinyUSDRenderCamera *)c->handle)); \
}

CAM_FLOAT_GETTER(RenderCamera_znear, c_tinyusd_render_camera_znear)
CAM_FLOAT_GETTER(RenderCamera_zfar,  c_tinyusd_render_camera_zfar)
CAM_FLOAT_GETTER(RenderCamera_focal_length, c_tinyusd_render_camera_focal_length)
CAM_FLOAT_GETTER(RenderCamera_horizontal_aperture, c_tinyusd_render_camera_horizontal_aperture)
CAM_FLOAT_GETTER(RenderCamera_vertical_aperture,   c_tinyusd_render_camera_vertical_aperture)
#undef CAM_FLOAT_GETTER

static PyObject *
RenderCamera_name(PyObject *self, void *closure) { (void)closure;
    return child_str_getter(self,
        (int (*)(const void *, c_tinyusd_string_t *))c_tinyusd_render_camera_get_name); }
static PyObject *
RenderCamera_abs_path(PyObject *self, void *closure) { (void)closure;
    return child_str_getter(self,
        (int (*)(const void *, c_tinyusd_string_t *))c_tinyusd_render_camera_get_abs_path); }

static PyObject *
RenderCamera_projection(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    int ortho = c_tinyusd_render_camera_projection(
        (const CTinyUSDRenderCamera *)c->handle);
    return PyUnicode_FromString(ortho ? "orthographic" : "perspective");
}
static PyObject *
RenderCamera_repr(PyObject *self) {
    return child_str_getter(self,
        (int (*)(const void *, c_tinyusd_string_t *))c_tinyusd_render_camera_get_abs_path);
}

static PyGetSetDef RenderCamera_getset[] = {
    {"name",                RenderCamera_name,                NULL, NULL, NULL},
    {"abs_path",            RenderCamera_abs_path,            NULL, NULL, NULL},
    {"znear",               RenderCamera_znear,               NULL, NULL, NULL},
    {"zfar",                RenderCamera_zfar,                NULL, NULL, NULL},
    {"focal_length",        RenderCamera_focal_length,        NULL, NULL, NULL},
    {"horizontal_aperture", RenderCamera_horizontal_aperture, NULL, NULL, NULL},
    {"vertical_aperture",   RenderCamera_vertical_aperture,   NULL, NULL, NULL},
    {"projection",          RenderCamera_projection,          NULL, NULL, NULL},
    {NULL}
};
static PyType_Slot RenderCamera_slots[] = {
    {Py_tp_doc, "Render-scene camera."},
    {Py_tp_dealloc, Child_dealloc},
    {Py_tp_getset, RenderCamera_getset},
    {Py_tp_repr, RenderCamera_repr},
    {0, NULL}
};
static PyType_Spec RenderCamera_spec = {
    "tinyusdz._core.RenderCamera", sizeof(ChildObject), 0,
    Py_TPFLAGS_DEFAULT, RenderCamera_slots
};

static PyObject *
make_render_camera(const CTinyUSDRenderCamera *c, PyObject *owner) {
    return make_child(RenderCameraType, (const void *)c, owner);
}

/* ---- RenderLight ---- */

static PyObject *
RenderLight_name(PyObject *self, void *closure) { (void)closure;
    return child_str_getter(self,
        (int (*)(const void *, c_tinyusd_string_t *))c_tinyusd_render_light_get_name); }
static PyObject *
RenderLight_abs_path(PyObject *self, void *closure) { (void)closure;
    return child_str_getter(self,
        (int (*)(const void *, c_tinyusd_string_t *))c_tinyusd_render_light_get_abs_path); }

static PyObject *
RenderLight_type(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    CTinyUSDLightType t = c_tinyusd_render_light_type(
        (const CTinyUSDRenderLight *)c->handle);
    const char *s = "unknown";
    switch (t) {
        case C_TINYUSD_LIGHT_POINT:    s = "point";    break;
        case C_TINYUSD_LIGHT_SPHERE:   s = "sphere";   break;
        case C_TINYUSD_LIGHT_DISK:     s = "disk";     break;
        case C_TINYUSD_LIGHT_RECT:     s = "rect";     break;
        case C_TINYUSD_LIGHT_CYLINDER: s = "cylinder"; break;
        case C_TINYUSD_LIGHT_DISTANT:  s = "distant";  break;
        case C_TINYUSD_LIGHT_DOME:     s = "dome";     break;
        case C_TINYUSD_LIGHT_GEOMETRY: s = "geometry"; break;
        case C_TINYUSD_LIGHT_PORTAL:   s = "portal";   break;
        default: break;
    }
    return PyUnicode_FromString(s);
}

static PyObject *
RenderLight_intensity(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyFloat_FromDouble((double)c_tinyusd_render_light_intensity(
        (const CTinyUSDRenderLight *)c->handle));
}

static PyObject *
RenderLight_color(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    float rgb[3];
    if (!c_tinyusd_render_light_get_color(
            (const CTinyUSDRenderLight *)c->handle, rgb)) Py_RETURN_NONE;
    return Py_BuildValue("(fff)", rgb[0], rgb[1], rgb[2]);
}

static PyObject *
RenderLight_repr(PyObject *self) {
    return child_str_getter(self,
        (int (*)(const void *, c_tinyusd_string_t *))c_tinyusd_render_light_get_abs_path);
}

static PyGetSetDef RenderLight_getset[] = {
    {"name",      RenderLight_name,      NULL, NULL, NULL},
    {"abs_path",  RenderLight_abs_path,  NULL, NULL, NULL},
    {"type",      RenderLight_type,      NULL, NULL, NULL},
    {"intensity", RenderLight_intensity, NULL, NULL, NULL},
    {"color",     RenderLight_color,     NULL, NULL, NULL},
    {NULL}
};
static PyType_Slot RenderLight_slots[] = {
    {Py_tp_doc, "Render-scene light."},
    {Py_tp_dealloc, Child_dealloc},
    {Py_tp_getset, RenderLight_getset},
    {Py_tp_repr, RenderLight_repr},
    {0, NULL}
};
static PyType_Spec RenderLight_spec = {
    "tinyusdz._core.RenderLight", sizeof(ChildObject), 0,
    Py_TPFLAGS_DEFAULT, RenderLight_slots
};

static PyObject *
make_render_light(const CTinyUSDRenderLight *l, PyObject *owner) {
    return make_child(RenderLightType, (const void *)l, owner);
}

/* ========================================================================
 * Extended Tydra types: textures, images, buffers, animations, skeletons.
 * ======================================================================== */

/* ---- PreviewSurface: exposed via RenderMaterial.preview_surface as dict. */

static PyObject *
make_shader_param_vec3(const float value[3], int32_t tex_id)
{
    PyObject *val = Py_BuildValue("(fff)", value[0], value[1], value[2]);
    if (!val) return NULL;
    PyObject *d = Py_BuildValue("{s:O,s:i}", "value", val, "texture_id",
                                (int)tex_id);
    Py_DECREF(val);
    return d;
}

static PyObject *
make_shader_param_scalar(float value, int32_t tex_id)
{
    return Py_BuildValue("{s:f,s:i}", "value", (double)value,
                         "texture_id", (int)tex_id);
}

static PyObject *
RenderMaterial_preview_surface(PyObject *self, void *closure)
{
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    CTinyUSDPreviewSurface ps;
    if (!c_tinyusd_render_material_get_preview_surface(
            (const CTinyUSDRenderMaterial *)c->handle, &ps)) Py_RETURN_NONE;
    if (!ps.has_shader) Py_RETURN_NONE;

    PyObject *d = PyDict_New();
    if (!d) return NULL;

#define SET_VEC3(KEY, V, T)                                                \
    do {                                                                   \
        PyObject *x = make_shader_param_vec3(V, T);                        \
        if (!x) { Py_DECREF(d); return NULL; }                             \
        if (PyDict_SetItemString(d, KEY, x) < 0) {                         \
            Py_DECREF(x); Py_DECREF(d); return NULL;                       \
        }                                                                  \
        Py_DECREF(x);                                                      \
    } while (0)

#define SET_SCALAR(KEY, V, T)                                              \
    do {                                                                   \
        PyObject *x = make_shader_param_scalar(V, T);                      \
        if (!x) { Py_DECREF(d); return NULL; }                             \
        if (PyDict_SetItemString(d, KEY, x) < 0) {                         \
            Py_DECREF(x); Py_DECREF(d); return NULL;                       \
        }                                                                  \
        Py_DECREF(x);                                                      \
    } while (0)

    PyDict_SetItemString(d, "use_specular_workflow",
                         PyBool_FromLong(ps.use_specular_workflow));
    SET_VEC3  ("diffuse_color",       ps.diffuse_color,       ps.diffuse_color_tex);
    SET_VEC3  ("emissive_color",      ps.emissive_color,      ps.emissive_color_tex);
    SET_VEC3  ("specular_color",      ps.specular_color,      ps.specular_color_tex);
    SET_SCALAR("metallic",            ps.metallic,            ps.metallic_tex);
    SET_SCALAR("roughness",           ps.roughness,           ps.roughness_tex);
    SET_SCALAR("clearcoat",           ps.clearcoat,           ps.clearcoat_tex);
    SET_SCALAR("clearcoat_roughness", ps.clearcoat_roughness, ps.clearcoat_roughness_tex);
    SET_SCALAR("opacity",             ps.opacity,             ps.opacity_tex);
    SET_SCALAR("opacity_threshold",   ps.opacity_threshold,   ps.opacity_threshold_tex);
    SET_SCALAR("ior",                 ps.ior,                 ps.ior_tex);
    SET_VEC3  ("normal",              ps.normal,              ps.normal_tex);
    SET_SCALAR("displacement",        ps.displacement,        ps.displacement_tex);
    SET_SCALAR("occlusion",           ps.occlusion,           ps.occlusion_tex);

#undef SET_VEC3
#undef SET_SCALAR
    return d;
}

/* ---- OpenPBR: exposed via RenderMaterial.open_pbr as dict. */

static PyObject *
RenderMaterial_open_pbr(PyObject *self, void *closure)
{
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    CTinyUSDOpenPBR ps;
    if (!c_tinyusd_render_material_get_openpbr(
            (const CTinyUSDRenderMaterial *)c->handle, &ps)) Py_RETURN_NONE;
    if (!ps.has_shader) Py_RETURN_NONE;

    PyObject *d = PyDict_New();
    if (!d) return NULL;

#define SET_VEC3(KEY, V, T)                                                \
    do {                                                                   \
        PyObject *x = make_shader_param_vec3(V, T);                        \
        if (!x) { Py_DECREF(d); return NULL; }                             \
        if (PyDict_SetItemString(d, KEY, x) < 0) {                         \
            Py_DECREF(x); Py_DECREF(d); return NULL;                       \
        }                                                                  \
        Py_DECREF(x);                                                      \
    } while (0)

#define SET_SCALAR(KEY, V, T)                                              \
    do {                                                                   \
        PyObject *x = make_shader_param_scalar(V, T);                      \
        if (!x) { Py_DECREF(d); return NULL; }                             \
        if (PyDict_SetItemString(d, KEY, x) < 0) {                         \
            Py_DECREF(x); Py_DECREF(d); return NULL;                       \
        }                                                                  \
        Py_DECREF(x);                                                      \
    } while (0)

#define SET_FLOAT(KEY, V)                                                  \
    do {                                                                   \
        PyObject *x = PyFloat_FromDouble((double)(V));                     \
        if (!x) { Py_DECREF(d); return NULL; }                             \
        if (PyDict_SetItemString(d, KEY, x) < 0) {                         \
            Py_DECREF(x); Py_DECREF(d); return NULL;                       \
        }                                                                  \
        Py_DECREF(x);                                                      \
    } while (0)

    /* Base. */
    SET_SCALAR("base_weight",            ps.base_weight,            ps.base_weight_tex);
    SET_VEC3  ("base_color",             ps.base_color,             ps.base_color_tex);
    SET_SCALAR("base_roughness",         ps.base_roughness,         ps.base_roughness_tex);
    SET_SCALAR("base_metalness",         ps.base_metalness,         ps.base_metalness_tex);
    SET_SCALAR("base_diffuse_roughness", ps.base_diffuse_roughness, ps.base_diffuse_roughness_tex);

    /* Specular. */
    SET_SCALAR("specular_weight",              ps.specular_weight,              ps.specular_weight_tex);
    SET_VEC3  ("specular_color",               ps.specular_color,               ps.specular_color_tex);
    SET_SCALAR("specular_roughness",           ps.specular_roughness,           ps.specular_roughness_tex);
    SET_SCALAR("specular_ior",                 ps.specular_ior,                 ps.specular_ior_tex);
    SET_SCALAR("specular_ior_level",           ps.specular_ior_level,           ps.specular_ior_level_tex);
    SET_SCALAR("specular_anisotropy",          ps.specular_anisotropy,          ps.specular_anisotropy_tex);
    SET_SCALAR("specular_rotation",            ps.specular_rotation,            ps.specular_rotation_tex);
    SET_SCALAR("specular_roughness_anisotropy",ps.specular_roughness_anisotropy,ps.specular_roughness_anisotropy_tex);

    /* Transmission. */
    SET_SCALAR("transmission_weight",               ps.transmission_weight,               ps.transmission_weight_tex);
    SET_VEC3  ("transmission_color",                ps.transmission_color,                ps.transmission_color_tex);
    SET_SCALAR("transmission_depth",                ps.transmission_depth,                ps.transmission_depth_tex);
    SET_VEC3  ("transmission_scatter",              ps.transmission_scatter,              ps.transmission_scatter_tex);
    SET_SCALAR("transmission_scatter_anisotropy",   ps.transmission_scatter_anisotropy,   ps.transmission_scatter_anisotropy_tex);
    SET_SCALAR("transmission_dispersion",           ps.transmission_dispersion,           ps.transmission_dispersion_tex);
    SET_SCALAR("transmission_dispersion_abbe_number", ps.transmission_dispersion_abbe_number, ps.transmission_dispersion_abbe_number_tex);
    SET_SCALAR("transmission_dispersion_scale",     ps.transmission_dispersion_scale,     ps.transmission_dispersion_scale_tex);

    /* Subsurface. */
    SET_SCALAR("subsurface_weight",              ps.subsurface_weight,              ps.subsurface_weight_tex);
    SET_VEC3  ("subsurface_color",               ps.subsurface_color,               ps.subsurface_color_tex);
    SET_SCALAR("subsurface_radius",              ps.subsurface_radius,              ps.subsurface_radius_tex);
    SET_VEC3  ("subsurface_radius_scale",        ps.subsurface_radius_scale,        ps.subsurface_radius_scale_tex);
    SET_SCALAR("subsurface_scale",               ps.subsurface_scale,               ps.subsurface_scale_tex);
    SET_SCALAR("subsurface_anisotropy",          ps.subsurface_anisotropy,          ps.subsurface_anisotropy_tex);
    SET_SCALAR("subsurface_scatter_anisotropy",  ps.subsurface_scatter_anisotropy,  ps.subsurface_scatter_anisotropy_tex);

    /* Sheen. */
    SET_SCALAR("sheen_weight",    ps.sheen_weight,    ps.sheen_weight_tex);
    SET_VEC3  ("sheen_color",     ps.sheen_color,     ps.sheen_color_tex);
    SET_SCALAR("sheen_roughness", ps.sheen_roughness, ps.sheen_roughness_tex);

    /* Fuzz. */
    SET_SCALAR("fuzz_weight",    ps.fuzz_weight,    ps.fuzz_weight_tex);
    SET_VEC3  ("fuzz_color",     ps.fuzz_color,     ps.fuzz_color_tex);
    SET_SCALAR("fuzz_roughness", ps.fuzz_roughness, ps.fuzz_roughness_tex);

    /* Thin film. */
    SET_SCALAR("thin_film_weight",    ps.thin_film_weight,    ps.thin_film_weight_tex);
    SET_SCALAR("thin_film_thickness", ps.thin_film_thickness, ps.thin_film_thickness_tex);
    SET_SCALAR("thin_film_ior",       ps.thin_film_ior,       ps.thin_film_ior_tex);

    /* Coat. */
    SET_SCALAR("coat_weight",               ps.coat_weight,               ps.coat_weight_tex);
    SET_VEC3  ("coat_color",                ps.coat_color,                ps.coat_color_tex);
    SET_SCALAR("coat_roughness",            ps.coat_roughness,            ps.coat_roughness_tex);
    SET_SCALAR("coat_anisotropy",           ps.coat_anisotropy,           ps.coat_anisotropy_tex);
    SET_SCALAR("coat_rotation",             ps.coat_rotation,             ps.coat_rotation_tex);
    SET_SCALAR("coat_ior",                  ps.coat_ior,                  ps.coat_ior_tex);
    SET_SCALAR("coat_affect_color",         ps.coat_affect_color,         ps.coat_affect_color_tex);
    SET_SCALAR("coat_affect_roughness",     ps.coat_affect_roughness,     ps.coat_affect_roughness_tex);
    SET_SCALAR("coat_roughness_anisotropy", ps.coat_roughness_anisotropy, ps.coat_roughness_anisotropy_tex);
    SET_SCALAR("coat_darkening",            ps.coat_darkening,            ps.coat_darkening_tex);

    /* Emission. */
    SET_SCALAR("emission_luminance", ps.emission_luminance, ps.emission_luminance_tex);
    SET_VEC3  ("emission_color",     ps.emission_color,     ps.emission_color_tex);

    /* Geometry. */
    SET_SCALAR("opacity", ps.opacity, ps.opacity_tex);
    SET_VEC3  ("normal",  ps.normal,  ps.normal_tex);
    SET_VEC3  ("tangent", ps.tangent, ps.tangent_tex);

    /* Plain scalars. */
    SET_FLOAT("tangent_rotation", ps.tangent_rotation);
    SET_FLOAT("normal_map_scale", ps.normal_map_scale);

    /* Coat normal / tangent separates. */
    SET_VEC3  ("coat_normal",           ps.coat_normal,           ps.coat_normal_tex);
    SET_VEC3  ("coat_tangent",          ps.coat_tangent,          ps.coat_tangent_tex);
    SET_FLOAT ("coat_tangent_rotation", ps.coat_tangent_rotation);
    SET_FLOAT ("coat_normal_map_scale", ps.coat_normal_map_scale);

#undef SET_VEC3
#undef SET_SCALAR
#undef SET_FLOAT
    return d;
}

static PyObject *
RenderMaterial_node_graph_json(PyObject *self, void *closure)
{
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    const char *ptr = NULL;
    uint64_t len = 0;
    if (!c_tinyusd_render_material_get_node_graph_json(
            (const CTinyUSDRenderMaterial *)c->handle, &ptr, &len)) {
        Py_RETURN_NONE;
    }
    return PyUnicode_FromStringAndSize(ptr, (Py_ssize_t)len);
}

/* ---- RenderTexture ---- */

#define CHILD_STR_GETTER(FNAME, CFN)                                       \
static PyObject *FNAME(PyObject *self, void *closure) {                    \
    (void)closure;                                                          \
    return child_str_getter(self,                                           \
        (int (*)(const void *, c_tinyusd_string_t *))CFN);                  \
}

CHILD_STR_GETTER(RenderTexture_name,        c_tinyusd_render_texture_get_name)
CHILD_STR_GETTER(RenderTexture_abs_path,    c_tinyusd_render_texture_get_abs_path)
CHILD_STR_GETTER(RenderTexture_varname_uv,  c_tinyusd_render_texture_get_varname_uv)

static const char *wrap_mode_str(int w) {
    switch (w) {
        case 0: return "clamp_to_edge";
        case 1: return "repeat";
        case 2: return "mirror";
        case 3: return "clamp_to_border";
        default: return "unknown";
    }
}

static PyObject *
RenderTexture_wrap_s(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyUnicode_FromString(wrap_mode_str(
        c_tinyusd_render_texture_wrap_s(
            (const CTinyUSDRenderTexture *)c->handle)));
}
static PyObject *
RenderTexture_wrap_t(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyUnicode_FromString(wrap_mode_str(
        c_tinyusd_render_texture_wrap_t(
            (const CTinyUSDRenderTexture *)c->handle)));
}
static PyObject *
RenderTexture_texture_image_id(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyLong_FromLongLong((long long)c_tinyusd_render_texture_image_id(
        (const CTinyUSDRenderTexture *)c->handle));
}
static PyObject *
RenderTexture_bias(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    float v[4];
    c_tinyusd_render_texture_get_bias(
        (const CTinyUSDRenderTexture *)c->handle, v);
    return Py_BuildValue("(ffff)", v[0], v[1], v[2], v[3]);
}
static PyObject *
RenderTexture_scale(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    float v[4];
    c_tinyusd_render_texture_get_scale(
        (const CTinyUSDRenderTexture *)c->handle, v);
    return Py_BuildValue("(ffff)", v[0], v[1], v[2], v[3]);
}
static PyObject *
RenderTexture_repr(PyObject *self) {
    return child_str_getter(self,
        (int (*)(const void *, c_tinyusd_string_t *))c_tinyusd_render_texture_get_abs_path);
}

static const char *channel_str(int ch) {
    switch (ch) {
        case 0: return "r";
        case 1: return "g";
        case 2: return "b";
        case 3: return "a";
        case 4: return "rgb";
        case 5: return "rgba";
        default: return "rgb";
    }
}

static PyObject *
RenderTexture_output_channel(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyUnicode_FromString(channel_str(
        c_tinyusd_render_texture_output_channel(
            (const CTinyUSDRenderTexture *)c->handle)));
}

static PyObject *
RenderTexture_fallback_uv(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    float v[4];
    c_tinyusd_render_texture_get_fallback_uv(
        (const CTinyUSDRenderTexture *)c->handle, v);
    return Py_BuildValue("(ffff)", v[0], v[1], v[2], v[3]);
}

static PyObject *
RenderTexture_has_transform2d(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyBool_FromLong(c_tinyusd_render_texture_has_transform2d(
        (const CTinyUSDRenderTexture *)c->handle));
}

static PyObject *
RenderTexture_tx_rotation(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyFloat_FromDouble((double)c_tinyusd_render_texture_tx_rotation(
        (const CTinyUSDRenderTexture *)c->handle));
}

static PyObject *
RenderTexture_tx_scale(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    float v[2];
    c_tinyusd_render_texture_get_tx_scale(
        (const CTinyUSDRenderTexture *)c->handle, v);
    return Py_BuildValue("(ff)", v[0], v[1]);
}

static PyObject *
RenderTexture_tx_translation(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    float v[2];
    c_tinyusd_render_texture_get_tx_translation(
        (const CTinyUSDRenderTexture *)c->handle, v);
    return Py_BuildValue("(ff)", v[0], v[1]);
}

static PyObject *
RenderTexture_transform(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    float m[9];
    c_tinyusd_render_texture_get_transform(
        (const CTinyUSDRenderTexture *)c->handle, m);
    return Py_BuildValue("((fff)(fff)(fff))",
        m[0], m[1], m[2],
        m[3], m[4], m[5],
        m[6], m[7], m[8]);
}

static PyGetSetDef RenderTexture_getset[] = {
    {"name",             RenderTexture_name,             NULL, NULL, NULL},
    {"abs_path",         RenderTexture_abs_path,         NULL, NULL, NULL},
    {"varname_uv",       RenderTexture_varname_uv,       NULL, NULL, NULL},
    {"wrap_s",           RenderTexture_wrap_s,           NULL, NULL, NULL},
    {"wrap_t",           RenderTexture_wrap_t,           NULL, NULL, NULL},
    {"texture_image_id", RenderTexture_texture_image_id, NULL, NULL, NULL},
    {"bias",             RenderTexture_bias,             NULL, NULL, NULL},
    {"scale",            RenderTexture_scale,            NULL, NULL, NULL},
    {"output_channel",   RenderTexture_output_channel,   NULL,
     "Connected output channel: 'r'|'g'|'b'|'a'|'rgb'|'rgba'.", NULL},
    {"fallback_uv",      RenderTexture_fallback_uv,      NULL, NULL, NULL},
    {"has_transform2d",  RenderTexture_has_transform2d,  NULL, NULL, NULL},
    {"tx_rotation",      RenderTexture_tx_rotation,      NULL, NULL, NULL},
    {"tx_scale",         RenderTexture_tx_scale,         NULL, NULL, NULL},
    {"tx_translation",   RenderTexture_tx_translation,   NULL, NULL, NULL},
    {"transform",        RenderTexture_transform,        NULL,
     "3x3 row-major transform matrix as nested tuple.", NULL},
    {NULL}
};
static PyType_Slot RenderTexture_slots[] = {
    {Py_tp_doc, "Render-scene UV texture node."},
    {Py_tp_dealloc, Child_dealloc},
    {Py_tp_getset, RenderTexture_getset},
    {Py_tp_repr, RenderTexture_repr},
    {0, NULL}
};
static PyType_Spec RenderTexture_spec = {
    "tinyusdz._core.RenderTexture", sizeof(ChildObject), 0,
    Py_TPFLAGS_DEFAULT, RenderTexture_slots
};

static PyObject *
make_render_texture(const CTinyUSDRenderTexture *t, PyObject *owner) {
    return make_child(RenderTextureType, (const void *)t, owner);
}

/* ---- RenderImage ---- */

static const char *color_space_str(int cs) {
    /* Maps tydra::ColorSpace enum ordinal to a token. Conservative list that
     * covers the common entries; anything unknown falls through. */
    switch (cs) {
        case 0: return "srgb";
        case 1: return "linear";
        case 2: return "raw";
        case 3: return "aces2065_1";
        case 4: return "rec709";
        case 5: return "rec2020";
        case 6: return "displayp3";
        default: return "unknown";
    }
}

static const char *component_type_str(int ct) {
    switch (ct) {
        case 0: return "uint8";
        case 1: return "int8";
        case 2: return "uint16";
        case 3: return "int16";
        case 4: return "uint32";
        case 5: return "int32";
        case 6: return "half";
        case 7: return "float";
        case 8: return "double";
        default: return "unknown";
    }
}

CHILD_STR_GETTER(RenderImage_asset_identifier, c_tinyusd_render_image_get_asset_identifier)

#define IMAGE_INT_GETTER(NAME, CFN)                                        \
static PyObject *NAME(PyObject *self, void *closure) {                     \
    (void)closure;                                                          \
    ChildObject *c = (ChildObject *)self;                                   \
    return PyLong_FromLong((long)CFN((const CTinyUSDRenderImage *)c->handle)); \
}

IMAGE_INT_GETTER(RenderImage_width,    c_tinyusd_render_image_width)
IMAGE_INT_GETTER(RenderImage_height,   c_tinyusd_render_image_height)
IMAGE_INT_GETTER(RenderImage_channels, c_tinyusd_render_image_channels)
IMAGE_INT_GETTER(RenderImage_miplevel, c_tinyusd_render_image_miplevel)

static PyObject *
RenderImage_buffer_id(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyLong_FromLongLong((long long)c_tinyusd_render_image_buffer_id(
        (const CTinyUSDRenderImage *)c->handle));
}
static PyObject *
RenderImage_color_space(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyUnicode_FromString(color_space_str(
        c_tinyusd_render_image_color_space(
            (const CTinyUSDRenderImage *)c->handle)));
}
static PyObject *
RenderImage_is_decoded(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyBool_FromLong(c_tinyusd_render_image_is_decoded(
        (const CTinyUSDRenderImage *)c->handle));
}
static PyObject *
RenderImage_component_type(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyUnicode_FromString(component_type_str(
        c_tinyusd_render_image_texel_component_type(
            (const CTinyUSDRenderImage *)c->handle)));
}
static PyObject *
RenderImage_repr(PyObject *self) {
    ChildObject *c = (ChildObject *)self;
    int32_t w = c_tinyusd_render_image_width((const CTinyUSDRenderImage *)c->handle);
    int32_t h = c_tinyusd_render_image_height((const CTinyUSDRenderImage *)c->handle);
    int32_t ch = c_tinyusd_render_image_channels((const CTinyUSDRenderImage *)c->handle);
    return PyUnicode_FromFormat(
        "<tinyusdz.RenderImage %dx%dx%d>", (int)w, (int)h, (int)ch);
}

static PyGetSetDef RenderImage_getset[] = {
    {"asset_identifier", RenderImage_asset_identifier, NULL, NULL, NULL},
    {"width",            RenderImage_width,            NULL, NULL, NULL},
    {"height",           RenderImage_height,           NULL, NULL, NULL},
    {"channels",         RenderImage_channels,         NULL, NULL, NULL},
    {"miplevel",         RenderImage_miplevel,         NULL, NULL, NULL},
    {"buffer_id",        RenderImage_buffer_id,        NULL, NULL, NULL},
    {"color_space",      RenderImage_color_space,      NULL, NULL, NULL},
    {"is_decoded",       RenderImage_is_decoded,       NULL, NULL, NULL},
    {"component_type",   RenderImage_component_type,   NULL, NULL, NULL},
    {NULL}
};
static PyType_Slot RenderImage_slots[] = {
    {Py_tp_doc, "Render-scene texture image descriptor."},
    {Py_tp_dealloc, Child_dealloc},
    {Py_tp_getset, RenderImage_getset},
    {Py_tp_repr, RenderImage_repr},
    {0, NULL}
};
static PyType_Spec RenderImage_spec = {
    "tinyusdz._core.RenderImage", sizeof(ChildObject), 0,
    Py_TPFLAGS_DEFAULT, RenderImage_slots
};

static PyObject *
make_render_image(const CTinyUSDRenderImage *i, PyObject *owner) {
    return make_child(RenderImageType, (const void *)i, owner);
}

/* ---- RenderBuffer ---- */

static PyObject *
RenderBuffer_component_type(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyUnicode_FromString(component_type_str(
        c_tinyusd_render_buffer_component_type(
            (const CTinyUSDRenderBuffer *)c->handle)));
}

static PyObject *
RenderBuffer_bytes(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    const void *ptr = NULL; uint64_t n = 0;
    c_tinyusd_render_buffer_get_bytes(
        (const CTinyUSDRenderBuffer *)c->handle, &ptr, &n);
    return make_buffer_view(ptr, n, 1, 1, "B", c->owner);
}

static PyObject *
RenderBuffer_nbytes(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    const void *ptr = NULL; uint64_t n = 0;
    c_tinyusd_render_buffer_get_bytes(
        (const CTinyUSDRenderBuffer *)c->handle, &ptr, &n);
    return PyLong_FromUnsignedLongLong((unsigned long long)n);
}
static PyObject *
RenderBuffer_repr(PyObject *self) {
    ChildObject *c = (ChildObject *)self;
    const void *ptr = NULL; uint64_t n = 0;
    c_tinyusd_render_buffer_get_bytes(
        (const CTinyUSDRenderBuffer *)c->handle, &ptr, &n);
    const char *ct = component_type_str(c_tinyusd_render_buffer_component_type(
        (const CTinyUSDRenderBuffer *)c->handle));
    return PyUnicode_FromFormat(
        "<tinyusdz.RenderBuffer %llu bytes component=%s>",
        (unsigned long long)n, ct);
}

static PyGetSetDef RenderBuffer_getset[] = {
    {"component_type", RenderBuffer_component_type, NULL, NULL, NULL},
    {"bytes",          RenderBuffer_bytes,          NULL,
     "BufferView of uint8 bytes.", NULL},
    {"nbytes",         RenderBuffer_nbytes,         NULL, NULL, NULL},
    {NULL}
};
static PyType_Slot RenderBuffer_slots[] = {
    {Py_tp_doc, "Raw byte-storage buffer backing images/textures."},
    {Py_tp_dealloc, Child_dealloc},
    {Py_tp_getset, RenderBuffer_getset},
    {Py_tp_repr, RenderBuffer_repr},
    {0, NULL}
};
static PyType_Spec RenderBuffer_spec = {
    "tinyusdz._core.RenderBuffer", sizeof(ChildObject), 0,
    Py_TPFLAGS_DEFAULT, RenderBuffer_slots
};

static PyObject *
make_render_buffer(const CTinyUSDRenderBuffer *b, PyObject *owner) {
    return make_child(RenderBufferType, (const void *)b, owner);
}

/* ---- AnimationSampler: (animation_owner, sampler_idx). ---- */

typedef struct {
    PyObject_HEAD
    PyObject *animation;   /* strong ref to RenderAnimation */
    uint64_t idx;
} SamplerObject;

static void
Sampler_dealloc(PyObject *self)
{
    SamplerObject *s = (SamplerObject *)self;
    Py_XDECREF(s->animation);
    PyTypeObject *tp = Py_TYPE(self);
    freefunc tp_free = (freefunc)PyType_GetSlot(tp, Py_tp_free);
    tp_free(self);
    Py_DECREF(tp);
}

static PyObject *
make_sampler(PyObject *animation, uint64_t idx)
{
    PyTypeObject *tp = (PyTypeObject *)AnimationSamplerType;
    allocfunc alloc = (allocfunc)PyType_GetSlot(tp, Py_tp_alloc);
    SamplerObject *obj = (SamplerObject *)alloc(tp, 0);
    if (!obj) return NULL;
    Py_INCREF(animation);
    obj->animation = animation;
    obj->idx = idx;
    return (PyObject *)obj;
}

static const char *interpolation_str(int interp) {
    switch (interp) {
        case 0: return "step";
        case 1: return "linear";
        case 2: return "cubicspline";
        default: return "unknown";
    }
}

static int
sampler_fetch(SamplerObject *s, const float **times, uint64_t *n_times,
              const float **values, uint64_t *n_values, int *interp)
{
    ChildObject *c = (ChildObject *)s->animation;
    return c_tinyusd_animation_get_sampler(
        (const CTinyUSDAnimationClip *)c->handle, s->idx,
        times, n_times, values, n_values, interp);
}

static PyObject *
Sampler_times(PyObject *self, void *closure)
{
    (void)closure;
    SamplerObject *s = (SamplerObject *)self;
    const float *t = NULL, *v = NULL;
    uint64_t nt = 0, nv = 0; int interp = 0;
    if (!sampler_fetch(s, &t, &nt, &v, &nv, &interp)) Py_RETURN_NONE;
    /* BufferView must keep the RenderScene alive. The sampler holds a ref
     * to RenderAnimation which is a ChildObject owning the RenderScene ref.
     */
    ChildObject *c = (ChildObject *)s->animation;
    return make_buffer_view(t, nt, 1, 4, "f", c->owner);
}
static PyObject *
Sampler_values(PyObject *self, void *closure)
{
    (void)closure;
    SamplerObject *s = (SamplerObject *)self;
    const float *t = NULL, *v = NULL;
    uint64_t nt = 0, nv = 0; int interp = 0;
    if (!sampler_fetch(s, &t, &nt, &v, &nv, &interp)) Py_RETURN_NONE;
    ChildObject *c = (ChildObject *)s->animation;
    return make_buffer_view(v, nv, 1, 4, "f", c->owner);
}
static PyObject *
Sampler_interpolation(PyObject *self, void *closure)
{
    (void)closure;
    SamplerObject *s = (SamplerObject *)self;
    const float *t = NULL, *v = NULL;
    uint64_t nt = 0, nv = 0; int interp = 0;
    sampler_fetch(s, &t, &nt, &v, &nv, &interp);
    return PyUnicode_FromString(interpolation_str(interp));
}
static PyObject *
Sampler_repr(PyObject *self)
{
    SamplerObject *s = (SamplerObject *)self;
    const float *t = NULL, *v = NULL;
    uint64_t nt = 0, nv = 0; int interp = 0;
    sampler_fetch(s, &t, &nt, &v, &nv, &interp);
    return PyUnicode_FromFormat(
        "<tinyusdz.AnimationSampler keyframes=%llu interpolation=%s>",
        (unsigned long long)nt, interpolation_str(interp));
}

static PyGetSetDef Sampler_getset[] = {
    {"times",         Sampler_times,         NULL, "BufferView float32[N].", NULL},
    {"values",        Sampler_values,        NULL, "BufferView float32[M].", NULL},
    {"interpolation", Sampler_interpolation, NULL, NULL, NULL},
    {NULL}
};
static PyType_Slot Sampler_slots[] = {
    {Py_tp_doc, "Keyframe sampler (times + values + interpolation)."},
    {Py_tp_dealloc, Sampler_dealloc},
    {Py_tp_getset, Sampler_getset},
    {Py_tp_repr, Sampler_repr},
    {0, NULL}
};
static PyType_Spec Sampler_spec = {
    "tinyusdz._core.AnimationSampler", sizeof(SamplerObject), 0,
    Py_TPFLAGS_DEFAULT, Sampler_slots
};

/* ---- RenderAnimation ---- */

CHILD_STR_GETTER(RenderAnimation_name,     c_tinyusd_animation_get_name)
CHILD_STR_GETTER(RenderAnimation_abs_path, c_tinyusd_animation_get_abs_path)

static PyObject *
RenderAnimation_duration(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyFloat_FromDouble((double)c_tinyusd_animation_duration(
        (const CTinyUSDAnimationClip *)c->handle));
}
static PyObject *
RenderAnimation_has_skeletal(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyBool_FromLong(c_tinyusd_animation_has_skeletal(
        (const CTinyUSDAnimationClip *)c->handle));
}
static PyObject *
RenderAnimation_has_node(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyBool_FromLong(c_tinyusd_animation_has_node(
        (const CTinyUSDAnimationClip *)c->handle));
}

static PyObject *
RenderAnimation_samplers(PyObject *self, PyObject *args) {
    (void)args;
    ChildObject *c = (ChildObject *)self;
    uint64_t n = c_tinyusd_animation_num_samplers(
        (const CTinyUSDAnimationClip *)c->handle);
    PyObject *list = PyList_New((Py_ssize_t)n);
    if (!list) return NULL;
    for (uint64_t i = 0; i < n; ++i) {
        PyObject *w = make_sampler(self, i);
        if (!w) { Py_DECREF(list); return NULL; }
        PyList_SetItem(list, (Py_ssize_t)i, w);
    }
    return list;
}

static const char *anim_path_str(int p) {
    switch (p) {
        case 0: return "translation";
        case 1: return "rotation";
        case 2: return "scale";
        case 3: return "weights";
        default: return "unknown";
    }
}

static PyObject *
RenderAnimation_channels(PyObject *self, PyObject *args) {
    (void)args;
    ChildObject *c = (ChildObject *)self;
    uint64_t n = c_tinyusd_animation_num_channels(
        (const CTinyUSDAnimationClip *)c->handle);
    PyObject *list = PyList_New((Py_ssize_t)n);
    if (!list) return NULL;
    for (uint64_t i = 0; i < n; ++i) {
        int path = 0, ttype = 0;
        int32_t tnode = -1, sk = -1, jt = -1, smp = -1;
        c_tinyusd_animation_get_channel(
            (const CTinyUSDAnimationClip *)c->handle, i,
            &path, &ttype, &tnode, &sk, &jt, &smp);
        PyObject *d = Py_BuildValue(
            "{s:s, s:s, s:i, s:i, s:i, s:i}",
            "path", anim_path_str(path),
            "target_type", (ttype == 1) ? "skeleton_joint" : "scene_node",
            "target_node", (int)tnode,
            "skeleton_id", (int)sk,
            "joint_id",    (int)jt,
            "sampler",     (int)smp);
        if (!d) { Py_DECREF(list); return NULL; }
        PyList_SetItem(list, (Py_ssize_t)i, d);
    }
    return list;
}

static PyObject *
RenderAnimation_repr(PyObject *self) {
    return child_str_getter(self,
        (int (*)(const void *, c_tinyusd_string_t *))c_tinyusd_animation_get_abs_path);
}

static PyGetSetDef RenderAnimation_getset[] = {
    {"name",         RenderAnimation_name,         NULL, NULL, NULL},
    {"abs_path",     RenderAnimation_abs_path,     NULL, NULL, NULL},
    {"duration",     RenderAnimation_duration,     NULL, NULL, NULL},
    {"has_skeletal", RenderAnimation_has_skeletal, NULL, NULL, NULL},
    {"has_node",     RenderAnimation_has_node,     NULL, NULL, NULL},
    {NULL}
};
static PyMethodDef RenderAnimation_methods[] = {
    {"samplers", RenderAnimation_samplers, METH_NOARGS, "list[AnimationSampler]"},
    {"channels", RenderAnimation_channels, METH_NOARGS,
     "list[dict] — each has path/target_type/target_node/skeleton_id/joint_id/sampler"},
    {NULL, NULL, 0, NULL}
};
static PyType_Slot RenderAnimation_slots[] = {
    {Py_tp_doc, "Animation clip (glTF-like)."},
    {Py_tp_dealloc, Child_dealloc},
    {Py_tp_getset, RenderAnimation_getset},
    {Py_tp_methods, RenderAnimation_methods},
    {Py_tp_repr, RenderAnimation_repr},
    {0, NULL}
};
static PyType_Spec RenderAnimation_spec = {
    "tinyusdz._core.RenderAnimation", sizeof(ChildObject), 0,
    Py_TPFLAGS_DEFAULT, RenderAnimation_slots
};

static PyObject *
make_render_animation(const CTinyUSDAnimationClip *a, PyObject *owner) {
    return make_child(RenderAnimationType, (const void *)a, owner);
}

/* ---- RenderSkeleton ---- */

CHILD_STR_GETTER(RenderSkeleton_name,     c_tinyusd_skel_get_name)
CHILD_STR_GETTER(RenderSkeleton_abs_path, c_tinyusd_skel_get_abs_path)

static PyObject *
RenderSkeleton_num_joints(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyLong_FromUnsignedLongLong(
        (unsigned long long)c_tinyusd_skel_num_joints(
            (const CTinyUSDSkelHierarchy *)c->handle));
}
static PyObject *
RenderSkeleton_default_anim_id(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyLong_FromLong((long)c_tinyusd_skel_default_anim_id(
        (const CTinyUSDSkelHierarchy *)c->handle));
}
static PyObject *
RenderSkeleton_parent_joint_indices(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    const void *ptr = NULL; uint64_t n = 0;
    c_tinyusd_skel_get_parent_joint_indices(
        (const CTinyUSDSkelHierarchy *)c->handle, &ptr, &n);
    return make_buffer_view(ptr, n, 1, 4, "i", c->owner);
}
static PyObject *
RenderSkeleton_bind_transforms(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    const void *ptr = NULL; uint64_t n = 0;
    c_tinyusd_skel_get_bind_transforms(
        (const CTinyUSDSkelHierarchy *)c->handle, &ptr, &n);
    /* matrix4d[] → N outer elements, 16 inner float64 components. */
    return make_buffer_view(ptr, n, 16, 8, "d", c->owner);
}
static PyObject *
RenderSkeleton_rest_transforms(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    const void *ptr = NULL; uint64_t n = 0;
    c_tinyusd_skel_get_rest_transforms(
        (const CTinyUSDSkelHierarchy *)c->handle, &ptr, &n);
    return make_buffer_view(ptr, n, 16, 8, "d", c->owner);
}
static PyObject *
RenderSkeleton_repr(PyObject *self) {
    return child_str_getter(self,
        (int (*)(const void *, c_tinyusd_string_t *))c_tinyusd_skel_get_abs_path);
}

static PyGetSetDef RenderSkeleton_getset[] = {
    {"name",                  RenderSkeleton_name,                  NULL, NULL, NULL},
    {"abs_path",              RenderSkeleton_abs_path,              NULL, NULL, NULL},
    {"num_joints",            RenderSkeleton_num_joints,            NULL, NULL, NULL},
    {"default_anim_id",       RenderSkeleton_default_anim_id,       NULL, NULL, NULL},
    {"parent_joint_indices",  RenderSkeleton_parent_joint_indices,  NULL,
     "BufferView int32[N].", NULL},
    {"bind_transforms",       RenderSkeleton_bind_transforms,       NULL,
     "BufferView [N,16] float64 — row-major matrix4d.", NULL},
    {"rest_transforms",       RenderSkeleton_rest_transforms,       NULL,
     "BufferView [N,16] float64 — row-major matrix4d.", NULL},
    {NULL}
};
static PyType_Slot RenderSkeleton_slots[] = {
    {Py_tp_doc, "Skeleton hierarchy: flat parent indices + bind/rest transforms."},
    {Py_tp_dealloc, Child_dealloc},
    {Py_tp_getset, RenderSkeleton_getset},
    {Py_tp_repr, RenderSkeleton_repr},
    {0, NULL}
};
static PyType_Spec RenderSkeleton_spec = {
    "tinyusdz._core.RenderSkeleton", sizeof(ChildObject), 0,
    Py_TPFLAGS_DEFAULT, RenderSkeleton_slots
};

static PyObject *
make_render_skeleton(const CTinyUSDSkelHierarchy *s, PyObject *owner) {
    return make_child(RenderSkeletonType, (const void *)s, owner);
}

/* ---- RenderNode ---- */

CHILD_STR_GETTER(RenderNode_name,         c_tinyusd_render_node_get_name)
CHILD_STR_GETTER(RenderNode_abs_path,     c_tinyusd_render_node_get_abs_path)
CHILD_STR_GETTER(RenderNode_display_name, c_tinyusd_render_node_get_display_name)

static const char *node_category_str(int cat) {
    switch (cat) {
        case 0: return "group";
        case 1: return "geom";
        case 2: return "light";
        case 3: return "camera";
        case 4: return "material";
        case 5: return "skeleton";
        default: return "unknown";
    }
}

static const char *node_type_str(int t) {
    switch (t) {
        case 0:  return "xform";
        case 1:  return "mesh";
        case 2:  return "camera";
        case 3:  return "skel_root";
        case 4:  return "skeleton";
        case 5:  return "point_light";
        case 6:  return "directional_light";
        case 7:  return "envmap_light";
        case 8:  return "rect_light";
        case 9:  return "disk_light";
        case 10: return "cylinder_light";
        case 11: return "geometry_light";
        default: return "unknown";
    }
}

static PyObject *
RenderNode_category(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyUnicode_FromString(node_category_str(
        c_tinyusd_render_node_category(
            (const CTinyUSDRenderNode *)c->handle)));
}

static PyObject *
RenderNode_node_type(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyUnicode_FromString(node_type_str(
        c_tinyusd_render_node_node_type(
            (const CTinyUSDRenderNode *)c->handle)));
}

static PyObject *
RenderNode_content_id(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyLong_FromLong((long)c_tinyusd_render_node_content_id(
        (const CTinyUSDRenderNode *)c->handle));
}

static PyObject *
RenderNode_is_instance(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyBool_FromLong(c_tinyusd_render_node_is_instance(
        (const CTinyUSDRenderNode *)c->handle));
}

static PyObject *
RenderNode_prototype_index(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyLong_FromLong((long)c_tinyusd_render_node_prototype_index(
        (const CTinyUSDRenderNode *)c->handle));
}

static PyObject *
RenderNode_instance_id(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyLong_FromLong((long)c_tinyusd_render_node_instance_id(
        (const CTinyUSDRenderNode *)c->handle));
}

static PyObject *
RenderNode_has_reset_xform(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    return PyBool_FromLong(c_tinyusd_render_node_has_reset_xform(
        (const CTinyUSDRenderNode *)c->handle));
}

static PyObject *
RenderNode_local_matrix(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    double m[16];
    c_tinyusd_render_node_get_local_matrix(
        (const CTinyUSDRenderNode *)c->handle, m);
    return Py_BuildValue("(dddd)(dddd)(dddd)(dddd)",
        m[0], m[1], m[2], m[3],
        m[4], m[5], m[6], m[7],
        m[8], m[9], m[10], m[11],
        m[12], m[13], m[14], m[15]);
}

static PyObject *
RenderNode_global_matrix(PyObject *self, void *closure) {
    (void)closure;
    ChildObject *c = (ChildObject *)self;
    double m[16];
    c_tinyusd_render_node_get_global_matrix(
        (const CTinyUSDRenderNode *)c->handle, m);
    return Py_BuildValue("(dddd)(dddd)(dddd)(dddd)",
        m[0], m[1], m[2], m[3],
        m[4], m[5], m[6], m[7],
        m[8], m[9], m[10], m[11],
        m[12], m[13], m[14], m[15]);
}

static PyObject *
RenderNode_children(PyObject *self, PyObject *args)
{
    (void)args;
    ChildObject *c = (ChildObject *)self;
    const CTinyUSDRenderNode *n = (const CTinyUSDRenderNode *)c->handle;
    uint64_t cn = c_tinyusd_render_node_num_children(n);
    PyObject *list = PyList_New((Py_ssize_t)cn);
    if (!list) return NULL;
    for (uint64_t i = 0; i < cn; ++i) {
        const CTinyUSDRenderNode *ch =
            c_tinyusd_render_node_get_child(n, i);
        /* Owner is this node's owning RenderScene — c->owner. */
        PyObject *w = make_render_node(ch, c->owner);
        if (!w) { Py_DECREF(list); return NULL; }
        PyList_SetItem(list, (Py_ssize_t)i, w);
    }
    return list;
}

static PyObject *
RenderNode_repr(PyObject *self) {
    return child_str_getter(self,
        (int (*)(const void *, c_tinyusd_string_t *))c_tinyusd_render_node_get_abs_path);
}

static PyGetSetDef RenderNode_getset[] = {
    {"name",             RenderNode_name,             NULL, NULL, NULL},
    {"abs_path",         RenderNode_abs_path,         NULL, NULL, NULL},
    {"display_name",     RenderNode_display_name,     NULL, NULL, NULL},
    {"category",         RenderNode_category,         NULL,
     "High-level category: 'group'|'geom'|'light'|'camera'|'material'|'skeleton'.", NULL},
    {"node_type",        RenderNode_node_type,        NULL,
     "Node type within the category (xform/mesh/camera/...).", NULL},
    {"content_id",       RenderNode_content_id,       NULL,
     "Index into RenderScene content for this node; -1 if none.", NULL},
    {"is_instance",      RenderNode_is_instance,      NULL, NULL, NULL},
    {"prototype_index",  RenderNode_prototype_index,  NULL, NULL, NULL},
    {"instance_id",      RenderNode_instance_id,      NULL, NULL, NULL},
    {"has_reset_xform",  RenderNode_has_reset_xform,  NULL, NULL, NULL},
    {"local_matrix",     RenderNode_local_matrix,     NULL,
     "Local 4x4 transform as nested double tuple.", NULL},
    {"global_matrix",    RenderNode_global_matrix,    NULL,
     "Global 4x4 transform as nested double tuple.", NULL},
    {NULL}
};

static PyMethodDef RenderNode_methods[] = {
    {"children", RenderNode_children, METH_NOARGS, "list[RenderNode]"},
    {NULL, NULL, 0, NULL}
};

static PyType_Slot RenderNode_slots[] = {
    {Py_tp_doc, "Render-scene graph node."},
    {Py_tp_dealloc, Child_dealloc},
    {Py_tp_getset, RenderNode_getset},
    {Py_tp_methods, RenderNode_methods},
    {Py_tp_repr, RenderNode_repr},
    {0, NULL}
};
static PyType_Spec RenderNode_spec = {
    "tinyusdz._core.RenderNode", sizeof(ChildObject), 0,
    Py_TPFLAGS_DEFAULT, RenderNode_slots
};

static PyObject *
make_render_node(const CTinyUSDRenderNode *n, PyObject *owner) {
    return make_child(RenderNodeType, (const void *)n, owner);
}

#undef CHILD_STR_GETTER
#undef IMAGE_INT_GETTER

/* ------------------------------------------------------------------------
 * tinyusdz._core.tydra — scene-access helpers.
 * -------------------------------------------------------------------- */

struct list_prims_ctx {
    PyObject *out;          /* list[(Prim, path_str, depth)] */
    PyObject *owner_stage;
    int py_err;
};

static int
list_prims_cb(const CTinyUSDPrim *prim, const CTinyUSDPath *path,
              uint32_t depth, void *ud)
{
    struct list_prims_ctx *c = (struct list_prims_ctx *)ud;
    if (c->py_err) return 0;

    PyObject *pobj = make_prim(prim, c->owner_stage);
    if (!pobj) { c->py_err = 1; return 0; }

    c_tinyusd_string_t *pstr = c_tinyusd_string_new_empty();
    const char *pc = "";
    if (pstr && c_tinyusd_path_to_string(path, pstr)) {
        pc = c_tinyusd_string_str(pstr);
    }
    PyObject *pathobj = PyUnicode_FromString(pc ? pc : "");
    if (pstr) c_tinyusd_string_free(pstr);
    if (!pathobj) { Py_DECREF(pobj); c->py_err = 1; return 0; }

    PyObject *tup = Py_BuildValue("(OOI)", pobj, pathobj, (unsigned int)depth);
    Py_DECREF(pobj);
    Py_DECREF(pathobj);
    if (!tup) { c->py_err = 1; return 0; }

    if (PyList_Append(c->out, tup) < 0) {
        Py_DECREF(tup);
        c->py_err = 1;
        return 0;
    }
    Py_DECREF(tup);
    return 1;
}

static PyObject *
tydra_list_prims_by_type(PyObject *self, PyObject *args)
{
    (void)self;
    PyObject *stage_obj;
    const char *type_name = NULL;
    if (!PyArg_ParseTuple(args, "O|s", &stage_obj, &type_name)) return NULL;
    if (!PyObject_IsInstance(stage_obj, StageType)) {
        PyErr_SetString(PyExc_TypeError, "first argument must be a Stage");
        return NULL;
    }
    StageObject *s = (StageObject *)stage_obj;

    PyObject *out = PyList_New(0);
    if (!out) return NULL;
    struct list_prims_ctx ctx = { out, stage_obj, 0 };

    c_tinyusd_string_t *err = c_tinyusd_string_new_empty();
    int ok = c_tinyusd_stage_list_prims_by_type(
        s->stage, type_name, list_prims_cb, &ctx, err);
    c_tinyusd_string_free(err);
    if (ctx.py_err) { Py_DECREF(out); return NULL; }
    if (!ok) {
        Py_DECREF(out);
        PyErr_SetString(UsdError, "list_prims_by_type failed");
        return NULL;
    }
    return out;
}

static PyObject *
tydra_visit_prims(PyObject *self, PyObject *args)
{
    (void)self;
    PyObject *stage_obj;
    PyObject *cb;
    if (!PyArg_ParseTuple(args, "OO", &stage_obj, &cb)) return NULL;
    if (!PyObject_IsInstance(stage_obj, StageType)) {
        PyErr_SetString(PyExc_TypeError, "first argument must be a Stage");
        return NULL;
    }
    /* Delegate to Stage.visit_prims. */
    return PyObject_CallMethod(stage_obj, "visit_prims", "O", cb);
}

static PyObject *
tydra_convert_to_render_scene(PyObject *self, PyObject *args)
{
    (void)self;
    PyObject *stage_obj;
    if (!PyArg_ParseTuple(args, "O", &stage_obj)) return NULL;
    if (!PyObject_IsInstance(stage_obj, StageType)) {
        PyErr_SetString(PyExc_TypeError, "first argument must be a Stage");
        return NULL;
    }
    StageObject *s = (StageObject *)stage_obj;

    c_tinyusd_string_t *warn = c_tinyusd_string_new_empty();
    c_tinyusd_string_t *err = c_tinyusd_string_new_empty();
    CTinyUSDRenderScene *scene =
        c_tinyusd_render_scene_convert(s->stage, warn, err);
    if (!scene) {
        const char *msg = c_tinyusd_string_str(err);
        PyErr_SetString(UsdError, msg && *msg ? msg :
                        "render-scene conversion failed");
        c_tinyusd_string_free(warn);
        c_tinyusd_string_free(err);
        return NULL;
    }
    c_tinyusd_string_free(warn);
    c_tinyusd_string_free(err);

    PyTypeObject *tp = (PyTypeObject *)RenderSceneType;
    allocfunc alloc = (allocfunc)PyType_GetSlot(tp, Py_tp_alloc);
    RenderSceneObject *obj = (RenderSceneObject *)alloc(tp, 0);
    if (!obj) {
        c_tinyusd_render_scene_free(scene);
        return NULL;
    }
    obj->scene = scene;
    return (PyObject *)obj;
}

static PyMethodDef tydra_methods[] = {
    {"list_prims_by_type", tydra_list_prims_by_type, METH_VARARGS,
     "list_prims_by_type(stage, type_name=None) -> list[(Prim, path, depth)]"},
    {"visit_prims", tydra_visit_prims, METH_VARARGS,
     "visit_prims(stage, callback) — DFS traversal."},
    {"convert_to_render_scene", tydra_convert_to_render_scene, METH_VARARGS,
     "convert_to_render_scene(stage) -> RenderScene"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef tydra_moduledef = {
    PyModuleDef_HEAD_INIT,
    "tinyusdz._core.tydra",
    "TinyUSDZ Tydra helpers (scene access, render-scene conversion).",
    -1,
    tydra_methods,
    NULL, NULL, NULL, NULL
};

/* ------------------------------------------------------------------------
 * Module init.
 * -------------------------------------------------------------------- */

static int
add_type(PyObject *module, const char *name, PyType_Spec *spec,
         PyObject **out)
{
    PyObject *tp = PyType_FromSpec(spec);
    if (!tp) return -1;
    if (PyModule_AddObject(module, name, Py_NewRef(tp)) < 0) {
        Py_DECREF(tp);
        return -1;
    }
    *out = tp;  /* module owns a ref; we also keep one */
    return 0;
}

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "tinyusdz._core",
    "TinyUSDZ CPython extension (stable ABI)",
    -1,
    module_methods,
    NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC
PyInit__core(void)
{
    PyObject *m = PyModule_Create(&moduledef);
    if (!m) return NULL;

    UsdError = PyErr_NewException("tinyusdz._core.UsdError", NULL, NULL);
    if (!UsdError) { Py_DECREF(m); return NULL; }
    if (PyModule_AddObject(m, "UsdError", Py_NewRef(UsdError)) < 0) goto fail;

    UsdParseError = PyErr_NewException("tinyusdz._core.UsdParseError",
                                       UsdError, NULL);
    if (!UsdParseError) goto fail;
    if (PyModule_AddObject(m, "UsdParseError", Py_NewRef(UsdParseError)) < 0) goto fail;

    UsdIoError = PyErr_NewException("tinyusdz._core.UsdIoError", UsdError, NULL);
    if (!UsdIoError) goto fail;
    if (PyModule_AddObject(m, "UsdIoError", Py_NewRef(UsdIoError)) < 0) goto fail;

    if (add_type(m, "Stage", &Stage_spec, &StageType) < 0) goto fail;
    if (add_type(m, "Prim", &Prim_spec, &PrimType) < 0) goto fail;
    if (add_type(m, "Attribute", &Attribute_spec, &AttributeType) < 0) goto fail;
    if (add_type(m, "Value", &Value_spec, &ValueType) < 0) goto fail;
    if (add_type(m, "BufferView", &BufferView_spec, &BufferViewType) < 0) goto fail;
    if (add_type(m, "RenderScene", &RenderScene_spec, &RenderSceneType) < 0) goto fail;
    if (add_type(m, "RenderMesh", &RenderMesh_spec, &RenderMeshType) < 0) goto fail;
    if (add_type(m, "RenderMaterial", &RenderMaterial_spec, &RenderMaterialType) < 0) goto fail;
    if (add_type(m, "RenderCamera", &RenderCamera_spec, &RenderCameraType) < 0) goto fail;
    if (add_type(m, "RenderLight", &RenderLight_spec, &RenderLightType) < 0) goto fail;
    if (add_type(m, "RenderTexture", &RenderTexture_spec, &RenderTextureType) < 0) goto fail;
    if (add_type(m, "RenderImage", &RenderImage_spec, &RenderImageType) < 0) goto fail;
    if (add_type(m, "RenderBuffer", &RenderBuffer_spec, &RenderBufferType) < 0) goto fail;
    if (add_type(m, "RenderAnimation", &RenderAnimation_spec, &RenderAnimationType) < 0) goto fail;
    if (add_type(m, "RenderSkeleton", &RenderSkeleton_spec, &RenderSkeletonType) < 0) goto fail;
    if (add_type(m, "AnimationSampler", &Sampler_spec, &AnimationSamplerType) < 0) goto fail;
    if (add_type(m, "RenderNode", &RenderNode_spec, &RenderNodeType) < 0) goto fail;

    /* tydra submodule. */
    PyObject *tydra = PyModule_Create(&tydra_moduledef);
    if (!tydra) goto fail;
    if (PyModule_AddObject(m, "tydra", Py_NewRef(tydra)) < 0) {
        Py_DECREF(tydra);
        goto fail;
    }
    Py_DECREF(tydra);

    return m;

fail:
    Py_DECREF(m);
    return NULL;
}
