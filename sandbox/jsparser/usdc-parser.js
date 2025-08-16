/**
 * USDC (USD Binary/Crate) Parser for JavaScript
 * Based on the C++ implementation in src/crate-reader.cc and sandbox/c/usdc_parser.c
 */

const { BinaryReader } = require('./binary-reader.js');
const { lz4DecompressSafe } = require('./lz4-decoder.js');

// USDC File Format Constants
const USDC_MAGIC = 'PXR-USDC';
const USDC_MAGIC_SIZE = 8;
const USDC_VERSION_SIZE = 8;
const USDC_TOC_OFFSET_SIZE = 8;
const USDC_HEADER_SIZE = USDC_MAGIC_SIZE + USDC_VERSION_SIZE + USDC_TOC_OFFSET_SIZE;

// Security limits (matching C implementation)
const USDC_LIMITS = {
    MAX_TOC_SECTIONS: 32,
    MAX_TOKENS: 1024 * 1024 * 64,        // 64M tokens
    MAX_STRINGS: 1024 * 1024 * 64,       // 64M strings  
    MAX_FIELDS: 1024 * 1024 * 256,       // 256M fields
    MAX_PATHS: 1024 * 1024 * 256,        // 256M paths
    MAX_SPECS: 1024 * 1024 * 256,        // 256M specs
    MAX_FIELDSETS: 1024 * 1024 * 64,     // 64M fieldsets
    MAX_STRING_LENGTH: 1024 * 1024 * 64, // 64MB string
    MAX_MEMORY_BUDGET: 2 * 1024 * 1024 * 1024 // 2GB
};

// USDC Data Types (matching crate-format.hh)
const UsdcDataType = {
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
};

// Value representation bit flags
const VALUE_IS_ARRAY_BIT = 1n << 63n;
const VALUE_IS_INLINED_BIT = 1n << 62n;
const VALUE_IS_COMPRESSED_BIT = 1n << 61n;
const VALUE_PAYLOAD_MASK = (1n << 48n) - 1n;

class UsdcParseError extends Error {
    constructor(message, position = 0) {
        super(message);
        this.name = 'UsdcParseError';
        this.position = position;
    }
}

class UsdcHeader {
    constructor() {
        this.magic = '';
        this.version = new Uint8Array(8);
        this.tocOffset = 0n;
    }
}

class UsdcSection {
    constructor() {
        this.name = '';
        this.start = 0n;
        this.size = 0n;
    }
}

class UsdcTableOfContents {
    constructor() {
        this.sections = [];
    }

    getSection(name) {
        return this.sections.find(section => section.name === name);
    }
}

class UsdcValueRep {
    constructor(data = 0n) {
        this.data = BigInt(data);
    }

    isArray() {
        return (this.data & VALUE_IS_ARRAY_BIT) !== 0n;
    }

    isInlined() {
        return (this.data & VALUE_IS_INLINED_BIT) !== 0n;
    }

    isCompressed() {
        return (this.data & VALUE_IS_COMPRESSED_BIT) !== 0n;
    }

    getTypeId() {
        return Number((this.data >> 48n) & 0xFFn);
    }

    getPayload() {
        return this.data & VALUE_PAYLOAD_MASK;
    }
}

class UsdcValue {
    constructor(type, value) {
        this.type = type;
        this.value = value;
    }
}

class UsdcField {
    constructor() {
        this.token = 0;
        this.valueRep = new UsdcValueRep();
    }
}

class UsdcFieldSet {
    constructor() {
        this.fields = [];
    }
}

class UsdcSpec {
    constructor() {
        this.path = 0;
        this.fieldSet = 0;
        this.specType = 0;
    }
}

class UsdcPath {
    constructor() {
        this.indices = [];
        this.isAbsolute = false;
    }

    toString() {
        let result = this.isAbsolute ? '/' : '';
        return result + this.indices.join('/');
    }
}

class UsdcTimeSamples {
    constructor() {
        this.times = [];      // Array of time values (numbers)
        this.values = [];     // Array of values corresponding to times
        this.interpolation = 'linear'; // Interpolation method
    }
    
    addSample(time, value) {
        this.times.push(time);
        this.values.push(value);
    }
    
    getSampleAtTime(time) {
        if (this.times.length === 0) return null;
        
        // Find exact match
        const exactIndex = this.times.indexOf(time);
        if (exactIndex !== -1) {
            return this.values[exactIndex];
        }
        
        // Find interpolated value
        if (this.interpolation === 'linear' && this.times.length >= 2) {
            return this.interpolateLinear(time);
        }
        
        // Return nearest sample
        return this.getNearestSample(time);
    }
    
