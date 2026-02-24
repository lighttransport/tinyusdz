/*
 * basic.c - Hello-world example for lightusd-c
 *
 * Demonstrates instance creation, token/path/value usage.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightusd/lightusd-c.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("lightusd-c basic example\n");
    printf("========================\n\n");

    /* -------------------------------------------------------------------
     * 1. Create an instance
     * ------------------------------------------------------------------- */
    LusdInstanceCreateInfo createInfo;
    memset(&createInfo, 0, sizeof(createInfo));
    createInfo.sType = LUSD_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.apiVersion = LUSD_API_VERSION;
    createInfo.pApplicationName = "lightusd-c-basic";
    createInfo.applicationVersion = LUSD_MAKE_API_VERSION(1, 0, 0);

    LusdInstance instance = NULL;
    LusdResult result = lusdCreateInstance(&createInfo, NULL, &instance);
    if (result != LUSD_SUCCESS) {
        printf("Failed to create instance: %s\n", lusdResultToString(result));
        return 1;
    }
    printf("Instance created successfully.\n\n");

    /* -------------------------------------------------------------------
     * 2. Create and compare tokens
     * ------------------------------------------------------------------- */
    printf("--- Tokens ---\n");
    LusdToken tokMesh, tokMesh2, tokXform;
    lusdCreateToken(instance, "Mesh", &tokMesh);
    lusdCreateToken(instance, "Mesh", &tokMesh2);
    lusdCreateToken(instance, "Xform", &tokXform);

    printf("Token 'Mesh':  \"%s\"\n", lusdTokenGetText(tokMesh));
    printf("Token 'Xform': \"%s\"\n", lusdTokenGetText(tokXform));
    printf("'Mesh' == 'Mesh': %s\n", lusdTokenEqual(tokMesh, tokMesh2) ? "true" : "false");
    printf("'Mesh' == 'Xform': %s\n\n", lusdTokenEqual(tokMesh, tokXform) ? "true" : "false");

    /* -------------------------------------------------------------------
     * 3. Create and manipulate paths
     * ------------------------------------------------------------------- */
    printf("--- Paths ---\n");
    LusdPath rootPath, worldPath, meshPath, propPath;

    lusdCreateRootPath(instance, &rootPath);
    printf("Root path: \"%s\"\n", lusdPathGetText(rootPath));

    lusdPathAppendChild(instance, rootPath, "World", &worldPath);
    printf("World path: \"%s\"\n", lusdPathGetText(worldPath));

    lusdPathAppendChild(instance, worldPath, "Mesh", &meshPath);
    printf("Mesh path: \"%s\"\n", lusdPathGetText(meshPath));

    lusdPathAppendProperty(instance, meshPath, "points", &propPath);
    printf("Property path: \"%s\"\n", lusdPathGetText(propPath));
    printf("Is property path: %s\n", lusdPathIsPropertyPath(propPath) ? "true" : "false");
    printf("Property name: \"%s\"\n", lusdPathGetPropertyName(propPath));
    printf("Element name: \"%s\"\n\n", lusdPathGetElementName(meshPath));

    LusdPath parentPath;
    lusdPathGetParent(instance, meshPath, &parentPath);
    printf("Parent of Mesh: \"%s\"\n", lusdPathGetText(parentPath));
    printf("HasPrefix(/World/Mesh, /World): %s\n\n",
           lusdPathHasPrefix(meshPath, worldPath) ? "true" : "false");

    /* -------------------------------------------------------------------
     * 4. Create typed values
     * ------------------------------------------------------------------- */
    printf("--- Values ---\n");

    /* Scalar float3 */
    LusdFloat3 position = {1.0f, 2.0f, 3.0f};
    LusdValue posValue;
    lusdCreateValueFloat3(instance, position, &posValue);
    printf("Float3 type: %s\n", lusdValueTypeGetName(lusdValueGetType(posValue)));

    LusdFloat3 readBack;
    lusdValueGetFloat3(posValue, &readBack);
    printf("Float3 value: (%.1f, %.1f, %.1f)\n", readBack.x, readBack.y, readBack.z);

    /* Array of float3 (vertex positions) */
    LusdFloat3 vertices[] = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.5f, 1.0f, 0.0f}
    };
    LusdValue vertArray;
    lusdCreateValueArrayFloat3(instance, 3, vertices, &vertArray);
    printf("Array type: %s[]\n", lusdValueTypeGetName(
        (LusdValueType)((uint32_t)lusdValueGetType(vertArray) & ~LUSD_VALUE_TYPE_ARRAY_BIT)));
    printf("Array size: %llu\n", (unsigned long long)lusdValueGetArraySize(vertArray));

    /* Zero-copy array access */
    uint64_t count;
    const LusdFloat3* vertPtr;
    lusdValueGetArrayPtrFloat3(vertArray, &count, &vertPtr);
    printf("Vertex[0]: (%.1f, %.1f, %.1f)\n", vertPtr[0].x, vertPtr[0].y, vertPtr[0].z);
    printf("Vertex[2]: (%.1f, %.1f, %.1f)\n\n", vertPtr[2].x, vertPtr[2].y, vertPtr[2].z);

    /* String value */
    LusdValue strVal;
    lusdCreateValueString(instance, "Hello from lightusd-c!", &strVal);
    const char* strOut;
    lusdValueGetString(strVal, &strOut);
    printf("String value: \"%s\"\n\n", strOut);

    /* -------------------------------------------------------------------
     * 5. Test stub APIs (expected to return FEATURE_NOT_PRESENT)
     * ------------------------------------------------------------------- */
    printf("--- Stub APIs ---\n");
    LusdStageCreateInfo stageCI;
    memset(&stageCI, 0, sizeof(stageCI));
    stageCI.sType = LUSD_STRUCTURE_TYPE_STAGE_CREATE_INFO;
    LusdStage stage;
    result = lusdCreateStage(instance, &stageCI, &stage);
    printf("lusdCreateStage: %s (expected: not yet implemented)\n",
           lusdResultToString(result));

    /* -------------------------------------------------------------------
     * 6. Cleanup
     * ------------------------------------------------------------------- */
    printf("\n--- Cleanup ---\n");
    lusdDestroyValue(instance, strVal);
    lusdDestroyValue(instance, vertArray);
    lusdDestroyValue(instance, posValue);
    lusdDestroyPath(instance, parentPath);
    lusdDestroyPath(instance, propPath);
    lusdDestroyPath(instance, meshPath);
    lusdDestroyPath(instance, worldPath);
    lusdDestroyPath(instance, rootPath);
    lusdDestroyInstance(instance, NULL);
    printf("All resources freed.\n");

    return 0;
}
