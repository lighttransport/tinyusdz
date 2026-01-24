#include "usdc_parser.h"
#include <stdio.h>

void print_header_info(usdc_reader_t *reader) {
    printf("=== USDC File Header ===\n");
    printf("Magic: %.8s\n", reader->header.magic);
    printf("Version: %d.%d.%d\n", 
           reader->header.version[0],
           reader->header.version[1], 
           reader->header.version[2]);
    printf("TOC Offset: %llu\n", (unsigned long long)reader->header.toc_offset);
    printf("\n");
}

void print_toc_info(usdc_reader_t *reader) {
    printf("=== Table of Contents ===\n");
    printf("Number of sections: %llu\n", (unsigned long long)reader->toc.num_sections);
    printf("\nSections:\n");
    for (uint64_t i = 0; i < reader->toc.num_sections; i++) {
        usdc_section_t *section = &reader->toc.sections[i];
        printf("  [%llu] Name: %-15s Start: %10llu Size: %10llu\n",
               (unsigned long long)i,
               section->name,
               (unsigned long long)section->start,
               (unsigned long long)section->size);
    }
    printf("\n");
}

void print_tokens_info(usdc_reader_t *reader) {
    printf("=== Tokens ===\n");
    printf("Number of tokens: %zu\n", reader->num_tokens);
    if (reader->num_tokens > 0) {
        printf("First 10 tokens:\n");
        size_t max_tokens = (reader->num_tokens < 10) ? reader->num_tokens : 10;
        for (size_t i = 0; i < max_tokens; i++) {
            usdc_token_t *token = &reader->tokens[i];
            if (token->str) {
                printf("  [%zu] \"%s\" (len: %zu)\n", i, token->str, token->length);
            } else {
                printf("  [%zu] <NULL> (len: %zu)\n", i, token->length);
            }
        }
        if (reader->num_tokens > 10) {
            printf("  ... (%zu more tokens)\n", reader->num_tokens - 10);
        }
    }
    printf("\n");
}

void print_strings_info(usdc_reader_t *reader) {
    printf("=== String Indices ===\n");
    printf("Number of string indices: %zu\n", reader->num_string_indices);
    if (reader->num_string_indices > 0) {
        printf("First 10 string indices:\n");
        size_t max_strings = (reader->num_string_indices < 10) ? reader->num_string_indices : 10;
        for (size_t i = 0; i < max_strings; i++) {
            usdc_index_t *index = &reader->string_indices[i];
            printf("  [%zu] -> token[%u]", i, index->value);
            if (index->value < reader->num_tokens && reader->tokens[index->value].str) {
                printf(" \"%s\"", reader->tokens[index->value].str);
            }
            printf("\n");
        }
        if (reader->num_string_indices > 10) {
            printf("  ... (%zu more string indices)\n", reader->num_string_indices - 10);
        }
    }
    printf("\n");
}

void print_fields_info(usdc_reader_t *reader) {
    printf("=== Fields ===\n");
    printf("Number of fields: %zu\n", reader->num_fields);
    if (reader->num_fields > 0) {
        printf("First 10 fields:\n");
        size_t max_fields = (reader->num_fields < 10) ? reader->num_fields : 10;
        for (size_t i = 0; i < max_fields; i++) {
            usdc_field_t *field = &reader->fields[i];
            printf("  [%zu] token[%u]", i, field->token_index.value);
            if (field->token_index.value < reader->num_tokens && 
                reader->tokens[field->token_index.value].str) {
                printf(" \"%s\"", reader->tokens[field->token_index.value].str);
            }
            printf(" value_rep=0x%016llx", (unsigned long long)field->value_rep.data);
            
            /* Try to parse the value */
            usdc_parsed_value_t parsed_value;
            if (usdc_parse_value_rep(reader, field->value_rep, &parsed_value)) {
                printf(" ");
                usdc_print_parsed_value(reader, &parsed_value);
                usdc_cleanup_parsed_value(&parsed_value);
            } else {
                /* Fallback to raw info */
                printf(" (type=%u", usdc_get_type_id(field->value_rep));
                if (usdc_is_array(field->value_rep)) printf(" ARRAY");
                if (usdc_is_inlined(field->value_rep)) printf(" INLINED");
                if (usdc_is_compressed(field->value_rep)) printf(" COMPRESSED");
                printf(" payload=0x%llx)", (unsigned long long)usdc_get_payload(field->value_rep));
            }
            printf("\n");
        }
        if (reader->num_fields > 10) {
            printf("  ... (%zu more fields)\n", reader->num_fields - 10);
        }
    }
    printf("\n");
}