    interpolateLinear(time) {
        // Find surrounding samples
        let beforeIndex = -1;
        let afterIndex = -1;
        
        for (let i = 0; i < this.times.length - 1; i++) {
            if (this.times[i] <= time && this.times[i + 1] >= time) {
                beforeIndex = i;
                afterIndex = i + 1;
                break;
            }
        }
        
        if (beforeIndex === -1) {
            return this.getNearestSample(time);
        }
        
        const t0 = this.times[beforeIndex];
        const t1 = this.times[afterIndex];
        const v0 = this.values[beforeIndex];
        const v1 = this.values[afterIndex];
        
        // Linear interpolation factor
        const factor = (time - t0) / (t1 - t0);
        
        // Interpolate based on value type
        return this.interpolateValues(v0, v1, factor);
    }
    
    interpolateValues(v0, v1, factor) {
        if (typeof v0 === 'number' && typeof v1 === 'number') {
            return v0 + (v1 - v0) * factor;
        }
        
        if (Array.isArray(v0) && Array.isArray(v1) && v0.length === v1.length) {
            const result = [];
            for (let i = 0; i < v0.length; i++) {
                if (typeof v0[i] === 'number' && typeof v1[i] === 'number') {
                    result.push(v0[i] + (v1[i] - v0[i]) * factor);
                } else {
                    result.push(factor < 0.5 ? v0[i] : v1[i]);
                }
            }
            return result;
        }
        
        // For non-numeric values, return nearest
        return factor < 0.5 ? v0 : v1;
    }
    
    getNearestSample(time) {
        if (this.times.length === 0) return null;
        
        let nearestIndex = 0;
        let nearestDistance = Math.abs(this.times[0] - time);
        
        for (let i = 1; i < this.times.length; i++) {
            const distance = Math.abs(this.times[i] - time);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestIndex = i;
            }
        }
        
        return this.values[nearestIndex];
    }
}

class UsdcLayer {
    constructor() {
        this.tokens = [];
        this.strings = [];
        this.paths = [];
        this.fields = [];
        this.fieldSets = [];
        this.specs = [];
        this.metadata = {};
        this.timeSamples = new Map(); // fieldIndex -> UsdcTimeSamples
    }
}

class UsdcParser {
    constructor(buffer) {
        this.reader = new BinaryReader(buffer);
        this.header = new UsdcHeader();
        this.toc = new UsdcTableOfContents();
        this.layer = new UsdcLayer();
        this.errors = [];
        this.warnings = [];
        this.memoryUsed = 0;
    }

    error(message) {
        const err = new UsdcParseError(message, this.reader.getPosition());
        this.errors.push(err);
        throw err;
    }

    warning(message) {
        this.warnings.push(`Warning at position ${this.reader.getPosition()}: ${message}`);
    }

    checkMemoryLimit(additionalBytes) {
        if (this.memoryUsed + additionalBytes > USDC_LIMITS.MAX_MEMORY_BUDGET) {
            this.error('Memory limit exceeded');
        }
        this.memoryUsed += additionalBytes;
    }

    // Parse USDC file header
    parseHeader() {
        // Read magic
        const magicBytes = this.reader.readBytes(USDC_MAGIC_SIZE);
        this.header.magic = new TextDecoder('ascii').decode(magicBytes);
        
        if (this.header.magic !== USDC_MAGIC) {
            this.error(`Invalid USDC magic: expected "${USDC_MAGIC}", got "${this.header.magic}"`);
        }

        // Read version
        this.header.version = this.reader.readBytes(USDC_VERSION_SIZE);
        
        // Read TOC offset
        this.header.tocOffset = this.reader.readUint64();
        
        return this.header;
    }

    // Parse Table of Contents
    parseTableOfContents() {
        this.reader.seek(Number(this.header.tocOffset));

        // Read number of sections
        const numSections = this.reader.readUint64();
        
        if (numSections > USDC_LIMITS.MAX_TOC_SECTIONS) {
            this.error(`Too many TOC sections: ${numSections} > ${USDC_LIMITS.MAX_TOC_SECTIONS}`);
        }

        // Read sections
        for (let i = 0; i < numSections; i++) {
            const section = new UsdcSection();
            
            // Read section name (null-terminated, max 16 chars)
            const nameBytes = this.reader.readBytes(16);
            const nullIndex = nameBytes.indexOf(0);
            const nameLength = nullIndex >= 0 ? nullIndex : 16;
            section.name = new TextDecoder('ascii').decode(nameBytes.slice(0, nameLength));
            
            // Read section start and size
            section.start = this.reader.readUint64();
            section.size = this.reader.readUint64();
            
            this.toc.sections.push(section);
        }

        return this.toc;
    }

