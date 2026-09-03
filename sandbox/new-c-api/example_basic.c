/**
 * @file example_basic.c
 * @brief Basic example of using LightUSD C API
 *
 * This example demonstrates loading a USD file and traversing its hierarchy.
 */

#include "lightusd_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Print indentation for hierarchy display
 */
static void print_indent(int level) {
    for (int i = 0; i < level; i++) {
        printf("  ");
    }
}

/**
 * Recursively traverse and print prim hierarchy
 */
static void traverse_prim(lightusd_prim prim, int depth) {
    if (!prim) {
        return;
    }

    // Print prim info
    const char* name = lightusd_prim_get_name(prim);
    lightusd_prim_type type = lightusd_prim_get_type(prim);
    const char* type_name = lightusd_prim_type_to_string(type);

    print_indent(depth);
    printf("- %s [%s]", name, type_name);

    // Print path if not root
    if (depth > 0) {
        const char* path = lightusd_prim_get_path(prim);
        printf(" (path: %s)", path);
    }

    // If mesh, print some stats
    if (type == LIGHTUSD_PRIM_MESH) {
        const float* points = NULL;
        size_t point_count = 0;
        if (lightusd_mesh_get_points(prim, &points, &point_count) == LIGHTUSD_SUCCESS) {
            printf(" - %zu vertices", point_count / 3);
        }

        const int* face_counts = NULL;
        size_t face_count = 0;
        if (lightusd_mesh_get_face_counts(prim, &face_counts, &face_count) == LIGHTUSD_SUCCESS) {
            printf(", %zu faces", face_count);
        }
    }

    printf("\n");

    // Print properties
    size_t prop_count = lightusd_prim_get_property_count(prim);
    if (prop_count > 0 && depth < 2) {  // Only show properties for first 2 levels
        print_indent(depth + 1);
        printf("Properties (%zu):\n", prop_count);

        for (size_t i = 0; i < prop_count && i < 5; i++) {  // Show first 5 properties
            const char* prop_name = lightusd_prim_get_property_name_at(prim, i);
            lightusd_value value = lightusd_prim_get_property(prim, prop_name);

            if (value) {
                lightusd_value_type vtype = lightusd_value_get_type(value);
                print_indent(depth + 2);
                printf("%s: %s", prop_name, lightusd_value_type_to_string(vtype));

                // Show sample values for simple types
                switch (vtype) {
                    case LIGHTUSD_VALUE_FLOAT: {
                        float f;
                        if (lightusd_value_get_float(value, &f) == LIGHTUSD_SUCCESS) {
                            printf(" = %f", f);
                        }
                        break;
                    }
                    case LIGHTUSD_VALUE_FLOAT3: {
                        float vec[3];
                        if (lightusd_value_get_float3(value, vec) == LIGHTUSD_SUCCESS) {
                            printf(" = (%f, %f, %f)", vec[0], vec[1], vec[2]);
                        }
                        break;
                    }
                    case LIGHTUSD_VALUE_STRING:
                    case LIGHTUSD_VALUE_TOKEN: {
                        const char* str;
                        if (lightusd_value_get_string(value, &str) == LIGHTUSD_SUCCESS) {
                            printf(" = \"%s\"", str);
                        }
                        break;
                    }
                    default:
                        if (lightusd_value_is_array(value)) {
                            size_t array_size = lightusd_value_get_array_size(value);
                            printf(" [array of %zu]", array_size);
                        }
                        break;
                }
                printf("\n");

                lightusd_value_free(value);
            }
        }

        if (prop_count > 5) {
            print_indent(depth + 2);
            printf("... and %zu more\n", prop_count - 5);
        }
    }

    // Traverse children
    size_t child_count = lightusd_prim_get_child_count(prim);
    for (size_t i = 0; i < child_count; i++) {
        lightusd_prim child = lightusd_prim_get_child_at(prim, i);
        traverse_prim(child, depth + 1);
    }
}

/**
 * Main example function
 */
int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <usd_file>\n", argv[0]);
        printf("Example: %s model.usda\n", argv[0]);
        return 1;
    }

    const char* filepath = argv[1];

    // Initialize library
    lightusd_result result = lightusd_init();
    if (result != LIGHTUSD_SUCCESS) {
        fprintf(stderr, "Failed to initialize LightUSD: %s\n",
                lightusd_result_to_string(result));
        return 1;
    }

    printf("LightUSD C API Version: %s\n", lightusd_get_version());
    printf("Loading USD file: %s\n", filepath);

    // Detect format
    lightusd_format format = lightusd_detect_format(filepath);
    const char* format_name = "auto";
    switch (format) {
        case LIGHTUSD_FORMAT_USDA: format_name = "USDA (ASCII)"; break;
        case LIGHTUSD_FORMAT_USDC: format_name = "USDC (Binary)"; break;
        case LIGHTUSD_FORMAT_USDZ: format_name = "USDZ (Archive)"; break;
        default: break;
    }
    printf("Detected format: %s\n", format_name);

    // Setup load options
    lightusd_load_options options = {
        .max_memory_limit_mb = 1024,  // 1GB limit
        .max_depth = 10,               // Max composition depth
        .enable_composition = 1,        // Enable references/payloads
        .strict_mode = 0,              // Don't fail on warnings
        .structure_only = 0,           // Load full data
        .asset_resolver = NULL,
        .asset_resolver_data = NULL
    };

    // Load the file
    lightusd_stage stage = NULL;
    char error_buf[1024] = {0};

    result = lightusd_load_from_file(filepath, &options, &stage, error_buf, sizeof(error_buf));

    if (result != LIGHTUSD_SUCCESS) {
        fprintf(stderr, "Failed to load USD file: %s\n", lightusd_result_to_string(result));
        if (error_buf[0]) {
            fprintf(stderr, "Error details: %s\n", error_buf);
        }
        lightusd_shutdown();
        return 1;
    }

    printf("Successfully loaded USD file!\n\n");

    // Check for animation
    if (lightusd_stage_has_animation(stage)) {
        double start_time, end_time, fps;
        if (lightusd_stage_get_time_range(stage, &start_time, &end_time, &fps) == LIGHTUSD_SUCCESS) {
            printf("Animation detected: %.2f to %.2f @ %.2f fps\n\n",
                   start_time, end_time, fps);
        }
    }

    // Traverse hierarchy
    printf("Scene Hierarchy:\n");
    printf("================\n");

    lightusd_prim root = lightusd_stage_get_root_prim(stage);
    if (root) {
        traverse_prim(root, 0);
    } else {
        printf("No root prim found\n");
    }

    printf("\n");

    // Try to find a specific prim by path
    const char* test_path = "/World";
    printf("Looking for prim at path: %s\n", test_path);
    lightusd_prim world = lightusd_stage_get_prim_at_path(stage, test_path);
    if (world) {
        printf("Found: %s [%s]\n", lightusd_prim_get_name(world),
               lightusd_prim_get_type_name(world));
    } else {
        printf("Not found\n");
    }

    // Print memory statistics
    size_t bytes_used, bytes_peak;
    lightusd_get_memory_stats(stage, &bytes_used, &bytes_peak);
    printf("\nMemory usage: %zu KB (peak: %zu KB)\n",
           bytes_used / 1024, bytes_peak / 1024);

    // Clean up
    lightusd_stage_free(stage);
    lightusd_shutdown();

    printf("\nDone!\n");
    return 0;
}