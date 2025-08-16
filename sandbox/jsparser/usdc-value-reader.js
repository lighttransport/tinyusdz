/**
 * USDC Value Reader for deserializing binary values
 * Based on the C++ implementation in src/crate-reader.cc
 */

const { BinaryReader } = require('./binary-reader.js');
const { UsdcDataType, UsdcValue, UsdcValueRep } = require('./usdc-parser.js');
const { lz4DecompressSafe } = require('./lz4-decoder.js');

class UsdcValueReader {
    constructor(parser) {
        this.parser = parser;
        this.reader = parser.reader;
        this.layer = parser.layer;
    }

    // Read a value based on its representation
    readValue(valueRep, dataSection = null) {
        if (!(valueRep instanceof UsdcValueRep)) {
            valueRep = new UsdcValueRep(valueRep);
        }

        const typeId = valueRep.getTypeId();
        const payload = valueRep.getPayload();

        if (valueRep.isInlined()) {
            return this.readInlinedValue(typeId, payload);
        } else {
            return this.readNonInlinedValue(typeId, payload, valueRep, dataSection);
        }
    }

    // Read inlined values (small values stored directly in the representation)
    readInlinedValue(typeId, payload) {
        switch (typeId) {
            case UsdcDataType.BOOL:
                return new UsdcValue('bool', payload !== 0n);

            case UsdcDataType.UCHAR:
                return new UsdcValue('uchar', Number(payload & 0xFFn));

            case UsdcDataType.INT:
                // Handle sign extension for 32-bit signed integers
                const int32 = Number(payload & 0xFFFFFFFFn);
                const signedInt = int32 > 0x7FFFFFFF ? int32 - 0x100000000 : int32;
                return new UsdcValue('int', signedInt);

            case UsdcDataType.UINT:
                return new UsdcValue('uint', Number(payload & 0xFFFFFFFFn));

            case UsdcDataType.INT64:
                return new UsdcValue('int64', payload);

            case UsdcDataType.UINT64:
                return new UsdcValue('uint64', payload);

            case UsdcDataType.HALF:
                // Convert 16-bit payload to half-precision float
                const halfValue = this.uint16ToFloat32(Number(payload & 0xFFFFn));
                return new UsdcValue('half', halfValue);

            case UsdcDataType.FLOAT:
                // Convert 32-bit payload to float
                const buffer = new ArrayBuffer(4);
                const view = new DataView(buffer);
                view.setUint32(0, Number(payload & 0xFFFFFFFFn), true);
                return new UsdcValue('float', view.getFloat32(0, true));

            case UsdcDataType.TOKEN:
                // Token index
                const tokenIndex = Number(payload);
                return new UsdcValue('token', this.parser.getToken(tokenIndex));

            default:
                this.parser.warning(`Unsupported inlined type: ${typeId}`);
                return new UsdcValue('unknown', payload);
        }
    }

    // Read non-inlined values (stored in data sections)
    readNonInlinedValue(typeId, payload, valueRep, dataSection) {
        if (!dataSection) {
            // Use improved data section lookup
            dataSection = this.findDataSectionForValue(valueRep);
        }

        if (!dataSection) {
            this.parser.warning('No data section found for non-inlined value');
            return new UsdcValue('unknown', null);
        }

        // Validate data offset
        const dataOffset = Number(payload);
        if (dataOffset < 0 || dataOffset >= Number(dataSection.size)) {
            this.parser.warning(`Invalid data offset ${dataOffset} for section ${dataSection.name} (size: ${dataSection.size})`);
            return new UsdcValue('unknown', null);
        }

        // Seek to the data location
        const savedPosition = this.reader.getPosition();
        
        try {
            this.reader.seek(Number(dataSection.start) + dataOffset);
            
            // Check if we're within bounds
            const remainingInSection = Number(dataSection.size) - dataOffset;
            if (remainingInSection <= 0) {
                this.parser.warning(`Data offset points outside section bounds`);
                return new UsdcValue('unknown', null);
            }
            
            if (valueRep.isArray()) {
                return this.readArrayValue(typeId, valueRep);
            } else {
                return this.readSingleValue(typeId);
            }
        } catch (error) {
            this.parser.warning(`Error reading non-inlined value: ${error.message}`);
            return new UsdcValue('unknown', null);
        } finally {
            this.reader.setPosition(savedPosition);
        }
    }

