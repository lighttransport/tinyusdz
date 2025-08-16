/**
 * USD ↔ JSON Bidirectional Converter
 * Converts between USD data structures and JSON format for easier web integration
 */

const { UsdaParser } = require('./usda-parser.js');
const { UsdcParser, UsdcTimeSamples } = require('./usdc-parser.js');

class UsdJsonConverter {
    constructor(options = {}) {
        this.options = {
            // Conversion options
            includeMetadata: true,
            includeTimeSamples: true,
            preserveTypes: true,
            compactArrays: false,
            maxArrayLength: 1000,
            maxDepth: 20,
            // JSON formatting
            indent: 2,
            sortKeys: false,
            // USD options
            defaultSpecifier: 'def',
            defaultVariability: 'varying',
            ...options
        };
        this.conversionDepth = 0;
    }

    // =================== USD TO JSON ===================

    /**
     * Convert USD layer to JSON representation
     */
    usdToJson(layer, format = 'usda') {
        this.conversionDepth = 0;
        
        if (!layer) {
            throw new Error('Invalid USD layer provided');
        }

        const jsonData = {
            format: format, // 'usda' or 'usdc'
            version: '1.0',
            metadata: {},
            prims: {},
            timeSamples: {},
            statistics: this.generateStatistics(layer)
        };

        try {
            // Convert layer metadata
            if (this.options.includeMetadata && layer.metadata) {
                jsonData.metadata = this.convertMetadataToJson(layer.metadata);
            }

            // Convert primitives
            if (format === 'usda' && layer.rootPrim) {
                jsonData.prims = this.convertPrimToJson(layer.rootPrim);
            } else if (format === 'usdc') {
                jsonData.prims = this.convertUsdcLayerToJson(layer);
                
                // Convert time samples
                if (this.options.includeTimeSamples && layer.timeSamples) {
                    jsonData.timeSamples = this.convertTimeSamplesToJson(layer.timeSamples);
                }
            }

            return jsonData;
        } catch (error) {
            throw new Error(`USD to JSON conversion failed: ${error.message}`);
        }
    }

    /**
     * Convert USDA primitive to JSON
     */
    convertPrimToJson(prim) {
        if (this.conversionDepth > this.options.maxDepth) {
            return { error: 'Max depth exceeded' };
        }
        this.conversionDepth++;

        const jsonPrim = {
            name: prim.name,
            type: prim.type,
            specifier: prim.specifier || this.options.defaultSpecifier
        };

        // Convert attributes
        if (prim.attributes && Object.keys(prim.attributes).length > 0) {
            jsonPrim.attributes = {};
            for (const [name, attr] of Object.entries(prim.attributes)) {
                jsonPrim.attributes[name] = this.convertAttributeToJson(attr);
            }
        }

        // Convert metadata
        if (this.options.includeMetadata && prim.metadata && Object.keys(prim.metadata).length > 0) {
            jsonPrim.metadata = this.convertMetadataToJson(prim.metadata);
        }

        // Convert children
        if (prim.children && prim.children.length > 0) {
            jsonPrim.children = {};
            for (const child of prim.children) {
                jsonPrim.children[child.name] = this.convertPrimToJson(child);
            }
        }

        this.conversionDepth--;
        return jsonPrim;
    }

    /**
     * Convert USDC layer to JSON
     */
    convertUsdcLayerToJson(layer) {
        const jsonLayer = {
            tokens: layer.tokens || [],
            strings: layer.strings || [],
            fields: [],
            specs: [],
            paths: []
        };

        // Convert fields
        if (layer.fields) {
            jsonLayer.fields = layer.fields.map((field, index) => ({
                index: index,
                token: field.token,
                tokenName: layer.tokens[field.token] || `<unknown:${field.token}>`,
                valueRep: this.convertValueRepToJson(field.valueRep),
                value: this.convertFieldValueToJson(field, layer)
            }));
        }

        // Convert specs
        if (layer.specs) {
            jsonLayer.specs = layer.specs.map((spec, index) => ({
                index: index,
                path: spec.path,
                pathString: this.getPathString(spec.path, layer.paths),
                fieldSet: spec.fieldSet,
                specType: spec.specType,
                fields: this.getFieldSetFields(spec.fieldSet, layer.fieldSets)
            }));
        }

        // Convert paths
        if (layer.paths) {
            jsonLayer.paths = layer.paths.map((path, index) => ({
                index: index,
                isAbsolute: path.isAbsolute,
                elements: path.indices,
                pathString: path.toString()
            }));
        }

        return jsonLayer;
    }

