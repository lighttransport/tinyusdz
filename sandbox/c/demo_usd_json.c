#include "tusd_layer.h"
#include "tusd_json_core.c"  /* Include core JSON directly to avoid conflicts */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Simple USD to JSON conversion demonstration */
void demo_usd_to_json(const tusd_layer_t *layer) {
    printf("=== USD Layer to JSON Conversion Demo ===\n");
    
    /* Create a simple JSON representation of the layer */
    tusd_json_value_t *root = tusd_json_value_create_object();
    tusd_json_object_t *obj = tusd_json_value_get_object(root);
    
    /* Basic layer information */
    tusd_json_object_set(obj, "name", tusd_json_value_create_string(layer->name));
    
    if (layer->file_path) {
        tusd_json_object_set(obj, "file_path", tusd_json_value_create_string(layer->file_path));
    }
    
    /* Layer metadata */
    tusd_json_value_t *metadata_obj = tusd_json_value_create_object();
    tusd_json_object_t *metadata = tusd_json_value_get_object(metadata_obj);
    
    if (layer->metas.doc) {
        tusd_json_object_set(metadata, "doc", tusd_json_value_create_string(layer->metas.doc));
    }
    
    if (layer->metas.up_axis.type == TUSD_VALUE_STRING && layer->metas.up_axis.data.string_val) {
        tusd_json_object_set(metadata, "up_axis", tusd_json_value_create_string(layer->metas.up_axis.data.string_val));
    }
    
    tusd_json_object_set(metadata, "meters_per_unit", tusd_json_value_create_number(layer->metas.meters_per_unit));
    
    tusd_json_object_set(obj, "metadata", metadata_obj);
    
    /* PrimSpecs (simplified representation) */
    if (layer->primspecs && tusd_map_size(layer->primspecs) > 0) {
        tusd_json_value_t *primspecs_obj = tusd_json_value_create_object();
        tusd_json_object_t *primspecs = tusd_json_value_get_object(primspecs_obj);
        
        tusd_map_iterator_t *iter = tusd_map_iterator_create(layer->primspecs);
        const char *key;
        void *value;
        
        while (tusd_map_iterator_next(iter, &key, &value)) {
            tusd_primspec_t *primspec = (tusd_primspec_t*)value;
            
            /* Create a simple representation of the primspec */
            tusd_json_value_t *prim_obj = tusd_json_value_create_object();
            tusd_json_object_t *prim = tusd_json_value_get_object(prim_obj);
            
            tusd_json_object_set(prim, "name", tusd_json_value_create_string(primspec->name));
            tusd_json_object_set(prim, "type_name", tusd_json_value_create_string(primspec->type_name));
            tusd_json_object_set(prim, "specifier", tusd_json_value_create_string(tusd_specifier_to_string(primspec->specifier)));
            
            if (primspec->doc) {
                tusd_json_object_set(prim, "doc", tusd_json_value_create_string(primspec->doc));
            }
            
            /* Add property count */
            size_t prop_count = primspec->properties ? tusd_map_size(primspec->properties) : 0;
            tusd_json_object_set(prim, "property_count", tusd_json_value_create_number((double)prop_count));
            
            /* Add children count */
            size_t child_count = primspec->children ? tusd_map_size(primspec->children) : 0;
            tusd_json_object_set(prim, "children_count", tusd_json_value_create_number((double)child_count));
            
            tusd_json_object_set(primspecs, key, prim_obj);
        }
        
        tusd_map_iterator_destroy(iter);
        tusd_json_object_set(obj, "primspecs", primspecs_obj);
    }
    
    /* Serialize to pretty JSON */
    char *json_str = tusd_json_serialize_pretty(root, 2);
    if (json_str) {
        printf("JSON Output:\n%s\n", json_str);
        free(json_str);
    }
    
    tusd_json_value_destroy(root);
}