void print_paths_info(usdc_reader_t *reader) {
    printf("=== Paths ===\n");
    printf("Number of paths: %zu\n", reader->num_paths);
    if (reader->num_paths > 0) {
        printf("First 10 paths:\n");
        size_t max_paths = (reader->num_paths < 10) ? reader->num_paths : 10;
        for (size_t i = 0; i < max_paths; i++) {
            usdc_path_t *path = &reader->paths[i];
            printf("  [%zu] \"%s\" (len: %zu, %s)\n", 
                   i, 
                   path->path_string ? path->path_string : "<NULL>",
                   path->length,
                   path->is_absolute ? "absolute" : "relative");
        }
        if (reader->num_paths > 10) {
            printf("  ... (%zu more paths)\n", reader->num_paths - 10);
        }
    }
    printf("\n");
}

void print_specs_info(usdc_reader_t *reader) {
    printf("=== Specs ===\n");
    printf("Number of specs: %zu\n", reader->num_specs);
    if (reader->num_specs > 0) {
        printf("First 10 specs:\n");
        size_t max_specs = (reader->num_specs < 10) ? reader->num_specs : 10;
        for (size_t i = 0; i < max_specs; i++) {
            usdc_spec_t *spec = &reader->specs[i];
            printf("  [%zu] path[%u]", i, spec->path_index.value);
            
            /* Try to resolve path name */
            if (spec->path_index.value < reader->num_paths && 
                reader->paths[spec->path_index.value].path_string) {
                printf(" \"%s\"", reader->paths[spec->path_index.value].path_string);
            }
            
            printf(" fieldset[%u] type=%s\n", 
                   spec->fieldset_index.value,
                   usdc_get_spec_type_name(spec->spec_type));
        }
        if (reader->num_specs > 10) {
            printf("  ... (%zu more specs)\n", reader->num_specs - 10);
        }
    }
    printf("\n");
}

void print_fieldsets_info(usdc_reader_t *reader) {
    printf("=== FieldSets ===\n");
    printf("Number of fieldsets: %zu\n", reader->num_fieldsets);
    if (reader->num_fieldsets > 0) {
        printf("First 10 fieldsets:\n");
        size_t max_fieldsets = (reader->num_fieldsets < 10) ? reader->num_fieldsets : 10;
        for (size_t i = 0; i < max_fieldsets; i++) {
            usdc_fieldset_t *fieldset = &reader->fieldsets[i];
            printf("  [%zu] %zu field indices: [", i, fieldset->num_field_indices);
            
            size_t max_indices = (fieldset->num_field_indices < 5) ? fieldset->num_field_indices : 5;
            for (size_t j = 0; j < max_indices; j++) {
                if (j > 0) printf(", ");
                printf("%u", fieldset->field_indices[j].value);
            }
            if (fieldset->num_field_indices > max_indices) {
                printf(", ...");
            }
            printf("]\n");
        }
        if (reader->num_fieldsets > 10) {
            printf("  ... (%zu more fieldsets)\n", reader->num_fieldsets - 10);
        }
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <usdc_file>\n", argv[0]);
        return 1;
    }
    
    const char *filename = argv[1];
    usdc_reader_t reader;
    
    printf("Testing USDC parser with file: %s\n\n", filename);
    
    /* Initialize reader */
    if (!usdc_reader_init(&reader, filename)) {
        printf("Failed to initialize reader: %s\n", usdc_reader_get_error(&reader));
        return 1;
    }
    
    printf("File size: %zu bytes\n\n", reader.file_size);
    
    /* Read file */
    if (!usdc_reader_read_file(&reader)) {
        printf("Failed to read USDC file: %s\n", usdc_reader_get_error(&reader));
        const char *warning = usdc_reader_get_warning(&reader);
        if (warning && strlen(warning) > 0) {
            printf("Warnings: %s\n", warning);
        }
        usdc_reader_cleanup(&reader);
        return 1;
    }
    
    /* Print information */
    print_header_info(&reader);
    print_toc_info(&reader);
    print_tokens_info(&reader);
    print_strings_info(&reader);
    print_fields_info(&reader);
    print_paths_info(&reader);
    print_specs_info(&reader);
    print_fieldsets_info(&reader);
    
    /* Print hierarchical paths if available */
    if (reader.hierarchical_paths && reader.num_hierarchical_paths > 0) {
        usdc_print_hierarchical_paths(&reader);
    }
    
    printf("Memory used: %zu bytes\n", reader.memory_used);
    
    const char *warning = usdc_reader_get_warning(&reader);
    if (warning && strlen(warning) > 0) {
        printf("Warnings: %s\n", warning);
    }
    
    printf("\nUSDC file parsing completed successfully!\n");
    
    /* Cleanup */
    usdc_reader_cleanup(&reader);
    
    return 0;
}