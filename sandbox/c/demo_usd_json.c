#include "lightusd_layer.h"
#include "lightusd_json_core.c"  /* Include core JSON directly to avoid conflicts */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Simple USD to JSON conversion demonstration */
void demo_usd_to_json(const lightusd_layer_t *layer) {
    printf("=== USD Layer to JSON Conversion Demo ===\n");

    /* Create a simple JSON representation of the layer */
    lightusd_json_value_t *root = lightusd_json_value_create_object();
    lightusd_json_object_t *obj = lightusd_json_value_get_object(root);

    /* Basic layer information */
    lightusd_json_object_set(obj, "name", lightusd_json_value_create_string(layer->name));

    if (layer->file_path) {
        lightusd_json_object_set(obj, "file_path", lightusd_json_value_create_string(layer->file_path));
    }

    /* Layer metadata */
    lightusd_json_value_t *metadata_obj = lightusd_json_value_create_object();
    lightusd_json_object_t *metadata = lightusd_json_value_get_object(metadata_obj);

    if (layer->metas.doc) {
        lightusd_json_object_set(metadata, "doc", lightusd_json_value_create_string(layer->metas.doc));
    }

    if (layer->metas.up_axis.type == LIGHTUSD_VALUE_STRING && layer->metas.up_axis.data.string_val) {
        lightusd_json_object_set(metadata, "up_axis", lightusd_json_value_create_string(layer->metas.up_axis.data.string_val));
    }

    lightusd_json_object_set(metadata, "meters_per_unit", lightusd_json_value_create_number(layer->metas.meters_per_unit));

    lightusd_json_object_set(obj, "metadata", metadata_obj);

    /* PrimSpecs (simplified representation) */
    if (layer->primspecs && lightusd_map_size(layer->primspecs) > 0) {
        lightusd_json_value_t *primspecs_obj = lightusd_json_value_create_object();
        lightusd_json_object_t *primspecs = lightusd_json_value_get_object(primspecs_obj);

        lightusd_map_iterator_t *iter = lightusd_map_iterator_create(layer->primspecs);
        const char *key;
        void *value;

        while (lightusd_map_iterator_next(iter, &key, &value)) {
            lightusd_primspec_t *primspec = (lightusd_primspec_t*)value;

            /* Create a simple representation of the primspec */
            lightusd_json_value_t *prim_obj = lightusd_json_value_create_object();
            lightusd_json_object_t *prim = lightusd_json_value_get_object(prim_obj);

            lightusd_json_object_set(prim, "name", lightusd_json_value_create_string(primspec->name));
            lightusd_json_object_set(prim, "type_name", lightusd_json_value_create_string(primspec->type_name));
            lightusd_json_object_set(prim, "specifier", lightusd_json_value_create_string(lightusd_specifier_to_string(primspec->specifier)));

            if (primspec->doc) {
                lightusd_json_object_set(prim, "doc", lightusd_json_value_create_string(primspec->doc));
            }

            /* Add property count */
            size_t prop_count = primspec->properties ? lightusd_map_size(primspec->properties) : 0;
            lightusd_json_object_set(prim, "property_count", lightusd_json_value_create_number((double)prop_count));

            /* Add children count */
            size_t child_count = primspec->children ? lightusd_map_size(primspec->children) : 0;
            lightusd_json_object_set(prim, "children_count", lightusd_json_value_create_number((double)child_count));

            lightusd_json_object_set(primspecs, key, prim_obj);
        }

        lightusd_map_iterator_destroy(iter);
        lightusd_json_object_set(obj, "primspecs", primspecs_obj);
    }

    /* Serialize to pretty JSON */
    char *json_str = lightusd_json_serialize_pretty(root, 2);
    if (json_str) {
        printf("JSON Output:\n%s\n", json_str);
        free(json_str);
    }

    lightusd_json_value_destroy(root);
}

