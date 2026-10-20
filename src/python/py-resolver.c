/* SPDX-License-Identifier: Apache-2.0
 * lightusd._core.AssetResolver — resolver contexts, scoped caches, package
 * resolver callbacks, and path resolution.
 */

#include "py-internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#define LIGHTUSD_PY_THREAD_LOCAL __declspec(thread)
#else
#define LIGHTUSD_PY_THREAD_LOCAL _Thread_local
#endif

typedef struct {
  PyObject* type;
  PyObject* value;
  PyObject* tb;
} PendingPythonError;

static LIGHTUSD_PY_THREAD_LOCAL PendingPythonError g_package_error;

static void clear_package_error(void) {
  Py_CLEAR(g_package_error.type);
  Py_CLEAR(g_package_error.value);
  Py_CLEAR(g_package_error.tb);
}

static void capture_package_error(void) {
  PyObject *type = NULL, *value = NULL, *tb = NULL;
  PyErr_Fetch(&type, &value, &tb);
  PyErr_NormalizeException(&type, &value, &tb);
  clear_package_error();
  g_package_error.type = type;
  g_package_error.value = value;
  g_package_error.tb = tb;
}

static int restore_package_error(void) {
  if (!g_package_error.type) return 0;
  PyErr_Restore(g_package_error.type, g_package_error.value,
                g_package_error.tb);
  g_package_error.type = NULL;
  g_package_error.value = NULL;
  g_package_error.tb = NULL;
  return 1;
}

typedef struct {
  PyObject_HEAD
  lightusd_asset_resolver* resolver;
  PyObject* package_callbacks; /* dict[str, callable] */
  PyObject* scheme_callbacks;  /* dict[str, (resolver, reader)] */
} LightUSDResolver;

typedef struct {
  PyObject_HEAD
  PyObject* owner; /* LightUSDResolver */
  lightusd_resolver_context* context;
  void* binding;
  int bound;
} LightUSDResolverBinding;

typedef struct {
  PyObject_HEAD
  PyObject* owner; /* LightUSDResolver */
  lightusd_resolver_cache_scope* scope;
} LightUSDResolverCache;

/* ============================================================
 * Helpers
 * ============================================================ */

static LightUSDResolver* resolver_self(PyObject* self) {
  return (LightUSDResolver*)self;
}

lightusd_asset_resolver* lightusd_resolver_handle(lightusd_state* st,
                                                  PyObject* obj) {
  if (!obj || obj == Py_None) return NULL;
  if (!st || !st->ResolverType) {
    PyErr_SetString(PyExc_TypeError, "resolver type is unavailable");
    return NULL;
  }
  const int is_instance = PyObject_IsInstance(obj, st->ResolverType);
  if (is_instance < 0) return NULL;
  if (!is_instance) {
    PyErr_SetString(PyExc_TypeError, "resolver must be an AssetResolver");
    return NULL;
  }
  return ((LightUSDResolver*)obj)->resolver;
}

static void resolver_type_free(PyObject* self) {
  freefunc free_fn = (freefunc)PyType_GetSlot(Py_TYPE(self), Py_tp_free);
  free_fn(self);
}

static int normalize_extension(const char* extension, char* out, size_t cap) {
  if (!extension || !extension[0] || !out || cap == 0) return 0;
  if (*extension == '.') ++extension;
  const size_t n = strlen(extension);
  if (n == 0 || n >= cap) return 0;
  for (size_t i = 0; i < n; ++i) {
    out[i] = (char)tolower((unsigned char)extension[i]);
  }
  out[n] = '\0';
  return 1;
}

/* C API copies the string immediately after the callback returns; keep the
 * Python result alive long enough by copying into thread-local storage. */
static _Thread_local char g_package_result[4096];
static _Thread_local char g_scheme_result[4096];

static int normalize_scheme_name(const char* scheme, char* out, size_t cap) {
  if (!scheme || !out || cap == 0) return 0;
  const size_t full = strlen(scheme);
  size_t n = full;
  if (n && scheme[n - 1] == ':') --n;
  if (n == 0 || n >= cap) return 0;
  for (size_t i = 0; i < n; ++i) {
    out[i] = (char)tolower((unsigned char)scheme[i]);
  }
  out[n] = '\0';
  return 1;
}