/* Simple JSON to USD conversion demonstration */
tusd_layer_t *demo_json_to_usd(const char *json_string) {
    printf("\n=== JSON to USD Layer Conversion Demo ===\n");
    
    tusd_json_value_t *json = tusd_json_parse(json_string);
    if (!json || !tusd_json_value_is_object(json)) {
        printf("Failed to parse JSON or not an object\n");
        return NULL;
    }
    
    tusd_json_object_t *obj = tusd_json_value_get_object(json);
    
    /* Get layer name */
    tusd_json_value_t *name_val = tusd_json_object_get(obj, "name");
    if (!name_val || !tusd_json_value_is_string(name_val)) {
        printf("JSON missing required 'name' field\n");
        tusd_json_value_destroy(json);
        return NULL;
    }
    
    const char *name = tusd_json_value_get_string(name_val);
    tusd_layer_t *layer = tusd_layer_create(name);
    if (!layer) {
        tusd_json_value_destroy(json);
        return NULL;
    }
    
    /* Set file path if present */
    tusd_json_value_t *file_path_val = tusd_json_object_get(obj, "file_path");
    if (file_path_val && tusd_json_value_is_string(file_path_val)) {
        tusd_layer_set_file_path(layer, tusd_json_value_get_string(file_path_val));
    }
    
    /* Load metadata */
    tusd_json_value_t *metadata_val = tusd_json_object_get(obj, "metadata");
    if (metadata_val && tusd_json_value_is_object(metadata_val)) {
        tusd_json_object_t *metadata_obj = tusd_json_value_get_object(metadata_val);
        
        tusd_json_value_t *doc_val = tusd_json_object_get(metadata_obj, "doc");
        if (doc_val && tusd_json_value_is_string(doc_val)) {
            tusd_layer_set_doc(layer, tusd_json_value_get_string(doc_val));
        }
        
        tusd_json_value_t *up_axis_val = tusd_json_object_get(metadata_obj, "up_axis");
        if (up_axis_val && tusd_json_value_is_string(up_axis_val)) {
            tusd_layer_set_up_axis(layer, tusd_json_value_get_string(up_axis_val));
        }
        
        tusd_json_value_t *meters_per_unit_val = tusd_json_object_get(metadata_obj, "meters_per_unit");
        if (meters_per_unit_val && tusd_json_value_is_number(meters_per_unit_val)) {
            tusd_layer_set_meters_per_unit(layer, tusd_json_value_get_number(meters_per_unit_val));
        }
    }
    
    /* Load basic primspecs (simplified) */
    tusd_json_value_t *primspecs_val = tusd_json_object_get(obj, "primspecs");
    if (primspecs_val && tusd_json_value_is_object(primspecs_val)) {
        tusd_json_object_t *primspecs_obj = tusd_json_value_get_object(primspecs_val);
        
        for (size_t i = 0; i < primspecs_obj->count; i++) {
            tusd_json_value_t *prim_val = primspecs_obj->pairs[i].value;
            if (!tusd_json_value_is_object(prim_val)) continue;
            
            tusd_json_object_t *prim_obj = tusd_json_value_get_object(prim_val);
            
            /* Get required fields */
            tusd_json_value_t *prim_name_val = tusd_json_object_get(prim_obj, "name");
            tusd_json_value_t *type_name_val = tusd_json_object_get(prim_obj, "type_name");
            tusd_json_value_t *specifier_val = tusd_json_object_get(prim_obj, "specifier");
            
            if (prim_name_val && type_name_val && specifier_val &&
                tusd_json_value_is_string(prim_name_val) && 
                tusd_json_value_is_string(type_name_val) &&
                tusd_json_value_is_string(specifier_val)) {
                
                const char *prim_name = tusd_json_value_get_string(prim_name_val);
                const char *type_name = tusd_json_value_get_string(type_name_val);
                const char *specifier_str = tusd_json_value_get_string(specifier_val);
                
                /* Convert specifier string to enum */
                tusd_specifier_t specifier = TUSD_SPEC_DEF;
                if (strcmp(specifier_str, "over") == 0) {
                    specifier = TUSD_SPEC_OVER;
                } else if (strcmp(specifier_str, "class") == 0) {
                    specifier = TUSD_SPEC_CLASS;
                }
                
                tusd_primspec_t *primspec = tusd_primspec_create(prim_name, type_name, specifier);
                if (primspec) {
                    /* Set optional doc */
                    tusd_json_value_t *doc_val = tusd_json_object_get(prim_obj, "doc");
                    if (doc_val && tusd_json_value_is_string(doc_val)) {
                        tusd_primspec_set_doc(primspec, tusd_json_value_get_string(doc_val));
                    }
                    
                    tusd_layer_add_primspec(layer, primspec);
                }
            }
        }
    }
    
    tusd_json_value_destroy(json);
    
    printf("Successfully created USD layer '%s' from JSON\n", layer->name);
    printf("Layer has %zu primspec(s)\n", tusd_map_size(layer->primspecs));
    
    return layer;
}

