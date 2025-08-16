/**
 * USDA (USD ASCII) Lexer for JavaScript
 * Based on the C implementation in sandbox/c/usda_parser.c
 */

const TokenType = {
    EOF: 'EOF',
    IDENTIFIER: 'IDENTIFIER',
    STRING: 'STRING',
    NUMBER: 'NUMBER',
    LBRACE: 'LBRACE',           // {
    RBRACE: 'RBRACE',           // }
    LPAREN: 'LPAREN',           // (
    RPAREN: 'RPAREN',           // )
    LBRACKET: 'LBRACKET',       // [
    RBRACKET: 'RBRACKET',       // ]
    SEMICOLON: 'SEMICOLON',     // ;
    COLON: 'COLON',             // :
    COMMA: 'COMMA',             // ,
    EQUALS: 'EQUALS',           // =
    AT: 'AT',                   // @
    HASH: 'HASH',               // #
    DEF: 'DEF',                 // def keyword
    CLASS: 'CLASS',             // class keyword
    OVER: 'OVER',               // over keyword
    UNKNOWN: 'UNKNOWN'
};

class Token {
    constructor(type, text, line = 1, column = 1) {
        this.type = type;
        this.text = text;
        this.line = line;
        this.column = column;
    }

    toString() {
        return `Token(${this.type}, "${this.text}", ${this.line}:${this.column})`;
    }
}

class UsdaLexer {
    constructor(input) {
        this.input = input;
        this.length = input.length;
        this.position = 0;
        this.line = 1;
        this.column = 1;
        this.currentToken = null;
    }

    static isAlpha(c) {
        return /[a-zA-Z_]/.test(c);
    }

    static isAlnum(c) {
        return /[a-zA-Z0-9_:]/.test(c);
    }

    static isDigit(c) {
        return /[0-9]/.test(c);
    }

    static isWhitespace(c) {
        return /[ \t\r]/.test(c);
    }

    peekChar() {
        if (this.position >= this.length) {
            return '\0';
        }
        return this.input[this.position];
    }

    nextChar() {
        if (this.position >= this.length) {
            return '\0';
        }
        const c = this.input[this.position++];
        if (c === '\n') {
            this.line++;
            this.column = 1;
        } else {
            this.column++;
        }
        return c;
    }

    skipWhitespace() {
        while (this.position < this.length) {
            const c = this.peekChar();
            if (UsdaLexer.isWhitespace(c)) {
                this.nextChar();
            } else if (c === '\n') {
                this.nextChar();
            } else {
                break;
            }
        }
    }

    skipComment() {
        if (this.peekChar() === '#') {
            while (this.position < this.length && this.peekChar() !== '\n') {
                this.nextChar();
            }
        }
    }

    getKeywordToken(text) {
        switch (text) {
            case 'def': return TokenType.DEF;
            case 'class': return TokenType.CLASS;
            case 'over': return TokenType.OVER;
            default: return TokenType.IDENTIFIER;
        }
    }

    readStringLiteral() {
        const startLine = this.line;
        const startColumn = this.column;
        const quote = this.nextChar(); // consume opening quote
        let text = '';

        while (this.position < this.length) {
            const c = this.peekChar();
            if (c === quote) {
                this.nextChar(); // consume closing quote
                break;
            } else if (c === '\\') {
                this.nextChar(); // consume backslash
                if (this.position < this.length) {
                    const escaped = this.nextChar();
                    // Handle common escape sequences
                    switch (escaped) {
                        case 'n': text += '\n'; break;
                        case 't': text += '\t'; break;
                        case 'r': text += '\r'; break;
                        case '\\': text += '\\'; break;
                        case '"': text += '"'; break;
                        case "'": text += "'"; break;
                        default: text += escaped; break;
                    }
                }
            } else {
                text += this.nextChar();
            }
        }

        return new Token(TokenType.STRING, text, startLine, startColumn);
    }