static int py_scheme_resolver_callback(void* userdata, const char* identifier,
                                       const char* anchor_path,
                                       const char* scheme_context,
                                       const char** resolved) {
  PyObject* callbacks = (PyObject*)userdata;
  if (!callbacks || !PyTuple_Check(callbacks) ||
      PyTuple_Size(callbacks) < 2 || !resolved) {
    return 0;
  }
  PyObject* callable = PyTuple_GetItem(callbacks, 0);
  if (!callable || callable == Py_None) return 0;
  PyObject* result = PyObject_CallFunction(
      callable, "sss", identifier, anchor_path ? anchor_path : "",
      scheme_context ? scheme_context : "");
  if (!result) {
    capture_package_error();
    return 0;
  }
  if (!PyUnicode_Check(result)) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_TypeError,
                    "scheme resolver callback must return str");
    capture_package_error();
    return 0;
  }
  PyObject* utf8 = NULL;
  const char* text = lightusd_utf8(result, &utf8);
  if (!text) {
    Py_XDECREF(utf8);
    Py_DECREF(result);
    capture_package_error();
    return 0;
  }
  const size_t n = strlen(text);
  if (n == 0 || n >= sizeof(g_scheme_result)) {
    Py_XDECREF(utf8);
    Py_DECREF(result);
    return 0;
  }
  memcpy(g_scheme_result, text, n + 1);
  Py_XDECREF(utf8);
  Py_DECREF(result);
  *resolved = g_scheme_result;
  return 1;
}

static int py_scheme_reader_callback(void* userdata, const char* identifier,
                                     uint8_t** data, size_t* size) {
  PyObject* callbacks = (PyObject*)userdata;
  if (!callbacks || !PyTuple_Check(callbacks) ||
      PyTuple_Size(callbacks) < 2 || !data || !size) {
    return 0;
  }
  PyObject* callable = PyTuple_GetItem(callbacks, 1);
  if (!callable || callable == Py_None) return 0;
  *data = NULL;
  *size = 0;
  PyObject* result = PyObject_CallFunction(callable, "s", identifier);
  if (!result) {
    capture_package_error();
    return 0;
  }
  PyObject* bytes = PyBytes_FromObject(result);
  Py_DECREF(result);
  if (!bytes) {
    capture_package_error();
    return 0;
  }
  char* raw = NULL;
  Py_ssize_t n = 0;
  if (PyBytes_AsStringAndSize(bytes, &raw, &n) != 0) {
    Py_DECREF(bytes);
    capture_package_error();
    return 0;
  }
  if (n < 0) {
    Py_DECREF(bytes);
    PyErr_SetString(PyExc_ValueError, "invalid scheme reader payload");
    capture_package_error();
    return 0;
  }
  if (n > 0) {
    uint8_t* buffer = (uint8_t*)malloc((size_t)n);
    if (!buffer) {
      Py_DECREF(bytes);
      PyErr_NoMemory();
      capture_package_error();
      return 0;
    }
    memcpy(buffer, raw, (size_t)n);
    *data = buffer;
    *size = (size_t)n;
  }
  Py_DECREF(bytes);
  return 1;
}

static int py_package_callback(void* userdata, const char* package_path,
                               const char* asset_path, const char** resolved) {
  PyObject* callable = (PyObject*)userdata;
  if (!callable || !resolved) return 0;
  PyObject* result =
      PyObject_CallFunction(callable, "ss", package_path, asset_path);
  if (!result) {
    capture_package_error();
    return 0;
  }
  if (result == Py_None) {
    Py_DECREF(result);
    return 0;
  }
  if (!PyUnicode_Check(result)) {
    Py_DECREF(result);
    PyErr_SetString(PyExc_TypeError,
                    "package resolver callback must return str or None");
    capture_package_error();
    return 0;
  }
  PyObject* utf8 = NULL;
  const char* text = lightusd_utf8(result, &utf8);
  if (!text) {
    Py_XDECREF(utf8);
    Py_DECREF(result);
    capture_package_error();
    return 0;
  }
  const size_t n = strlen(text);
  if (n == 0 || n >= sizeof(g_package_result)) {
    Py_XDECREF(utf8);
    Py_DECREF(result);
    return 0;
  }
  memcpy(g_package_result, text, n);
  g_package_result[n] = '\0';
  Py_XDECREF(utf8);
  Py_DECREF(result);
  *resolved = g_package_result;
  return 1;
}

/* ============================================================
 * Resolver
 * ============================================================ */

