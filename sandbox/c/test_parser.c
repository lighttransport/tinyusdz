#include "usda_parser.h"
#include <stdio.h>
#include <string.h>

static void print_value(const usd_value_t *value, int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
    
    switch (value->type) {
        case USD_VALUE_STRING:
            printf("\"%s\"", value->data.string_val ? value->data.string_val : "");
            break;
        case USD_VALUE_INT:
            printf("%d", value->data.int_val);
            break;
        case USD_VALUE_FLOAT:
            printf("%f", value->data.float_val);
            break;
        case USD_VALUE_BOOL:
            printf("%s", value->data.bool_val ? "true" : "false");
            break;
        case USD_VALUE_ARRAY:
            printf("[\n");
            for (size_t i = 0; i < value->data.array_val.count; i++) {
                print_value(&value->data.array_val.elements[i], indent + 1);
                if (i < value->data.array_val.count - 1) printf(",");
                printf("\n");
            }
            for (int i = 0; i < indent; i++) printf("  ");
            printf("]");
            break;
        case USD_VALUE_NONE:
        default:
            printf("(none)");
            break;
    }
}

static void print_attributes(const usd_attribute_t *attr, int indent) {
    while (attr) {
        for (int i = 0; i < indent; i++) printf("  ");
        printf("%s %s = ", 
               attr->type_name ? attr->type_name : "(no-type)",
               attr->name ? attr->name : "(no-name)");
        print_value(&attr->value, 0);
        printf("\n");
        attr = attr->next;
    }
}

static void print_prim(const usd_prim_t *prim, int indent) {
    while (prim) {
        for (int i = 0; i < indent; i++) printf("  ");
        printf("def %s \"%s\" {\n", 
               prim->type_name ? prim->type_name : "",
               prim->name ? prim->name : "(no-name)");
        
        if (prim->attributes) {
            print_attributes(prim->attributes, indent + 1);
        }
        
        if (prim->children) {
            print_prim(prim->children, indent + 1);
        }
        
        for (int i = 0; i < indent; i++) printf("  ");
        printf("}\n");
        
        prim = prim->next;
    }
}

static void print_stage(const usd_stage_t *stage) {
    printf("USD Stage:\n");
    printf("  Default Prim: %s\n", stage->default_prim ? stage->default_prim : "(none)");
    printf("  Up Axis: (%.1f, %.1f, %.1f)\n", 
           stage->up_axis[0], stage->up_axis[1], stage->up_axis[2]);
    printf("  Meters Per Unit: %f\n", stage->meters_per_unit);
    printf("  Root Prims:\n");
    
    if (stage->root_prims) {
        print_prim(stage->root_prims, 1);
    } else {
        printf("    (none)\n");
    }
}

int main(int argc, char **argv) {
    const char *filename = NULL;
    
    if (argc > 1) {
        filename = argv[1];
    } else {
        printf("Usage: %s <usda_file>\n", argv[0]);
        printf("Testing with simple embedded example...\n\n");
    }
    
    const char *test_usda = 
        "#usda 1.0\n"
        "\n"
        "def Xform \"World\" {\n"
        "    double3 xformOp:translate = (0, 0, 0)\n"
        "    uniform token[] xformOpOrder = [\"xformOp:translate\"]\n"
        "\n"
        "    def Sphere \"MySphere\" {\n"
        "        float radius = 1.0\n"
        "        color3f[] primvars:displayColor = [(0.8, 0.2, 0.1)]\n"
        "    }\n"
        "}\n";
    
    usda_parser_t parser;
    int success = 0;
    
    if (filename) {
        FILE *file = fopen(filename, "rb");
        if (!file) {
            printf("Error: Cannot open file '%s'\n", filename);
            return 1;
        }
        
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        
        char *content = malloc(size + 1);
        if (!content) {
            printf("Error: Cannot allocate memory\n");
            fclose(file);
            return 1;
        }
        
        size_t bytes_read = fread(content, 1, size, file);
        if (bytes_read != (size_t)size) {
            printf("Warning: Only read %zu of %ld bytes\n", bytes_read, size);
        }
        content[size] = '\0';
        fclose(file);
        
        printf("Parsing file: %s (%ld bytes)\n\n", filename, size);
        
        if (usda_parser_init(&parser, content, size)) {
            success = usda_parser_parse(&parser);
            if (!success) {
                printf("Parse error: %s\n", usda_parser_get_error(&parser));
            }
        }
        
        free(content);
    } else {
        printf("Parsing embedded test data:\n");
        printf("%s\n", test_usda);
        printf("---\n\n");
        
        if (usda_parser_init(&parser, test_usda, strlen(test_usda))) {
            success = usda_parser_parse(&parser);
            if (!success) {
                printf("Parse error: %s\n", usda_parser_get_error(&parser));
            }
        }
    }
    
    if (success) {
        printf("Parse successful!\n\n");
        print_stage(&parser.stage);
    } else {
        printf("Parse failed.\n");
    }
    
    usda_parser_cleanup(&parser);
    
    return success ? 0 : 1;
}