    readNumber() {
        const startLine = this.line;
        const startColumn = this.column;
        let text = '';

        // Handle optional negative sign
        if (this.peekChar() === '-') {
            text += this.nextChar();
        }

        // Read integer part
        while (this.position < this.length && UsdaLexer.isDigit(this.peekChar())) {
            text += this.nextChar();
        }

        // Read decimal part
        if (this.peekChar() === '.') {
            text += this.nextChar();
            while (this.position < this.length && UsdaLexer.isDigit(this.peekChar())) {
                text += this.nextChar();
            }
        }

        // Read exponent part
        if (this.peekChar() === 'e' || this.peekChar() === 'E') {
            text += this.nextChar();
            if (this.peekChar() === '+' || this.peekChar() === '-') {
                text += this.nextChar();
            }
            while (this.position < this.length && UsdaLexer.isDigit(this.peekChar())) {
                text += this.nextChar();
            }
        }

        return new Token(TokenType.NUMBER, text, startLine, startColumn);
    }

    readIdentifier() {
        const startLine = this.line;
        const startColumn = this.column;
        let text = '';

        while (this.position < this.length && UsdaLexer.isAlnum(this.peekChar())) {
            text += this.nextChar();
        }

        const tokenType = this.getKeywordToken(text);
        return new Token(tokenType, text, startLine, startColumn);
    }

    nextToken() {
        while (this.position < this.length) {
            this.skipWhitespace();

            if (this.position >= this.length) {
                break;
            }

            // Skip comments
            if (this.peekChar() === '#') {
                this.skipComment();
                continue;
            }

            const c = this.peekChar();
            const line = this.line;
            const column = this.column;

            // Single character tokens
            switch (c) {
                case '{':
                    this.nextChar();
                    return new Token(TokenType.LBRACE, '{', line, column);
                case '}':
                    this.nextChar();
                    return new Token(TokenType.RBRACE, '}', line, column);
                case '(':
                    this.nextChar();
                    return new Token(TokenType.LPAREN, '(', line, column);
                case ')':
                    this.nextChar();
                    return new Token(TokenType.RPAREN, ')', line, column);
                case '[':
                    this.nextChar();
                    return new Token(TokenType.LBRACKET, '[', line, column);
                case ']':
                    this.nextChar();
                    return new Token(TokenType.RBRACKET, ']', line, column);
                case ';':
                    this.nextChar();
                    return new Token(TokenType.SEMICOLON, ';', line, column);
                case ':':
                    this.nextChar();
                    return new Token(TokenType.COLON, ':', line, column);
                case ',':
                    this.nextChar();
                    return new Token(TokenType.COMMA, ',', line, column);
                case '=':
                    this.nextChar();
                    return new Token(TokenType.EQUALS, '=', line, column);
                case '@':
                    this.nextChar();
                    return new Token(TokenType.AT, '@', line, column);
            }

            // String literals
            if (c === '"' || c === "'") {
                return this.readStringLiteral();
            }

            // Numbers
            if (UsdaLexer.isDigit(c) || (c === '-' && UsdaLexer.isDigit(this.input[this.position + 1]))) {
                return this.readNumber();
            }

            // Identifiers and keywords
            if (UsdaLexer.isAlpha(c)) {
                return this.readIdentifier();
            }

            // Unknown character
            this.nextChar();
            return new Token(TokenType.UNKNOWN, c, line, column);
        }

        return new Token(TokenType.EOF, '', this.line, this.column);
    }

    peek() {
        if (!this.currentToken) {
            this.currentToken = this.nextToken();
        }
        return this.currentToken;
    }

    advance() {
        const token = this.peek();
        this.currentToken = null;
        return token;
    }

    // Tokenize entire input for debugging
    tokenizeAll() {
        const tokens = [];
        let token;
        do {
            token = this.nextToken();
            tokens.push(token);
        } while (token.type !== TokenType.EOF);
        return tokens;
    }
}

// Export for Node.js and browser
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { UsdaLexer, Token, TokenType };
} else if (typeof window !== 'undefined') {
    window.UsdaLexer = UsdaLexer;
    window.Token = Token;
    window.TokenType = TokenType;
}