static void Resolver_dealloc(PyObject* self) {
  LightUSDResolver* r = resolver_self(self);
  lightusd_asset_resolver_destroy(r->resolver);
  Py_XDECREF(r->package_callbacks);
  Py_XDECREF(r->scheme_callbacks);
  resolver_type_free(self);
}

static PyObject* Resolver_repr(PyObject* self) {
  (void)self;
  return PyUnicode_FromString("AssetResolver()");
}

static int Resolver_init(PyObject* self, PyObject* args, PyObject* kwds) {
  LightUSDResolver* r = resolver_self(self);
  (void)kwds;
  if (!PyArg_ParseTuple(args, ":AssetResolver")) return -1;
  if (r->resolver) return 0;
  r->package_callbacks = PyDict_New();
  if (!r->package_callbacks) return -1;
  r->scheme_callbacks = PyDict_New();
  if (!r->scheme_callbacks) {
    Py_CLEAR(r->package_callbacks);
    return -1;
  }
  const lightusd_status status = lightusd_asset_resolver_create(&r->resolver);
  if (status != LIGHTUSD_OK) {
    Py_CLEAR(r->package_callbacks);
    Py_CLEAR(r->scheme_callbacks);
    lightusd_raise(lightusd_state_from_obj(self), status, "create resolver");
    return -1;
  }
  return 0;
}

static PyObject* Resolver_resolve_impl(PyObject* self, PyObject* args,
                                       int for_new) {
  LightUSDResolver* r = resolver_self(self);
  const char* asset_path = NULL;
  const char* anchor_path = NULL;
  if (!PyArg_ParseTuple(args, "s|z", &asset_path, &anchor_path)) return NULL;
  lightusd_string* resolved = NULL;
  uint8_t exists = 0, is_package = 0;
  clear_package_error();
  const lightusd_status status =
      for_new
          ? lightusd_asset_resolver_resolve_for_new_asset(
                r->resolver, asset_path, anchor_path, &resolved, &exists,
                &is_package)
          : lightusd_asset_resolver_resolve(r->resolver, asset_path,
                                            anchor_path, &resolved, &exists,
                                            &is_package);
  if (restore_package_error()) {
    lightusd_string_destroy(resolved);
    return NULL;
  }
  if (status != LIGHTUSD_OK) {
    return lightusd_raise(lightusd_state_from_obj(self), status,
                          "resolve asset");
  }
  const lightusd_sv view = lightusd_string_view(resolved);
  PyObject* text = lightusd_sv_to_str(view);
  lightusd_string_destroy(resolved);
  if (!text) return NULL;
  return Py_BuildValue("(Nii)", text, exists ? 1 : 0, is_package ? 1 : 0);
}

static PyObject* Resolver_resolve(PyObject* self, PyObject* args) {
  return Resolver_resolve_impl(self, args, 0);
}

static PyObject* Resolver_resolve_new(PyObject* self, PyObject* args) {
  return Resolver_resolve_impl(self, args, 1);
}

static PyObject* Resolver_is_context_dependent(PyObject* self, PyObject* args) {
  LightUSDResolver* r = resolver_self(self);
  const char* path = NULL;
  if (!PyArg_ParseTuple(args, "s", &path)) return NULL;
  return PyBool_FromLong(
      lightusd_asset_resolver_is_context_dependent(r->resolver, path));
}

static PyObject* Resolver_get_current_context(PyObject* self, void*) {
  LightUSDResolver* r = resolver_self(self);
  lightusd_resolver_context* context = NULL;
  const lightusd_status status =
      lightusd_asset_resolver_get_context(r->resolver, &context);
  if (status != LIGHTUSD_OK) {
    return lightusd_raise(lightusd_state_from_obj(self), status,
                          "get resolver context");
  }
  PyObject* text =
      lightusd_sv_to_str(lightusd_resolver_context_string(context));
  lightusd_resolver_context_destroy(context);
  return text;
}