    /**
     * Convert USD attribute to JSON
     */
    convertAttributeToJson(attr) {
        const jsonAttr = {
            type: attr.type,
            value: this.convertValueToJson(attr.value)
        };

        if (attr.variability && attr.variability !== this.options.defaultVariability) {
            jsonAttr.variability = attr.variability;
        }

        if (this.options.includeMetadata && attr.metadata && Object.keys(attr.metadata).length > 0) {
            jsonAttr.metadata = this.convertMetadataToJson(attr.metadata);
        }

        return jsonAttr;
    }

    /**
     * Convert USD value to JSON
     */
    convertValueToJson(value) {
        if (!value || typeof value !== 'object') {
            return value;
        }

        switch (value.type) {
            case 'number':
            case 'string':
            case 'identifier':
            case 'bool':
                return {
                    type: value.type,
                    value: value.value
                };

            case 'array':
                const arrayValue = value.value;
                if (this.options.compactArrays && arrayValue.length > this.options.maxArrayLength) {
                    return {
                        type: 'array',
                        length: arrayValue.length,
                        sample: arrayValue.slice(0, 10).map(v => this.convertValueToJson(v)),
                        truncated: true
                    };
                }
                return {
                    type: 'array',
                    value: arrayValue.map(v => this.convertValueToJson(v))
                };

            case 'tuple':
                return {
                    type: 'tuple',
                    value: value.value.map(v => this.convertValueToJson(v))
                };

            case 'dictionary':
                const dictResult = { type: 'dictionary', value: {} };
                for (const [key, val] of Object.entries(value.value)) {
                    dictResult.value[key] = this.convertValueToJson(val);
                }
                return dictResult;

            case 'time_samples':
                return this.convertTimeSampleValueToJson(value);

            default:
                if (this.options.preserveTypes) {
                    return {
                        type: value.type,
                        value: value.value,
                        originalType: true
                    };
                }
                return value.value;
        }
    }

    /**
     * Convert metadata to JSON
     */
    convertMetadataToJson(metadata) {
        const result = {};
        for (const [key, value] of Object.entries(metadata)) {
            result[key] = this.convertValueToJson(value);
        }
        return result;
    }

    /**
     * Convert time samples to JSON
     */
    convertTimeSamplesToJson(timeSamples) {
        const result = {};
        for (const [fieldIndex, samples] of timeSamples.entries()) {
            if (samples instanceof UsdcTimeSamples) {
                result[fieldIndex] = {
                    type: 'time_samples',
                    interpolation: samples.interpolation,
                    samples: samples.times.map((time, index) => ({
                        time: time,
                        value: this.convertValueToJson(samples.values[index])
                    }))
                };
            }
        }
        return result;
    }

    /**
     * Convert time sample value to JSON
     */
    convertTimeSampleValueToJson(value) {
        if (value.value && value.value.samples) {
            return {
                type: 'time_samples',
                interpolation: value.value.interpolation || 'linear',
                samples: value.value.samples.map(sample => ({
                    time: sample.time,
                    value: this.convertValueToJson(sample.value)
                }))
            };
        }
        return { type: 'time_samples', samples: [] };
    }

    /**
     * Convert USDC value representation to JSON
     */
    convertValueRepToJson(valueRep) {
        if (!valueRep) return null;
        
        return {
            data: valueRep.data.toString(),
            typeId: valueRep.getTypeId(),
            payload: valueRep.getPayload().toString(),
            isArray: valueRep.isArray(),
            isInlined: valueRep.isInlined(),
            isCompressed: valueRep.isCompressed()
        };
    }

    /**
     * Convert field value to JSON (for USDC)
     */
    convertFieldValueToJson(field, layer) {
        // This would require the value reader to actually read the value
        // For now, just return the representation info
        return {
            hasValue: true,
            needsValueReader: true,
            valueRep: this.convertValueRepToJson(field.valueRep)
        };
    }

    // =================== JSON TO USD ===================

