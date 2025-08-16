/**
 * USDA (USD ASCII) Parser for JavaScript
 * Based on the C++ implementation in src/ascii-parser.cc
 */

// Import lexer (only when running in Node.js)
let UsdaLexer, Token, TokenType;
if (typeof require !== 'undefined') {
    ({ UsdaLexer, Token, TokenType } = require('./usda-lexer.js'));
}

class ParseError extends Error {
    constructor(message, line = 0, column = 0) {
        super(message);
        this.name = 'ParseError';
        this.line = line;
        this.column = column;
    }

    toString() {
        return `${this.name} at ${this.line}:${this.column}: ${this.message}`;
    }
}

class UsdaValue {
    constructor(type, value) {
        this.type = type;
        this.value = value;
    }
}

class UsdaAttribute {
    constructor(name, type, value, metadata = {}) {
        this.name = name;
        this.type = type;
        this.value = value;
        this.metadata = metadata;
    }
}

class UsdaPrim {
    constructor(name, type, specifier = 'def') {
        this.name = name;
        this.type = type;
        this.specifier = specifier; // 'def', 'class', 'over'
        this.attributes = new Map();
        this.children = new Map();
        this.metadata = {};
        this.path = '';
    }

    addAttribute(attr) {
        this.attributes.set(attr.name, attr);
    }

    addChild(prim) {
        this.children.set(prim.name, prim);
    }

    getAttribute(name) {
        return this.attributes.get(name);
    }

    getChild(name) {
        return this.children.get(name);
    }
}

class UsdaLayer {
    constructor() {
        this.rootPrim = null;
        this.metadata = {};
        this.prims = new Map();
    }

    addPrim(prim) {
        this.prims.set(prim.name, prim);
    }

    getPrim(name) {
        return this.prims.get(name);
    }
}

class UsdaParser {
    constructor(input) {
        this.lexer = new UsdaLexer(input);
        this.layer = new UsdaLayer();
        this.errors = [];
        this.warnings = [];
    }

    error(message) {
        const token = this.lexer.peek();
        const err = new ParseError(message, token.line, token.column);
        this.errors.push(err);
        throw err;
    }

    warning(message) {
        const token = this.lexer.peek();
        this.warnings.push(`Warning at ${token.line}:${token.column}: ${message}`);
    }

    expect(expectedType) {
        const token = this.lexer.advance();
        if (token.type !== expectedType) {
            this.error(`Expected ${expectedType}, got ${token.type}`);
        }
        return token;
    }

    match(tokenType) {
        const token = this.lexer.peek();
        if (token.type === tokenType) {
            return this.lexer.advance();
        }
        return null;
    }

    // Parse value types like int, float, string, arrays, etc.
    parseValue() {
        const token = this.lexer.peek();

        switch (token.type) {
            case TokenType.NUMBER:
                this.lexer.advance();
                const num = parseFloat(token.text);
                return new UsdaValue('number', num);

            case TokenType.STRING:
                this.lexer.advance();
                return new UsdaValue('string', token.text);

            case TokenType.IDENTIFIER:
                this.lexer.advance();
                // Could be a type name or reference
                return new UsdaValue('identifier', token.text);

            case TokenType.LBRACKET:
                return this.parseArray();

            case TokenType.LPAREN:
                return this.parseTuple();

            default:
                this.error(`Unexpected token ${token.type} when parsing value`);
        }
    }

    parseArray() {
        this.expect(TokenType.LBRACKET);
        const values = [];

        while (!this.match(TokenType.RBRACKET)) {
            if (values.length > 0) {
                this.expect(TokenType.COMMA);
            }
            
            if (this.lexer.peek().type === TokenType.RBRACKET) {
                break; // Allow trailing comma
            }
            
            values.push(this.parseValue());
        }

        return new UsdaValue('array', values);
    }

    parseTuple() {
        this.expect(TokenType.LPAREN);
        const values = [];

        while (!this.match(TokenType.RPAREN)) {
            if (values.length > 0) {
                this.expect(TokenType.COMMA);
            }
            
            if (this.lexer.peek().type === TokenType.RPAREN) {
                break; // Allow trailing comma
            }
            
            values.push(this.parseValue());
        }

        return new UsdaValue('tuple', values);
    }

    // Parse metadata like (interpolation = "linear")
    parseMetadata() {
        const metadata = {};
        
        if (!this.match(TokenType.LPAREN)) {
            return metadata;
        }

        while (this.lexer.peek().type !== TokenType.RPAREN && this.lexer.peek().type !== TokenType.EOF) {
            // Skip any newlines or whitespace
            if (Object.keys(metadata).length > 0) {
                // Look for comma or just continue if we find identifier
                this.match(TokenType.COMMA); // Optional comma
            }

            if (this.lexer.peek().type === TokenType.RPAREN) {
                break; // End of metadata
            }

            if (this.lexer.peek().type === TokenType.IDENTIFIER) {
                const key = this.expect(TokenType.IDENTIFIER).text;
                this.expect(TokenType.EQUALS);
                const value = this.parseValue();
                metadata[key] = value;
            } else {
                // Skip unknown tokens 
                this.lexer.advance();
            }
        }

        this.expect(TokenType.RPAREN);
        return metadata;
    }

