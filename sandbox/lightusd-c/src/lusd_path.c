/*
 * lusd_path.c - Scene path manipulation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightusd/lusd_path.h"
#include "internal/lusd_internal.h"
#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------- */

static LusdPathData* alloc_path_data(struct LusdInstance_T* inst) {
    const LusdAllocationCallbacks* alloc = inst->alloc.pfnAllocation ? &inst->alloc : NULL;
    LusdPathData* pd = (LusdPathData*)lusd_alloc(alloc, sizeof(LusdPathData), sizeof(void*));
    if (pd) memset(pd, 0, sizeof(*pd));
    return pd;
}

static void free_path_data(struct LusdInstance_T* inst, LusdPathData* pd) {
    if (!pd) return;
    const LusdAllocationCallbacks* alloc = inst->alloc.pfnAllocation ? &inst->alloc : NULL;
    if (pd->text) lusd_free(alloc, pd->text);
    lusd_free(alloc, pd);
}

static const LusdAllocationCallbacks* inst_alloc(struct LusdInstance_T* inst) {
    return inst->alloc.pfnAllocation ? &inst->alloc : NULL;
}

/* Parse a path string and fill in LusdPathData fields */
static void parse_path(LusdPathData* pd) {
    if (!pd->text || pd->text[0] == '\0') return;

    pd->isAbsolute = (pd->text[0] == '/');

    /* Find property separator '.' */
    char* dot = strrchr(pd->text, '.');
    if (dot && dot != pd->text) {
        /* Check it's not inside a variant selection {...} */
        char* brace = strchr(pd->text, '{');
        if (!brace || dot > strchr(pd->text, '}')) {
            pd->isProperty = true;
            pd->primEnd = (uint32_t)(dot - pd->text);
            pd->propertyName = dot + 1;
        }
    }

    /* Find element name (last component after '/') */
    const char* search = pd->isProperty ? pd->text : pd->text;
    uint32_t end = pd->isProperty ? pd->primEnd : (uint32_t)strlen(pd->text);
    uint32_t lastSlash = 0;
    for (uint32_t i = 0; i < end; i++) {
        if (pd->text[i] == '/') lastSlash = i;
    }
    if (pd->text[lastSlash] == '/') {
        pd->elementName = pd->text + lastSlash + 1;
    } else {
        pd->elementName = pd->text;
    }
    LUSD_UNUSED(search);
}

static LusdPath path_to_handle(LusdPathData* pd) {
    return (LusdPath)(uintptr_t)pd;
}

static LusdPathData* handle_to_path(LusdPath handle) {
    return (LusdPathData*)(uintptr_t)handle;
}

/* -------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------- */

LusdResult lusdCreatePath(
    LusdInstance instance,
    const char*  pPathString,
    LusdPath*    pPath)
{
    if (!instance || !pPathString || !pPath) return LUSD_ERROR_INVALID_ARGUMENT;

    /* Validate: non-empty */
    if (pPathString[0] == '\0') return LUSD_ERROR_INVALID_PATH;

    LusdPathData* pd = alloc_path_data(instance);
    if (!pd) return LUSD_ERROR_OUT_OF_MEMORY;

    pd->text = lusd_strdup(inst_alloc(instance), pPathString);
    if (!pd->text) {
        free_path_data(instance, pd);
        return LUSD_ERROR_OUT_OF_MEMORY;
    }

    parse_path(pd);

    *pPath = path_to_handle(pd);
    return LUSD_SUCCESS;
}

LusdResult lusdCreateRootPath(
    LusdInstance instance,
    LusdPath*    pPath)
{
    return lusdCreatePath(instance, "/", pPath);
}

void lusdDestroyPath(
    LusdInstance instance,
    LusdPath     path)
{
    if (!instance || !path) return;
    free_path_data(instance, handle_to_path(path));
}

const char* lusdPathGetText(LusdPath path) {
    if (!path) return "";
    return handle_to_path(path)->text;
}

bool lusdPathIsAbsolute(LusdPath path) {
    if (!path) return false;
    return handle_to_path(path)->isAbsolute;
}

bool lusdPathIsRoot(LusdPath path) {
    if (!path) return false;
    const char* text = handle_to_path(path)->text;
    return text[0] == '/' && text[1] == '\0';
}

bool lusdPathIsPropertyPath(LusdPath path) {
    if (!path) return false;
    return handle_to_path(path)->isProperty;
}