    // Read single non-inlined value
    readSingleValue(typeId) {
        switch (typeId) {
            case UsdcDataType.DOUBLE:
                return new UsdcValue('double', this.reader.readFloat64());

            case UsdcDataType.STRING:
                const stringLength = this.reader.readUint32();
                if (stringLength > 1024 * 1024) { // 1MB limit for safety
                    this.parser.error(`String too long: ${stringLength}`);
                }
                return new UsdcValue('string', this.reader.readString(stringLength));

            case UsdcDataType.ASSET_PATH:
                const pathLength = this.reader.readUint32();
                if (pathLength > 1024 * 1024) {
                    this.parser.error(`Asset path too long: ${pathLength}`);
                }
                return new UsdcValue('asset_path', this.reader.readString(pathLength));

            case UsdcDataType.VEC2F:
                return new UsdcValue('vec2f', [
                    this.reader.readFloat32(),
                    this.reader.readFloat32()
                ]);

            case UsdcDataType.VEC3F:
                return new UsdcValue('vec3f', [
                    this.reader.readFloat32(),
                    this.reader.readFloat32(),
                    this.reader.readFloat32()
                ]);

            case UsdcDataType.VEC4F:
                return new UsdcValue('vec4f', [
                    this.reader.readFloat32(),
                    this.reader.readFloat32(),
                    this.reader.readFloat32(),
                    this.reader.readFloat32()
                ]);

            case UsdcDataType.VEC2D:
                return new UsdcValue('vec2d', [
                    this.reader.readFloat64(),
                    this.reader.readFloat64()
                ]);

            case UsdcDataType.VEC3D:
                return new UsdcValue('vec3d', [
                    this.reader.readFloat64(),
                    this.reader.readFloat64(),
                    this.reader.readFloat64()
                ]);

            case UsdcDataType.VEC4D:
                return new UsdcValue('vec4d', [
                    this.reader.readFloat64(),
                    this.reader.readFloat64(),
                    this.reader.readFloat64(),
                    this.reader.readFloat64()
                ]);

            case UsdcDataType.VEC2I:
                return new UsdcValue('vec2i', [
                    this.reader.readInt32(),
                    this.reader.readInt32()
                ]);

            case UsdcDataType.VEC3I:
                return new UsdcValue('vec3i', [
                    this.reader.readInt32(),
                    this.reader.readInt32(),
                    this.reader.readInt32()
                ]);

            case UsdcDataType.VEC4I:
                return new UsdcValue('vec4i', [
                    this.reader.readInt32(),
                    this.reader.readInt32(),
                    this.reader.readInt32(),
                    this.reader.readInt32()
                ]);

            case UsdcDataType.QUATF:
                return new UsdcValue('quatf', [
                    this.reader.readFloat32(), // w
                    this.reader.readFloat32(), // x
                    this.reader.readFloat32(), // y
                    this.reader.readFloat32()  // z
                ]);

            case UsdcDataType.QUATD:
                return new UsdcValue('quatd', [
                    this.reader.readFloat64(), // w
                    this.reader.readFloat64(), // x
                    this.reader.readFloat64(), // y
                    this.reader.readFloat64()  // z
                ]);

            case UsdcDataType.MATRIX4D:
                const matrix = [];
                for (let i = 0; i < 16; i++) {
                    matrix.push(this.reader.readFloat64());
                }
                return new UsdcValue('matrix4d', matrix);

            case UsdcDataType.DICTIONARY:
                return this.readDictionary();

            case UsdcDataType.TIME_SAMPLES:
                return this.readTimeSamples();

            default:
                this.parser.warning(`Unsupported single value type: ${typeId}`);
                return new UsdcValue('unknown', null);
        }
    }

