#include "usda_parser.h"
#include <ctype.h>
#include <assert.h>

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_alnum(char c) {
    return is_alpha(c) || (c >= '0' && c <= '9') || c == ':';
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static void skip_whitespace(lexer_t *lexer) {
    while (lexer->position < lexer->length) {
        char c = lexer->input[lexer->position];
        if (c == ' ' || c == '\t' || c == '\r') {
            lexer->position++;
            lexer->column++;
        } else if (c == '\n') {
            lexer->position++;
            lexer->line++;
            lexer->column = 1;
        } else {
            break;
        }
    }
}

static void skip_comment(lexer_t *lexer) {
    if (lexer->position < lexer->length && lexer->input[lexer->position] == '#') {
        while (lexer->position < lexer->length && lexer->input[lexer->position] != '\n') {
            lexer->position++;
        }
    }
}

static char peek_char(lexer_t *lexer) {
    if (lexer->position >= lexer->length) {
        return '\0';
    }
    return lexer->input[lexer->position];
}

static char next_char(lexer_t *lexer) {
    if (lexer->position >= lexer->length) {
        return '\0';
    }
    char c = lexer->input[lexer->position++];
    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    return c;
}

static token_type_t get_keyword_token(const char *text, size_t length) {
    if (length == 3 && strncmp(text, "def", 3) == 0) return TOKEN_DEF;
    if (length == 5 && strncmp(text, "class", 5) == 0) return TOKEN_CLASS;
    if (length == 4 && strncmp(text, "over", 4) == 0) return TOKEN_OVER;
    return TOKEN_IDENTIFIER;
}

static int read_string_literal(lexer_t *lexer, token_t *token) {
    char quote = next_char(lexer);
    size_t start = lexer->position;
    
    while (lexer->position < lexer->length) {
        char c = peek_char(lexer);
        if (c == quote) {
            next_char(lexer);
            break;
        } else if (c == '\\') {
            next_char(lexer);
            if (lexer->position < lexer->length) {
                next_char(lexer);
            }
        } else {
            next_char(lexer);
        }
    }
    
    size_t length = lexer->position - start - 1;
    token->text = malloc(length + 1);
    if (!token->text) return 0;
    
    strncpy(token->text, &lexer->input[start], length);
    token->text[length] = '\0';
    token->length = length;
    token->type = TOKEN_STRING;
    
    return 1;
}

static int read_number(lexer_t *lexer, token_t *token) {
    size_t start = lexer->position;
    
    if (peek_char(lexer) == '-') {
        next_char(lexer);
    }
    
    while (lexer->position < lexer->length && is_digit(peek_char(lexer))) {
        next_char(lexer);
    }
    
    if (peek_char(lexer) == '.') {
        next_char(lexer);
        while (lexer->position < lexer->length && is_digit(peek_char(lexer))) {
            next_char(lexer);
        }
    }
    
    if (peek_char(lexer) == 'e' || peek_char(lexer) == 'E') {
        next_char(lexer);
        if (peek_char(lexer) == '+' || peek_char(lexer) == '-') {
            next_char(lexer);
        }
        while (lexer->position < lexer->length && is_digit(peek_char(lexer))) {
            next_char(lexer);
        }
    }
    
    size_t length = lexer->position - start;
    token->text = malloc(length + 1);
    if (!token->text) return 0;
    
    strncpy(token->text, &lexer->input[start], length);
    token->text[length] = '\0';
    token->length = length;
    token->type = TOKEN_NUMBER;
    
    return 1;
}

static int read_identifier(lexer_t *lexer, token_t *token) {
    size_t start = lexer->position;
    
    while (lexer->position < lexer->length && is_alnum(peek_char(lexer))) {
        next_char(lexer);
    }
    
    size_t length = lexer->position - start;
    token->text = malloc(length + 1);
    if (!token->text) return 0;
    
    strncpy(token->text, &lexer->input[start], length);
    token->text[length] = '\0';
    token->length = length;
    token->type = get_keyword_token(token->text, length);
    
    return 1;
}

void lexer_init(lexer_t *lexer, const char *input, size_t length) {
    lexer->input = input;
    lexer->length = length;
    lexer->position = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->current_token.type = TOKEN_EOF;
    lexer->current_token.text = NULL;
    lexer->current_token.length = 0;
}

int lexer_next_token(lexer_t *lexer) {
    token_cleanup(&lexer->current_token);
    
    while (lexer->position < lexer->length) {
        skip_whitespace(lexer);
        
        if (lexer->position >= lexer->length) {
            break;
        }
        
        if (peek_char(lexer) == '#') {
            skip_comment(lexer);
            continue;
        }
        
        lexer->current_token.line = lexer->line;
        lexer->current_token.column = lexer->column;
        
        char c = peek_char(lexer);
        
        switch (c) {
            case '{': next_char(lexer); lexer->current_token.type = TOKEN_LBRACE; return 1;
            case '}': next_char(lexer); lexer->current_token.type = TOKEN_RBRACE; return 1;
            case '(': next_char(lexer); lexer->current_token.type = TOKEN_LPAREN; return 1;
            case ')': next_char(lexer); lexer->current_token.type = TOKEN_RPAREN; return 1;
            case '[': next_char(lexer); lexer->current_token.type = TOKEN_LBRACKET; return 1;
            case ']': next_char(lexer); lexer->current_token.type = TOKEN_RBRACKET; return 1;
            case ';': next_char(lexer); lexer->current_token.type = TOKEN_SEMICOLON; return 1;
            case ':': next_char(lexer); lexer->current_token.type = TOKEN_COLON; return 1;
            case ',': next_char(lexer); lexer->current_token.type = TOKEN_COMMA; return 1;
            case '=': next_char(lexer); lexer->current_token.type = TOKEN_EQUALS; return 1;
            case '@': next_char(lexer); lexer->current_token.type = TOKEN_AT; return 1;
            case '"':
            case '\'':
                return read_string_literal(lexer, &lexer->current_token);
            default:
                if (is_digit(c) || (c == '-' && is_digit(lexer->input[lexer->position + 1]))) {
                    return read_number(lexer, &lexer->current_token);
                } else if (is_alpha(c)) {
                    return read_identifier(lexer, &lexer->current_token);
                } else {
                    next_char(lexer);
                    lexer->current_token.type = TOKEN_UNKNOWN;
                    return 1;
                }
        }
    }
    
    lexer->current_token.type = TOKEN_EOF;
    return 1;
}

void token_cleanup(token_t *token) {
    if (token->text) {
        free(token->text);
        token->text = NULL;
    }
    token->length = 0;
}

void usd_value_cleanup(usd_value_t *value) {
    if (!value) return;
    
    switch (value->type) {
        case USD_VALUE_STRING:
            if (value->data.string_val) {
                free(value->data.string_val);
            }
            break;
        case USD_VALUE_ARRAY:
            if (value->data.array_val.elements) {
                for (size_t i = 0; i < value->data.array_val.count; i++) {
                    usd_value_cleanup(&value->data.array_val.elements[i]);
                }
                free(value->data.array_val.elements);
            }
            break;
        default:
            break;
    }
    value->type = USD_VALUE_NONE;
}

void usd_attribute_cleanup(usd_attribute_t *attr) {
    if (!attr) return;
    
    if (attr->name) free(attr->name);
    if (attr->type_name) free(attr->type_name);
    usd_value_cleanup(&attr->value);
    
    if (attr->next) {
        usd_attribute_cleanup(attr->next);
        free(attr->next);
    }
}

void usd_prim_cleanup(usd_prim_t *prim) {
    if (!prim) return;
    
    if (prim->name) free(prim->name);
    if (prim->type_name) free(prim->type_name);
    
    if (prim->attributes) {
        usd_attribute_cleanup(prim->attributes);
        free(prim->attributes);
    }
    
    if (prim->children) {
        usd_prim_cleanup(prim->children);
        free(prim->children);
    }
    
    if (prim->next) {
        usd_prim_cleanup(prim->next);
        free(prim->next);
    }
}

void usd_stage_cleanup(usd_stage_t *stage) {
    if (!stage) return;
    
    if (stage->default_prim) {
        free(stage->default_prim);
    }
    
    if (stage->root_prims) {
        usd_prim_cleanup(stage->root_prims);
        free(stage->root_prims);
    }
}

static char* strdup_safe(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char* copy = malloc(len + 1);
    if (!copy) return NULL;
    strcpy(copy, str);
    return copy;
}

static void set_error(usda_parser_t *parser, const char *message) {
    if (parser->error_message) {
        free(parser->error_message);
    }
    parser->error_message = strdup_safe(message);
}

static int parse_value(usda_parser_t *parser, usd_value_t *value);

static int parse_array_value(usda_parser_t *parser, usd_value_t *value) {
    if (parser->lexer.current_token.type != TOKEN_LBRACKET) {
        return 0;
    }
    
    lexer_next_token(&parser->lexer);
    
    value->type = USD_VALUE_ARRAY;
    value->data.array_val.elements = NULL;
    value->data.array_val.count = 0;
    
    if (parser->lexer.current_token.type == TOKEN_RBRACKET) {
        lexer_next_token(&parser->lexer);
        return 1;
    }
    
    size_t capacity = 4;
    value->data.array_val.elements = malloc(capacity * sizeof(usd_value_t));
    if (!value->data.array_val.elements) return 0;
    
    do {
        if (value->data.array_val.count >= capacity) {
            capacity *= 2;
            usd_value_t *new_elements = realloc(value->data.array_val.elements, 
                                              capacity * sizeof(usd_value_t));
            if (!new_elements) {
                return 0;
            }
            value->data.array_val.elements = new_elements;
        }
        
        usd_value_t *elem = &value->data.array_val.elements[value->data.array_val.count];
        if (!parse_value(parser, elem)) {
            return 0;
        }
        value->data.array_val.count++;
        
        if (parser->lexer.current_token.type == TOKEN_COMMA) {
            lexer_next_token(&parser->lexer);
        } else {
            break;
        }
    } while (parser->lexer.current_token.type != TOKEN_RBRACKET && 
             parser->lexer.current_token.type != TOKEN_EOF);
    
    if (parser->lexer.current_token.type != TOKEN_RBRACKET) {
        return 0;
    }
    
    lexer_next_token(&parser->lexer);
    return 1;
}

static int parse_value(usda_parser_t *parser, usd_value_t *value) {
    value->type = USD_VALUE_NONE;
    
    switch (parser->lexer.current_token.type) {
        case TOKEN_STRING:
            value->type = USD_VALUE_STRING;
            value->data.string_val = strdup_safe(parser->lexer.current_token.text);
            if (!value->data.string_val) return 0;
            lexer_next_token(&parser->lexer);
            return 1;
            
        case TOKEN_NUMBER: {
            const char *text = parser->lexer.current_token.text;
            if (strchr(text, '.') || strchr(text, 'e') || strchr(text, 'E')) {
                value->type = USD_VALUE_FLOAT;
                value->data.float_val = (float)atof(text);
            } else {
                value->type = USD_VALUE_INT;
                value->data.int_val = atoi(text);
            }
            lexer_next_token(&parser->lexer);
            return 1;
        }
        
        case TOKEN_IDENTIFIER:
            if (strcmp(parser->lexer.current_token.text, "true") == 0) {
                value->type = USD_VALUE_BOOL;
                value->data.bool_val = 1;
                lexer_next_token(&parser->lexer);
                return 1;
            } else if (strcmp(parser->lexer.current_token.text, "false") == 0) {
                value->type = USD_VALUE_BOOL;
                value->data.bool_val = 0;
                lexer_next_token(&parser->lexer);
                return 1;
            }
            break;
            
        case TOKEN_LBRACKET:
            return parse_array_value(parser, value);
            
        default:
            break;
    }
    
    return 0;
}

static int parse_attribute(usda_parser_t *parser, usd_attribute_t *attr) {
    if (parser->lexer.current_token.type != TOKEN_IDENTIFIER) {
        set_error(parser, "Expected attribute type identifier");
        return 0;
    }
    
    char *qualifier = NULL;
    if (strcmp(parser->lexer.current_token.text, "uniform") == 0 ||
        strcmp(parser->lexer.current_token.text, "varying") == 0 ||
        strcmp(parser->lexer.current_token.text, "custom") == 0) {
        qualifier = strdup_safe(parser->lexer.current_token.text);
        lexer_next_token(&parser->lexer);
        
        if (parser->lexer.current_token.type != TOKEN_IDENTIFIER) {
            free(qualifier);
            set_error(parser, "Expected type after qualifier");
            return 0;
        }
    }
    
    char *type_name = strdup_safe(parser->lexer.current_token.text);
    if (!type_name) {
        if (qualifier) free(qualifier);
        return 0;
    }
    
    if (qualifier) {
        size_t qual_len = strlen(qualifier);
        size_t type_len = strlen(type_name);
        char *full_type = malloc(qual_len + 1 + type_len + 1);
        if (!full_type) {
            free(qualifier);
            free(type_name);
            return 0;
        }
        strcpy(full_type, qualifier);
        strcat(full_type, " ");
        strcat(full_type, type_name);
        free(qualifier);
        free(type_name);
        type_name = full_type;
    }
    
    lexer_next_token(&parser->lexer);
    
    if (parser->lexer.current_token.type == TOKEN_LBRACKET) {
        lexer_next_token(&parser->lexer);
        if (parser->lexer.current_token.type != TOKEN_RBRACKET) {
            free(type_name);
            set_error(parser, "Expected ']' after '['");
            return 0;
        }
        lexer_next_token(&parser->lexer);
        
        size_t old_len = strlen(type_name);
        char *new_type = malloc(old_len + 3);
        if (!new_type) {
            free(type_name);
            return 0;
        }
        strcpy(new_type, type_name);
        strcat(new_type, "[]");
        free(type_name);
        type_name = new_type;
    }
    
    if (parser->lexer.current_token.type != TOKEN_IDENTIFIER) {
        free(type_name);
        set_error(parser, "Expected attribute name identifier");
        return 0;
    }
    
    attr->type_name = type_name;
    attr->name = strdup_safe(parser->lexer.current_token.text);
    if (!attr->name) return 0;
    
    lexer_next_token(&parser->lexer);
    
    if (parser->lexer.current_token.type == TOKEN_EQUALS) {
        lexer_next_token(&parser->lexer);
        if (!parse_value(parser, &attr->value)) {
            return 0;
        }
    }
    
    attr->next = NULL;
    return 1;
}

static int parse_prim(usda_parser_t *parser, usd_prim_t *prim);

static int parse_prim_content(usda_parser_t *parser, usd_prim_t *prim) {
    usd_attribute_t *last_attr = NULL;
    usd_prim_t *last_child = NULL;
    
    while (parser->lexer.current_token.type != TOKEN_RBRACE && 
           parser->lexer.current_token.type != TOKEN_EOF) {
        
        if (parser->lexer.current_token.type == TOKEN_DEF ||
            parser->lexer.current_token.type == TOKEN_CLASS ||
            parser->lexer.current_token.type == TOKEN_OVER) {
            
            usd_prim_t *child = malloc(sizeof(usd_prim_t));
            if (!child) return 0;
            memset(child, 0, sizeof(usd_prim_t));
            
            if (!parse_prim(parser, child)) {
                free(child);
                return 0;
            }
            
            if (last_child) {
                last_child->next = child;
            } else {
                prim->children = child;
            }
            last_child = child;
            
        } else if (parser->lexer.current_token.type == TOKEN_IDENTIFIER) {
            usd_attribute_t *attr = malloc(sizeof(usd_attribute_t));
            if (!attr) return 0;
            memset(attr, 0, sizeof(usd_attribute_t));
            
            if (!parse_attribute(parser, attr)) {
                usd_attribute_cleanup(attr);
                free(attr);
                return 0;
            }
            
            if (last_attr) {
                last_attr->next = attr;
            } else {
                prim->attributes = attr;
            }
            last_attr = attr;
            
        } else {
            set_error(parser, "Unexpected token in prim content");
            return 0;
        }
    }
    
    return 1;
}

static int parse_prim(usda_parser_t *parser, usd_prim_t *prim) {
    if (parser->lexer.current_token.type != TOKEN_DEF &&
        parser->lexer.current_token.type != TOKEN_CLASS &&
        parser->lexer.current_token.type != TOKEN_OVER) {
        set_error(parser, "Expected 'def', 'class', or 'over'");
        return 0;
    }
    
    lexer_next_token(&parser->lexer);
    
    if (parser->lexer.current_token.type == TOKEN_IDENTIFIER) {
        prim->type_name = strdup_safe(parser->lexer.current_token.text);
        lexer_next_token(&parser->lexer);
    }
    
    if (parser->lexer.current_token.type != TOKEN_STRING) {
        set_error(parser, "Expected prim name string");
        return 0;
    }
    
    prim->name = strdup_safe(parser->lexer.current_token.text);
    if (!prim->name) return 0;
    
    lexer_next_token(&parser->lexer);
    
    if (parser->lexer.current_token.type == TOKEN_LBRACE) {
        lexer_next_token(&parser->lexer);
        
        if (!parse_prim_content(parser, prim)) {
            return 0;
        }
        
        if (parser->lexer.current_token.type != TOKEN_RBRACE) {
            set_error(parser, "Expected closing brace '}'");
            return 0;
        }
        lexer_next_token(&parser->lexer);
    }
    
    prim->next = NULL;
    return 1;
}

int usda_parser_init(usda_parser_t *parser, const char *input, size_t length) {
    memset(parser, 0, sizeof(usda_parser_t));
    lexer_init(&parser->lexer, input, length);
    
    parser->stage.up_axis[1] = 1.0f;
    parser->stage.meters_per_unit = 1.0f;
    
    return 1;
}

void usda_parser_cleanup(usda_parser_t *parser) {
    if (!parser) return;
    
    token_cleanup(&parser->lexer.current_token);
    usd_stage_cleanup(&parser->stage);
    
    if (parser->error_message) {
        free(parser->error_message);
        parser->error_message = NULL;
    }
}

int usda_parser_parse(usda_parser_t *parser) {
    if (!parser) return 0;
    
    lexer_next_token(&parser->lexer);
    
    usd_prim_t *last_prim = NULL;
    
    while (parser->lexer.current_token.type != TOKEN_EOF) {
        if (parser->lexer.current_token.type == TOKEN_DEF ||
            parser->lexer.current_token.type == TOKEN_CLASS ||
            parser->lexer.current_token.type == TOKEN_OVER) {
            
            usd_prim_t *prim = malloc(sizeof(usd_prim_t));
            if (!prim) return 0;
            memset(prim, 0, sizeof(usd_prim_t));
            
            if (!parse_prim(parser, prim)) {
                free(prim);
                return 0;
            }
            
            if (last_prim) {
                last_prim->next = prim;
            } else {
                parser->stage.root_prims = prim;
            }
            last_prim = prim;
            
        } else {
            lexer_next_token(&parser->lexer);
        }
    }
    
    return 1;
}

const char* usda_parser_get_error(usda_parser_t *parser) {
    return parser ? parser->error_message : "Invalid parser";
}