    // Parse tokens section
    parseTokens() {
        const section = this.toc.getSection('TOKENS');
        if (!section) {
            this.warning('No TOKENS section found');
            return;
        }

        this.reader.seek(Number(section.start));
        
        // Read compressed tokens
        const compressedSize = Number(section.size);
        const compressedData = this.reader.readBytes(compressedSize);
        
        // Decompress using LZ4 (simplified - would need actual LZ4 implementation)
        const decompressedData = this.decompressLZ4(compressedData);
        
        // Parse decompressed tokens
        this.parseDecompressedTokens(decompressedData);
    }

    // LZ4 decompression using our pure JavaScript implementation
    decompressLZ4(compressedData) {
        try {
            // Add some debugging for the compressed data format
            if (compressedData.length > 0) {
                const firstByte = compressedData[0];
                console.log(`LZ4 debug: compressed size=${compressedData.length}, first byte=0x${firstByte.toString(16)}`);
            }
            
            const decompressed = lz4DecompressSafe(compressedData);
            if (!decompressed) {
                this.warning('LZ4 decompression failed - using uncompressed data');
                return compressedData; // Fall back to uncompressed
            }
            return decompressed;
        } catch (error) {
            this.warning(`LZ4 decompression error: ${error.message} - using uncompressed data`);
            return compressedData; // Fall back to uncompressed
        }
    }

    // Parse decompressed token data
    parseDecompressedTokens(data) {
        const reader = new BinaryReader(data);
        
        // Check magic ";-)" - but this might not be present in all versions
        if (reader.remaining() < 3) {
            this.error('Invalid token data - too short');
        }
        
        const magic = reader.readString(3);
        if (magic !== ';-)') {
            // Try parsing without magic (older format)
            this.warning(`No token magic found (got "${magic}") - trying legacy format`);
            reader.setPosition(0); // Reset to start
            this.parseTokensLegacyFormat(reader);
            return;
        }

        // Read token count
        const numTokens = reader.readUint32();
        
        if (numTokens > USDC_LIMITS.MAX_TOKENS) {
            this.error(`Too many tokens: ${numTokens} > ${USDC_LIMITS.MAX_TOKENS}`);
        }

        this.checkMemoryLimit(numTokens * 100); // rough estimate

        // Read tokens
        for (let i = 0; i < numTokens; i++) {
            const length = reader.readUint32();
            
            if (length > USDC_LIMITS.MAX_STRING_LENGTH) {
                this.error(`Token too long: ${length} > ${USDC_LIMITS.MAX_STRING_LENGTH}`);
            }
            
            const token = reader.readString(length);
            this.layer.tokens.push(token);
        }
    }

    // Parse tokens in legacy format (no magic marker)
    parseTokensLegacyFormat(reader) {
        // In legacy format, tokens are stored as null-terminated strings
        // We need to estimate the number by scanning through the data
        const tokens = [];
        
        while (reader.remaining() > 0) {
            try {
                const tokenStart = reader.getPosition();
                let tokenStr = '';
                
                // Read until null terminator or end of data
                while (reader.remaining() > 0) {
                    const byte = reader.readUint8();
                    if (byte === 0) {
                        break;
                    }
                    tokenStr += String.fromCharCode(byte);
                }
                
                if (tokenStr.length > 0) {
                    tokens.push(tokenStr);
                }
                
                // Break if we've read too many tokens (safety check)
                if (tokens.length > 10000) {
                    this.warning('Stopping token parsing - too many tokens found');
                    break;
                }
            } catch (error) {
                this.warning(`Error reading legacy token: ${error.message}`);
                break;
            }
        }
        
        this.layer.tokens = tokens;
        console.log(`Parsed ${tokens.length} tokens in legacy format`);
    }

    // Parse strings section
    parseStrings() {
        const section = this.toc.getSection('STRINGS');
        if (!section) {
            this.warning('No STRINGS section found');
            return;
        }

        this.reader.seek(Number(section.start));
        
        // Read compressed strings
        const compressedSize = Number(section.size);
        const compressedData = this.reader.readBytes(compressedSize);
        
        // Decompress and parse similar to tokens
        const decompressedData = this.decompressLZ4(compressedData);
        this.parseDecompressedStrings(decompressedData);
    }