LusdResult lusdPathGetPrimPath(
    LusdInstance instance,
    LusdPath     path,
    LusdPath*    pPrimPath)
{
    if (!instance || !path || !pPrimPath) return LUSD_ERROR_INVALID_ARGUMENT;

    LusdPathData* pd = handle_to_path(path);
    if (!pd->isProperty) {
        /* Not a property path, return a copy of the same path */
        return lusdCreatePath(instance, pd->text, pPrimPath);
    }

    /* Extract prim portion */
    char buf[2048];
    uint32_t len = pd->primEnd;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, pd->text, len);
    buf[len] = '\0';
    return lusdCreatePath(instance, buf, pPrimPath);
}

const char* lusdPathGetPropertyName(LusdPath path) {
    if (!path) return NULL;
    LusdPathData* pd = handle_to_path(path);
    if (!pd->isProperty) return NULL;
    return pd->propertyName;
}

LusdResult lusdPathGetParent(
    LusdInstance instance,
    LusdPath     path,
    LusdPath*    pParent)
{
    if (!instance || !path || !pParent) return LUSD_ERROR_INVALID_ARGUMENT;

    const char* text = handle_to_path(path)->text;

    /* Root has no parent, return root */
    if (text[0] == '/' && text[1] == '\0') {
        return lusdCreateRootPath(instance, pParent);
    }

    /* For property paths, parent is the prim path */
    LusdPathData* pd = handle_to_path(path);
    if (pd->isProperty) {
        return lusdPathGetPrimPath(instance, path, pParent);
    }

    /* Find last slash */
    size_t len = strlen(text);
    size_t lastSlash = 0;
    for (size_t i = len - 1; i > 0; i--) {
        if (text[i] == '/') {
            lastSlash = i;
            break;
        }
    }

    if (lastSlash == 0) {
        /* Direct child of root */
        return lusdCreateRootPath(instance, pParent);
    }

    char buf[2048];
    if (lastSlash >= sizeof(buf)) lastSlash = sizeof(buf) - 1;
    memcpy(buf, text, lastSlash);
    buf[lastSlash] = '\0';
    return lusdCreatePath(instance, buf, pParent);
}

const char* lusdPathGetElementName(LusdPath path) {
    if (!path) return "";
    LusdPathData* pd = handle_to_path(path);
    if (!pd->elementName) return "";
    /* For property paths, element name is the property name */
    if (pd->isProperty && pd->propertyName) return pd->propertyName;
    return pd->elementName;
}

LusdResult lusdPathAppendChild(
    LusdInstance instance,
    LusdPath     parent,
    const char*  pChildName,
    LusdPath*    pResult)
{
    if (!instance || !parent || !pChildName || !pResult)
        return LUSD_ERROR_INVALID_ARGUMENT;

    const char* parentText = handle_to_path(parent)->text;
    size_t plen = strlen(parentText);
    size_t clen = strlen(pChildName);

    char buf[4096];
    if (plen + clen + 2 > sizeof(buf)) return LUSD_ERROR_INVALID_PATH;

    /* "/World" + "Mesh" -> "/World/Mesh" */
    /* "/" + "World" -> "/World" */
    memcpy(buf, parentText, plen);
    if (plen > 0 && parentText[plen - 1] != '/') {
        buf[plen] = '/';
        plen++;
    }
    memcpy(buf + plen, pChildName, clen);
    buf[plen + clen] = '\0';

    return lusdCreatePath(instance, buf, pResult);
}

LusdResult lusdPathAppendProperty(
    LusdInstance instance,
    LusdPath     primPath,
    const char*  pPropertyName,
    LusdPath*    pResult)
{
    if (!instance || !primPath || !pPropertyName || !pResult)
        return LUSD_ERROR_INVALID_ARGUMENT;

    const char* primText = handle_to_path(primPath)->text;
    size_t plen = strlen(primText);
    size_t nlen = strlen(pPropertyName);

    char buf[4096];
    if (plen + nlen + 2 > sizeof(buf)) return LUSD_ERROR_INVALID_PATH;

    /* "/Mesh" + "points" -> "/Mesh.points" */
    memcpy(buf, primText, plen);
    buf[plen] = '.';
    memcpy(buf + plen + 1, pPropertyName, nlen);
    buf[plen + 1 + nlen] = '\0';

    return lusdCreatePath(instance, buf, pResult);
}

bool lusdPathEqual(LusdPath a, LusdPath b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return strcmp(handle_to_path(a)->text, handle_to_path(b)->text) == 0;
}

bool lusdPathHasPrefix(LusdPath descendant, LusdPath ancestor) {
    if (!descendant || !ancestor) return false;
    const char* d = handle_to_path(descendant)->text;
    const char* a = handle_to_path(ancestor)->text;
    size_t alen = strlen(a);

    /* Root is prefix of everything */
    if (a[0] == '/' && a[1] == '\0') return true;

    if (strncmp(d, a, alen) != 0) return false;
    /* Must be followed by '/' or end of string */
    return d[alen] == '/' || d[alen] == '\0' || d[alen] == '.';
}
