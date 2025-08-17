/**
 * USD Type Definitions and Constants
 * Centralized type definitions for USD data structures
 */

// USD Data Types (matching crate-format.hh)
export const UsdDataType = Object.freeze({
    INVALID: 0,
    BOOL: 1,
    UCHAR: 2,
    INT: 3,
    UINT: 4,
    INT64: 5,
    UINT64: 6,
    HALF: 7,
    FLOAT: 8,
    DOUBLE: 9,
    STRING: 10,
    TOKEN: 11,
    ASSET_PATH: 12,
    MATRIX2D: 13,
    MATRIX3D: 14,
    MATRIX4D: 15,
    QUATD: 16,
    QUATF: 17,
    QUATH: 18,
    VEC2D: 19,
    VEC2F: 20,
    VEC2H: 21,
    VEC2I: 22,
    VEC3D: 23,
    VEC3F: 24,
    VEC3H: 25,
    VEC3I: 26,
    VEC4D: 27,
    VEC4F: 28,
    VEC4H: 29,
    VEC4I: 30,
    DICTIONARY: 31,
    TOKEN_LIST_OP: 32,
    STRING_LIST_OP: 33,
    PATH_LIST_OP: 34,
    REFERENCE_LIST_OP: 35,
    INT_LIST_OP: 36,
    INT64_LIST_OP: 37,
    UINT_LIST_OP: 38,
    UINT64_LIST_OP: 39,
    PATH_VECTOR: 40,
    TOKEN_VECTOR: 41,
    SPECIFIER: 42,
    PERMISSION: 43,
    VARIABILITY: 44,
    VARIANT_SELECTION_MAP: 45,
    TIME_SAMPLES: 46,
    PAYLOAD: 47,
    DOUBLE_VECTOR: 48,
    LAYER_OFFSET_VECTOR: 49,
    STRING_VECTOR: 50,
    VALUE_BLOCK: 51,
    VALUE: 52,
    UNREGISTERED_VALUE: 53,
    UNREGISTERED_VALUE_LIST_OP: 54,
    PAYLOAD_LIST_OP: 55,
    TIME_CODE: 56
});

// Type name mappings
export const UsdTypeNames = Object.freeze({
    [UsdDataType.BOOL]: 'bool',
    [UsdDataType.UCHAR]: 'uchar',
    [UsdDataType.INT]: 'int',
    [UsdDataType.UINT]: 'uint',
    [UsdDataType.INT64]: 'int64',
    [UsdDataType.UINT64]: 'uint64',
    [UsdDataType.HALF]: 'half',
    [UsdDataType.FLOAT]: 'float',
    [UsdDataType.DOUBLE]: 'double',
    [UsdDataType.STRING]: 'string',
    [UsdDataType.TOKEN]: 'token',
    [UsdDataType.ASSET_PATH]: 'asset_path',
    [UsdDataType.VEC2F]: 'vec2f',
    [UsdDataType.VEC3F]: 'vec3f',
    [UsdDataType.VEC4F]: 'vec4f',
    [UsdDataType.VEC2D]: 'vec2d',
    [UsdDataType.VEC3D]: 'vec3d',
    [UsdDataType.VEC4D]: 'vec4d',
    [UsdDataType.VEC2I]: 'vec2i',
    [UsdDataType.VEC3I]: 'vec3i',
    [UsdDataType.VEC4I]: 'vec4i',
    [UsdDataType.QUATF]: 'quatf',
    [UsdDataType.QUATD]: 'quatd',
    [UsdDataType.MATRIX4D]: 'matrix4d',
    [UsdDataType.DICTIONARY]: 'dictionary',
    [UsdDataType.TIME_SAMPLES]: 'time_samples'
});