static PyObject* Resolver_bind_context(PyObject* self, PyObject* args,
                                       PyObject* kwargs) {
  lightusd_state* st = lightusd_state_from_obj(self);
  LightUSDResolver* r = resolver_self(self);
  static char* kwlist[] = {"context_string", "scheme_contexts", NULL};
  const char* context_string = NULL;
  PyObject* scheme_contexts = Py_None;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|zO:bind_context", kwlist,
                                   &context_string, &scheme_contexts)) {
    return NULL;
  }
  if (scheme_contexts != Py_None && !PyDict_Check(scheme_contexts)) {
    PyErr_SetString(PyExc_TypeError,
                    "scheme_contexts must be a dict of str: str");
    return NULL;
  }
  lightusd_resolver_context* context = NULL;
  const lightusd_status status = lightusd_asset_resolver_create_context(
      r->resolver, context_string ? context_string : "", &context);
  if (status != LIGHTUSD_OK) {
    return lightusd_raise(st, status, "create resolver context");
  }
  if (scheme_contexts != Py_None) {
    PyObject *key = NULL, *value = NULL;
    Py_ssize_t pos = 0;
    while (PyDict_Next(scheme_contexts, &pos, &key, &value)) {
      if (!PyUnicode_Check(key) || !PyUnicode_Check(value)) {
        PyErr_SetString(PyExc_TypeError,
                        "scheme_contexts must be a dict of str: str");
        lightusd_resolver_context_destroy(context);
        return NULL;
      }
      PyObject* key_utf8 = NULL;
      PyObject* value_utf8 = NULL;
      const char* scheme_text = lightusd_utf8(key, &key_utf8);
      const char* context_text = scheme_text ? lightusd_utf8(value, &value_utf8)
                                             : NULL;
      if (!scheme_text || !context_text) {
        Py_XDECREF(key_utf8);
        Py_XDECREF(value_utf8);
        lightusd_resolver_context_destroy(context);
        return NULL;
      }
      const lightusd_status scheme_status =
          lightusd_resolver_context_set_scheme(context, scheme_text,
                                               context_text);
      Py_XDECREF(key_utf8);
      Py_XDECREF(value_utf8);
      if (scheme_status != LIGHTUSD_OK) {
        lightusd_resolver_context_destroy(context);
        return lightusd_raise(st, scheme_status, "set scheme context");
      }
    }
  }
  PyObject* obj = lightusd_alloc(st->ResolverBindingType);
  if (!obj) {
    lightusd_resolver_context_destroy(context);
    return NULL;
  }
  LightUSDResolverBinding* binding = (LightUSDResolverBinding*)obj;
  Py_INCREF(self);
  binding->owner = self;
  binding->context = context;
  binding->binding = NULL;
  binding->bound = 0;
  return obj;
}

static PyObject* Resolver_cache_scope(PyObject* self, PyObject*) {
  lightusd_state* st = lightusd_state_from_obj(self);
  PyObject* obj = lightusd_alloc(st->ResolverCacheScopeType);
  if (!obj) return NULL;
  LightUSDResolverCache* cache = (LightUSDResolverCache*)obj;
  Py_INCREF(self);
  cache->owner = self;
  cache->scope = NULL;
  return obj;
}

static PyObject* Resolver_register_memory(PyObject* self, PyObject* args) {
  LightUSDResolver* r = resolver_self(self);
  const char* identifier = NULL;
  PyObject* data_obj = NULL;
  if (!PyArg_ParseTuple(args, "sO:register_memory", &identifier, &data_obj)) {
    return NULL;
  }
  PyObject* bytes = PyBytes_FromObject(data_obj);
  if (!bytes) return NULL;
  char* data = NULL;
  Py_ssize_t size = 0;
  if (PyBytes_AsStringAndSize(bytes, &data, &size) != 0) {
    Py_DECREF(bytes);
    return NULL;
  }
  const lightusd_status status = lightusd_asset_resolver_register_memory(
      r->resolver, identifier, (const uint8_t*)data, (size_t)size);
  Py_DECREF(bytes);
  if (status != LIGHTUSD_OK) {
    return lightusd_raise(lightusd_state_from_obj(self), status,
                          "register memory asset");
  }
  Py_RETURN_NONE;
}

static PyObject* Resolver_unregister_memory(PyObject* self, PyObject* args) {
  LightUSDResolver* r = resolver_self(self);
  const char* identifier = NULL;
  if (!PyArg_ParseTuple(args, "s:unregister_memory", &identifier)) return NULL;
  const lightusd_status status =
      lightusd_asset_resolver_unregister_memory(r->resolver, identifier);
  if (status == LIGHTUSD_ERR_NOT_FOUND) return PyBool_FromLong(0);
  if (status != LIGHTUSD_OK) {
    return lightusd_raise(lightusd_state_from_obj(self), status,
                          "unregister memory asset");
  }
  return PyBool_FromLong(1);
}

