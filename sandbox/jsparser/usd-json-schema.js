/**
 * JSON Schema definitions and validation for USD-JSON conversion
 * Provides schema validation for JSON representations of USD data
 */

class UsdJsonSchemaValidator {
    constructor() {
        this.schemas = this.defineSchemas();
    }

    /**
     * Define JSON schemas for USD data structures
     */
    defineSchemas() {
        return {
            // Main USD JSON schema
            usdJson: {
                type: 'object',
                required: ['format', 'version', 'prims'],
                properties: {
                    format: {
                        type: 'string',
                        enum: ['usda', 'usdc']
                    },
                    version: {
                        type: 'string',
                        pattern: '^\\d+\\.\\d+$'
                    },
                    metadata: {
                        type: 'object',
                        additionalProperties: { $ref: '#/definitions/usdValue' }
                    },
                    prims: {
                        oneOf: [
                            { $ref: '#/definitions/usdaPrim' },
                            { $ref: '#/definitions/usdcLayer' }
                        ]
                    },
                    timeSamples: {
                        type: 'object',
                        additionalProperties: { $ref: '#/definitions/timeSamples' }
                    },
                    statistics: {
                        type: 'object',
                        properties: {
                            generatedAt: { type: 'string', format: 'date-time' },
                            tokenCount: { type: 'integer', minimum: 0 },
                            stringCount: { type: 'integer', minimum: 0 },
                            fieldCount: { type: 'integer', minimum: 0 },
                            specCount: { type: 'integer', minimum: 0 },
                            pathCount: { type: 'integer', minimum: 0 },
                            timeSampleCount: { type: 'integer', minimum: 0 }
                        }
                    }
                },
                definitions: {
                    // USD Value schema
                    usdValue: {
                        type: 'object',
                        required: ['type'],
                        properties: {
                            type: {
                                type: 'string',
                                enum: [
                                    'number', 'string', 'identifier', 'bool',
                                    'array', 'tuple', 'dictionary', 'time_samples'
                                ]
                            },
                            value: {},
                            originalType: { type: 'boolean' }
                        }
                    },

                    // USDA Primitive schema
                    usdaPrim: {
                        type: 'object',
                        required: ['name', 'type'],
                        properties: {
                            name: { type: 'string' },
                            type: { type: 'string' },
                            specifier: {
                                type: 'string',
                                enum: ['def', 'over', 'class']
                            },
                            attributes: {
                                type: 'object',
                                additionalProperties: { $ref: '#/definitions/usdAttribute' }
                            },
                            metadata: {
                                type: 'object',
                                additionalProperties: { $ref: '#/definitions/usdValue' }
                            },
                            children: {
                                type: 'object',
                                additionalProperties: { $ref: '#/definitions/usdaPrim' }
                            }
                        }
                    },

                    // USD Attribute schema
                    usdAttribute: {
                        type: 'object',
                        required: ['type', 'value'],
                        properties: {
                            type: { type: 'string' },
                            value: { $ref: '#/definitions/usdValue' },
                            variability: {
                                type: 'string',
                                enum: ['varying', 'uniform', 'config']
                            },
                            metadata: {
                                type: 'object',
                                additionalProperties: { $ref: '#/definitions/usdValue' }
                            }
                        }
                    },

                    // USDC Layer schema
                    usdcLayer: {
                        type: 'object',
                        properties: {
                            tokens: {
                                type: 'array',
                                items: { type: 'string' }
                            },
                            strings: {
                                type: 'array',
                                items: { type: 'string' }
                            },
                            fields: {
                                type: 'array',
                                items: { $ref: '#/definitions/usdcField' }
                            },
                            specs: {
                                type: 'array',
                                items: { $ref: '#/definitions/usdcSpec' }
                            },
                            paths: {
                                type: 'array',
                                items: { $ref: '#/definitions/usdcPath' }
                            }
                        }
                    },

                    // USDC Field schema
                    usdcField: {
                        type: 'object',
                        required: ['index', 'token'],
                        properties: {
                            index: { type: 'integer', minimum: 0 },
                            token: { type: 'integer', minimum: 0 },
                            tokenName: { type: 'string' },
                            valueRep: { $ref: '#/definitions/usdcValueRep' },
                            value: {}
                        }
                    },

                    // USDC Value Representation schema
                    usdcValueRep: {
                        type: 'object',
                        properties: {
                            data: { type: 'string' },
                            typeId: { type: 'integer', minimum: 0, maximum: 255 },
                            payload: { type: 'string' },
                            isArray: { type: 'boolean' },
                            isInlined: { type: 'boolean' },
                            isCompressed: { type: 'boolean' }
                        }
                    },

                    // USDC Spec schema
                    usdcSpec: {
                        type: 'object',
                        required: ['index'],
                        properties: {
                            index: { type: 'integer', minimum: 0 },
                            path: { type: 'integer', minimum: 0 },
                            pathString: { type: 'string' },
                            fieldSet: { type: 'integer', minimum: 0 },
                            specType: { type: 'integer', minimum: 0 },
                            fields: {
                                type: 'array',
                                items: { type: 'integer', minimum: 0 }
                            }
                        }
                    },

                    // USDC Path schema
                    usdcPath: {
                        type: 'object',
                        required: ['index'],
                        properties: {
                            index: { type: 'integer', minimum: 0 },
                            isAbsolute: { type: 'boolean' },
                            elements: {
                                type: 'array',
                                items: { type: 'string' }
                            },
                            pathString: { type: 'string' }
                        }
                    },

                    // Time Samples schema
                    timeSamples: {
                        type: 'object',
                        required: ['type', 'samples'],
                        properties: {
                            type: {
                                type: 'string',
                                enum: ['time_samples']
                            },
                            interpolation: {
                                type: 'string',
                                enum: ['linear', 'held', 'bezier', 'bspline']
                            },
                            samples: {
                                type: 'array',
                                items: {
                                    type: 'object',
                                    required: ['time', 'value'],
                                    properties: {
                                        time: { type: 'number' },
                                        value: { $ref: '#/definitions/usdValue' }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        };
    }

    /**
     * Validate JSON data against USD JSON schema
     */
    validateUsdJson(jsonData) {
        const errors = [];
        const warnings = [];

        try {
            // Basic structure validation
            if (!this.isObject(jsonData)) {
                errors.push('Root must be an object');
                return { valid: false, errors, warnings };
            }

            // Required fields
            if (!jsonData.format) {
                errors.push('Missing required field: format');
            } else if (!['usda', 'usdc'].includes(jsonData.format)) {
                errors.push(`Invalid format: ${jsonData.format} (must be 'usda' or 'usdc')`);
            }

            if (!jsonData.version) {
                errors.push('Missing required field: version');
            } else if (!/^\d+\.\d+$/.test(jsonData.version)) {
                warnings.push(`Version format may be invalid: ${jsonData.version}`);
            }

            if (!jsonData.prims) {
                errors.push('Missing required field: prims');
            } else {
                // Validate prims based on format
                if (jsonData.format === 'usda') {
                    const primErrors = this.validateUsdaPrim(jsonData.prims);
                    errors.push(...primErrors);
                } else if (jsonData.format === 'usdc') {
                    const layerErrors = this.validateUsdcLayer(jsonData.prims);
                    errors.push(...layerErrors);
                }
            }

            // Optional fields validation
            if (jsonData.metadata) {
                const metadataErrors = this.validateMetadata(jsonData.metadata);
                errors.push(...metadataErrors);
            }

            if (jsonData.timeSamples) {
                const timeSamplesErrors = this.validateTimeSamples(jsonData.timeSamples);
                errors.push(...timeSamplesErrors);
            }

            return {
                valid: errors.length === 0,
                errors,
                warnings
            };

        } catch (error) {
            return {
                valid: false,
                errors: [`Validation error: ${error.message}`],
                warnings
            };
        }
    }

    /**
     * Validate USDA primitive structure
     */
    validateUsdaPrim(prim, path = 'prims') {
        const errors = [];

        if (!this.isObject(prim)) {
            errors.push(`${path} must be an object`);
            return errors;
        }

        // Required fields
        if (!prim.name || typeof prim.name !== 'string') {
            errors.push(`${path}.name must be a non-empty string`);
        }

        if (!prim.type || typeof prim.type !== 'string') {
            errors.push(`${path}.type must be a non-empty string`);
        }

        // Optional fields
        if (prim.specifier && !['def', 'over', 'class'].includes(prim.specifier)) {
            errors.push(`${path}.specifier must be 'def', 'over', or 'class'`);
        }

        if (prim.attributes) {
            if (!this.isObject(prim.attributes)) {
                errors.push(`${path}.attributes must be an object`);
            } else {
                for (const [attrName, attr] of Object.entries(prim.attributes)) {
                    const attrErrors = this.validateUsdAttribute(attr, `${path}.attributes.${attrName}`);
                    errors.push(...attrErrors);
                }
            }
        }

        if (prim.children) {
            if (!this.isObject(prim.children)) {
                errors.push(`${path}.children must be an object`);
            } else {
                for (const [childName, child] of Object.entries(prim.children)) {
                    const childErrors = this.validateUsdaPrim(child, `${path}.children.${childName}`);
                    errors.push(...childErrors);
                }
            }
        }

        return errors;
    }

    /**
     * Validate USD attribute structure
     */
    validateUsdAttribute(attr, path) {
        const errors = [];

        if (!this.isObject(attr)) {
            errors.push(`${path} must be an object`);
            return errors;
        }

        if (!attr.type || typeof attr.type !== 'string') {
            errors.push(`${path}.type must be a non-empty string`);
        }

        if (!attr.value) {
            errors.push(`${path}.value is required`);
        } else {
            const valueErrors = this.validateUsdValue(attr.value, `${path}.value`);
            errors.push(...valueErrors);
        }

        if (attr.variability && !['varying', 'uniform', 'config'].includes(attr.variability)) {
            errors.push(`${path}.variability must be 'varying', 'uniform', or 'config'`);
        }

        return errors;
    }

    /**
     * Validate USD value structure
     */
    validateUsdValue(value, path) {
        const errors = [];

        if (!this.isObject(value)) {
            errors.push(`${path} must be an object`);
            return errors;
        }

        if (!value.type || typeof value.type !== 'string') {
            errors.push(`${path}.type must be a non-empty string`);
        }

        const validTypes = ['number', 'string', 'identifier', 'bool', 'array', 'tuple', 'dictionary', 'time_samples'];
        if (value.type && !validTypes.includes(value.type)) {
            errors.push(`${path}.type must be one of: ${validTypes.join(', ')}`);
        }

        // Type-specific validation
        if (value.type === 'array' && value.value) {
            if (!Array.isArray(value.value)) {
                errors.push(`${path}.value must be an array for array type`);
            }
        }

        if (value.type === 'tuple' && value.value) {
            if (!Array.isArray(value.value)) {
                errors.push(`${path}.value must be an array for tuple type`);
            }
        }

        return errors;
    }

    /**
     * Validate USDC layer structure
     */
    validateUsdcLayer(layer, path = 'prims') {
        const errors = [];

        if (!this.isObject(layer)) {
            errors.push(`${path} must be an object`);
            return errors;
        }

        // Validate arrays
        const arrayFields = ['tokens', 'strings', 'fields', 'specs', 'paths'];
        for (const field of arrayFields) {
            if (layer[field] && !Array.isArray(layer[field])) {
                errors.push(`${path}.${field} must be an array`);
            }
        }

        // Validate field structures
        if (layer.fields) {
            layer.fields.forEach((field, index) => {
                if (!this.isObject(field)) {
                    errors.push(`${path}.fields[${index}] must be an object`);
                } else {
                    if (typeof field.index !== 'number' || field.index < 0) {
                        errors.push(`${path}.fields[${index}].index must be a non-negative number`);
                    }
                    if (typeof field.token !== 'number' || field.token < 0) {
                        errors.push(`${path}.fields[${index}].token must be a non-negative number`);
                    }
                }
            });
        }

        return errors;
    }

    /**
     * Validate metadata structure
     */
    validateMetadata(metadata, path = 'metadata') {
        const errors = [];

        if (!this.isObject(metadata)) {
            errors.push(`${path} must be an object`);
            return errors;
        }

        for (const [key, value] of Object.entries(metadata)) {
            const valueErrors = this.validateUsdValue(value, `${path}.${key}`);
            errors.push(...valueErrors);
        }

        return errors;
    }

    /**
     * Validate time samples structure
     */
    validateTimeSamples(timeSamples, path = 'timeSamples') {
        const errors = [];

        if (!this.isObject(timeSamples)) {
            errors.push(`${path} must be an object`);
            return errors;
        }

        for (const [key, samples] of Object.entries(timeSamples)) {
            if (!this.isObject(samples)) {
                errors.push(`${path}.${key} must be an object`);
                continue;
            }

            if (samples.type !== 'time_samples') {
                errors.push(`${path}.${key}.type must be 'time_samples'`);
            }

            if (!Array.isArray(samples.samples)) {
                errors.push(`${path}.${key}.samples must be an array`);
            } else {
                samples.samples.forEach((sample, index) => {
                    if (!this.isObject(sample)) {
                        errors.push(`${path}.${key}.samples[${index}] must be an object`);
                    } else {
                        if (typeof sample.time !== 'number') {
                            errors.push(`${path}.${key}.samples[${index}].time must be a number`);
                        }
                        if (!sample.value) {
                            errors.push(`${path}.${key}.samples[${index}].value is required`);
                        }
                    }
                });
            }
        }

        return errors;
    }

    /**
     * Helper: Check if value is a plain object
     */
    isObject(value) {
        return value !== null && typeof value === 'object' && !Array.isArray(value);
    }

    /**
     * Generate example JSON schema for documentation
     */
    generateExampleSchema() {
        return {
            "$schema": "http://json-schema.org/draft-07/schema#",
            "title": "USD JSON Format",
            "description": "JSON representation of USD (Universal Scene Description) data",
            ...this.schemas.usdJson
        };
    }

    /**
     * Get validation summary
     */
    getValidationSummary(validationResult) {
        const { valid, errors, warnings } = validationResult;
        
        return {
            isValid: valid,
            errorCount: errors.length,
            warningCount: warnings.length,
            summary: valid ? 'Valid USD JSON' : `Invalid: ${errors.length} errors, ${warnings.length} warnings`,
            errors,
            warnings
        };
    }
}

// Export for Node.js and browser
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { UsdJsonSchemaValidator };
} else if (typeof window !== 'undefined') {
    window.UsdJsonSchemaValidator = UsdJsonSchemaValidator;
}