// USD Token Types
export const TokenType = Object.freeze({
    // Literals
    STRING: 'STRING',
    NUMBER: 'NUMBER',
    IDENTIFIER: 'IDENTIFIER',
    BOOL: 'BOOL',
    
    // Keywords
    DEF: 'DEF',
    OVER: 'OVER',
    CLASS: 'CLASS',
    
    // Operators
    EQUALS: 'EQUALS',
    DOT: 'DOT',
    COLON: 'COLON',
    
    // Delimiters
    LPAREN: 'LPAREN',
    RPAREN: 'RPAREN',
    LBRACE: 'LBRACE',
    RBRACE: 'RBRACE',
    LBRACKET: 'LBRACKET',
    RBRACKET: 'RBRACKET',
    COMMA: 'COMMA',
    SEMICOLON: 'SEMICOLON',
    
    // Special
    EOF: 'EOF',
    NEWLINE: 'NEWLINE',
    COMMENT: 'COMMENT'
});

// USD Primitive Specifiers
export const PrimSpecifier = Object.freeze({
    DEF: 'def',
    OVER: 'over',
    CLASS: 'class'
});

// USD Attribute Variability
export const AttributeVariability = Object.freeze({
    VARYING: 'varying',
    UNIFORM: 'uniform',
    CONFIG: 'config'
});

// Value representation bit flags
export const ValueRepFlags = Object.freeze({
    IS_ARRAY_BIT: 1n << 63n,
    IS_INLINED_BIT: 1n << 62n,
    IS_COMPRESSED_BIT: 1n << 61n,
    PAYLOAD_MASK: (1n << 48n) - 1n
});

// USDC Constants
export const UsdcConstants = Object.freeze({
    MAGIC: 'PXR-USDC',
    MAGIC_SIZE: 8,
    VERSION_SIZE: 8,
    TOC_OFFSET_SIZE: 8,
    HEADER_SIZE: 24, // MAGIC_SIZE + VERSION_SIZE + TOC_OFFSET_SIZE
    
    // Security limits
    MAX_TOC_SECTIONS: 32,
    MAX_TOKENS: 1024 * 1024 * 64,        // 64M tokens
    MAX_STRINGS: 1024 * 1024 * 64,       // 64M strings  
    MAX_FIELDS: 1024 * 1024 * 256,       // 256M fields
    MAX_PATHS: 1024 * 1024 * 256,        // 256M paths
    MAX_SPECS: 1024 * 1024 * 256,        // 256M specs
    MAX_FIELDSETS: 1024 * 1024 * 64,     // 64M fieldsets
    MAX_STRING_LENGTH: 1024 * 1024 * 64, // 64MB string
    MAX_MEMORY_BUDGET: 2 * 1024 * 1024 * 1024 // 2GB
});

// Time sample interpolation types
export const InterpolationType = Object.freeze({
    LINEAR: 'linear',
    HELD: 'held',
    BEZIER: 'bezier',
    BSPLINE: 'bspline'
});

// JSON conversion options
export const JsonConversionOptions = Object.freeze({
    DEFAULT_SPECIFIER: PrimSpecifier.DEF,
    DEFAULT_VARIABILITY: AttributeVariability.VARYING,
    DEFAULT_INTERPOLATION: InterpolationType.LINEAR,
    MAX_ARRAY_LENGTH: 1000,
    MAX_DEPTH: 20,
    DEFAULT_INDENT: 2
});

/**
 * Base USD Error class
 */
export class UsdError extends Error {
    constructor(message, code = null, position = null) {
        super(message);
        this.name = this.constructor.name;
        this.code = code;
        this.position = position;
    }
}

/**
 * USD Parse Error with position information
 */
export class UsdParseError extends UsdError {
    constructor(message, line = 0, column = 0, position = 0) {
        super(message, 'PARSE_ERROR', position);
        this.line = line;
        this.column = column;
    }

    toString() {
        return `${this.name} at ${this.line}:${this.column}: ${this.message}`;
    }
}

/**
 * USD Validation Error
 */
export class UsdValidationError extends UsdError {
    constructor(message, path = null) {
        super(message, 'VALIDATION_ERROR');
        this.path = path;
    }
}

/**
 * USD Memory Error
 */
export class UsdMemoryError extends UsdError {
    constructor(message, limit = null, used = null) {
        super(message, 'MEMORY_ERROR');
        this.limit = limit;
        this.used = used;
    }
}

/**
 * USD IO Error
 */
export class UsdIOError extends UsdError {
    constructor(message, filename = null) {
        super(message, 'IO_ERROR');
        this.filename = filename;
    }
}