static PyObject* Resolver_set_alias(PyObject* self, PyObject* args) {
  LightUSDResolver* r = resolver_self(self);
  const char* authored = NULL;
  const char* resolved = NULL;
  if (!PyArg_ParseTuple(args, "ss:set_alias", &authored, &resolved)) {
    return NULL;
  }
  const lightusd_status status =
      lightusd_asset_resolver_set_alias(r->resolver, authored, resolved);
  if (status != LIGHTUSD_OK) {
    return lightusd_raise(lightusd_state_from_obj(self), status,
                          "set asset alias");
  }
  Py_RETURN_NONE;
}

static PyObject* Resolver_read(PyObject* self, PyObject* args) {
  LightUSDResolver* r = resolver_self(self);
  const char* identifier = NULL;
  if (!PyArg_ParseTuple(args, "s:read", &identifier)) return NULL;
  lightusd_string* text = NULL;
  clear_package_error();
  const lightusd_status status =
      lightusd_asset_resolver_read(r->resolver, identifier, &text);
  if (restore_package_error()) {
    lightusd_string_destroy(text);
    return NULL;
  }
  if (status != LIGHTUSD_OK) {
    return lightusd_raise(lightusd_state_from_obj(self), status,
                          "read asset");
  }
  const lightusd_sv view = lightusd_string_view(text);
  PyObject* bytes = PyBytes_FromStringAndSize(view.data ? view.data : "",
                                              (Py_ssize_t)view.len);
  lightusd_string_destroy(text);
  return bytes;
}

static PyObject* Resolver_dependency_report(PyObject* self, PyObject* args) {
  LightUSDResolver* r = resolver_self(self);
  const char* root = NULL;
  if (!PyArg_ParseTuple(args, "s:dependency_report", &root)) return NULL;
  lightusd_string* text = NULL;
  uint8_t complete = 0;
  const lightusd_status status =
      lightusd_dependency_report_json(r->resolver, root, &text, &complete);
  if (status != LIGHTUSD_OK) {
    return lightusd_raise(lightusd_state_from_obj(self), status,
                          "dependency report");
  }
  const lightusd_sv view = lightusd_string_view(text);
  PyObject* report = lightusd_sv_to_str(view);
  lightusd_string_destroy(text);
  if (!report) return NULL;
  return Py_BuildValue("(Ni)", report, complete ? 1 : 0);
}

static PyObject* Resolver_get_memory_stats(PyObject* self, void*) {
  LightUSDResolver* r = resolver_self(self);
  size_t count = 0, bytes = 0, limit = 0;
  const lightusd_status status = lightusd_asset_resolver_get_memory_stats(
      r->resolver, &count, &bytes, &limit);
  if (status != LIGHTUSD_OK) {
    return lightusd_raise(lightusd_state_from_obj(self), status,
                          "resolver memory stats");
  }
  return Py_BuildValue("(KKK)", (unsigned long long)count,
                       (unsigned long long)bytes,
                       (unsigned long long)limit);
}

static PyObject* Resolver_register_scheme(PyObject* self, PyObject* args,
                                         PyObject* kwargs) {
  LightUSDResolver* r = resolver_self(self);
  static char* kwlist[] = {"scheme", "resolver", "reader", NULL};
  const char* scheme = NULL;
  PyObject* resolver_obj = Py_None;
  PyObject* reader_obj = Py_None;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sO|O:register_scheme",
                                   kwlist, &scheme, &resolver_obj,
                                   &reader_obj)) {
    return NULL;
  }
  if (resolver_obj != Py_None && !PyCallable_Check(resolver_obj)) {
    PyErr_SetString(PyExc_TypeError, "resolver must be callable");
    return NULL;
  }
  if (reader_obj != Py_None && !PyCallable_Check(reader_obj)) {
    PyErr_SetString(PyExc_TypeError, "reader must be callable");
    return NULL;
  }
  if (resolver_obj == Py_None && reader_obj == Py_None) {
    PyErr_SetString(PyExc_TypeError,
                    "register_scheme needs a resolver or reader callback");
    return NULL;
  }
  char normalized[128];
  if (!normalize_scheme_name(scheme, normalized, sizeof(normalized))) {
    PyErr_SetString(PyExc_ValueError, "invalid scheme");
    return NULL;
  }
  PyObject* callbacks = PyTuple_Pack(2, resolver_obj, reader_obj);
  if (!callbacks) return NULL;
  const lightusd_status status = lightusd_asset_resolver_register_scheme(
      r->resolver, scheme,
      resolver_obj == Py_None ? NULL : py_scheme_resolver_callback,
      reader_obj == Py_None ? NULL : py_scheme_reader_callback,
      (void*)callbacks);
  if (status != LIGHTUSD_OK) {
    Py_DECREF(callbacks);
    return lightusd_raise(lightusd_state_from_obj(self), status,
                          "register scheme");
  }
  if (PyDict_SetItemString(r->scheme_callbacks, normalized, callbacks) < 0) {
    lightusd_asset_resolver_unregister_scheme(r->resolver, normalized);
    Py_DECREF(callbacks);
    return NULL;
  }
  Py_DECREF(callbacks);
  Py_RETURN_NONE;
}