    // Read array values
    readArrayValue(typeId, valueRep) {
        const arraySize = this.reader.readUint64();
        
        if (arraySize > 1024 * 1024 * 1024) { // 1B elements max
            this.parser.error(`Array too large: ${arraySize}`);
        }

        const numElements = Number(arraySize);
        const array = [];

        // Handle compressed arrays
        if (valueRep.isCompressed()) {
            return this.readCompressedArray(typeId, numElements);
        }

        // Read uncompressed array
        for (let i = 0; i < numElements; i++) {
            switch (typeId) {
                case UsdcDataType.BOOL:
                    array.push(this.reader.readUint8() !== 0);
                    break;

                case UsdcDataType.UCHAR:
                    array.push(this.reader.readUint8());
                    break;

                case UsdcDataType.INT:
                    array.push(this.reader.readInt32());
                    break;

                case UsdcDataType.UINT:
                    array.push(this.reader.readUint32());
                    break;

                case UsdcDataType.INT64:
                    array.push(this.reader.readInt64());
                    break;

                case UsdcDataType.UINT64:
                    array.push(this.reader.readUint64());
                    break;

                case UsdcDataType.HALF:
                    array.push(this.reader.readFloat16());
                    break;

                case UsdcDataType.FLOAT:
                    array.push(this.reader.readFloat32());
                    break;

                case UsdcDataType.DOUBLE:
                    array.push(this.reader.readFloat64());
                    break;

                case UsdcDataType.VEC3F:
                    array.push([
                        this.reader.readFloat32(),
                        this.reader.readFloat32(),
                        this.reader.readFloat32()
                    ]);
                    break;

                case UsdcDataType.VEC3D:
                    array.push([
                        this.reader.readFloat64(),
                        this.reader.readFloat64(),
                        this.reader.readFloat64()
                    ]);
                    break;

                case UsdcDataType.TOKEN:
                    const tokenIndex = this.reader.readUint32();
                    array.push(this.parser.getToken(tokenIndex));
                    break;

                case UsdcDataType.STRING:
                    const strLength = this.reader.readUint32();
                    array.push(this.reader.readString(strLength));
                    break;

                default:
                    this.parser.warning(`Unsupported array element type: ${typeId}`);
                    array.push(null);
                    break;
            }
        }

        return new UsdcValue(`${this.getTypeName(typeId)}[]`, array);
    }

