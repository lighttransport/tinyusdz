/**
 * Enhanced Binary Reader with improved performance and error handling
 */

import { UsdIOError } from '../types/usd-types.js';
import { Logger } from './common-utils.js';

export class BinaryReader {
    constructor(buffer, options = {}) {
        if (buffer instanceof ArrayBuffer) {
            this.buffer = buffer;
            this.view = new DataView(buffer);
            this.bytes = new Uint8Array(buffer);
        } else if (buffer instanceof Uint8Array) {
            this.buffer = buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength);
            this.view = new DataView(this.buffer);
            this.bytes = new Uint8Array(this.buffer);
        } else {
            throw new UsdIOError('Buffer must be ArrayBuffer or Uint8Array');
        }

        this.position = 0;
        this.littleEndian = options.littleEndian !== false; // default to little endian
        this.logger = new Logger('BinaryReader', options.logLevel);
        this.enableBoundsChecking = options.enableBoundsChecking !== false;
        
        this.logger.debug(`Initialized with ${this.buffer.byteLength} bytes`);
    }

    // Position management
    getPosition() {
        return this.position;
    }

    setPosition(position) {
        if (this.enableBoundsChecking && (position < 0 || position > this.buffer.byteLength)) {
            throw new UsdIOError(`Invalid position: ${position} (buffer size: ${this.buffer.byteLength})`);
        }
        this.position = position;
    }

    seek(position) {
        this.setPosition(position);
    }

    skip(bytes) {
        this.setPosition(this.position + bytes);
    }

    remaining() {
        return this.buffer.byteLength - this.position;
    }

    isEOF() {
        return this.position >= this.buffer.byteLength;
    }

    // Bounds checking
    checkBounds(size) {
        if (!this.enableBoundsChecking) return;
        
        if (this.position + size > this.buffer.byteLength) {
            throw new UsdIOError(
                `Read would exceed buffer bounds: position=${this.position}, ` +
                `size=${size}, buffer=${this.buffer.byteLength}`
            );
        }
    }

    // Basic data type reading
    readUint8() {
        this.checkBounds(1);
        const value = this.bytes[this.position];
        this.position += 1;
        return value;
    }

    readInt8() {
        this.checkBounds(1);
        const value = this.view.getInt8(this.position);
        this.position += 1;
        return value;
    }

    readUint16() {
        this.checkBounds(2);
        const value = this.view.getUint16(this.position, this.littleEndian);
        this.position += 2;
        return value;
    }

    readInt16() {
        this.checkBounds(2);
        const value = this.view.getInt16(this.position, this.littleEndian);
        this.position += 2;
        return value;
    }

    readUint32() {
        this.checkBounds(4);
        const value = this.view.getUint32(this.position, this.littleEndian);
        this.position += 4;
        return value;
    }

    readInt32() {
        this.checkBounds(4);
        const value = this.view.getInt32(this.position, this.littleEndian);
        this.position += 4;
        return value;
    }

    readUint64() {
        this.checkBounds(8);
        const value = this.view.getBigUint64(this.position, this.littleEndian);
        this.position += 8;
        return value;
    }

    readInt64() {
        this.checkBounds(8);
        const value = this.view.getBigInt64(this.position, this.littleEndian);
        this.position += 8;
        return value;
    }

    readFloat32() {
        this.checkBounds(4);
        const value = this.view.getFloat32(this.position, this.littleEndian);
        this.position += 4;
        return value;
    }

    readFloat64() {
        this.checkBounds(8);
        const value = this.view.getFloat64(this.position, this.littleEndian);
        this.position += 8;
        return value;
    }

    // Half-precision float (16-bit)
    readFloat16() {
        const uint16 = this.readUint16();
        return this.uint16ToFloat32(uint16);
    }

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

    // String reading
    readString(length, encoding = 'utf-8') {
        if (length === 0) return '';
        
        this.checkBounds(length);
        const bytes = this.bytes.slice(this.position, this.position + length);
        this.position += length;

        try {
            return new TextDecoder(encoding).decode(bytes);
        } catch (error) {
            this.logger.warn(`Failed to decode string with ${encoding}: ${error.message}`);
            // Fallback to ASCII
            return String.fromCharCode(...bytes);
        }
    }

    // Null-terminated string
    readCString(maxLength = 1024, encoding = 'utf-8') {
        const start = this.position;
        let length = 0;

        while (length < maxLength && this.position < this.buffer.byteLength) {
            if (this.bytes[this.position] === 0) {
                break;
            }
            this.position++;
            length++;
        }

        if (length === maxLength) {
            this.logger.warn(`C-string exceeded maximum length: ${maxLength}`);
        }

        // Read the string content
        this.setPosition(start);
        const result = length > 0 ? this.readString(length, encoding) : '';
        
        // Skip null terminator if present
        if (this.position < this.buffer.byteLength && this.bytes[this.position] === 0) {
            this.position++;
        }

        return result;
    }

    // Raw byte reading
    readBytes(length) {
        if (length === 0) return new Uint8Array(0);
        
        this.checkBounds(length);
        const bytes = this.bytes.slice(this.position, this.position + length);
        this.position += length;
        return bytes;
    }

    // Peek operations (read without advancing position)
    peekUint8() {
        this.checkBounds(1);
        return this.bytes[this.position];
    }

    peekUint16() {
        this.checkBounds(2);
        return this.view.getUint16(this.position, this.littleEndian);
    }

    peekUint32() {
        this.checkBounds(4);
        return this.view.getUint32(this.position, this.littleEndian);
    }

    peekBytes(length) {
        this.checkBounds(length);
        return this.bytes.slice(this.position, this.position + length);
    }

    // Array reading with optimization
    readUint8Array(length) {
        if (length === 0) return new Uint8Array(0);
        
        this.checkBounds(length);
        const result = new Uint8Array(length);
        result.set(this.bytes.subarray(this.position, this.position + length));
        this.position += length;
        return result;
    }

    readUint16Array(length) {
        if (length === 0) return new Uint16Array(0);
        
        const byteLength = length * 2;
        this.checkBounds(byteLength);
        
        const result = new Uint16Array(length);
        for (let i = 0; i < length; i++) {
            result[i] = this.view.getUint16(this.position + i * 2, this.littleEndian);
        }
        this.position += byteLength;
        return result;
    }

    readUint32Array(length) {
        if (length === 0) return new Uint32Array(0);
        
        const byteLength = length * 4;
        this.checkBounds(byteLength);
        
        const result = new Uint32Array(length);
        for (let i = 0; i < length; i++) {
            result[i] = this.view.getUint32(this.position + i * 4, this.littleEndian);
        }
        this.position += byteLength;
        return result;
    }

    readFloat32Array(length) {
        if (length === 0) return new Float32Array(0);
        
        const byteLength = length * 4;
        this.checkBounds(byteLength);
        
        const result = new Float32Array(length);
        for (let i = 0; i < length; i++) {
            result[i] = this.view.getFloat32(this.position + i * 4, this.littleEndian);
        }
        this.position += byteLength;
        return result;
    }

    readFloat64Array(length) {
        if (length === 0) return new Float64Array(0);
        
        const byteLength = length * 8;
        this.checkBounds(byteLength);
        
        const result = new Float64Array(length);
        for (let i = 0; i < length; i++) {
            result[i] = this.view.getFloat64(this.position + i * 8, this.littleEndian);
        }
        this.position += byteLength;
        return result;
    }

    // Utility methods
    align(alignment) {
        const misalignment = this.position % alignment;
        if (misalignment !== 0) {
            this.skip(alignment - misalignment);
        }
    }

    clone() {
        const cloned = new BinaryReader(this.buffer.slice(), {
            littleEndian: this.littleEndian,
            enableBoundsChecking: this.enableBoundsChecking
        });
        cloned.setPosition(this.position);
        return cloned;
    }

    // Debug utilities
    hexDump(start = 0, length = 64) {
        const end = Math.min(start + length, this.buffer.byteLength);
        const lines = [];
        
        for (let i = start; i < end; i += 16) {
            const lineEnd = Math.min(i + 16, end);
            const hexBytes = [];
            const asciiChars = [];
            
            for (let j = i; j < lineEnd; j++) {
                const byte = this.bytes[j];
                hexBytes.push(byte.toString(16).padStart(2, '0'));
                asciiChars.push(byte >= 32 && byte <= 126 ? String.fromCharCode(byte) : '.');
            }
            
            // Pad hex output to maintain alignment
            while (hexBytes.length < 16) {
                hexBytes.push('  ');
            }
            
            const offset = i.toString(16).padStart(8, '0');
            const hex = hexBytes.join(' ');
            const ascii = asciiChars.join('');
            
            lines.push(`${offset}: ${hex} |${ascii}|`);
        }
        
        return lines.join('\n');
    }

    getStats() {
        return {
            bufferSize: this.buffer.byteLength,
            position: this.position,
            remaining: this.remaining(),
            progress: (this.position / this.buffer.byteLength) * 100
        };
    }

    // Stream-like interface
    pipe(writer, chunkSize = 8192) {
        const chunks = [];
        
        while (!this.isEOF()) {
            const remainingBytes = this.remaining();
            const readSize = Math.min(chunkSize, remainingBytes);
            const chunk = this.readBytes(readSize);
            
            if (writer && typeof writer.write === 'function') {
                writer.write(chunk);
            } else {
                chunks.push(chunk);
            }
        }
        
        return chunks;
    }

    // Safe reading with error recovery
    safeRead(readFn, defaultValue = null) {
        const savedPosition = this.position;
        
        try {
            return readFn.call(this);
        } catch (error) {
            this.logger.warn(`Safe read failed at position ${savedPosition}: ${error.message}`);
            this.setPosition(savedPosition);
            return defaultValue;
        }
    }

    // Batch reading for performance
    readBatch(operations) {
        const results = [];
        const savedPosition = this.position;
        
        try {
            for (const op of operations) {
                if (typeof op === 'function') {
                    results.push(op.call(this));
                } else if (typeof op === 'object' && op.method) {
                    const args = op.args || [];
                    results.push(this[op.method](...args));
                }
            }
            return results;
        } catch (error) {
            this.setPosition(savedPosition);
            throw error;
        }
    }
}

// Factory function for convenience
export function createBinaryReader(buffer, options = {}) {
    return new BinaryReader(buffer, options);
}