static PyObject* Resolver_unregister_scheme(PyObject* self, PyObject* args) {
  LightUSDResolver* r = resolver_self(self);
  const char* scheme = NULL;
  if (!PyArg_ParseTuple(args, "s:unregister_scheme", &scheme)) return NULL;
  char normalized[128];
  if (!normalize_scheme_name(scheme, normalized, sizeof(normalized))) {
    PyErr_SetString(PyExc_ValueError, "invalid scheme");
    return NULL;
  }
  const lightusd_status status =
      lightusd_asset_resolver_unregister_scheme(r->resolver, normalized);
  if (status == LIGHTUSD_ERR_NOT_FOUND) return PyBool_FromLong(0);
  if (status != LIGHTUSD_OK) {
    return lightusd_raise(lightusd_state_from_obj(self), status,
                          "unregister scheme");
  }
  if (PyDict_DelItemString(r->scheme_callbacks, normalized) < 0) {
    return NULL;
  }
  return PyBool_FromLong(1);
}

static PyObject* Resolver_register_package(PyObject* self, PyObject* args) {
  LightUSDResolver* r = resolver_self(self);
  const char* extension = NULL;
  PyObject* callback = NULL;
  if (!PyArg_ParseTuple(args, "sO:register_package", &extension, &callback)) {
    return NULL;
  }
  if (!PyCallable_Check(callback)) {
    PyErr_SetString(PyExc_TypeError, "callback must be callable");
    return NULL;
  }
  char normalized[128];
  if (!normalize_extension(extension, normalized, sizeof(normalized))) {
    PyErr_SetString(PyExc_ValueError, "invalid package extension");
    return NULL;
  }
  if (PyDict_SetItemString(r->package_callbacks, normalized, callback) < 0) {
    return NULL;
  }
  const lightusd_status status = lightusd_asset_resolver_register_package(
      r->resolver, normalized, py_package_callback, (void*)callback);
  if (status != LIGHTUSD_OK) {
    PyDict_DelItemString(r->package_callbacks, normalized);
    PyErr_Clear();
    return lightusd_raise(lightusd_state_from_obj(self), status,
                          "register package resolver");
  }
  Py_RETURN_NONE;
}

static PyObject* Resolver_unregister_package(PyObject* self, PyObject* args) {
  LightUSDResolver* r = resolver_self(self);
  const char* extension = NULL;
  if (!PyArg_ParseTuple(args, "s:unregister_package", &extension)) return NULL;
  char normalized[128];
  if (!normalize_extension(extension, normalized, sizeof(normalized))) {
    PyErr_SetString(PyExc_ValueError, "invalid package extension");
    return NULL;
  }
  if (PyDict_DelItemString(r->package_callbacks, normalized) < 0) {
    PyErr_Clear();
  }
  const lightusd_status status =
      lightusd_asset_resolver_unregister_package(r->resolver, normalized);
  if (status == LIGHTUSD_ERR_NOT_FOUND) return PyBool_FromLong(0);
  if (status != LIGHTUSD_OK) {
    return lightusd_raise(lightusd_state_from_obj(self), status,
                          "unregister package resolver");
  }
  return PyBool_FromLong(1);
}