    // Read compressed array using LZ4 decompression
    readCompressedArray(typeId, numElements) {
        // Read compressed size
        const compressedSize = this.reader.readUint32();
        if (compressedSize <= 0 || compressedSize > 64 * 1024 * 1024) {
            this.parser.warning(`Invalid compressed size: ${compressedSize}`);
            return new UsdcValue(`${this.getTypeName(typeId)}[]`, []);
        }
        
        const compressedData = this.reader.readBytes(compressedSize);
        
        // Decompress using LZ4
        const decompressedData = lz4DecompressSafe(compressedData);
        if (!decompressedData) {
            this.parser.warning('LZ4 decompression failed for compressed array');
            return new UsdcValue(`${this.getTypeName(typeId)}[]`, []);
        }
        
        // Parse decompressed array data
        const decompressedReader = new BinaryReader(decompressedData);
        const array = [];
        
        try {
            for (let i = 0; i < numElements && decompressedReader.remaining() > 0; i++) {
                switch (typeId) {
                    case UsdcDataType.BOOL:
                        array.push(decompressedReader.readUint8() !== 0);
                        break;
                        
                    case UsdcDataType.UCHAR:
                        array.push(decompressedReader.readUint8());
                        break;
                        
                    case UsdcDataType.INT:
                        array.push(decompressedReader.readInt32());
                        break;
                        
                    case UsdcDataType.UINT:
                        array.push(decompressedReader.readUint32());
                        break;
                        
                    case UsdcDataType.INT64:
                        array.push(decompressedReader.readInt64());
                        break;
                        
                    case UsdcDataType.UINT64:
                        array.push(decompressedReader.readUint64());
                        break;
                        
                    case UsdcDataType.HALF:
                        array.push(decompressedReader.readFloat16());
                        break;
                        
                    case UsdcDataType.FLOAT:
                        array.push(decompressedReader.readFloat32());
                        break;
                        
                    case UsdcDataType.DOUBLE:
                        array.push(decompressedReader.readFloat64());
                        break;
                        
                    case UsdcDataType.VEC2F:
                        array.push([
                            decompressedReader.readFloat32(),
                            decompressedReader.readFloat32()
                        ]);
                        break;
                        
                    case UsdcDataType.VEC3F:
                        array.push([
                            decompressedReader.readFloat32(),
                            decompressedReader.readFloat32(),
                            decompressedReader.readFloat32()
                        ]);
                        break;
                        
                    case UsdcDataType.VEC4F:
                        array.push([
                            decompressedReader.readFloat32(),
                            decompressedReader.readFloat32(),
                            decompressedReader.readFloat32(),
                            decompressedReader.readFloat32()
                        ]);
                        break;
                        
                    case UsdcDataType.VEC2D:
                        array.push([
                            decompressedReader.readFloat64(),
                            decompressedReader.readFloat64()
                        ]);
                        break;
                        
                    case UsdcDataType.VEC3D:
                        array.push([
                            decompressedReader.readFloat64(),
                            decompressedReader.readFloat64(),
                            decompressedReader.readFloat64()
                        ]);
                        break;
                        
                    case UsdcDataType.VEC4D:
                        array.push([
                            decompressedReader.readFloat64(),
                            decompressedReader.readFloat64(),
                            decompressedReader.readFloat64(),
                            decompressedReader.readFloat64()
                        ]);
                        break;
                        
                    case UsdcDataType.QUATF:
                        array.push([
                            decompressedReader.readFloat32(), // w
                            decompressedReader.readFloat32(), // x
                            decompressedReader.readFloat32(), // y
                            decompressedReader.readFloat32()  // z
                        ]);
                        break;
                        
                    case UsdcDataType.QUATD:
                        array.push([
                            decompressedReader.readFloat64(), // w
                            decompressedReader.readFloat64(), // x
                            decompressedReader.readFloat64(), // y
                            decompressedReader.readFloat64()  // z
                        ]);
                        break;
                        
                    case UsdcDataType.MATRIX4D:
                        const matrix = [];
                        for (let j = 0; j < 16; j++) {
                            matrix.push(decompressedReader.readFloat64());
                        }
                        array.push(matrix);
                        break;
                        
                    default:
                        this.parser.warning(`Unsupported compressed array element type: ${typeId}`);
                        array.push(null);
                        break;
                }
            }
        } catch (error) {
            this.parser.warning(`Error reading compressed array: ${error.message}`);
        }
        
        return new UsdcValue(`${this.getTypeName(typeId)}[]`, array);
    }

    // Read dictionary value
    readDictionary() {
        const numEntries = this.reader.readUint32();
        const dict = {};

        for (let i = 0; i < numEntries; i++) {
            // Read key (token index)
            const keyTokenIndex = this.reader.readUint32();
            const key = this.parser.getToken(keyTokenIndex);

            // Read value representation
            const valueRep = new UsdcValueRep(this.reader.readUint64());
            const value = this.readValue(valueRep);

            dict[key] = value;
        }

        return new UsdcValue('dictionary', dict);
    }

    // Read time samples value
    readTimeSamples() {
        try {
            // Read number of time samples
            const numSamples = this.reader.readUint32();
            
            if (numSamples > 10000) { // Reasonable limit
                this.parser.warning(`Too many time samples: ${numSamples}`);
                return new UsdcValue('time_samples', null);
            }

            const timeSamples = [];

            for (let i = 0; i < numSamples; i++) {
                // Read time value (typically double)
                const time = this.reader.readFloat64();
                
                // Read value representation for this sample
                const valueRep = new UsdcValueRep(this.reader.readUint64());
                const value = this.readValue(valueRep);
                
                timeSamples.push({
                    time: time,
                    value: value
                });
            }

            // Sort by time to ensure correct ordering
            timeSamples.sort((a, b) => a.time - b.time);

            return new UsdcValue('time_samples', {
                samples: timeSamples,
                interpolation: 'linear' // Default interpolation
            });
            
        } catch (error) {
            this.parser.warning(`Error reading time samples: ${error.message}`);
            return new UsdcValue('time_samples', null);
        }
    }