    parseDecompressedStrings(data) {
        const reader = new BinaryReader(data);
        
        // Read string count
        const numStrings = reader.readUint32();
        
        if (numStrings > USDC_LIMITS.MAX_STRINGS) {
            this.error(`Too many strings: ${numStrings} > ${USDC_LIMITS.MAX_STRINGS}`);
        }

        this.checkMemoryLimit(numStrings * 100); // rough estimate

        // Read strings
        for (let i = 0; i < numStrings; i++) {
            const length = reader.readUint32();
            
            if (length > USDC_LIMITS.MAX_STRING_LENGTH) {
                this.error(`String too long: ${length} > ${USDC_LIMITS.MAX_STRING_LENGTH}`);
            }
            
            const str = reader.readString(length);
            this.layer.strings.push(str);
        }
    }

    // Parse fields section
    parseFields() {
        const section = this.toc.getSection('FIELDS');
        if (!section) {
            this.warning('No FIELDS section found');
            return;
        }

        this.reader.seek(Number(section.start));
        
        const numFields = Number(section.size) / 16; // Each field is 16 bytes
        
        if (numFields > USDC_LIMITS.MAX_FIELDS) {
            this.error(`Too many fields: ${numFields} > ${USDC_LIMITS.MAX_FIELDS}`);
        }

        this.checkMemoryLimit(numFields * 16);

        for (let i = 0; i < numFields; i++) {
            const field = new UsdcField();
            field.token = this.reader.readUint32();
            this.reader.readUint32(); // padding
            field.valueRep = new UsdcValueRep(this.reader.readUint64());
            this.layer.fields.push(field);
        }
    }

    // Parse fieldsets section
    parseFieldSets() {
        const section = this.toc.getSection('FIELDSETS');
        if (!section) {
            this.warning('No FIELDSETS section found');
            return;
        }

        this.reader.seek(Number(section.start));
        
        // FieldSets have variable size, so we need to parse the structure
        const endPosition = Number(section.start + section.size);
        let fieldSetIndex = 0;
        
        while (this.reader.getPosition() < endPosition) {
            if (fieldSetIndex > USDC_LIMITS.MAX_FIELDSETS) {
                this.error(`Too many fieldsets: ${fieldSetIndex} > ${USDC_LIMITS.MAX_FIELDSETS}`);
            }

            const fieldSet = new UsdcFieldSet();
            const numFields = this.reader.readUint32();
            
            for (let i = 0; i < numFields; i++) {
                const fieldIndex = this.reader.readUint32();
                fieldSet.fields.push(fieldIndex);
            }
            
            this.layer.fieldSets.push(fieldSet);
            fieldSetIndex++;
        }
    }

    // Parse specs section  
    parseSpecs() {
        const section = this.toc.getSection('SPECS');
        if (!section) {
            this.warning('No SPECS section found');
            return;
        }

        this.reader.seek(Number(section.start));
        
        const numSpecs = Number(section.size) / 16; // Each spec is 16 bytes
        
        if (numSpecs > USDC_LIMITS.MAX_SPECS) {
            this.error(`Too many specs: ${numSpecs} > ${USDC_LIMITS.MAX_SPECS}`);
        }

        this.checkMemoryLimit(numSpecs * 16);

        for (let i = 0; i < numSpecs; i++) {
            const spec = new UsdcSpec();
            spec.path = this.reader.readUint32();
            spec.fieldSet = this.reader.readUint32();
            spec.specType = this.reader.readUint32();
            this.reader.readUint32(); // padding
            this.layer.specs.push(spec);
        }
    }

    // Parse paths section
    parsePaths() {
        const section = this.toc.getSection('PATHS');
        if (!section) {
            this.warning('No PATHS section found');
            return;
        }

        this.reader.seek(Number(section.start));
        
        // Paths are variable-length encoded
        const endPosition = Number(section.start + section.size);
        let pathIndex = 0;
        
        while (this.reader.getPosition() < endPosition) {
            if (pathIndex > USDC_LIMITS.MAX_PATHS) {
                this.error(`Too many paths: ${pathIndex} > ${USDC_LIMITS.MAX_PATHS}`);
            }

            const path = this.parsePath();
            this.layer.paths.push(path);
            pathIndex++;
        }
    }

