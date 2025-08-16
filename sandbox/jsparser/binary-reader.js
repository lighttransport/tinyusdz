/**
 * Binary data reader utilities for JavaScript USDC parser
 * Provides safe, bounds-checked binary data reading from ArrayBuffer/Uint8Array
 */

class BinaryReader {
    constructor(buffer) {
        if (buffer instanceof ArrayBuffer) {
            this.buffer = buffer;
            this.view = new DataView(buffer);
            this.uint8View = new Uint8Array(buffer);
        } else if (buffer instanceof Uint8Array) {
            this.buffer = buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength);
            this.view = new DataView(this.buffer);
            this.uint8View = new Uint8Array(this.buffer);
        } else {
            throw new Error('Buffer must be ArrayBuffer or Uint8Array');
        }
        
        this.position = 0;
        this.length = this.buffer.byteLength;
    }

    // Position management
    getPosition() {
        return this.position;
    }

    setPosition(pos) {
        if (pos < 0 || pos > this.length) {
            throw new Error(`Invalid position ${pos}, buffer length is ${this.length}`);
        }
        this.position = pos;
    }

    seek(offset) {
        this.setPosition(offset);
    }

    skip(bytes) {
        this.setPosition(this.position + bytes);
    }

    remaining() {
        return this.length - this.position;
    }

    isEof() {
        return this.position >= this.length;
    }

    // Bounds checking
    checkBounds(size) {
        if (this.position + size > this.length) {
            throw new Error(`Read beyond buffer bounds: position ${this.position}, size ${size}, buffer length ${this.length}`);
        }
    }

    // Basic data type readers (little-endian)
    readUint8() {
        this.checkBounds(1);
        const value = this.view.getUint8(this.position);
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
        const value = this.view.getUint16(this.position, true); // little-endian
        this.position += 2;
        return value;
    }

    readInt16() {
        this.checkBounds(2);
        const value = this.view.getInt16(this.position, true); // little-endian
        this.position += 2;
        return value;
    }

    readUint32() {
        this.checkBounds(4);
        const value = this.view.getUint32(this.position, true); // little-endian
        this.position += 4;
        return value;
    }

    readInt32() {
        this.checkBounds(4);
        const value = this.view.getInt32(this.position, true); // little-endian
        this.position += 4;
        return value;
    }

    readUint64() {
        this.checkBounds(8);
        const value = this.view.getBigUint64(this.position, true); // little-endian
        this.position += 8;
        return value;
    }

    readInt64() {
        this.checkBounds(8);
        const value = this.view.getBigInt64(this.position, true); // little-endian
        this.position += 8;
        return value;
    }

    readFloat32() {
        this.checkBounds(4);
        const value = this.view.getFloat32(this.position, true); // little-endian
        this.position += 4;
        return value;
    }

    readFloat64() {
        this.checkBounds(8);
        const value = this.view.getFloat64(this.position, true); // little-endian
        this.position += 8;
        return value;
    }

    // Half-precision float (16-bit IEEE 754)
    readFloat16() {
        const uint16 = this.readUint16();
        return this.uint16ToFloat32(uint16);
    }

    // Convert IEEE 754 half-precision to single-precision
    uint16ToFloat32(uint16) {
        const sign = (uint16 & 0x8000) >> 15;
        const exponent = (uint16 & 0x7C00) >> 10;
        const mantissa = uint16 & 0x03FF;

        if (exponent === 0) {
            if (mantissa === 0) {
                // Zero
                return sign === 0 ? 0.0 : -0.0;
            } else {
                // Subnormal number
                return (sign === 0 ? 1 : -1) * Math.pow(2, -14) * (mantissa / 1024);
            }
        } else if (exponent === 31) {
            if (mantissa === 0) {
                // Infinity
                return sign === 0 ? Infinity : -Infinity;
            } else {
                // NaN
                return NaN;
            }
        } else {
            // Normal number
            return (sign === 0 ? 1 : -1) * Math.pow(2, exponent - 15) * (1 + mantissa / 1024);
        }
    }

    // Array readers
    readBytes(size) {
        this.checkBounds(size);
        const bytes = this.uint8View.slice(this.position, this.position + size);
        this.position += size;
        return bytes;
    }

    readString(size) {
        const bytes = this.readBytes(size);
        return new TextDecoder('utf-8').decode(bytes);
    }

    // Read null-terminated string
    readCString() {
        const start = this.position;
        while (this.position < this.length && this.uint8View[this.position] !== 0) {
            this.position++;
        }
        
        if (this.position >= this.length) {
            throw new Error('Unterminated C string');
        }
        
        const length = this.position - start;
        const str = this.readString(length);
        this.readUint8(); // consume null terminator
        return str;
    }

    // Read string with length prefix
    readLengthPrefixedString() {
        const length = this.readUint32();
        if (length === 0) {
            return '';
        }
        return this.readString(length);
    }

    // Alignment helpers
    alignTo(alignment) {
        const remainder = this.position % alignment;
        if (remainder !== 0) {
            this.skip(alignment - remainder);
        }
    }

    // Create a sub-reader for a specific range
    slice(start, length) {
        if (start < 0 || start >= this.length) {
            throw new Error(`Invalid slice start ${start}`);
        }
        if (length < 0 || start + length > this.length) {
            throw new Error(`Invalid slice length ${length}`);
        }
        
        const slicedBuffer = this.buffer.slice(start, start + length);
        return new BinaryReader(slicedBuffer);
    }

    // Read fixed-size arrays
    readUint8Array(count) {
        const array = new Array(count);
        for (let i = 0; i < count; i++) {
            array[i] = this.readUint8();
        }
        return array;
    }

    readUint32Array(count) {
        const array = new Array(count);
        for (let i = 0; i < count; i++) {
            array[i] = this.readUint32();
        }
        return array;
    }

    readFloat32Array(count) {
        const array = new Array(count);
        for (let i = 0; i < count; i++) {
            array[i] = this.readFloat32();
        }
        return array;
    }

    readFloat64Array(count) {
        const array = new Array(count);
        for (let i = 0; i < count; i++) {
            array[i] = this.readFloat64();
        }
        return array;
    }

    // Debug helpers
    hexDump(start = 0, length = Math.min(256, this.length)) {
        const end = Math.min(start + length, this.length);
        let result = '';
        
        for (let i = start; i < end; i += 16) {
            // Address
            result += i.toString(16).padStart(8, '0') + ': ';
            
            // Hex bytes
            let hex = '';
            let ascii = '';
            for (let j = 0; j < 16 && i + j < end; j++) {
                const byte = this.uint8View[i + j];
                hex += byte.toString(16).padStart(2, '0') + ' ';
                ascii += (byte >= 32 && byte <= 126) ? String.fromCharCode(byte) : '.';
            }
            
            result += hex.padEnd(48, ' ') + ' |' + ascii + '|\n';
        }
        
        return result;
    }

    toString() {
        return `BinaryReader(position=${this.position}, length=${this.length})`;
    }
}

// Export for Node.js and browser
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { BinaryReader };
} else if (typeof window !== 'undefined') {
    window.BinaryReader = BinaryReader;
}