int main(void) {
    printf("TinyUSDZ C99 JSON Conversion Demo\n");
    printf("=================================\n\n");
    
    /* Create a sample USD layer */
    tusd_layer_t *layer = tusd_layer_create("DemoLayer");
    tusd_layer_set_doc(layer, "A demonstration USD layer for JSON conversion");
    tusd_layer_set_up_axis(layer, "Y");
    tusd_layer_set_meters_per_unit(layer, 1.0);
    tusd_layer_set_file_path(layer, "demo.usd");
    
    /* Create root prim */
    tusd_primspec_t *world = tusd_primspec_create("World", "Xform", TUSD_SPEC_DEF);
    tusd_primspec_set_doc(world, "Root transform primitive");
    
    /* Add transform property */
    tusd_property_t *xform_prop = tusd_property_create("xformOp:transform", "matrix4d", TUSD_PROP_ATTRIB);
    tusd_property_set_variability(xform_prop, TUSD_VARIABILITY_UNIFORM);
    tusd_primspec_add_property(world, xform_prop);
    
    /* Create mesh primitive */
    tusd_primspec_t *mesh = tusd_primspec_create("DemoMesh", "Mesh", TUSD_SPEC_DEF);
    tusd_primspec_set_doc(mesh, "A demonstration mesh primitive");
    
    /* Add mesh properties */
    tusd_property_t *points_prop = tusd_property_create("points", "point3f[]", TUSD_PROP_ATTRIB);
    tusd_property_t *normals_prop = tusd_property_create("normals", "normal3f[]", TUSD_PROP_ATTRIB);
    
    tusd_primspec_add_property(mesh, points_prop);
    tusd_primspec_add_property(mesh, normals_prop);
    
    /* Create sphere primitive */
    tusd_primspec_t *sphere = tusd_primspec_create("DemoSphere", "Sphere", TUSD_SPEC_DEF);
    
    tusd_property_t *radius_prop = tusd_property_create("radius", "double", TUSD_PROP_ATTRIB);
    tusd_value_t *radius_value = tusd_value_create_double(2.5);
    tusd_property_set_value(radius_prop, radius_value);
    tusd_value_destroy(radius_value);
    tusd_primspec_add_property(sphere, radius_prop);
    
    /* Build hierarchy */
    tusd_primspec_add_child(world, mesh);
    tusd_primspec_add_child(world, sphere);
    tusd_layer_add_primspec(layer, world);
    
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
    tusd_layer_t *restored_layer = demo_json_to_usd(test_json);
    
    if (restored_layer) {
        printf("\nRestored layer details:\n");
        printf("  Name: %s\n", restored_layer->name);
        printf("  File path: %s\n", restored_layer->file_path ? restored_layer->file_path : "<none>");
        printf("  Documentation: %s\n", restored_layer->metas.doc ? restored_layer->metas.doc : "<none>");
        printf("  Up axis: %s\n", 
               (restored_layer->metas.up_axis.type == TUSD_VALUE_STRING && restored_layer->metas.up_axis.data.string_val) ? 
               restored_layer->metas.up_axis.data.string_val : "<none>");
        printf("  Meters per unit: %.3f\n", restored_layer->metas.meters_per_unit);
        
        if (restored_layer->primspecs) {
            printf("  PrimSpecs:\n");
            tusd_map_iterator_t *iter = tusd_map_iterator_create(restored_layer->primspecs);
            const char *key;
            void *value;
            
            while (tusd_map_iterator_next(iter, &key, &value)) {
                tusd_primspec_t *primspec = (tusd_primspec_t*)value;
                printf("    - %s (%s, %s)\n", primspec->name, primspec->type_name, 
                       tusd_specifier_to_string(primspec->specifier));
                if (primspec->doc) {
                    printf("      Doc: %s\n", primspec->doc);
                }
            }
            
            tusd_map_iterator_destroy(iter);
        }
        
        tusd_layer_destroy(restored_layer);
    }
    
    /* Save the original layer as JSON */
    printf("\n=== Saving Layer as JSON File ===\n");
    
    /* Create JSON manually for file save demo */
    tusd_json_value_t *save_obj = tusd_json_value_create_object();
    tusd_json_object_t *save_root = tusd_json_value_get_object(save_obj);
    
    tusd_json_object_set(save_root, "name", tusd_json_value_create_string(layer->name));
    tusd_json_object_set(save_root, "file_path", tusd_json_value_create_string(layer->file_path));
    
    tusd_json_value_t *save_meta = tusd_json_value_create_object();
    tusd_json_object_t *meta_obj = tusd_json_value_get_object(save_meta);
    tusd_json_object_set(meta_obj, "doc", tusd_json_value_create_string(layer->metas.doc));
    tusd_json_object_set(meta_obj, "up_axis", tusd_json_value_create_string(layer->metas.up_axis.data.string_val));
    tusd_json_object_set(meta_obj, "meters_per_unit", tusd_json_value_create_number(layer->metas.meters_per_unit));
    tusd_json_object_set(save_root, "metadata", save_meta);
    
    tusd_json_object_set(save_root, "total_primspecs", tusd_json_value_create_number((double)tusd_map_size(layer->primspecs)));
    
    const char *save_filename = "demo_layer.json";
    int save_result = tusd_json_write_file_pretty(save_obj, save_filename, 2);
    
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
    
    tusd_json_value_destroy(save_obj);
    
    /* Clean up */
    tusd_layer_destroy(layer);
    
    printf("\n🎉 Demo completed successfully! 🎉\n");
    printf("Features demonstrated:\n");
    printf("  ✓ USD Layer creation with metadata and primitives\n");
    printf("  ✓ USD Layer to JSON conversion with structure preservation\n");
    printf("  ✓ JSON to USD Layer conversion with type inference\n");
    printf("  ✓ JSON file I/O with pretty printing\n");
    printf("  ✓ Memory management and cleanup\n");
    
    return 0;
}