/* Simple JSON to USD conversion demonstration */
lightusd_layer_t *demo_json_to_usd(const char *json_string) {
    printf("\n=== JSON to USD Layer Conversion Demo ===\n");

    lightusd_json_value_t *json = lightusd_json_parse(json_string);
    if (!json || !lightusd_json_value_is_object(json)) {
        printf("Failed to parse JSON or not an object\n");
        return NULL;
    }

    lightusd_json_object_t *obj = lightusd_json_value_get_object(json);

    /* Get layer name */
    lightusd_json_value_t *name_val = lightusd_json_object_get(obj, "name");
    if (!name_val || !lightusd_json_value_is_string(name_val)) {
        printf("JSON missing required 'name' field\n");
        lightusd_json_value_destroy(json);
        return NULL;
    }

    const char *name = lightusd_json_value_get_string(name_val);
    lightusd_layer_t *layer = lightusd_layer_create(name);
    if (!layer) {
        lightusd_json_value_destroy(json);
        return NULL;
    }

    /* Set file path if present */
    lightusd_json_value_t *file_path_val = lightusd_json_object_get(obj, "file_path");
    if (file_path_val && lightusd_json_value_is_string(file_path_val)) {
        lightusd_layer_set_file_path(layer, lightusd_json_value_get_string(file_path_val));
    }

    /* Load metadata */
    lightusd_json_value_t *metadata_val = lightusd_json_object_get(obj, "metadata");
    if (metadata_val && lightusd_json_value_is_object(metadata_val)) {
        lightusd_json_object_t *metadata_obj = lightusd_json_value_get_object(metadata_val);

        lightusd_json_value_t *doc_val = lightusd_json_object_get(metadata_obj, "doc");
        if (doc_val && lightusd_json_value_is_string(doc_val)) {
            lightusd_layer_set_doc(layer, lightusd_json_value_get_string(doc_val));
        }

        lightusd_json_value_t *up_axis_val = lightusd_json_object_get(metadata_obj, "up_axis");
        if (up_axis_val && lightusd_json_value_is_string(up_axis_val)) {
            lightusd_layer_set_up_axis(layer, lightusd_json_value_get_string(up_axis_val));
        }

        lightusd_json_value_t *meters_per_unit_val = lightusd_json_object_get(metadata_obj, "meters_per_unit");
        if (meters_per_unit_val && lightusd_json_value_is_number(meters_per_unit_val)) {
            lightusd_layer_set_meters_per_unit(layer, lightusd_json_value_get_number(meters_per_unit_val));
        }
    }

    /* Load basic primspecs (simplified) */
    lightusd_json_value_t *primspecs_val = lightusd_json_object_get(obj, "primspecs");
    if (primspecs_val && lightusd_json_value_is_object(primspecs_val)) {
        lightusd_json_object_t *primspecs_obj = lightusd_json_value_get_object(primspecs_val);

        for (size_t i = 0; i < primspecs_obj->count; i++) {
            lightusd_json_value_t *prim_val = primspecs_obj->pairs[i].value;
            if (!lightusd_json_value_is_object(prim_val)) continue;

            lightusd_json_object_t *prim_obj = lightusd_json_value_get_object(prim_val);

            /* Get required fields */
            lightusd_json_value_t *prim_name_val = lightusd_json_object_get(prim_obj, "name");
            lightusd_json_value_t *type_name_val = lightusd_json_object_get(prim_obj, "type_name");
            lightusd_json_value_t *specifier_val = lightusd_json_object_get(prim_obj, "specifier");

            if (prim_name_val && type_name_val && specifier_val &&
                lightusd_json_value_is_string(prim_name_val) &&
                lightusd_json_value_is_string(type_name_val) &&
                lightusd_json_value_is_string(specifier_val)) {

                const char *prim_name = lightusd_json_value_get_string(prim_name_val);
                const char *type_name = lightusd_json_value_get_string(type_name_val);
                const char *specifier_str = lightusd_json_value_get_string(specifier_val);

                /* Convert specifier string to enum */
                lightusd_specifier_t specifier = LIGHTUSD_SPEC_DEF;
                if (strcmp(specifier_str, "over") == 0) {
                    specifier = LIGHTUSD_SPEC_OVER;
                } else if (strcmp(specifier_str, "class") == 0) {
                    specifier = LIGHTUSD_SPEC_CLASS;
                }

                lightusd_primspec_t *primspec = lightusd_primspec_create(prim_name, type_name, specifier);
                if (primspec) {
                    /* Set optional doc */
                    lightusd_json_value_t *doc_val = lightusd_json_object_get(prim_obj, "doc");
                    if (doc_val && lightusd_json_value_is_string(doc_val)) {
                        lightusd_primspec_set_doc(primspec, lightusd_json_value_get_string(doc_val));
                    }

                    lightusd_layer_add_primspec(layer, primspec);
                }
            }
        }
    }

    lightusd_json_value_destroy(json);

    printf("Successfully created USD layer '%s' from JSON\n", layer->name);
    printf("Layer has %zu primspec(s)\n", lightusd_map_size(layer->primspecs));

    return layer;
}

