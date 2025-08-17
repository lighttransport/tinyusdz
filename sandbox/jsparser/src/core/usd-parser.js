/**
 * Main USD Parser Entry Point
 * Unified interface for parsing USD files in various formats
 */

import { UsdLayer } from '../types/usd-data-structures.js';
import { UsdParseError, UsdIOError } from '../types/usd-types.js';
import { FormatUtils, Logger, PerformanceTracker } from '../utils/common-utils.js';

// Parser imports (will be implemented in separate files)
// import { UsdaParser } from '../parsers/usda-parser.js';
// import { UsdcParser } from '../parsers/usdc-parser.js';

export class UsdParser {
    constructor(options = {}) {
        this.options = {
            autoDetectFormat: options.autoDetectFormat !== false,
            strictMode: options.strictMode || false,
            memoryLimit: options.memoryLimit || 2 * 1024 * 1024 * 1024, // 2GB
            timeoutMs: options.timeoutMs || 30000, // 30 seconds
            enableValidation: options.enableValidation !== false,
            preserveComments: options.preserveComments || false,
            ...options
        };

        this.logger = new Logger('UsdParser', options.logLevel);
        this.perf = new PerformanceTracker();
        this.errors = [];
        this.warnings = [];
    }

    // Main parsing interface
    async parse(input, format = null) {
        this.clearErrors();
        
        return this.perf.measureAsync('total_parse', async () => {
            try {
                // Detect format if not specified
                const detectedFormat = format || this.detectFormat(input);
                this.logger.info(`Parsing USD file in ${detectedFormat} format`);

                // Parse based on format
                let layer;
                switch (detectedFormat) {
                    case 'usda':
                        layer = await this.parseUsda(input);
                        break;
                    case 'usdc':
                        layer = await this.parseUsdc(input);
                        break;
                    case 'usdz':
                        layer = await this.parseUsdz(input);
                        break;
                    default:
                        throw new UsdParseError(`Unsupported format: ${detectedFormat}`);
                }

                // Validate if enabled
                if (this.options.enableValidation && layer) {
                    await this.validateLayer(layer);
                }

                this.logger.info('Parsing completed successfully');
                return layer;

            } catch (error) {
                this.addError(error);
                this.logger.error(`Parsing failed: ${error.message}`);
                
                if (this.options.strictMode) {
                    throw error;
                }
                
                return null;
            }
        });
    }

    // Synchronous parsing for simple cases
    parseSync(input, format = null) {
        this.clearErrors();
        
        return this.perf.measure('total_parse_sync', () => {
            try {
                const detectedFormat = format || this.detectFormat(input);
                this.logger.info(`Parsing USD file in ${detectedFormat} format (sync)`);

                let layer;
                switch (detectedFormat) {
                    case 'usda':
                        layer = this.parseUsdaSync(input);
                        break;
                    case 'usdc':
                        layer = this.parseUsdcSync(input);
                        break;
                    default:
                        throw new UsdParseError(`Synchronous parsing not supported for format: ${detectedFormat}`);
                }

                if (this.options.enableValidation && layer) {
                    this.validateLayerSync(layer);
                }

                return layer;

            } catch (error) {
                this.addError(error);
                if (this.options.strictMode) {
                    throw error;
                }
                return null;
            }
        });
    }

    // Format detection
    detectFormat(input) {
        if (!this.options.autoDetectFormat) {
            throw new UsdIOError('Format detection disabled, must specify format explicitly');
        }

        const format = FormatUtils.detectUsdFormat(input);
        if (format === 'unknown') {
            throw new UsdIOError('Unable to detect USD format');
        }

        return format;
    }

    // USDA parsing
    async parseUsda(input) {
        return this.perf.measureAsync('parse_usda', async () => {
            // Import dynamically to avoid circular dependencies
            const { UsdaParser } = await import('../parsers/usda-parser.js');
            
            const parser = new UsdaParser(input, this.options);
            return parser.parse();
        });
    }

    parseUsdaSync(input) {
        return this.perf.measure('parse_usda_sync', () => {
            // For now, throw error as we need to implement sync version
            throw new UsdParseError('Synchronous USDA parsing not yet implemented');
        });
    }

    // USDC parsing
    async parseUsdc(input) {
        return this.perf.measureAsync('parse_usdc', async () => {
            const { UsdcParser } = await import('../parsers/usdc-parser.js');
            
            const parser = new UsdcParser(input, this.options);
            return parser.parse();
        });
    }

    parseUsdcSync(input) {
        return this.perf.measure('parse_usdc_sync', () => {
            throw new UsdParseError('Synchronous USDC parsing not yet implemented');
        });
    }

