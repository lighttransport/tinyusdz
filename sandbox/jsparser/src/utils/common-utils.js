/**
 * Common Utility Functions
 * Shared utilities used across the USD parser
 */

import { UsdError } from '../types/usd-types.js';

/**
 * Logger utility with configurable levels
 */
export class Logger {
    static Level = {
        ERROR: 0,
        WARN: 1,
        INFO: 2,
        DEBUG: 3
    };

    constructor(name, level = Logger.Level.INFO) {
        this.name = name;
        this.level = level;
    }

    error(message, ...args) {
        if (this.level >= Logger.Level.ERROR) {
            console.error(`[${this.name}] ERROR:`, message, ...args);
        }
    }

    warn(message, ...args) {
        if (this.level >= Logger.Level.WARN) {
            console.warn(`[${this.name}] WARN:`, message, ...args);
        }
    }

    info(message, ...args) {
        if (this.level >= Logger.Level.INFO) {
            console.info(`[${this.name}] INFO:`, message, ...args);
        }
    }

    debug(message, ...args) {
        if (this.level >= Logger.Level.DEBUG) {
            console.debug(`[${this.name}] DEBUG:`, message, ...args);
        }
    }
}

/**
 * Memory usage tracker
 */
export class MemoryTracker {
    constructor(maxBudget = 2 * 1024 * 1024 * 1024) { // 2GB default
        this.maxBudget = maxBudget;
        this.used = 0;
        this.allocations = new Map();
    }

    allocate(id, bytes) {
        if (this.used + bytes > this.maxBudget) {
            throw new UsdError(`Memory budget exceeded: ${this.used + bytes} > ${this.maxBudget}`);
        }
        
        this.used += bytes;
        this.allocations.set(id, bytes);
        return true;
    }

    deallocate(id) {
        const bytes = this.allocations.get(id);
        if (bytes) {
            this.used -= bytes;
            this.allocations.delete(id);
        }
        return bytes || 0;
    }

    getUsage() {
        return {
            used: this.used,
            budget: this.maxBudget,
            percent: (this.used / this.maxBudget) * 100,
            allocations: this.allocations.size
        };
    }

    reset() {
        this.used = 0;
        this.allocations.clear();
    }
}

/**
 * Performance measurement utility
 */
export class PerformanceTracker {
    constructor() {
        this.timers = new Map();
        this.measurements = new Map();
    }

    start(label) {
        this.timers.set(label, performance.now());
    }

    end(label) {
        const startTime = this.timers.get(label);
        if (startTime === undefined) {
            throw new Error(`Timer '${label}' was not started`);
        }

        const duration = performance.now() - startTime;
        this.timers.delete(label);

        if (!this.measurements.has(label)) {
            this.measurements.set(label, []);
        }
        this.measurements.get(label).push(duration);

        return duration;
    }

    measure(label, fn) {
        this.start(label);
        try {
            const result = fn();
            this.end(label);
            return result;
        } catch (error) {
            this.timers.delete(label); // Clean up on error
            throw error;
        }
    }

    async measureAsync(label, fn) {
        this.start(label);
        try {
            const result = await fn();
            this.end(label);
            return result;
        } catch (error) {
            this.timers.delete(label); // Clean up on error
            throw error;
        }
    }

    getStats(label) {
        const measurements = this.measurements.get(label);
        if (!measurements || measurements.length === 0) {
            return null;
        }

        const sorted = [...measurements].sort((a, b) => a - b);
        const sum = measurements.reduce((a, b) => a + b, 0);

        return {
            count: measurements.length,
            total: sum,
            average: sum / measurements.length,
            min: sorted[0],
            max: sorted[sorted.length - 1],
            median: sorted[Math.floor(sorted.length / 2)]
        };
    }

    getAllStats() {
        const stats = {};
        for (const label of this.measurements.keys()) {
            stats[label] = this.getStats(label);
        }
        return stats;
    }

    reset() {
        this.timers.clear();
        this.measurements.clear();
    }
}

/**
 * String utilities
 */
export class StringUtils {
    static isWhitespace(char) {
        return /\s/.test(char);
    }

    static isAlpha(char) {
        return /[a-zA-Z_]/.test(char);
    }

    static isAlnum(char) {
        return /[a-zA-Z0-9_:]/.test(char);
    }

    static isDigit(char) {
        return /[0-9]/.test(char);
    }

    static isHexDigit(char) {
        return /[0-9a-fA-F]/.test(char);
    }