static PyMethodDef Resolver_methods[] = {
    {"resolve", Resolver_resolve, METH_VARARGS,
     "resolve(asset_path, anchor_path=None) -> (resolved, exists, is_package)"},
    {"resolve_new", Resolver_resolve_new, METH_VARARGS,
     "resolve_new(asset_path, anchor_path=None) -> (resolved, exists, is_package)"},
    {"is_context_dependent", Resolver_is_context_dependent, METH_VARARGS,
     "is_context_dependent(asset_path) -> bool"},
    {"bind_context", (PyCFunction)Resolver_bind_context,
     METH_VARARGS | METH_KEYWORDS,
     "bind_context(context_string=None, scheme_contexts=None) -> context manager\n"
     "Bindings are thread-local; enter and exit on the same thread."},
    {"cache_scope", Resolver_cache_scope, METH_NOARGS,
     "cache_scope() -> context manager"},
    {"register_memory", Resolver_register_memory, METH_VARARGS,
     "register_memory(identifier, data)"},
    {"unregister_memory", Resolver_unregister_memory, METH_VARARGS,
     "unregister_memory(identifier) -> bool"},
    {"set_alias", Resolver_set_alias, METH_VARARGS,
     "set_alias(authored_path, resolved_identifier)"},
    {"read", Resolver_read, METH_VARARGS,
     "read(resolved_identifier) -> bytes"},
    {"dependency_report", Resolver_dependency_report, METH_VARARGS,
     "dependency_report(root) -> (json, complete)"},
    {"register_scheme", (PyCFunction)Resolver_register_scheme,
     METH_VARARGS | METH_KEYWORDS,
     "register_scheme(scheme, resolver, reader=None)"},
    {"unregister_scheme", Resolver_unregister_scheme, METH_VARARGS,
     "unregister_scheme(scheme) -> bool"},
    {"register_package", Resolver_register_package, METH_VARARGS,
     "register_package(extension, callback)"},
    {"unregister_package", Resolver_unregister_package, METH_VARARGS,
     "unregister_package(extension) -> bool"},
    {NULL, NULL, 0, NULL},
};

static PyGetSetDef Resolver_getset[] = {
    {"current_context", Resolver_get_current_context, NULL,
     "Current resolver context string.", NULL},
    {"memory_stats", Resolver_get_memory_stats, NULL,
     "Memory asset stats (count, bytes, limit).", NULL},
    {NULL, NULL, NULL, NULL, NULL},
};

static PyType_Slot Resolver_slots[] = {
    {Py_tp_dealloc, (void*)Resolver_dealloc},
    {Py_tp_repr, (void*)Resolver_repr},
    {Py_tp_new, (void*)PyType_GenericNew},
    {Py_tp_init, (void*)Resolver_init},
    {Py_tp_methods, (void*)Resolver_methods},
    {Py_tp_getset, (void*)Resolver_getset},
    {0, NULL},
};

static PyType_Spec Resolver_spec = {
    .name = "lightusd._core.AssetResolver",
    .basicsize = sizeof(LightUSDResolver),
    .flags = Py_TPFLAGS_DEFAULT
#ifdef Py_TPFLAGS_IMMUTABLETYPE
             | Py_TPFLAGS_IMMUTABLETYPE
#endif
    ,
    .slots = Resolver_slots,
};

/* ============================================================
 * Context binding context manager
 * ============================================================ */

static void Binding_dealloc(PyObject* self) {
  LightUSDResolverBinding* b = (LightUSDResolverBinding*)self;
  if (b->bound && b->owner && b->context) {
    LightUSDResolver* r = (LightUSDResolver*)b->owner;
    void* data = b->binding;
    lightusd_asset_resolver_unbind_context(r->resolver, b->context, &data);
    b->bound = 0;
    b->binding = NULL;
  }
  lightusd_resolver_context_destroy(b->context);
  Py_XDECREF(b->owner);
  resolver_type_free(self);
}

static PyObject* Binding_enter(PyObject* self, PyObject*) {
  LightUSDResolverBinding* b = (LightUSDResolverBinding*)self;
  if (b->bound) {
    Py_INCREF(self);
    return self;
  }
  if (!b->owner || !b->context) {
    PyErr_SetString(PyExc_RuntimeError, "context binding is closed");
    return NULL;
  }
  LightUSDResolver* r = (LightUSDResolver*)b->owner;
  void* binding = NULL;
  const lightusd_status status = lightusd_asset_resolver_bind_context(
      r->resolver, b->context, &binding);
  if (status != LIGHTUSD_OK) {
    return lightusd_raise(lightusd_state_from_obj(b->owner), status,
                          "bind resolver context");
  }
  b->binding = binding;
  b->bound = 1;
  Py_INCREF(self);
  return self;
}

