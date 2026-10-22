/* SPDX-License-Identifier: Apache-2.0 */
#include "lightusd-render-c.h"
#include "py-internal.h"

static int put(PyObject* dict, const char* key, PyObject* value) {
  if (!value) return -1;
  int result = PyDict_SetItemString(dict, key, value);
  Py_DECREF(value);
  return result;
}
static PyObject* vector3(const float* v) {
  return Py_BuildValue("(fff)", v[0], v[1], v[2]);
}

PyObject* lightusd_physics_annotations(lightusd_state* st,
                                       lightusd_render_scene* scene) {
  const char* names[] = {
      "scenes",    "rigid_bodies",   "colliders",         "joints",
      "materials", "filtered_pairs", "articulation_roots"};
  PyObject* result = PyDict_New();
  if (!result) return NULL;
  for (uint8_t kind = 0; kind < 7; kind++) {
    size_t count = lightusd_render_physics_count(scene, kind);
    if (count > (size_t)PY_SSIZE_T_MAX) {
      Py_DECREF(result);
      return PyErr_NoMemory();
    }
    PyObject* items = PyTuple_New((Py_ssize_t)count);
    if (!items) {
      Py_DECREF(result);
      return NULL;
    }
    for (size_t i = 0; i < count; i++) {
      lightusd_render_physics_info info;
      lightusd_status status =
          lightusd_render_physics_get_info(scene, kind, i, &info);
      if (status != LIGHTUSD_OK) {
        Py_DECREF(items);
        Py_DECREF(result);
        return lightusd_raise(st, status, "physics record");
      }
      PyObject* record = PyDict_New();
      if (!record) {
        Py_DECREF(items);
        Py_DECREF(result);
        return NULL;
      }
      int failed =
          put(record, "prim_path", lightusd_sv_to_str(info.prim_path)) < 0 ||
          put(record, "type_name", lightusd_sv_to_str(info.type_name)) < 0;
      if (kind == 0)
        failed |= put(record, "gravity_direction", vector3(info.vector0)) < 0 ||
                  put(record, "gravity_magnitude",
                      PyFloat_FromDouble(info.scalar0)) < 0;
      if (kind == 1) {
        failed |= put(record, "velocity", vector3(info.vector0)) < 0 ||
                  put(record, "angular_velocity", vector3(info.vector1)) < 0 ||
                  put(record, "center_of_mass", vector3(info.vector2)) < 0;
        failed |= put(record, "mass", PyFloat_FromDouble(info.scalar0)) < 0 ||
                  put(record, "density", PyFloat_FromDouble(info.scalar1)) < 0;
        failed |=
            put(record, "rigid_body_enabled", PyBool_FromLong(info.flags & 1)) <
                0 ||
            put(record, "kinematic_enabled", PyBool_FromLong(info.flags & 2)) <
                0 ||
            put(record, "starts_asleep", PyBool_FromLong(info.flags & 4)) < 0;
      }
      if (kind == 2)
        failed |= put(record, "collision_enabled",
                      PyBool_FromLong(info.flags & 1)) < 0 ||
                  put(record, "approximation",
                      lightusd_sv_to_str(info.approximation)) < 0;
      if (kind == 3) {
        failed |= put(record, "body0", lightusd_sv_to_str(info.body0)) < 0 ||
                  put(record, "body1", lightusd_sv_to_str(info.body1)) < 0;
        failed |= put(record, "local_pos0", vector3(info.vector0)) < 0 ||
                  put(record, "local_pos1", vector3(info.vector1)) < 0 ||
                  put(record, "axis", vector3(info.vector2)) < 0;
        failed |=
            put(record, "lower_limit", PyFloat_FromDouble(info.scalar0)) < 0 ||
            put(record, "upper_limit", PyFloat_FromDouble(info.scalar1)) < 0;
      }
      if (kind == 4)
        failed |=
            put(record, "static_friction", PyFloat_FromDouble(info.scalar0)) <
                0 ||
            put(record, "dynamic_friction", PyFloat_FromDouble(info.scalar1)) <
                0 ||
            put(record, "restitution", PyFloat_FromDouble(info.scalar2)) < 0 ||
            put(record, "density", PyFloat_FromDouble(info.scalar3)) < 0;
      if (kind == LIGHTUSD_RENDER_PHYSICS_FILTERED_PAIR) {
        PyObject* targets = PyList_New(0);
        if (!targets) failed = 1;
        for (size_t target = 0; !failed; target++) {
          size_t required = 0;
          status = lightusd_render_physics_string_copy(
              scene, kind, i, 0, target, NULL, 0, &required);
          if (status == LIGHTUSD_ERR_NOT_FOUND) break;
          if (status != LIGHTUSD_OK || required > (size_t)PY_SSIZE_T_MAX) {
            failed = 1;
            break;
          }
          char* buffer = (char*)PyMem_Malloc(required ? required : 1);
          if (!buffer) {
            PyErr_NoMemory();
            failed = 1;
            break;
          }
          status = lightusd_render_physics_string_copy(
              scene, kind, i, 0, target, buffer, required, &required);
          PyObject* path =
              status == LIGHTUSD_OK
                  ? PyUnicode_DecodeUTF8(buffer, (Py_ssize_t)required, "strict")
                  : NULL;
          PyMem_Free(buffer);
          if (!path || PyList_Append(targets, path) < 0) failed = 1;
          Py_XDECREF(path);
        }
        if (targets) {
          PyObject* tuple = PyList_AsTuple(targets);
          Py_DECREF(targets);
          if (put(record, "targets", tuple) < 0) failed = 1;
        }
      }
      PyObject* extensions = PyDict_New();
      if (!extensions) failed = 1;
      for (size_t p = 0; !failed; p++) {
        lightusd_sv name, value;
        status = lightusd_render_physics_extension_property(scene, kind, i, p,
                                                            &name, &value);
        if (status == LIGHTUSD_ERR_NOT_FOUND) break;
        if (status != LIGHTUSD_OK) {
          failed = 1;
          break;
        }
        PyObject *key = lightusd_sv_to_str(name),
                 *text = lightusd_sv_to_str(value);
        if (!key || !text || PyDict_SetItem(extensions, key, text) < 0)
          failed = 1;
        Py_XDECREF(key);
        Py_XDECREF(text);
      }
      if (extensions)
        failed |= put(record, "extension_summaries", extensions) < 0;
      if (failed) {
        Py_DECREF(record);
        Py_DECREF(items);
        Py_DECREF(result);
        if (!PyErr_Occurred())
          lightusd_raise(st, LIGHTUSD_ERR_INTERNAL, "physics annotations");
        return NULL;
      }
      PyTuple_SetItem(items, (Py_ssize_t)i, record);
    }
    if (put(result, names[kind], items) < 0) {
      Py_DECREF(result);
      return NULL;
    }
  }
  return result;
}