    /**
     * Convert JSON back to USD format
     */
    jsonToUsd(jsonData, outputFormat = 'usda') {
        try {
            if (!jsonData || typeof jsonData !== 'object') {
                throw new Error('Invalid JSON data provided');
            }

            if (outputFormat === 'usda') {
                return this.jsonToUsda(jsonData);
            } else if (outputFormat === 'usdc') {
                throw new Error('JSON to USDC conversion not yet implemented');
            } else {
                throw new Error(`Unsupported output format: ${outputFormat}`);
            }
        } catch (error) {
            throw new Error(`JSON to USD conversion failed: ${error.message}`);
        }
    }

    /**
     * Convert JSON to USDA text format
     */
    jsonToUsda(jsonData) {
        let usdaText = '';

        // Add header comment
        usdaText += '# Converted from JSON\n';
        usdaText += `# Format: ${jsonData.format || 'unknown'}\n`;
        usdaText += `# Version: ${jsonData.version || '1.0'}\n\n`;

        // Add layer metadata in proper USD format
        if (jsonData.metadata && Object.keys(jsonData.metadata).length > 0) {
            usdaText += '(\n';
            for (const [key, value] of Object.entries(jsonData.metadata)) {
                const valueStr = this.jsonValueToUsda(value);
                usdaText += `    ${key} = ${valueStr}\n`;
            }
            usdaText += ')\n\n';
        }

        // Convert primitives
        if (jsonData.prims) {
            if (jsonData.format === 'usda' && typeof jsonData.prims === 'object') {
                usdaText += this.jsonPrimToUsda(jsonData.prims, 0);
            } else if (jsonData.format === 'usdc') {
                usdaText += this.jsonUsdcToUsda(jsonData.prims);
            }
        }

        return usdaText;
    }

    /**
     * Convert JSON primitive to USDA text
     */
    jsonPrimToUsda(primData, indent = 0) {
        const indentStr = '    '.repeat(indent);
        let usdaText = '';

        const specifier = primData.specifier || 'def';
        const primType = primData.type || '';
        const primName = primData.name || 'UnnamedPrim';

        // Build prim declaration with metadata if present
        let declaration = `${indentStr}${specifier} ${primType} "${primName}"`;
        
        // Add prim metadata in parentheses if present
        if (primData.metadata && Object.keys(primData.metadata).length > 0) {
            declaration += ' (\n';
            for (const [key, value] of Object.entries(primData.metadata)) {
                const valueStr = this.jsonValueToUsda(value);
                declaration += `${indentStr}    ${key} = ${valueStr}\n`;
            }
            declaration += `${indentStr})`;
        }
        
        declaration += ' {\n';
        usdaText += declaration;

        // Add attributes
        if (primData.attributes) {
            for (const [attrName, attrData] of Object.entries(primData.attributes)) {
                usdaText += this.jsonAttributeToUsda(attrName, attrData, indent + 1);
            }
        }

        // Add children
        if (primData.children) {
            for (const [childName, childData] of Object.entries(primData.children)) {
                usdaText += '\n';
                usdaText += this.jsonPrimToUsda(childData, indent + 1);
            }
        }

        usdaText += `${indentStr}}\n`;
        return usdaText;
    }

    /**
     * Convert JSON attribute to USDA text
     */
    jsonAttributeToUsda(name, attrData, indent = 0) {
        const indentStr = '    '.repeat(indent);
        let usdaText = '';

        // Add attribute metadata as comments if present
        if (attrData.metadata) {
            usdaText += `${indentStr}# Metadata: ${JSON.stringify(attrData.metadata)}\n`;
        }

        // Build attribute declaration
        const variability = attrData.variability && attrData.variability !== 'varying' 
            ? `${attrData.variability} ` : '';
        const attrType = attrData.type || 'string';
        const value = this.jsonValueToUsda(attrData.value);

        usdaText += `${indentStr}${variability}${attrType} ${name} = ${value}\n`;

        return usdaText;
    }

