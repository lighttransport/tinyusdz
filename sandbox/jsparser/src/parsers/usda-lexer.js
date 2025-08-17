/**
 * Enhanced USDA Lexer with improved performance and error handling
 */

import { TokenType, UsdParseError } from '../types/usd-types.js';
import { StringUtils, Logger } from '../utils/common-utils.js';

export class Token {
    constructor(type, value, line, column, position) {
        this.type = type;
        this.value = value;
        this.line = line;
        this.column = column;
        this.position = position;
    }

    toString() {
        return `Token(${this.type}, "${this.value}", ${this.line}:${this.column})`;
    }

    is(type) {
        return this.type === type;
    }

    isOneOf(...types) {
        return types.includes(this.type);
    }
}

export class UsdaLexer {
    constructor(input, options = {}) {
        this.input = input;
        this.position = 0;
        this.line = 1;
        this.column = 1;
        this.length = input.length;
        
        this.logger = new Logger('UsdaLexer', options.logLevel);
        this.options = {
            skipComments: options.skipComments || false,
            preserveWhitespace: options.preserveWhitespace || false,
            trackPositions: options.trackPositions !== false,
            ...options
        };

        // Token patterns
        this.patterns = this.initializePatterns();
        
        // Keyword mapping
        this.keywords = new Map([
            ['def', TokenType.DEF],
            ['over', TokenType.OVER],
            ['class', TokenType.CLASS],
            ['true', TokenType.BOOL],
            ['false', TokenType.BOOL],
            ['True', TokenType.BOOL],
            ['False', TokenType.BOOL]
        ]);

        this.logger.debug(`Initialized lexer for ${this.length} characters`);
    }

    initializePatterns() {
        return {
            // Single character tokens
            singleChar: new Map([
                ['(', TokenType.LPAREN],
                [')', TokenType.RPAREN],
                ['{', TokenType.LBRACE],
                ['}', TokenType.RBRACE],
                ['[', TokenType.LBRACKET],
                [']', TokenType.RBRACKET],
                [',', TokenType.COMMA],
                [';', TokenType.SEMICOLON],
                ['=', TokenType.EQUALS],
                ['.', TokenType.DOT],
                [':', TokenType.COLON]
            ]),

            // Multi-character patterns
            identifier: /^[a-zA-Z_][a-zA-Z0-9_:]*$/,
            number: /^-?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?$/,
            hexNumber: /^0[xX][0-9a-fA-F]+$/
        };
    }

    // Position tracking
    getCurrentPosition() {
        return {
            position: this.position,
            line: this.line,
            column: this.column
        };
    }

    peek(offset = 0) {
        const pos = this.position + offset;
        return pos < this.length ? this.input[pos] : null;
    }

    advance() {
        if (this.position < this.length) {
            const char = this.input[this.position];
            this.position++;
            
            if (char === '\n') {
                this.line++;
                this.column = 1;
            } else {
                this.column++;
            }
            
            return char;
        }
        return null;
    }

    skipWhitespace() {
        let skipped = 0;
        while (this.position < this.length && StringUtils.isWhitespace(this.peek())) {
            this.advance();
            skipped++;
        }
        return skipped;
    }

    skipLineComment() {
        // Skip #comment until end of line
        this.advance(); // skip #
        let content = '';
        
        while (this.position < this.length && this.peek() !== '\n') {
            content += this.advance();
        }
        
        return content.trim();
    }

    readString() {
        const start = this.getCurrentPosition();
        const quote = this.advance(); // consume opening quote
        let value = '';
        let escaped = false;

        while (this.position < this.length) {
            const char = this.peek();
            
            if (escaped) {
                this.advance();
                switch (char) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case 'r': value += '\r'; break;
                    case '\\': value += '\\'; break;
                    case '"': value += '"'; break;
                    case '\'': value += '\''; break;
                    default: 
                        value += char;
                        this.logger.warn(`Unknown escape sequence: \\${char} at ${this.line}:${this.column}`);
                        break;
                }
                escaped = false;
            } else if (char === '\\') {
                this.advance();
                escaped = true;
            } else if (char === quote) {
                this.advance(); // consume closing quote
                break;
            } else if (char === '\n') {
                throw new UsdParseError(
                    'Unterminated string literal',
                    start.line,
                    start.column,
                    start.position
                );
            } else {
                value += this.advance();
            }
        }

        if (this.position >= this.length && this.peek(-1) !== quote) {
            throw new UsdParseError(
                'Unterminated string literal',
                start.line,
                start.column,
                start.position
            );
        }

