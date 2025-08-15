#include "usda_parser.h"
#include <stdio.h>
#include <string.h>

static const char* token_type_name(token_type_t type) {
    switch (type) {
        case TOKEN_EOF: return "EOF";
        case TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TOKEN_STRING: return "STRING";
        case TOKEN_NUMBER: return "NUMBER";
        case TOKEN_LBRACE: return "LBRACE";
        case TOKEN_RBRACE: return "RBRACE";
        case TOKEN_LPAREN: return "LPAREN";
        case TOKEN_RPAREN: return "RPAREN";
        case TOKEN_LBRACKET: return "LBRACKET";
        case TOKEN_RBRACKET: return "RBRACKET";
        case TOKEN_SEMICOLON: return "SEMICOLON";
        case TOKEN_COLON: return "COLON";
        case TOKEN_COMMA: return "COMMA";
        case TOKEN_EQUALS: return "EQUALS";
        case TOKEN_AT: return "AT";
        case TOKEN_HASH: return "HASH";
        case TOKEN_DEF: return "DEF";
        case TOKEN_CLASS: return "CLASS";
        case TOKEN_OVER: return "OVER";
        case TOKEN_UNKNOWN: return "UNKNOWN";
        default: return "INVALID";
    }
}

int main() {
    const char *test_usda = 
        "#usda 1.0\n"
        "\n"
        "def Xform \"World\" {\n"
        "    double3 xformOp:translate = (0, 0, 0)\n"
        "}\n";
    
    usda_parser_t parser;
    usda_parser_init(&parser, test_usda, strlen(test_usda));
    
    printf("Tokenizing: %s\n", test_usda);
    printf("---\n");
    
    while (1) {
        lexer_next_token(&parser.lexer);
        
        printf("Token: %s", token_type_name(parser.lexer.current_token.type));
        if (parser.lexer.current_token.text) {
            printf(" [%s]", parser.lexer.current_token.text);
        }
        printf(" at line %d, col %d\n", 
               parser.lexer.current_token.line, 
               parser.lexer.current_token.column);
        
        if (parser.lexer.current_token.type == TOKEN_EOF) {
            break;
        }
    }
    
    usda_parser_cleanup(&parser);
    return 0;
}