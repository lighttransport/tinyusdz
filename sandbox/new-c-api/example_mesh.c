/**
 * @file example_mesh.c
 * @brief Example of extracting mesh data using LightUSD C API
 *
 * This example shows how to extract vertex, face, normal, and UV data from meshes.
 */

#include "lightusd_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/**
 * Extract and print mesh data
 */
static void process_mesh(lightusd_prim mesh, const char* mesh_name) {
    printf("\nMesh: %s\n", mesh_name);
    printf("----------------------------------------\n");

    // Get vertex positions
    const float* points = NULL;
    size_t point_count = 0;
    lightusd_result result = lightusd_mesh_get_points(mesh, &points, &point_count);

    if (result == LIGHTUSD_SUCCESS && points) {
        size_t vertex_count = point_count / 3;  // Each point is 3 floats
        printf("Vertices: %zu\n", vertex_count);

        // Print first few vertices
        size_t max_show = 3;
        if (vertex_count < max_show) max_show = vertex_count;

        for (size_t i = 0; i < max_show; i++) {
            size_t idx = i * 3;
            printf("  v[%zu]: (%f, %f, %f)\n", i,
                   points[idx], points[idx + 1], points[idx + 2]);
        }
        if (vertex_count > max_show) {
            printf("  ... and %zu more vertices\n", vertex_count - max_show);
        }

        // Calculate bounding box
        if (vertex_count > 0) {
            float min_x = points[0], min_y = points[1], min_z = points[2];
            float max_x = points[0], max_y = points[1], max_z = points[2];

            for (size_t i = 0; i < vertex_count; i++) {
                size_t idx = i * 3;
                if (points[idx] < min_x) min_x = points[idx];
                if (points[idx] > max_x) max_x = points[idx];
                if (points[idx + 1] < min_y) min_y = points[idx + 1];
                if (points[idx + 1] > max_y) max_y = points[idx + 1];
                if (points[idx + 2] < min_z) min_z = points[idx + 2];
                if (points[idx + 2] > max_z) max_z = points[idx + 2];
            }

            printf("\nBounding Box:\n");
            printf("  Min: (%f, %f, %f)\n", min_x, min_y, min_z);
            printf("  Max: (%f, %f, %f)\n", max_x, max_y, max_z);
            printf("  Size: (%f, %f, %f)\n",
                   max_x - min_x, max_y - min_y, max_z - min_z);
        }
    }

    // Get face information
    const int* face_counts = NULL;
    size_t face_count = 0;
    result = lightusd_mesh_get_face_counts(mesh, &face_counts, &face_count);

    if (result == LIGHTUSD_SUCCESS && face_counts) {
        printf("\nFaces: %zu\n", face_count);

        // Count face types
        int triangles = 0, quads = 0, ngons = 0;
        int min_verts = 999999, max_verts = 0;
        long total_verts = 0;

        for (size_t i = 0; i < face_count; i++) {
            int count = face_counts[i];
            total_verts += count;

            if (count < min_verts) min_verts = count;
            if (count > max_verts) max_verts = count;

            if (count == 3) triangles++;
            else if (count == 4) quads++;
            else ngons++;
        }

        printf("  Triangles: %d\n", triangles);
        printf("  Quads: %d\n", quads);
        if (ngons > 0) {
            printf("  N-gons: %d\n", ngons);
        }
        printf("  Vertices per face: %d to %d\n", min_verts, max_verts);
        printf("  Total face vertices: %ld\n", total_verts);
    }

    // Get vertex indices
    const int* indices = NULL;
    size_t index_count = 0;
    result = lightusd_mesh_get_indices(mesh, &indices, &index_count);

    if (result == LIGHTUSD_SUCCESS && indices) {
        printf("\nIndices: %zu\n", index_count);

        // Find min/max indices
        if (index_count > 0) {
            int min_idx = indices[0], max_idx = indices[0];
            for (size_t i = 1; i < index_count; i++) {
                if (indices[i] < min_idx) min_idx = indices[i];
                if (indices[i] > max_idx) max_idx = indices[i];
            }
            printf("  Index range: %d to %d\n", min_idx, max_idx);
        }

        // Print first few faces (if we have face counts)
        if (face_counts && face_count > 0) {
            printf("\nFirst few faces:\n");
            size_t idx_offset = 0;
            size_t max_faces = 3;
            if (face_count < max_faces) max_faces = face_count;

            for (size_t f = 0; f < max_faces; f++) {
                printf("  Face %zu:", f);
                for (int v = 0; v < face_counts[f]; v++) {
                    printf(" %d", indices[idx_offset + v]);
                }
                printf("\n");
                idx_offset += face_counts[f];
            }
        }
    }

    // Get normals
    const float* normals = NULL;
    size_t normal_count = 0;
    result = lightusd_mesh_get_normals(mesh, &normals, &normal_count);

    if (result == LIGHTUSD_SUCCESS && normals) {
        printf("\nNormals: %zu\n", normal_count / 3);

        // Check if normals are normalized
        int unnormalized = 0;
        for (size_t i = 0; i < normal_count / 3; i++) {
            size_t idx = i * 3;
            float len = sqrtf(normals[idx] * normals[idx] +
                            normals[idx + 1] * normals[idx + 1] +
                            normals[idx + 2] * normals[idx + 2]);
            if (fabsf(len - 1.0f) > 0.01f) {
                unnormalized++;
            }
        }
        if (unnormalized > 0) {
            printf("  Warning: %d normals are not unit length\n", unnormalized);
        }
    } else {
        printf("\nNormals: Not present\n");
    }

    // Get UVs
    const float* uvs = NULL;
    size_t uv_count = 0;
    result = lightusd_mesh_get_uvs(mesh, &uvs, &uv_count, 0);  // Primary UV set

    if (result == LIGHTUSD_SUCCESS && uvs) {
        printf("\nUV Coordinates: %zu\n", uv_count / 2);

        // Check UV range
        if (uv_count > 0) {
            float min_u = uvs[0], min_v = uvs[1];
            float max_u = uvs[0], max_v = uvs[1];

            for (size_t i = 0; i < uv_count / 2; i++) {
                size_t idx = i * 2;
                if (uvs[idx] < min_u) min_u = uvs[idx];
                if (uvs[idx] > max_u) max_u = uvs[idx];
                if (uvs[idx + 1] < min_v) min_v = uvs[idx + 1];
                if (uvs[idx + 1] > max_v) max_v = uvs[idx + 1];
            }

            printf("  U range: [%f, %f]\n", min_u, max_u);
            printf("  V range: [%f, %f]\n", min_v, max_v);

            if (min_u < 0 || max_u > 1 || min_v < 0 || max_v > 1) {
                printf("  Note: UVs extend outside [0,1] range\n");
            }
        }
    } else {
        printf("\nUV Coordinates: Not present\n");
    }

    // Get subdivision scheme
    const char* subdiv = lightusd_mesh_get_subdivision_scheme(mesh);
    if (subdiv && strcmp(subdiv, "none") != 0) {
        printf("\nSubdivision: %s\n", subdiv);
    }

    // Get material binding
    lightusd_prim material = lightusd_prim_get_bound_material(mesh);
    if (material) {
        printf("\nMaterial: %s\n", lightusd_prim_get_name(material));

        // Get surface shader
        lightusd_prim shader = lightusd_material_get_surface_shader(material);
        if (shader) {
            const char* shader_type = lightusd_shader_get_type_id(shader);
            printf("  Shader Type: %s\n", shader_type);

            // Get some common shader inputs
            const char* common_inputs[] = {
                "diffuseColor", "roughness", "metallic", "opacity"
            };

            for (int i = 0; i < 4; i++) {
                lightusd_value input = lightusd_shader_get_input(shader, common_inputs[i]);
                if (input) {
                    printf("  %s: ", common_inputs[i]);

                    lightusd_value_type type = lightusd_value_get_type(input);
                    if (type == LIGHTUSD_VALUE_FLOAT3 || type == LIGHTUSD_VALUE_COLOR3F) {
                        float color[3];
                        if (lightusd_value_get_float3(input, color) == LIGHTUSD_SUCCESS) {
                            printf("(%f, %f, %f)\n", color[0], color[1], color[2]);
                        }
                    } else if (type == LIGHTUSD_VALUE_FLOAT) {
                        float val;
                        if (lightusd_value_get_float(input, &val) == LIGHTUSD_SUCCESS) {
                            printf("%f\n", val);
                        }
                    } else if (type == LIGHTUSD_VALUE_ASSET_PATH) {
                        const char* path;
                        if (lightusd_value_get_asset_path(input, &path) == LIGHTUSD_SUCCESS) {
                            printf("%s\n", path);
                        }
                    } else {
                        printf("<%s>\n", lightusd_value_type_to_string(type));
                    }

                    lightusd_value_free(input);
                }
            }
        }
    }
}

