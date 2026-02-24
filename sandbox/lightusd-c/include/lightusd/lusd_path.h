/*
 * lusd_path.h - Scene path manipulation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_PATH_H
#define LUSD_PATH_H

#include "lusd_platform.h"
#include "lusd_result.h"
#include "lusd_handles.h"

LUSD_EXTERN_C_BEGIN

/*
 * Create a path from a string (e.g. "/World/Mesh.points").
 * Paths are owned by the instance and must be destroyed with lusdDestroyPath.
 */
LUSD_API LusdResult lusdCreatePath(
    LusdInstance  instance,
    const char*   pPathString,
    LusdPath*     pPath);

/*
 * Create the absolute root path "/".
 */
LUSD_API LusdResult lusdCreateRootPath(
    LusdInstance  instance,
    LusdPath*     pPath);

/*
 * Destroy a path. No-op if path is LUSD_NULL_HANDLE.
 */
LUSD_API void lusdDestroyPath(
    LusdInstance  instance,
    LusdPath      path);

/*
 * Get the string representation of a path.
 * Returned pointer is valid for the lifetime of the path.
 */
LUSD_API const char* lusdPathGetText(LusdPath path);

/*
 * Check if a path is absolute (starts with '/').
 */
LUSD_API bool lusdPathIsAbsolute(LusdPath path);

/*
 * Check if a path is the root path "/".
 */
LUSD_API bool lusdPathIsRoot(LusdPath path);

/*
 * Check if the path has a property component (e.g. "/Mesh.points").
 */
LUSD_API bool lusdPathIsPropertyPath(LusdPath path);

/*
 * Get the prim portion of a property path (e.g. "/Mesh" from "/Mesh.points").
 * Returns the path itself if it's not a property path.
 */
LUSD_API LusdResult lusdPathGetPrimPath(
    LusdInstance  instance,
    LusdPath      path,
    LusdPath*     pPrimPath);

/*
 * Get the property name (e.g. "points" from "/Mesh.points").
 * Returns NULL if not a property path.
 */
LUSD_API const char* lusdPathGetPropertyName(LusdPath path);

/*
 * Get the parent path (e.g. "/World" from "/World/Mesh").
 * Returns root path for direct children of root.
 */
LUSD_API LusdResult lusdPathGetParent(
    LusdInstance  instance,
    LusdPath      path,
    LusdPath*     pParent);

/*
 * Get the element name (last component, e.g. "Mesh" from "/World/Mesh").
 */
LUSD_API const char* lusdPathGetElementName(LusdPath path);

/*
 * Append a child element (e.g. "/World" + "Mesh" -> "/World/Mesh").
 */
LUSD_API LusdResult lusdPathAppendChild(
    LusdInstance  instance,
    LusdPath      parent,
    const char*   pChildName,
    LusdPath*     pResult);

/*
 * Append a property (e.g. "/Mesh" + "points" -> "/Mesh.points").
 */
LUSD_API LusdResult lusdPathAppendProperty(
    LusdInstance  instance,
    LusdPath      primPath,
    const char*   pPropertyName,
    LusdPath*     pResult);

/*
 * Compare two paths for equality.
 */
LUSD_API bool lusdPathEqual(LusdPath a, LusdPath b);

/*
 * Check if `ancestor` is a prefix of `descendant`.
 */
LUSD_API bool lusdPathHasPrefix(LusdPath descendant, LusdPath ancestor);

LUSD_EXTERN_C_END

#endif /* LUSD_PATH_H */