    /**
     * Convert JSON value to USDA text representation
     */
    jsonValueToUsda(valueData) {
        if (!valueData || typeof valueData !== 'object') {
            return JSON.stringify(valueData);
        }

        switch (valueData.type) {
            case 'number':
                return valueData.value.toString();

            case 'string':
                return `"${valueData.value}"`;

            case 'identifier':
            case 'bool':
                return valueData.value.toString();

            case 'array':
                if (valueData.truncated) {
                    return `# Array with ${valueData.length} elements (truncated)`;
                }
                const arrayValues = valueData.value.map(v => this.jsonValueToUsda(v));
                return `[${arrayValues.join(', ')}]`;

            case 'tuple':
                const tupleValues = valueData.value.map(v => this.jsonValueToUsda(v));
                return `(${tupleValues.join(', ')})`;

            case 'dictionary':
                const dictEntries = [];
                for (const [key, value] of Object.entries(valueData.value)) {
                    dictEntries.push(`"${key}": ${this.jsonValueToUsda(value)}`);
                }
                return `{${dictEntries.join(', ')}}`;

            case 'time_samples':
                // Time samples are complex - represent as comment for now
                const numSamples = valueData.samples ? valueData.samples.length : 0;
                return `# TimeSamples (${numSamples} samples, ${valueData.interpolation || 'linear'})`;

            default:
                if (valueData.originalType) {
                    return `# ${valueData.type}: ${JSON.stringify(valueData.value)}`;
                }
                return JSON.stringify(valueData);
        }
    }

    /**
     * Convert JSON metadata to USDA text
     */
    jsonMetadataToUsda(metadata, indent = 0) {
        const indentStr = '    '.repeat(indent);
        let usdaText = '';

        for (const [key, value] of Object.entries(metadata)) {
            const valueStr = this.jsonValueToUsda(value);
            usdaText += `${indentStr}${key} = ${valueStr}\n`;
        }

        return usdaText;
    }

    // =================== UTILITY METHODS ===================

    /**
     * Generate statistics about the USD layer
     */
    generateStatistics(layer) {
        const stats = {
            generatedAt: new Date().toISOString(),
            conversionOptions: this.options
        };

        if (layer.tokens) stats.tokenCount = layer.tokens.length;
        if (layer.strings) stats.stringCount = layer.strings.length;
        if (layer.fields) stats.fieldCount = layer.fields.length;
        if (layer.specs) stats.specCount = layer.specs.length;
        if (layer.paths) stats.pathCount = layer.paths.length;
        if (layer.timeSamples) stats.timeSampleCount = layer.timeSamples.size;

        return stats;
    }

    /**
     * Get path string from path index
     */
    getPathString(pathIndex, paths) {
        if (paths && pathIndex >= 0 && pathIndex < paths.length) {
            return paths[pathIndex].toString();
        }
        return `<path:${pathIndex}>`;
    }

    /**
     * Get field set fields
     */
    getFieldSetFields(fieldSetIndex, fieldSets) {
        if (fieldSets && fieldSetIndex >= 0 && fieldSetIndex < fieldSets.length) {
            return fieldSets[fieldSetIndex].fields || [];
        }
        return [];
    }

    /**
     * Convert JSON USDC data to USDA (simplified)
     */
    jsonUsdcToUsda(usdcData) {
        let usdaText = '';
        usdaText += '# Converted from USDC binary format\n';
        usdaText += '# This is a simplified representation\n\n';

        if (usdcData.specs && usdcData.specs.length > 0) {
            for (const spec of usdcData.specs) {
                usdaText += `# Spec ${spec.index}: ${spec.pathString}\n`;
                usdaText += `# Fields: ${spec.fields.length}\n`;
            }
        }

        return usdaText;
    }

    // =================== VALIDATION ===================

    /**
     * Validate JSON data structure
     */
    validateJsonData(jsonData) {
        const errors = [];

        if (!jsonData || typeof jsonData !== 'object') {
            errors.push('JSON data must be an object');
            return errors;
        }

        if (!jsonData.format) {
            errors.push('Missing format field');
        } else if (!['usda', 'usdc'].includes(jsonData.format)) {
            errors.push(`Invalid format: ${jsonData.format}`);
        }

        if (!jsonData.prims) {
            errors.push('Missing prims field');
        }

        return errors;
    }
}

// Export for Node.js and browser
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { UsdJsonConverter };
} else if (typeof window !== 'undefined') {
    window.UsdJsonConverter = UsdJsonConverter;
}