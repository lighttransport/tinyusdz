/**
 * Pure JavaScript LZ4 Decoder
 * 
 * Implements LZ4 block decompression format as specified in:
 * https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md
 * 
 * This implementation is focused on the LZ4 block format used by TinyUSDZ.
 */

class Lz4DecodeError extends Error {
    constructor(message, position = 0) {
        super(message);
        this.name = 'Lz4DecodeError';
        this.position = position;
    }
}

class Lz4Decoder {
    constructor() {
        this.errors = [];
    }

    error(message, position = 0) {
        const err = new Lz4DecodeError(message, position);
        this.errors.push(err);
        throw err;
    }

    /**
     * Decode LZ4 block format
     * @param {Uint8Array} src - Compressed data
     * @param {number} maxOutputSize - Maximum output size for safety
     * @returns {Uint8Array} - Decompressed data
     */
    decompressBlock(src, maxOutputSize = 64 * 1024 * 1024) {
        if (!src || src.length === 0) {
            this.error('Empty input data');
        }

        let srcPos = 0;
        const srcEnd = src.length;
        let output = new Uint8Array(Math.min(maxOutputSize, srcEnd * 4)); // Initial guess
        let outputPos = 0;

        // LZ4 block format parsing
        while (srcPos < srcEnd) {
            if (srcPos >= srcEnd) {
                break;
            }

            // Read token byte
            const token = src[srcPos++];
            if (srcPos > srcEnd) {
                this.error('Unexpected end of input while reading token', srcPos);
            }

            // Extract literal length (upper 4 bits)
            let literalLength = (token >> 4) & 0x0F;
            
            // If literal length is 15, read additional bytes
            if (literalLength === 15) {
                let addByte;
                do {
                    if (srcPos >= srcEnd) {
                        this.error('Unexpected end of input while reading literal length', srcPos);
                    }
                    addByte = src[srcPos++];
                    literalLength += addByte;
                } while (addByte === 255);
            }

            // Check bounds for literal copy
            if (srcPos + literalLength > srcEnd) {
                this.error(`Literal extends beyond input: ${srcPos + literalLength} > ${srcEnd}`, srcPos);
            }

            // Ensure output buffer has enough space
            if (outputPos + literalLength > output.length) {
                const newSize = Math.max(output.length * 2, outputPos + literalLength);
                if (newSize > maxOutputSize) {
                    this.error(`Output would exceed maximum size: ${newSize} > ${maxOutputSize}`, outputPos);
                }
                const newOutput = new Uint8Array(newSize);
                newOutput.set(output.subarray(0, outputPos));
                output = newOutput;
            }

            // Copy literals
            for (let i = 0; i < literalLength; i++) {
                output[outputPos++] = src[srcPos++];
            }

            // Check if we've reached the end
            if (srcPos >= srcEnd) {
                break;
            }

            // Read offset (little-endian 16-bit)
            if (srcPos + 2 > srcEnd) {
                this.error('Unexpected end of input while reading offset', srcPos);
            }
            const offset = src[srcPos] | (src[srcPos + 1] << 8);
            srcPos += 2;

            if (offset === 0) {
                this.error('Invalid offset: 0', srcPos - 2);
            }

            if (offset > outputPos) {
                this.error(`Offset points before output start: ${offset} > ${outputPos}`, srcPos - 2);
            }

            // Extract match length (lower 4 bits) 
            let matchLength = (token & 0x0F) + 4; // Minimum match length is 4

            // If match length field is 15, read additional bytes
            if ((token & 0x0F) === 15) {
                let addByte;
                do {
                    if (srcPos >= srcEnd) {
                        this.error('Unexpected end of input while reading match length', srcPos);
                    }
                    addByte = src[srcPos++];
                    matchLength += addByte;
                } while (addByte === 255);
            }

            // Ensure output buffer has enough space for match
            if (outputPos + matchLength > output.length) {
                const newSize = Math.max(output.length * 2, outputPos + matchLength);
                if (newSize > maxOutputSize) {
                    this.error(`Output would exceed maximum size: ${newSize} > ${maxOutputSize}`, outputPos);
                }
                const newOutput = new Uint8Array(newSize);
                newOutput.set(output.subarray(0, outputPos));
                output = newOutput;
            }

            // Copy match data
            const matchStart = outputPos - offset;
            
            // Handle overlapping matches (copy byte by byte)
            for (let i = 0; i < matchLength; i++) {
                output[outputPos++] = output[matchStart + i];
            }
        }

        // Return exact-sized result
        return output.subarray(0, outputPos);
    }

