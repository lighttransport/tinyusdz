/**
 * TinyUSDZ JavaScript Parser
 * Main entry point for the refactored USD parsing library
 */

// Core parser functionality
export {
    UsdParser,
    createParser,
    parseUsd,
    parseUsdSync
} from './core/usd-parser.js';

// Data structures
export {
    UsdLayer,
    UsdPrim,
    UsdAttribute,
    UsdValue,
    UsdTimeSamples
} from './types/usd-data-structures.js';

// Type definitions and constants
export {
    UsdDataType,
    UsdTypeNames,
    TokenType,
    PrimSpecifier,
    AttributeVariability,
    InterpolationType,
    UsdcConstants,
    UsdError,
    UsdParseError,
    UsdValidationError,
    UsdMemoryError,
    UsdIOError
} from './types/usd-types.js';

// Utilities
export {
    Logger,
    MemoryTracker,
    PerformanceTracker,
    StringUtils,
    ArrayUtils,
    ObjectUtils,
    MathUtils,
    ValidationUtils,
    FormatUtils
} from './utils/common-utils.js';

export {
    BinaryReader,
    createBinaryReader
} from './utils/binary-reader.js';

// Lexer
export {
    UsdaLexer,
    Token,
    TokenUtils,
    createLexer
} from './parsers/usda-lexer.js';

// Version information
export const VERSION = '2.0.0';
export const BUILD_DATE = new Date().toISOString();

// Library metadata
export const LIBRARY_INFO = {
    name: 'TinyUSDZ JavaScript Parser',
    version: VERSION,
    buildDate: BUILD_DATE,
    description: 'Pure JavaScript USD parser with USDA and USDC support',
    features: [
        'USDA (ASCII) parsing',
        'USDC (binary) parsing',
        'Time samples support',
        'USD to JSON conversion',
        'JSON schema validation',
        'Memory-safe parsing',
        'Cross-platform support'
    ],
    license: 'Apache 2.0',
    repository: 'https://github.com/lighttransport/tinyusdz'
};

// Convenience functions for common use cases
export class UsdUtils {
    /**
     * Quick parse function that auto-detects format
     */
    static async parse(input, options = {}) {
        const parser = createParser(options);
        return parser.parse(input);
    }

    /**
     * Parse from URL with format detection
     */
    static async parseFromUrl(url, options = {}) {
        return UsdParser.loadFromUrl(url, options);
    }

    /**
     * Parse from file with format detection
     */
    static async parseFromFile(file, options = {}) {
        return UsdParser.loadFromFile(file, options);
    }

    /**
     * Validate USD data structure
     */
    static validate(layer) {
        if (!(layer instanceof UsdLayer)) {
            throw new Error('Expected UsdLayer instance');
        }
        return layer.validate();
    }

    /**
     * Get supported file formats
     */
    static getSupportedFormats() {
        return ['usda', 'usdc', 'usdz'];
    }

    /**
     * Check if format is supported
     */
    static isSupportedFormat(format) {
        return FormatUtils.isSupportedFormat(format);
    }

    /**
     * Create a simple USD layer programmatically
     */
    static createSimpleLayer(primName, primType = 'Xform') {
        const layer = new UsdLayer();
        const rootPrim = new UsdPrim(primName, primType);
        layer.setRootPrim(rootPrim);
        return layer;
    }

    /**
     * Get library information
     */
    static getLibraryInfo() {
        return { ...LIBRARY_INFO };
    }
}

// Default export for convenience
export default {
    // Main classes
    UsdParser,
    UsdLayer,
    UsdPrim,
    UsdAttribute,
    UsdValue,
    UsdTimeSamples,
    UsdUtils,

    // Utilities
    BinaryReader,
    UsdaLexer,
    Logger,

    // Constants
    UsdDataType,
    TokenType,
    VERSION,
    LIBRARY_INFO,

    // Factory functions
    createParser,
    parseUsd,
    parseUsdSync,
    createBinaryReader,
    createLexer
};

// Global configuration for browser environments
if (typeof window !== 'undefined') {
    window.TinyUSDZ = {
        ...LIBRARY_INFO,
        UsdUtils,
        createParser,
        parseUsd
    };
}