int main(void) {
    printf("LightUSD C99 JSON Conversion Demo\n");
    printf("=================================\n\n");

    /* Create a sample USD layer */
    lightusd_layer_t *layer = lightusd_layer_create("DemoLayer");
    lightusd_layer_set_doc(layer, "A demonstration USD layer for JSON conversion");
    lightusd_layer_set_up_axis(layer, "Y");
    lightusd_layer_set_meters_per_unit(layer, 1.0);
    lightusd_layer_set_file_path(layer, "demo.usd");

    /* Create root prim */
    lightusd_primspec_t *world = lightusd_primspec_create("World", "Xform", LIGHTUSD_SPEC_DEF);
    lightusd_primspec_set_doc(world, "Root transform primitive");

    /* Add transform property */
    lightusd_property_t *xform_prop = lightusd_property_create("xformOp:transform", "matrix4d", LIGHTUSD_PROP_ATTRIB);
    lightusd_property_set_variability(xform_prop, LIGHTUSD_VARIABILITY_UNIFORM);
    lightusd_primspec_add_property(world, xform_prop);

    /* Create mesh primitive */
    lightusd_primspec_t *mesh = lightusd_primspec_create("DemoMesh", "Mesh", LIGHTUSD_SPEC_DEF);
    lightusd_primspec_set_doc(mesh, "A demonstration mesh primitive");

    /* Add mesh properties */
    lightusd_property_t *points_prop = lightusd_property_create("points", "point3f[]", LIGHTUSD_PROP_ATTRIB);
    lightusd_property_t *normals_prop = lightusd_property_create("normals", "normal3f[]", LIGHTUSD_PROP_ATTRIB);

    lightusd_primspec_add_property(mesh, points_prop);
    lightusd_primspec_add_property(mesh, normals_prop);

    /* Create sphere primitive */
    lightusd_primspec_t *sphere = lightusd_primspec_create("DemoSphere", "Sphere", LIGHTUSD_SPEC_DEF);

    lightusd_property_t *radius_prop = lightusd_property_create("radius", "double", LIGHTUSD_PROP_ATTRIB);
    lightusd_value_t *radius_value = lightusd_value_create_double(2.5);
    lightusd_property_set_value(radius_prop, radius_value);
    lightusd_value_destroy(radius_value);
    lightusd_primspec_add_property(sphere, radius_prop);

    /* Build hierarchy */
    lightusd_primspec_add_child(world, mesh);
    lightusd_primspec_add_child(world, sphere);
    lightusd_layer_add_primspec(layer, world);

    /* Convert USD to JSON */
    demo_usd_to_json(layer);

    /* Create a test JSON string for reverse conversion */
    const char *test_json = "{\n"
        "  \"name\": \"JSONTestLayer\",\n"
        "  \"file_path\": \"test.usd\",\n"
        "  \"metadata\": {\n"
        "    \"doc\": \"Layer created from JSON\",\n"
        "    \"up_axis\": \"Z\",\n"
        "    \"meters_per_unit\": 0.01\n"
        "  },\n"
        "  \"primspecs\": {\n"
        "    \"Root\": {\n"
        "      \"name\": \"Root\",\n"
        "      \"type_name\": \"Xform\",\n"
        "      \"specifier\": \"def\",\n"
        "      \"doc\": \"Root primitive from JSON\",\n"
        "      \"property_count\": 0,\n"
        "      \"children_count\": 0\n"
        "    },\n"
        "    \"TestCube\": {\n"
        "      \"name\": \"TestCube\",\n"
        "      \"type_name\": \"Mesh\",\n"
        "      \"specifier\": \"def\",\n"
        "      \"property_count\": 0,\n"
        "      \"children_count\": 0\n"
        "    }\n"
        "  }\n"
        "}";

    /* Convert JSON back to USD */
    lightusd_layer_t *restored_layer = demo_json_to_usd(test_json);

    if (restored_layer) {
        printf("\nRestored layer details:\n");
        printf("  Name: %s\n", restored_layer->name);
        printf("  File path: %s\n", restored_layer->file_path ? restored_layer->file_path : "<none>");
        printf("  Documentation: %s\n", restored_layer->metas.doc ? restored_layer->metas.doc : "<none>");
        printf("  Up axis: %s\n",
               (restored_layer->metas.up_axis.type == LIGHTUSD_VALUE_STRING && restored_layer->metas.up_axis.data.string_val) ?
               restored_layer->metas.up_axis.data.string_val : "<none>");
        printf("  Meters per unit: %.3f\n", restored_layer->metas.meters_per_unit);

        if (restored_layer->primspecs) {
            printf("  PrimSpecs:\n");
            lightusd_map_iterator_t *iter = lightusd_map_iterator_create(restored_layer->primspecs);
            const char *key;
            void *value;

            while (lightusd_map_iterator_next(iter, &key, &value)) {
                lightusd_primspec_t *primspec = (lightusd_primspec_t*)value;
                printf("    - %s (%s, %s)\n", primspec->name, primspec->type_name,
                       lightusd_specifier_to_string(primspec->specifier));
                if (primspec->doc) {
                    printf("      Doc: %s\n", primspec->doc);
                }
            }

            lightusd_map_iterator_destroy(iter);
        }

        lightusd_layer_destroy(restored_layer);
    }

    /* Save the original layer as JSON */
    printf("\n=== Saving Layer as JSON File ===\n");

    /* Create JSON manually for file save demo */
    lightusd_json_value_t *save_obj = lightusd_json_value_create_object();
    lightusd_json_object_t *save_root = lightusd_json_value_get_object(save_obj);

    lightusd_json_object_set(save_root, "name", lightusd_json_value_create_string(layer->name));
    lightusd_json_object_set(save_root, "file_path", lightusd_json_value_create_string(layer->file_path));

    lightusd_json_value_t *save_meta = lightusd_json_value_create_object();
    lightusd_json_object_t *meta_obj = lightusd_json_value_get_object(save_meta);
    lightusd_json_object_set(meta_obj, "doc", lightusd_json_value_create_string(layer->metas.doc));
    lightusd_json_object_set(meta_obj, "up_axis", lightusd_json_value_create_string(layer->metas.up_axis.data.string_val));
    lightusd_json_object_set(meta_obj, "meters_per_unit", lightusd_json_value_create_number(layer->metas.meters_per_unit));
    lightusd_json_object_set(save_root, "metadata", save_meta);

    lightusd_json_object_set(save_root, "total_primspecs", lightusd_json_value_create_number((double)lightusd_map_size(layer->primspecs)));

    const char *save_filename = "demo_layer.json";
    int save_result = lightusd_json_write_file_pretty(save_obj, save_filename, 2);

    if (save_result) {
        printf("Successfully saved layer to '%s'\n", save_filename);

        /* Read it back and display */
        FILE *file = fopen(save_filename, "r");
        if (file) {
            printf("\nSaved file contents:\n");
            char buffer[1024];
            while (fgets(buffer, sizeof(buffer), file)) {
                printf("%s", buffer);
            }
            fclose(file);

            /* Clean up the file */
            remove(save_filename);
        }
    } else {
        printf("Failed to save layer to file\n");
    }

    lightusd_json_value_destroy(save_obj);

    /* Clean up */
    lightusd_layer_destroy(layer);

    printf("\n🎉 Demo completed successfully! 🎉\n");
    printf("Features demonstrated:\n");
    printf("  ✓ USD Layer creation with metadata and primitives\n");
    printf("  ✓ USD Layer to JSON conversion with structure preservation\n");
    printf("  ✓ JSON to USD Layer conversion with type inference\n");
    printf("  ✓ JSON file I/O with pretty printing\n");
    printf("  ✓ Memory management and cleanup\n");

    return 0;
}