    // Parse attribute definition like: float3 translate = (0, 0, 0)
    parseAttribute() {
        // Parse type (e.g., "float3", "token", "string")
        let typeToken = this.expect(TokenType.IDENTIFIER);
        let attrType = typeToken.text;

        // Check for array syntax: type[]
        if (this.match(TokenType.LBRACKET)) {
            this.expect(TokenType.RBRACKET);
            attrType += '[]';
        }

        // Parse attribute name
        const nameToken = this.expect(TokenType.IDENTIFIER);
        const attrName = nameToken.text;

        // Parse optional metadata
        const metadata = this.parseMetadata();

        let value = null;
        if (this.match(TokenType.EQUALS)) {
            value = this.parseValue();
        }

        return new UsdaAttribute(attrName, attrType, value, metadata);
    }

    // Parse prim definition like: def Sphere "ball" { ... }
    parsePrim() {
        // Parse specifier (def, class, over)
        let specifier = 'def';
        const token = this.lexer.peek();
        if (token.type === TokenType.DEF || token.type === TokenType.CLASS || token.type === TokenType.OVER) {
            specifier = this.lexer.advance().text;
        }

        // Parse prim type
        const typeToken = this.expect(TokenType.IDENTIFIER);
        const primType = typeToken.text;

        // Parse prim name (optional for some cases)
        let primName = '';
        if (this.lexer.peek().type === TokenType.STRING) {
            primName = this.lexer.advance().text;
        }

        // Parse optional metadata before opening brace
        const metadata = this.parseMetadata();

        const prim = new UsdaPrim(primName, primType, specifier);
        prim.metadata = metadata;

        // Parse prim body
        this.expect(TokenType.LBRACE);

        while (!this.match(TokenType.RBRACE)) {
            const nextToken = this.lexer.peek();

            if (nextToken.type === TokenType.EOF) {
                this.error('Unexpected end of file');
            }

            // Check if this is a nested prim definition
            if (this.looksLikePrimDefinition()) {
                const childPrim = this.parsePrim();
                prim.addChild(childPrim);
            } else if (nextToken.type === TokenType.IDENTIFIER) {
                // This should be an attribute
                const attr = this.parseAttribute();
                prim.addAttribute(attr);
            } else {
                this.error(`Unexpected token ${nextToken.type} in prim body`);
            }
        }

        return prim;
    }

    // Helper to check if we're looking at a prim definition
    looksLikePrimDefinition() {
        const token = this.lexer.peek();
        
        if (token.type === TokenType.DEF || token.type === TokenType.CLASS || token.type === TokenType.OVER) {
            return true;
        }
        
        // Look ahead to see if this is a type followed by string name
        if (token.type === TokenType.IDENTIFIER) {
            // Create a temporary lexer to look ahead
            const tempLexer = new UsdaLexer(this.lexer.input);
            tempLexer.position = this.lexer.position;
            tempLexer.line = this.lexer.line;
            tempLexer.column = this.lexer.column;
            
            const token1 = tempLexer.nextToken();
            const token2 = tempLexer.nextToken();
            
            // If second token is string (prim name) or opening brace, it's likely a prim
            return token2.type === TokenType.STRING || token2.type === TokenType.LBRACE;
        }
        
        return false;
    }

    // Check if identifier is a known USD type
    isTypeKeyword(text) {
        const usdTypes = [
            'bool', 'uchar', 'int', 'uint', 'int64', 'uint64',
            'half', 'float', 'double',
            'string', 'token', 'asset',
            'int2', 'int3', 'int4',
            'float2', 'float3', 'float4',
            'double2', 'double3', 'double4',
            'point3h', 'point3f', 'point3d',
            'vector3h', 'vector3f', 'vector3d',
            'normal3h', 'normal3f', 'normal3d',
            'color3h', 'color3f', 'color3d',
            'color4h', 'color4f', 'color4d',
            'quath', 'quatf', 'quatd',
            'matrix2d', 'matrix3d', 'matrix4d',
            'frame4d'
        ];
        return usdTypes.includes(text);
    }

    // Main parse function
    parse() {
        try {
            while (this.lexer.peek().type !== TokenType.EOF) {
                const prim = this.parsePrim();
                this.layer.addPrim(prim);
                
                // Set root prim if this is the first one
                if (!this.layer.rootPrim) {
                    this.layer.rootPrim = prim;
                }
            }
            
            return this.layer;
        } catch (error) {
            if (error instanceof ParseError) {
                console.error(error.toString());
            } else {
                console.error('Unexpected error:', error);
            }
            return null;
        }
    }

    getErrors() {
        return this.errors;
    }

    getWarnings() {
        return this.warnings;
    }
}

// Export for Node.js and browser
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { 
        UsdaParser, 
        UsdaLayer, 
        UsdaPrim, 
        UsdaAttribute, 
        UsdaValue, 
        ParseError 
    };
} else if (typeof window !== 'undefined') {
    window.UsdaParser = UsdaParser;
    window.UsdaLayer = UsdaLayer;
    window.UsdaPrim = UsdaPrim;
    window.UsdaAttribute = UsdaAttribute;
    window.UsdaValue = UsdaValue;
    window.ParseError = ParseError;
}