    // USDZ parsing (ZIP archive)
    async parseUsdz(input) {
        return this.perf.measureAsync('parse_usdz', async () => {
            throw new UsdParseError('USDZ format not yet implemented');
        });
    }

    // Validation
    async validateLayer(layer) {
        return this.perf.measureAsync('validate_layer', async () => {
            const errors = layer.validate();
            if (errors.length > 0) {
                errors.forEach(error => this.addError(new UsdParseError(error)));
                if (this.options.strictMode) {
                    throw new UsdParseError(`Validation failed: ${errors.join(', ')}`);
                }
            }
        });
    }

    validateLayerSync(layer) {
        return this.perf.measure('validate_layer_sync', () => {
            const errors = layer.validate();
            if (errors.length > 0) {
                errors.forEach(error => this.addError(new UsdParseError(error)));
                if (this.options.strictMode) {
                    throw new UsdParseError(`Validation failed: ${errors.join(', ')}`);
                }
            }
        });
    }

    // Error management
    addError(error) {
        this.errors.push(error);
    }

    addWarning(warning) {
        this.warnings.push(warning);
    }

    clearErrors() {
        this.errors = [];
        this.warnings = [];
    }

    hasErrors() {
        return this.errors.length > 0;
    }

    hasWarnings() {
        return this.warnings.length > 0;
    }

    getErrors() {
        return [...this.errors];
    }

    getWarnings() {
        return [...this.warnings];
    }

    // Statistics and performance
    getStatistics() {
        return {
            errors: this.errors.length,
            warnings: this.warnings.length,
            performance: this.perf.getAllStats(),
            options: this.options
        };
    }

    // Utility methods
    static async loadFromUrl(url, options = {}) {
        try {
            const response = await fetch(url);
            if (!response.ok) {
                throw new UsdIOError(`Failed to fetch ${url}: ${response.statusText}`);
            }

            const contentType = response.headers.get('content-type');
            let data;

            if (contentType && contentType.includes('text')) {
                data = await response.text();
            } else {
                data = await response.arrayBuffer();
            }

            const parser = new UsdParser(options);
            return parser.parse(data);

        } catch (error) {
            throw new UsdIOError(`Failed to load USD from URL: ${error.message}`);
        }
    }

    static async loadFromFile(file, options = {}) {
        try {
            let data;
            
            if (file instanceof File) {
                // Browser File API
                const fileExtension = FormatUtils.getFileExtension(file.name);
                if (fileExtension === 'usda') {
                    data = await file.text();
                } else {
                    data = await file.arrayBuffer();
                }
            } else if (typeof file === 'string') {
                // Node.js file path
                const fs = await import('fs');
                const path = await import('path');
                
                const fileExtension = FormatUtils.getFileExtension(file);
                if (fileExtension === 'usda') {
                    data = fs.readFileSync(file, 'utf-8');
                } else {
                    data = fs.readFileSync(file);
                }
            } else {
                throw new UsdIOError('Invalid file input type');
            }

            const parser = new UsdParser(options);
            return parser.parse(data);

        } catch (error) {
            throw new UsdIOError(`Failed to load USD from file: ${error.message}`);
        }
    }

    // Streaming interface for large files
    static createStreamingParser(options = {}) {
        return new StreamingUsdParser(options);
    }
}

// Streaming parser for large files
class StreamingUsdParser {
    constructor(options = {}) {
        this.options = options;
        this.chunks = [];
        this.totalSize = 0;
        this.maxChunkSize = options.maxChunkSize || 1024 * 1024; // 1MB chunks
    }

    addChunk(chunk) {
        this.chunks.push(chunk);
        this.totalSize += chunk.length || chunk.byteLength;
    }

    async parse() {
        // Combine chunks and parse
        let combined;
        
        if (this.chunks[0] instanceof ArrayBuffer || this.chunks[0] instanceof Uint8Array) {
            // Binary data
            combined = new Uint8Array(this.totalSize);
            let offset = 0;
            
            for (const chunk of this.chunks) {
                const view = chunk instanceof ArrayBuffer ? new Uint8Array(chunk) : chunk;
                combined.set(view, offset);
                offset += view.length;
            }
        } else {
            // Text data
            combined = this.chunks.join('');
        }

        const parser = new UsdParser(this.options);
        return parser.parse(combined);
    }
}

// Factory functions for convenience
export function createParser(options = {}) {
    return new UsdParser(options);
}

export async function parseUsd(input, options = {}) {
    const parser = new UsdParser(options);
    return parser.parse(input);
}

export function parseUsdSync(input, options = {}) {
    const parser = new UsdParser(options);
    return parser.parseSync(input);
}

// Re-export key classes for convenience
export { UsdLayer } from '../types/usd-data-structures.js';
export { UsdParseError, UsdIOError } from '../types/usd-types.js';