/**
 * Find and process all meshes in hierarchy
 */
static void find_meshes(lightusd_prim prim, int* mesh_count) {
    if (!prim) return;

    // Check if this is a mesh
    if (lightusd_prim_is_type(prim, LIGHTUSD_PRIM_MESH)) {
        (*mesh_count)++;
        const char* name = lightusd_prim_get_name(prim);
        const char* path = lightusd_prim_get_path(prim);
        process_mesh(prim, path);
    }

    // Recursively check children
    size_t child_count = lightusd_prim_get_child_count(prim);
    for (size_t i = 0; i < child_count; i++) {
        lightusd_prim child = lightusd_prim_get_child_at(prim, i);
        find_meshes(child, mesh_count);
    }
}

/**
 * Main function
 */
int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <usd_file>\n", argv[0]);
        printf("Example: %s scene.usd\n", argv[0]);
        printf("\nThis tool extracts and displays mesh data from USD files.\n");
        return 1;
    }

    const char* filepath = argv[1];

    // Initialize
    if (lightusd_init() != LIGHTUSD_SUCCESS) {
        fprintf(stderr, "Failed to initialize LightUSD\n");
        return 1;
    }

    printf("Loading: %s\n", filepath);

    // Load file
    lightusd_stage stage = NULL;
    char error_buf[1024] = {0};
    lightusd_result result = lightusd_load_from_file(filepath, NULL, &stage,
                                               error_buf, sizeof(error_buf));

    if (result != LIGHTUSD_SUCCESS) {
        fprintf(stderr, "Failed to load file: %s\n", error_buf);
        lightusd_shutdown();
        return 1;
    }

    printf("File loaded successfully!\n");
    printf("========================================\n");

    // Find and process all meshes
    int mesh_count = 0;
    lightusd_prim root = lightusd_stage_get_root_prim(stage);
    find_meshes(root, &mesh_count);

    if (mesh_count == 0) {
        printf("\nNo meshes found in the file.\n");
    } else {
        printf("\n========================================\n");
        printf("Total meshes processed: %d\n", mesh_count);
    }

    // Cleanup
    lightusd_stage_free(stage);
    lightusd_shutdown();

    return 0;
}