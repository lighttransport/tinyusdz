#ifndef USDA_PARSER_H
#define USDA_PARSER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TOKEN_EOF = 0,
    TOKEN_IDENTIFIER,
    TOKEN_STRING,
    TOKEN_NUMBER,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_SEMICOLON,
    TOKEN_COLON,
    TOKEN_COMMA,
    TOKEN_EQUALS,
    TOKEN_AT,
    TOKEN_HASH,
    TOKEN_DEF,
    TOKEN_CLASS,
    TOKEN_OVER,
    TOKEN_UNKNOWN
} token_type_t;

typedef struct {
    token_type_t type;
    char *text;
    size_t length;
    int line;
    int column;
} token_t;

typedef struct {
    const char *input;
    size_t length;
    size_t position;
    int line;
    int column;
    token_t current_token;
} lexer_t;

typedef struct usd_value {
    enum {
        USD_VALUE_NONE,
        USD_VALUE_STRING,
        USD_VALUE_INT,
        USD_VALUE_FLOAT,
        USD_VALUE_BOOL,
        USD_VALUE_ARRAY
    } type;
    union {
        char *string_val;
        int int_val;
        float float_val;
        int bool_val;
        struct {
            struct usd_value *elements;
            size_t count;
        } array_val;
    } data;
} usd_value_t;

typedef struct usd_attribute {
    char *name;
    char *type_name;
    usd_value_t value;
    struct usd_attribute *next;
} usd_attribute_t;

typedef struct usd_prim {
    char *name;
    char *type_name;
    usd_attribute_t *attributes;
    struct usd_prim *children;
    struct usd_prim *next;
} usd_prim_t;

typedef struct {
    usd_prim_t *root_prims;
    char *default_prim;
    float up_axis[3];
    float meters_per_unit;
} usd_stage_t;

typedef struct {
    lexer_t lexer;
    usd_stage_t stage;
    char *error_message;
} usda_parser_t;

int usda_parser_init(usda_parser_t *parser, const char *input, size_t length);
void usda_parser_cleanup(usda_parser_t *parser);
int usda_parser_parse(usda_parser_t *parser);
const char* usda_parser_get_error(usda_parser_t *parser);

void lexer_init(lexer_t *lexer, const char *input, size_t length);
int lexer_next_token(lexer_t *lexer);
void token_cleanup(token_t *token);

void usd_value_cleanup(usd_value_t *value);
void usd_attribute_cleanup(usd_attribute_t *attr);
void usd_prim_cleanup(usd_prim_t *prim);
void usd_stage_cleanup(usd_stage_t *stage);

#ifdef __cplusplus
}
#endif

#endif