static PyObject* Binding_exit(PyObject* self, PyObject*) {
  LightUSDResolverBinding* b = (LightUSDResolverBinding*)self;
  if (b->bound && b->owner && b->context) {
    LightUSDResolver* r = (LightUSDResolver*)b->owner;
    void* data = b->binding;
    lightusd_asset_resolver_unbind_context(r->resolver, b->context, &data);
    b->bound = 0;
    b->binding = NULL;
  }
  Py_RETURN_FALSE;
}

static PyMethodDef Binding_methods[] = {
    {"__enter__", Binding_enter, METH_NOARGS, NULL},
    {"__exit__", Binding_exit, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL},
};

static PyType_Slot Binding_slots[] = {
    {Py_tp_dealloc, (void*)Binding_dealloc},
    {Py_tp_methods, (void*)Binding_methods},
    {0, NULL},
};

static PyType_Spec Binding_spec = {
    .name = "lightusd._core._ResolverBinding",
    .basicsize = sizeof(LightUSDResolverBinding),
    .flags = Py_TPFLAGS_DEFAULT
#ifdef Py_TPFLAGS_IMMUTABLETYPE
             | Py_TPFLAGS_IMMUTABLETYPE
#endif
    ,
    .slots = Binding_slots,
};

/* ============================================================
 * Scoped cache context manager
 * ============================================================ */

static void Cache_dealloc(PyObject* self) {
  LightUSDResolverCache* c = (LightUSDResolverCache*)self;
  if (c->scope) lightusd_resolver_cache_scope_destroy(c->scope);
  Py_XDECREF(c->owner);
  resolver_type_free(self);
}

static PyObject* Cache_enter(PyObject* self, PyObject*) {
  LightUSDResolverCache* c = (LightUSDResolverCache*)self;
  if (c->scope) {
    Py_INCREF(self);
    return self;
  }
  if (!c->owner) {
    PyErr_SetString(PyExc_RuntimeError, "cache scope is closed");
    return NULL;
  }
  LightUSDResolver* r = (LightUSDResolver*)c->owner;
  const lightusd_status status =
      lightusd_asset_resolver_create_cache_scope(r->resolver, &c->scope);
  if (status != LIGHTUSD_OK) {
    return lightusd_raise(lightusd_state_from_obj(c->owner), status,
                          "create resolver cache scope");
  }
  Py_INCREF(self);
  return self;
}

static PyObject* Cache_exit(PyObject* self, PyObject*) {
  LightUSDResolverCache* c = (LightUSDResolverCache*)self;
  if (c->scope) {
    lightusd_resolver_cache_scope_destroy(c->scope);
    c->scope = NULL;
  }
  Py_RETURN_FALSE;
}

static PyMethodDef Cache_methods[] = {
    {"__enter__", Cache_enter, METH_NOARGS, NULL},
    {"__exit__", Cache_exit, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL},
};

static PyType_Slot Cache_slots[] = {
    {Py_tp_dealloc, (void*)Cache_dealloc},
    {Py_tp_methods, (void*)Cache_methods},
    {0, NULL},
};

static PyType_Spec Cache_spec = {
    .name = "lightusd._core._ResolverCacheScope",
    .basicsize = sizeof(LightUSDResolverCache),
    .flags = Py_TPFLAGS_DEFAULT
#ifdef Py_TPFLAGS_IMMUTABLETYPE
             | Py_TPFLAGS_IMMUTABLETYPE
#endif
    ,
    .slots = Cache_slots,
};

/* ============================================================
 * Registration
 * ============================================================ */

int lightusd_register_resolver_types(PyObject* module, lightusd_state* st) {
  st->ResolverType = PyType_FromModuleAndSpec(module, &Resolver_spec, NULL);
  if (!st->ResolverType) return -1;
  if (PyModule_AddObjectRef(module, "AssetResolver", st->ResolverType) < 0) {
    return -1;
  }
  st->ResolverBindingType =
      PyType_FromModuleAndSpec(module, &Binding_spec, NULL);
  if (!st->ResolverBindingType) return -1;
  st->ResolverCacheScopeType =
      PyType_FromModuleAndSpec(module, &Cache_spec, NULL);
  if (!st->ResolverCacheScopeType) return -1;
  return 0;
}