    static escapeString(str) {
        return str
            .replace(/\\/g, '\\\\')
            .replace(/"/g, '\\"')
            .replace(/\n/g, '\\n')
            .replace(/\t/g, '\\t')
            .replace(/\r/g, '\\r');
    }

    static unescapeString(str) {
        return str
            .replace(/\\n/g, '\n')
            .replace(/\\t/g, '\t')
            .replace(/\\r/g, '\r')
            .replace(/\\"/g, '"')
            .replace(/\\\\/g, '\\');
    }

    static indent(text, spaces = 4) {
        const indentStr = ' '.repeat(spaces);
        return text.split('\n').map(line => indentStr + line).join('\n');
    }

    static dedent(text) {
        const lines = text.split('\n');
        const nonEmptyLines = lines.filter(line => line.trim().length > 0);
        
        if (nonEmptyLines.length === 0) return text;

        const minIndent = Math.min(...nonEmptyLines.map(line => {
            const match = line.match(/^(\s*)/);
            return match ? match[1].length : 0;
        }));

        return lines.map(line => 
            line.length >= minIndent ? line.substring(minIndent) : line
        ).join('\n');
    }
}

/**
 * Array utilities
 */
export class ArrayUtils {
    static chunk(array, size) {
        const chunks = [];
        for (let i = 0; i < array.length; i += size) {
            chunks.push(array.slice(i, i + size));
        }
        return chunks;
    }

    static flatten(array) {
        return array.reduce((acc, item) => 
            Array.isArray(item) ? acc.concat(ArrayUtils.flatten(item)) : acc.concat(item), 
            []
        );
    }

    static unique(array) {
        return [...new Set(array)];
    }

    static groupBy(array, keyFn) {
        return array.reduce((groups, item) => {
            const key = keyFn(item);
            if (!groups[key]) {
                groups[key] = [];
            }
            groups[key].push(item);
            return groups;
        }, {});
    }

    static binarySearch(array, target, compareFn = (a, b) => a - b) {
        let left = 0;
        let right = array.length - 1;

        while (left <= right) {
            const mid = Math.floor((left + right) / 2);
            const cmp = compareFn(array[mid], target);

            if (cmp === 0) {
                return mid;
            } else if (cmp < 0) {
                left = mid + 1;
            } else {
                right = mid - 1;
            }
        }

        return -1;
    }
}

/**
 * Object utilities
 */
export class ObjectUtils {
    static deepClone(obj) {
        if (obj === null || typeof obj !== 'object') {
            return obj;
        }

        if (obj instanceof Date) {
            return new Date(obj.getTime());
        }

        if (obj instanceof Array) {
            return obj.map(item => ObjectUtils.deepClone(item));
        }

        if (obj instanceof Map) {
            const cloned = new Map();
            for (const [key, value] of obj.entries()) {
                cloned.set(key, ObjectUtils.deepClone(value));
            }
            return cloned;
        }

        if (obj instanceof Set) {
            const cloned = new Set();
            for (const value of obj) {
                cloned.add(ObjectUtils.deepClone(value));
            }
            return cloned;
        }

        if (typeof obj === 'object') {
            const cloned = {};
            for (const key in obj) {
                if (obj.hasOwnProperty(key)) {
                    cloned[key] = ObjectUtils.deepClone(obj[key]);
                }
            }
            return cloned;
        }

        return obj;
    }

    static deepEqual(a, b) {
        if (a === b) return true;
        if (a === null || b === null) return a === b;
        if (typeof a !== typeof b) return false;

        if (typeof a === 'object') {
            if (Array.isArray(a) !== Array.isArray(b)) return false;

            if (Array.isArray(a)) {
                if (a.length !== b.length) return false;
                for (let i = 0; i < a.length; i++) {
                    if (!ObjectUtils.deepEqual(a[i], b[i])) return false;
                }
                return true;
            }

            const keysA = Object.keys(a);
            const keysB = Object.keys(b);
            if (keysA.length !== keysB.length) return false;

            for (const key of keysA) {
                if (!keysB.includes(key)) return false;
                if (!ObjectUtils.deepEqual(a[key], b[key])) return false;
            }
            return true;
        }

        return false;
    }

    static getPath(obj, path, defaultValue = undefined) {
        const keys = path.split('.');
        let current = obj;

        for (const key of keys) {
            if (current === null || current === undefined || typeof current !== 'object') {
                return defaultValue;
            }
            current = current[key];
        }

        return current !== undefined ? current : defaultValue;
    }

    static setPath(obj, path, value) {
        const keys = path.split('.');
        const lastKey = keys.pop();
        let current = obj;

        for (const key of keys) {
            if (!(key in current) || typeof current[key] !== 'object') {
                current[key] = {};
            }
            current = current[key];
        }

        current[lastKey] = value;
    }
}

/**
 * Math utilities
 */
export class MathUtils {
    static clamp(value, min, max) {
        return Math.min(Math.max(value, min), max);
    }

    static lerp(a, b, t) {
        return a + (b - a) * t;
    }

    static smoothstep(edge0, edge1, x) {
        const t = MathUtils.clamp((x - edge0) / (edge1 - edge0), 0, 1);
        return t * t * (3 - 2 * t);
    }

    static isPowerOfTwo(n) {
        return n > 0 && (n & (n - 1)) === 0;
    }

    static nextPowerOfTwo(n) {
        if (n <= 0) return 1;
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        return n + 1;
    }

    static degToRad(degrees) {
        return degrees * (Math.PI / 180);
    }

    static radToDeg(radians) {
        return radians * (180 / Math.PI);
    }
}

/**
 * Validation utilities
 */
export class ValidationUtils {
    static isValidIdentifier(str) {
        if (!str || typeof str !== 'string') return false;
        return /^[a-zA-Z_][a-zA-Z0-9_]*$/.test(str);
    }

    static isValidPath(str) {
        if (!str || typeof str !== 'string') return false;
        return /^\/[a-zA-Z0-9_\/]*$/.test(str);
    }

    static isValidUrl(str) {
        try {
            new URL(str);
            return true;
        } catch {
            return false;
        }
    }

    static isValidEmail(str) {
        const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
        return emailRegex.test(str);
    }

    static checkRequired(obj, fields) {
        const missing = [];
        for (const field of fields) {
            if (!(field in obj) || obj[field] === null || obj[field] === undefined) {
                missing.push(field);
            }
        }
        return missing;
    }

    static validateSchema(obj, schema) {
        const errors = [];
        
        for (const [key, rules] of Object.entries(schema)) {
            const value = obj[key];
            
            if (rules.required && (value === null || value === undefined)) {
                errors.push(`Field '${key}' is required`);
                continue;
            }
            
            if (value !== null && value !== undefined) {
                if (rules.type && typeof value !== rules.type) {
                    errors.push(`Field '${key}' must be of type ${rules.type}`);
                }
                
                if (rules.min !== undefined && value < rules.min) {
                    errors.push(`Field '${key}' must be >= ${rules.min}`);
                }
                
                if (rules.max !== undefined && value > rules.max) {
                    errors.push(`Field '${key}' must be <= ${rules.max}`);
                }
                
                if (rules.pattern && !rules.pattern.test(value)) {
                    errors.push(`Field '${key}' does not match required pattern`);
                }
                
                if (rules.enum && !rules.enum.includes(value)) {
                    errors.push(`Field '${key}' must be one of: ${rules.enum.join(', ')}`);
                }
            }
        }
        
        return errors;
    }
}

/**
 * File format detection utilities
 */
export class FormatUtils {
    static detectUsdFormat(data) {
        if (typeof data === 'string') {
            // Check for USDA format
            if (data.includes('def ') || data.includes('over ') || data.includes('class ')) {
                return 'usda';
            }
            return 'unknown';
        }

        if (data instanceof ArrayBuffer || data instanceof Uint8Array) {
            const bytes = data instanceof ArrayBuffer ? new Uint8Array(data) : data;
            
            // Check for USDC magic
            if (bytes.length >= 8) {
                const magic = new TextDecoder('ascii').decode(bytes.slice(0, 8));
                if (magic === 'PXR-USDC') {
                    return 'usdc';
                }
            }
            
            // Check for USDZ (ZIP) format
            if (bytes.length >= 4) {
                const zipMagic = bytes[0] === 0x50 && bytes[1] === 0x4B && 
                                bytes[2] === 0x03 && bytes[3] === 0x04;
                if (zipMagic) {
                    return 'usdz';
                }
            }
            
            return 'unknown';
        }

        return 'unknown';
    }

    static getFileExtension(filename) {
        if (!filename || typeof filename !== 'string') return '';
        const parts = filename.split('.');
        return parts.length > 1 ? parts.pop().toLowerCase() : '';
    }

    static isSupportedFormat(formatOrFilename) {
        const supportedFormats = ['usda', 'usdc', 'usdz'];
        
        if (supportedFormats.includes(formatOrFilename)) {
            return true;
        }
        
        const ext = FormatUtils.getFileExtension(formatOrFilename);
        return supportedFormats.includes(ext);
    }
}