        return new Token(TokenType.STRING, value, start.line, start.column, start.position);
    }

    readNumber() {
        const start = this.getCurrentPosition();
        let value = '';
        let hasDecimal = false;
        let hasExponent = false;

        // Handle negative sign
        if (this.peek() === '-') {
            value += this.advance();
        }

        // Handle hex numbers
        if (this.peek() === '0' && (this.peek(1) === 'x' || this.peek(1) === 'X')) {
            value += this.advance(); // 0
            value += this.advance(); // x or X
            
            while (this.position < this.length && StringUtils.isHexDigit(this.peek())) {
                value += this.advance();
            }
            
            return new Token(TokenType.NUMBER, value, start.line, start.column, start.position);
        }

        // Regular decimal numbers
        while (this.position < this.length) {
            const char = this.peek();
            
            if (StringUtils.isDigit(char)) {
                value += this.advance();
            } else if (char === '.' && !hasDecimal && !hasExponent) {
                hasDecimal = true;
                value += this.advance();
            } else if ((char === 'e' || char === 'E') && !hasExponent && value.length > 0) {
                hasExponent = true;
                value += this.advance();
                
                // Handle optional + or - after exponent
                if (this.peek() === '+' || this.peek() === '-') {
                    value += this.advance();
                }
            } else {
                break;
            }
        }

        // Validate number format
        if (!this.patterns.number.test(value) && !this.patterns.hexNumber.test(value)) {
            throw new UsdParseError(
                `Invalid number format: ${value}`,
                start.line,
                start.column,
                start.position
            );
        }

        return new Token(TokenType.NUMBER, value, start.line, start.column, start.position);
    }

    readIdentifier() {
        const start = this.getCurrentPosition();
        let value = '';

        while (this.position < this.length && StringUtils.isAlnum(this.peek())) {
            value += this.advance();
        }

        // Check if it's a keyword
        const tokenType = this.keywords.get(value) || TokenType.IDENTIFIER;
        
        return new Token(tokenType, value, start.line, start.column, start.position);
    }

    nextToken() {
        // Skip whitespace unless preserving it
        if (!this.options.preserveWhitespace) {
            this.skipWhitespace();
        }

        // Check for end of input
        if (this.position >= this.length) {
            const pos = this.getCurrentPosition();
            return new Token(TokenType.EOF, '', pos.line, pos.column, pos.position);
        }

        const char = this.peek();
        const pos = this.getCurrentPosition();

        // Handle comments
        if (char === '#') {
            const content = this.skipLineComment();
            if (!this.options.skipComments) {
                return new Token(TokenType.COMMENT, content, pos.line, pos.column, pos.position);
            }
            return this.nextToken(); // Skip and get next token
        }

        // Handle newlines
        if (char === '\n') {
            this.advance();
            if (this.options.preserveWhitespace) {
                return new Token(TokenType.NEWLINE, '\n', pos.line, pos.column, pos.position);
            }
            return this.nextToken(); // Skip and get next token
        }

        // Handle strings
        if (char === '"' || char === "'") {
            return this.readString();
        }

        // Handle numbers
        if (StringUtils.isDigit(char) || (char === '-' && StringUtils.isDigit(this.peek(1)))) {
            return this.readNumber();
        }

        // Handle single-character tokens
        if (this.patterns.singleChar.has(char)) {
            const tokenType = this.patterns.singleChar.get(char);
            this.advance();
            return new Token(tokenType, char, pos.line, pos.column, pos.position);
        }

        // Handle identifiers and keywords
        if (StringUtils.isAlpha(char)) {
            return this.readIdentifier();
        }

        // Unknown character
        throw new UsdParseError(
            `Unexpected character: '${char}'`,
            pos.line,
            pos.column,
            pos.position
        );
    }

    tokenize() {
        const tokens = [];
        
        try {
            while (true) {
                const token = this.nextToken();
                tokens.push(token);
                
                if (token.type === TokenType.EOF) {
                    break;
                }
            }
        } catch (error) {
            this.logger.error(`Tokenization failed: ${error.message}`);
            throw error;
        }

        this.logger.debug(`Tokenized ${tokens.length} tokens`);
        return tokens;
    }

    // Iterator interface
    *[Symbol.iterator]() {
        while (true) {
            const token = this.nextToken();
            yield token;
            
            if (token.type === TokenType.EOF) {
                break;
            }
        }
    }

    // Utility methods
    reset() {
        this.position = 0;
        this.line = 1;
        this.column = 1;
    }

    getContext(position = this.position, contextSize = 50) {
        const start = Math.max(0, position - contextSize);
        const end = Math.min(this.length, position + contextSize);
        const context = this.input.slice(start, end);
        const marker = ' '.repeat(position - start) + '^';
        
        return {
            context,
            marker,
            line: this.line,
            column: this.column
        };
    }

    getStatistics() {
        return {
            totalLength: this.length,
            currentPosition: this.position,
            currentLine: this.line,
            currentColumn: this.column,
            progress: (this.position / this.length) * 100
        };
    }

    // Error recovery
    skipToNextStatement() {
        // Skip to next statement boundary (typically '}' or end of file)
        while (this.position < this.length) {
            const char = this.peek();
            if (char === '}' || char === '\n') {
                break;
            }
            this.advance();
        }
    }

    synchronize() {
        // Synchronize to a known good state for error recovery
        this.skipWhitespace();
        
        while (this.position < this.length) {
            const char = this.peek();
            if (char === '{' || char === '}' || StringUtils.isAlpha(char)) {
                break;
            }
            this.advance();
        }
    }
}

// Factory function
export function createLexer(input, options = {}) {
    return new UsdaLexer(input, options);
}

// Token type checking utilities
export class TokenUtils {
    static isLiteral(token) {
        return token.isOneOf(TokenType.STRING, TokenType.NUMBER, TokenType.BOOL);
    }

    static isKeyword(token) {
        return token.isOneOf(TokenType.DEF, TokenType.OVER, TokenType.CLASS);
    }

    static isDelimiter(token) {
        return token.isOneOf(
            TokenType.LPAREN, TokenType.RPAREN,
            TokenType.LBRACE, TokenType.RBRACE,
            TokenType.LBRACKET, TokenType.RBRACKET
        );
    }

    static isOperator(token) {
        return token.isOneOf(TokenType.EQUALS, TokenType.DOT, TokenType.COLON);
    }

    static matchingDelimiter(tokenType) {
        const pairs = new Map([
            [TokenType.LPAREN, TokenType.RPAREN],
            [TokenType.LBRACE, TokenType.RBRACE],
            [TokenType.LBRACKET, TokenType.RBRACKET]
        ]);
        return pairs.get(tokenType);
    }
}