    // Helper functions
    uint16ToFloat32(uint16) {
        const sign = (uint16 & 0x8000) >> 15;
        const exponent = (uint16 & 0x7C00) >> 10;
        const mantissa = uint16 & 0x03FF;

        if (exponent === 0) {
            if (mantissa === 0) {
                return sign === 0 ? 0.0 : -0.0;
            } else {
                return (sign === 0 ? 1 : -1) * Math.pow(2, -14) * (mantissa / 1024);
            }
        } else if (exponent === 31) {
            if (mantissa === 0) {
                return sign === 0 ? Infinity : -Infinity;
            } else {
                return NaN;
            }
        } else {
            return (sign === 0 ? 1 : -1) * Math.pow(2, exponent - 15) * (1 + mantissa / 1024);
        }
    }

    findDataSection() {
        // Look for common data section names in priority order
        const dataNames = ['DATA', 'VALUES', 'COMPRESSED_DATA', 'BOOTSTRAP'];
        
        for (const name of dataNames) {
            const section = this.parser.toc.getSection(name);
            if (section) {
                return section;
            }
        }

        return null;
    }
    
    // Get all available data sections for more comprehensive data handling
    getAllDataSections() {
        const dataSections = {};
        
        for (const section of this.parser.toc.sections) {
            // Include sections that typically contain value data
            if (section.name.includes('DATA') || 
                section.name.includes('VALUES') || 
                section.name.includes('BOOTSTRAP') ||
                section.name.includes('COMPRESSED')) {
                dataSections[section.name] = section;
            }
        }
        
        return dataSections;
    }
    
    // Improved data section lookup for specific value types
    findDataSectionForValue(valueRep) {
        const dataSections = this.getAllDataSections();
        
        // Try to find the most appropriate section based on value characteristics
        if (valueRep.isCompressed()) {
            // Prefer compressed data sections for compressed values
            if (dataSections['COMPRESSED_DATA']) return dataSections['COMPRESSED_DATA'];
            if (dataSections['DATA']) return dataSections['DATA'];
        } else {
            // Prefer uncompressed sections for uncompressed values
            if (dataSections['DATA']) return dataSections['DATA'];
            if (dataSections['VALUES']) return dataSections['VALUES'];
            if (dataSections['BOOTSTRAP']) return dataSections['BOOTSTRAP'];
        }
        
        // Fall back to any available data section
        const sectionNames = Object.keys(dataSections);
        if (sectionNames.length > 0) {
            return dataSections[sectionNames[0]];
        }
        
        return null;
    }

    getTypeName(typeId) {
        const typeNames = {
            [UsdcDataType.BOOL]: 'bool',
            [UsdcDataType.UCHAR]: 'uchar',
            [UsdcDataType.INT]: 'int',
            [UsdcDataType.UINT]: 'uint',
            [UsdcDataType.INT64]: 'int64',
            [UsdcDataType.UINT64]: 'uint64',
            [UsdcDataType.HALF]: 'half',
            [UsdcDataType.FLOAT]: 'float',
            [UsdcDataType.DOUBLE]: 'double',
            [UsdcDataType.STRING]: 'string',
            [UsdcDataType.TOKEN]: 'token',
            [UsdcDataType.ASSET_PATH]: 'asset_path',
            [UsdcDataType.VEC2F]: 'vec2f',
            [UsdcDataType.VEC3F]: 'vec3f',
            [UsdcDataType.VEC4F]: 'vec4f',
            [UsdcDataType.VEC2D]: 'vec2d',
            [UsdcDataType.VEC3D]: 'vec3d',
            [UsdcDataType.VEC4D]: 'vec4d',
            [UsdcDataType.VEC2I]: 'vec2i',
            [UsdcDataType.VEC3I]: 'vec3i',
            [UsdcDataType.VEC4I]: 'vec4i',
            [UsdcDataType.QUATF]: 'quatf',
            [UsdcDataType.QUATD]: 'quatd',
            [UsdcDataType.MATRIX4D]: 'matrix4d',
            [UsdcDataType.DICTIONARY]: 'dictionary',
            [UsdcDataType.TIME_SAMPLES]: 'time_samples'
        };

        return typeNames[typeId] || 'unknown';
    }
}

// Export for Node.js and browser
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { UsdcValueReader };
} else if (typeof window !== 'undefined') {
    window.UsdcValueReader = UsdcValueReader;
}