    parsePath() {
        const path = new UsdcPath();
        
        // Check if we have enough data for header
        if (this.reader.remaining() < 4) {
            this.warning('Not enough data for path header');
            return path;
        }
        
        // Read path flags and element count
        const header = this.reader.readUint32();
        path.isAbsolute = (header & 1) !== 0;
        const numElements = header >> 1;
        
        // More sophisticated validation for number of elements
        if (numElements > 1000) {
            this.warning(`Suspicious number of path elements: ${numElements}`);
            return path;
        }
        
        // Additional validation - check if the header looks reasonable
        if (numElements > 0 && numElements * 4 > this.reader.remaining()) {
            // This might be malformed data - try to recover
            const maxPossibleElements = Math.floor(this.reader.remaining() / 4);
            if (maxPossibleElements > 0) {
                this.warning(`Adjusting path elements from ${numElements} to ${maxPossibleElements} due to insufficient data`);
                const adjustedElements = Math.min(numElements, maxPossibleElements);
                return this.parsePathElements(path, adjustedElements);
            } else {
                this.warning(`Not enough data for any path elements`);
                return path;
            }
        }
        
        return this.parsePathElements(path, numElements);
    }
    
    parsePathElements(path, numElements) {
        // Read path elements (token indices) with validation
        for (let i = 0; i < numElements; i++) {
            if (this.reader.remaining() < 4) {
                this.warning(`Stopping path parsing - insufficient data for element ${i}`);
                break;
            }
            
            const tokenIndex = this.reader.readUint32();
            
            // More sophisticated token index validation
            if (tokenIndex < this.layer.tokens.length && tokenIndex >= 0) {
                const token = this.layer.tokens[tokenIndex];
                // Additional validation - check if token looks like a valid path component
                if (token && typeof token === 'string' && token.length < 1000) {
                    path.indices.push(token);
                } else {
                    this.warning(`Invalid token content for path at index ${tokenIndex}`);
                    path.indices.push(`<invalid:${tokenIndex}>`);
                }
            } else if (tokenIndex === 0xFFFFFFFF || tokenIndex === 0) {
                // Special markers that might be valid
                path.indices.push(tokenIndex === 0 ? '<root>' : '<invalid>');
            } else {
                // Check if this looks like corrupted data
                if (tokenIndex > 1000000) { // Arbitrary large number indicating likely corruption
                    this.warning(`Path parsing stopped due to corrupted token index: ${tokenIndex}`);
                    break;
                } else {
                    this.warning(`Invalid token index in path: ${tokenIndex} (max: ${this.layer.tokens.length - 1})`);
                    path.indices.push(`<invalid:${tokenIndex}>`);
                }
            }
        }
        
        return path;
    }

    // Main parsing function
    parse() {
        try {
            // Parse header
            this.parseHeader();
            
            // Parse table of contents
            this.parseTableOfContents();
            
            // Parse sections in order
            this.parseTokens();
            this.parseStrings();
            this.parseFields();
            this.parseFieldSets();
            this.parseSpecs();
            this.parsePaths();
            
            return this.layer;
            
        } catch (error) {
            if (error instanceof UsdcParseError) {
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

    // Helper to get readable token
    getToken(index) {
        if (index >= 0 && index < this.layer.tokens.length) {
            return this.layer.tokens[index];
        }
        return `<invalid:${index}>`;
    }

    // Helper to get readable string
    getString(index) {
        if (index >= 0 && index < this.layer.strings.length) {
            return this.layer.strings[index];
        }
        return `<invalid:${index}>`;
    }

    // Helper to get readable path
    getPath(index) {
        if (index >= 0 && index < this.layer.paths.length) {
            return this.layer.paths[index].toString();
        }
        return `<invalid:${index}>`;
    }
}

// Export for Node.js and browser
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { 
        UsdcParser, 
        UsdcLayer, 
        UsdcHeader, 
        UsdcTableOfContents,
        UsdcSection,
        UsdcField,
        UsdcFieldSet,
        UsdcSpec,
        UsdcPath,
        UsdcValue,
        UsdcValueRep,
        UsdcTimeSamples,
        UsdcDataType,
        UsdcParseError 
    };
} else if (typeof window !== 'undefined') {
    window.UsdcParser = UsdcParser;
    window.UsdcLayer = UsdcLayer;
    window.UsdcHeader = UsdcHeader;
    window.UsdcTableOfContents = UsdcTableOfContents;
    window.UsdcSection = UsdcSection;
    window.UsdcField = UsdcField;
    window.UsdcFieldSet = UsdcFieldSet;
    window.UsdcSpec = UsdcSpec;
    window.UsdcPath = UsdcPath;
    window.UsdcValue = UsdcValue;
    window.UsdcValueRep = UsdcValueRep;
    window.UsdcTimeSamples = UsdcTimeSamples;
    window.UsdcDataType = UsdcDataType;
    window.UsdcParseError = UsdcParseError;
}