    /**
     * TinyUSDZ-specific LZ4 wrapper decoder
     * Handles the custom chunk format used by TinyUSDZ
     * @param {Uint8Array} src - Compressed data with TinyUSDZ wrapper
     * @param {number} maxOutputSize - Maximum output size for safety
     * @returns {Uint8Array} - Decompressed data
     */
    decompressTinyUsdz(src, maxOutputSize = 64 * 1024 * 1024) {
        if (!src || src.length <= 1) {
            this.error('Invalid TinyUSDZ LZ4 data: too short');
        }

        // Read number of chunks (first byte)
        const numChunks = src[0];
        const compressedData = src.subarray(1);

        if (numChunks > 127) {
            this.error(`Too many chunks: ${numChunks} > 127`);
        }

        if (numChunks === 0) {
            // Single chunk - direct LZ4 decompression
            return this.decompressBlock(compressedData, maxOutputSize);
        } else {
            // Multiple chunks - concatenate decompressed results
            let srcPos = 0;
            let totalOutput = new Uint8Array(0);
            
            for (let chunk = 0; chunk < numChunks; chunk++) {
                if (srcPos + 4 > compressedData.length) {
                    this.error(`Chunk ${chunk}: Not enough data for size header`, srcPos);
                }

                // Read chunk size (little-endian 32-bit)
                const chunkSize = compressedData[srcPos] | 
                                (compressedData[srcPos + 1] << 8) |
                                (compressedData[srcPos + 2] << 16) |
                                (compressedData[srcPos + 3] << 24);
                srcPos += 4;

                if (chunkSize <= 0 || chunkSize > compressedData.length - srcPos) {
                    this.error(`Chunk ${chunk}: Invalid size ${chunkSize}`, srcPos - 4);
                }

                // Decompress this chunk
                const chunkData = compressedData.subarray(srcPos, srcPos + chunkSize);
                const decompressedChunk = this.decompressBlock(chunkData, maxOutputSize - totalOutput.length);
                
                // Concatenate results
                const newTotal = new Uint8Array(totalOutput.length + decompressedChunk.length);
                newTotal.set(totalOutput);
                newTotal.set(decompressedChunk, totalOutput.length);
                totalOutput = newTotal;

                srcPos += chunkSize;

                if (totalOutput.length > maxOutputSize) {
                    this.error(`Total output exceeds maximum size: ${totalOutput.length} > ${maxOutputSize}`);
                }
            }

            return totalOutput;
        }
    }

    /**
     * Decompress with automatic format detection
     * @param {Uint8Array} src - Compressed data
     * @param {number} maxOutputSize - Maximum output size for safety
     * @returns {Uint8Array} - Decompressed data
     */
    decompress(src, maxOutputSize = 64 * 1024 * 1024) {
        try {
            // Try TinyUSDZ format first
            return this.decompressTinyUsdz(src, maxOutputSize);
        } catch (error) {
            // If that fails, try direct LZ4 block
            try {
                return this.decompressBlock(src, maxOutputSize);
            } catch (blockError) {
                // Return the original TinyUSDZ error as it's more likely correct
                throw error;
            }
        }
    }

    getErrors() {
        return this.errors;
    }

    /**
     * Create a simple test compressed data for verification
     * This creates data that represents: "Hello, World! Hello, World!"
     * using LZ4 format (simplified for testing)
     */
    static createTestData() {
        // This is a manually created LZ4 block for testing
        // Format: literals "Hello, World! " + match (offset=14, length=13)
        const testData = new Uint8Array([
            // Token: 14 literals (0xE0), 0 match length initially
            0xE0,
            // 14 literal bytes: "Hello, World! "
            0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20,
            0x57, 0x6F, 0x72, 0x6C, 0x64, 0x21, 0x20,
            // Offset: 14 (little-endian)
            0x0E, 0x00,
            // Token: 0 literals, match length 13-4=9 (0x09)
            0x09
        ]);

        // Wrap in TinyUSDZ format (single chunk)
        const wrapped = new Uint8Array(testData.length + 1);
        wrapped[0] = 0; // 0 chunks = single chunk mode
        wrapped.set(testData, 1);

        return wrapped;
    }
}

// Utility functions for integration

/**
 * Convenience function for LZ4 decompression
 * @param {Uint8Array} compressedData - Compressed data
 * @param {number} maxOutputSize - Maximum output size
 * @returns {Uint8Array} - Decompressed data
 */
function lz4Decompress(compressedData, maxOutputSize = 64 * 1024 * 1024) {
    const decoder = new Lz4Decoder();
    return decoder.decompress(compressedData, maxOutputSize);
}

/**
 * Decompress LZ4 data with error handling
 * @param {Uint8Array} compressedData - Compressed data
 * @param {number} maxOutputSize - Maximum output size  
 * @returns {Uint8Array|null} - Decompressed data or null on error
 */
function lz4DecompressSafe(compressedData, maxOutputSize = 64 * 1024 * 1024) {
    try {
        return lz4Decompress(compressedData, maxOutputSize);
    } catch (error) {
        console.error('LZ4 decompression error:', error.message);
        return null;
    }
}

// Export for Node.js and browser
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { 
        Lz4Decoder, 
        Lz4DecodeError,
        lz4Decompress, 
        lz4DecompressSafe 
    };
} else if (typeof window !== 'undefined') {
    window.Lz4Decoder = Lz4Decoder;
    window.Lz4DecodeError = Lz4DecodeError;
    window.lz4Decompress = lz4Decompress;
    window.lz4DecompressSafe = lz4DecompressSafe;
}