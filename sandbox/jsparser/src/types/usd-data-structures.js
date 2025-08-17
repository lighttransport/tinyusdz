/**
 * USD Data Structure Classes
 * Core data structures for representing USD content
 */

import { UsdDataType, InterpolationType, AttributeVariability } from './usd-types.js';

/**
 * Base USD Value class
 */
export class UsdValue {
    constructor(type, value) {
        this.type = type;
        this.value = value;
    }

    static createNumber(value) {
        return new UsdValue('number', value);
    }

    static createString(value) {
        return new UsdValue('string', value);
    }

    static createBool(value) {
        return new UsdValue('bool', value);
    }

    static createArray(values) {
        return new UsdValue('array', values);
    }

    static createTuple(values) {
        return new UsdValue('tuple', values);
    }

    static createIdentifier(value) {
        return new UsdValue('identifier', value);
    }

    isArray() {
        return this.type === 'array';
    }

    isTuple() {
        return this.type === 'tuple';
    }

    isNumeric() {
        return this.type === 'number';
    }

    toString() {
        switch (this.type) {
            case 'string':
                return `"${this.value}"`;
            case 'array':
                return `[${this.value.map(v => v.toString()).join(', ')}]`;
            case 'tuple':
                return `(${this.value.map(v => v.toString()).join(', ')})`;
            default:
                return String(this.value);
        }
    }

    clone() {
        if (this.isArray() || this.isTuple()) {
            const clonedValues = this.value.map(v => v instanceof UsdValue ? v.clone() : v);
            return new UsdValue(this.type, clonedValues);
        }
        return new UsdValue(this.type, this.value);
    }
}

/**
 * USD Attribute class
 */
export class UsdAttribute {
    constructor(name, type, value, options = {}) {
        this.name = name;
        this.type = type;
        this.value = value instanceof UsdValue ? value : new UsdValue('unknown', value);
        this.variability = options.variability || AttributeVariability.VARYING;
        this.metadata = options.metadata || {};
        this.interpolation = options.interpolation;
        this.timeSamples = options.timeSamples;
    }

    hasMetadata() {
        return Object.keys(this.metadata).length > 0;
    }

    isTimeVarying() {
        return this.timeSamples && this.timeSamples.length > 0;
    }

    clone() {
        return new UsdAttribute(
            this.name,
            this.type,
            this.value.clone(),
            {
                variability: this.variability,
                metadata: { ...this.metadata },
                interpolation: this.interpolation,
                timeSamples: this.timeSamples ? [...this.timeSamples] : null
            }
        );
    }

    toString() {
        let result = '';
        if (this.variability !== AttributeVariability.VARYING) {
            result += `${this.variability} `;
        }
        result += `${this.type} ${this.name} = ${this.value.toString()}`;
        return result;
    }
}

/**
 * USD Primitive class
 */
export class UsdPrim {
    constructor(name, type, specifier = 'def', options = {}) {
        this.name = name;
        this.type = type;
        this.specifier = specifier;
        this.attributes = new Map();
        this.children = new Map();
        this.metadata = options.metadata || {};
        this.parent = null;
    }

    addAttribute(attribute) {
        if (!(attribute instanceof UsdAttribute)) {
            throw new Error('Expected UsdAttribute instance');
        }
        this.attributes.set(attribute.name, attribute);
        return this;
    }

    getAttribute(name) {
        return this.attributes.get(name);
    }

    hasAttribute(name) {
        return this.attributes.has(name);
    }

    removeAttribute(name) {
        return this.attributes.delete(name);
    }

    addChild(prim) {
        if (!(prim instanceof UsdPrim)) {
            throw new Error('Expected UsdPrim instance');
        }
        prim.parent = this;
        this.children.set(prim.name, prim);
        return this;
    }

    getChild(name) {
        return this.children.get(name);
    }

    hasChild(name) {
        return this.children.has(name);
    }

    removeChild(name) {
        const child = this.children.get(name);
        if (child) {
            child.parent = null;
            this.children.delete(name);
        }
        return child;
    }

    getPath() {
        if (!this.parent) {
            return `/${this.name}`;
        }
        const parentPath = this.parent.getPath();
        return parentPath === '/' ? `/${this.name}` : `${parentPath}/${this.name}`;
    }

    hasMetadata() {
        return Object.keys(this.metadata).length > 0;
    }

    findPrim(path) {
        if (path.startsWith('/')) {
            path = path.substring(1);
        }
        
        const parts = path.split('/').filter(p => p.length > 0);
        if (parts.length === 0) {
            return this;
        }

        const [first, ...rest] = parts;
        const child = this.getChild(first);
        if (!child) {
            return null;
        }

        if (rest.length === 0) {
            return child;
        }

        return child.findPrim(rest.join('/'));
    }

    getAllDescendants() {
        const descendants = [];
        for (const child of this.children.values()) {
            descendants.push(child);
            descendants.push(...child.getAllDescendants());
        }
        return descendants;
    }

    clone() {
        const cloned = new UsdPrim(this.name, this.type, this.specifier, {
            metadata: { ...this.metadata }
        });

        // Clone attributes
        for (const attr of this.attributes.values()) {
            cloned.addAttribute(attr.clone());
        }

        // Clone children
        for (const child of this.children.values()) {
            cloned.addChild(child.clone());
        }

        return cloned;
    }

    toString(indent = 0) {
        const spaces = '    '.repeat(indent);
        let result = `${spaces}${this.specifier} ${this.type} "${this.name}"`;
        
        if (this.hasMetadata()) {
            result += ' (\n';
            for (const [key, value] of Object.entries(this.metadata)) {
                result += `${spaces}    ${key} = ${value}\n`;
            }
            result += `${spaces})`;
        }
        
        result += ' {\n';

        // Add attributes
        for (const attr of this.attributes.values()) {
            result += `${spaces}    ${attr.toString()}\n`;
        }

        // Add children
        if (this.children.size > 0 && this.attributes.size > 0) {
            result += '\n';
        }
        for (const child of this.children.values()) {
            result += child.toString(indent + 1);
        }

        result += `${spaces}}\n`;
        return result;
    }
}

/**
 * USD Layer class
 */
export class UsdLayer {
    constructor(options = {}) {
        this.formatVersion = options.formatVersion || '1.0';
        this.metadata = options.metadata || {};
        this.rootPrim = null;
        this.timeSamples = new Map();
        this.paths = new Map(); // path string -> prim mapping
        this.defaultPrim = options.defaultPrim;
        this.upAxis = options.upAxis || 'Y';
        this.metersPerUnit = options.metersPerUnit || 1.0;
        this.timeCodesPerSecond = options.timeCodesPerSecond || 24.0;
        this.startTimeCode = options.startTimeCode;
        this.endTimeCode = options.endTimeCode;
    }

    setRootPrim(prim) {
        if (!(prim instanceof UsdPrim)) {
            throw new Error('Expected UsdPrim instance');
        }
        this.rootPrim = prim;
        this.rebuildPathIndex();
        return this;
    }

    rebuildPathIndex() {
        this.paths.clear();
        if (this.rootPrim) {
            this._indexPrim(this.rootPrim);
        }
    }

    _indexPrim(prim) {
        this.paths.set(prim.getPath(), prim);
        for (const child of prim.children.values()) {
            this._indexPrim(child);
        }
    }

    findPrim(path) {
        return this.paths.get(path) || null;
    }

    getAllPrims() {
        return Array.from(this.paths.values());
    }

    addTimeSamples(primPath, attributeName, timeSamples) {
        const key = `${primPath}.${attributeName}`;
        this.timeSamples.set(key, timeSamples);
    }

    getTimeSamples(primPath, attributeName) {
        const key = `${primPath}.${attributeName}`;
        return this.timeSamples.get(key);
    }

    hasTimeSamples() {
        return this.timeSamples.size > 0;
    }

    validate() {
        const errors = [];

        // Check for valid root prim
        if (!this.rootPrim) {
            errors.push('Layer must have a root primitive');
        }

        // Validate all prims
        if (this.rootPrim) {
            this._validatePrim(this.rootPrim, errors);
        }

        return errors;
    }

    _validatePrim(prim, errors, visited = new Set()) {
        // Check for circular references
        const primPath = prim.getPath();
        if (visited.has(primPath)) {
            errors.push(`Circular reference detected: ${primPath}`);
            return;
        }
        visited.add(primPath);

        // Validate prim properties
        if (!prim.name || typeof prim.name !== 'string') {
            errors.push(`Invalid prim name: ${prim.name}`);
        }

        if (!prim.type || typeof prim.type !== 'string') {
            errors.push(`Invalid prim type: ${prim.type} for ${primPath}`);
        }

        // Validate attributes
        for (const attr of prim.attributes.values()) {
            if (!attr.name || typeof attr.name !== 'string') {
                errors.push(`Invalid attribute name in ${primPath}`);
            }
            if (!attr.type || typeof attr.type !== 'string') {
                errors.push(`Invalid attribute type for ${primPath}.${attr.name}`);
            }
        }

        // Recursively validate children
        for (const child of prim.children.values()) {
            this._validatePrim(child, errors, new Set(visited));
        }
    }

    clone() {
        const cloned = new UsdLayer({
            formatVersion: this.formatVersion,
            metadata: { ...this.metadata },
            defaultPrim: this.defaultPrim,
            upAxis: this.upAxis,
            metersPerUnit: this.metersPerUnit,
            timeCodesPerSecond: this.timeCodesPerSecond,
            startTimeCode: this.startTimeCode,
            endTimeCode: this.endTimeCode
        });

        if (this.rootPrim) {
            cloned.setRootPrim(this.rootPrim.clone());
        }

        // Clone time samples
        for (const [key, samples] of this.timeSamples.entries()) {
            cloned.timeSamples.set(key, samples); // Note: might need deep clone depending on timeSamples structure
        }

        return cloned;
    }

    toString() {
        let result = `# USD Layer (version ${this.formatVersion})\n`;
        
        if (Object.keys(this.metadata).length > 0) {
            result += '(\n';
            for (const [key, value] of Object.entries(this.metadata)) {
                result += `    ${key} = ${value}\n`;
            }
            result += ')\n\n';
        }

        if (this.rootPrim) {
            result += this.rootPrim.toString();
        }

        return result;
    }
}

/**
 * USD Time Samples class for animation data
 */
export class UsdTimeSamples {
    constructor(interpolation = InterpolationType.LINEAR) {
        this.times = [];
        this.values = [];
        this.interpolation = interpolation;
    }

    addSample(time, value) {
        // Insert in chronological order
        const insertIndex = this.times.findIndex(t => t > time);
        if (insertIndex === -1) {
            this.times.push(time);
            this.values.push(value);
        } else {
            this.times.splice(insertIndex, 0, time);
            this.values.splice(insertIndex, 0, value);
        }
    }

    getSampleAtTime(time) {
        if (this.times.length === 0) return null;

        // Find exact match
        const exactIndex = this.times.indexOf(time);
        if (exactIndex !== -1) {
            return this.values[exactIndex];
        }

        // Interpolate
        return this.interpolateAtTime(time);
    }

    interpolateAtTime(time) {
        if (this.times.length === 0) return null;
        if (this.times.length === 1) return this.values[0];

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
            // Time is outside range, return nearest
            if (time < this.times[0]) return this.values[0];
            return this.values[this.values.length - 1];
        }

        const t0 = this.times[beforeIndex];
        const t1 = this.times[afterIndex];
        const v0 = this.values[beforeIndex];
        const v1 = this.values[afterIndex];

        const factor = (time - t0) / (t1 - t0);

        switch (this.interpolation) {
            case InterpolationType.LINEAR:
                return this._interpolateLinear(v0, v1, factor);
            case InterpolationType.HELD:
                return factor < 0.5 ? v0 : v1;
            default:
                return this._interpolateLinear(v0, v1, factor);
        }
    }

    _interpolateLinear(v0, v1, factor) {
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

        return factor < 0.5 ? v0 : v1;
    }

    getTimeRange() {
        if (this.times.length === 0) return null;
        return {
            start: this.times[0],
            end: this.times[this.times.length - 1]
        };
    }

    getSampleCount() {
        return this.times.length;
    }

    clone() {
        const cloned = new UsdTimeSamples(this.interpolation);
        cloned.times = [...this.times];
        cloned.values = this.values.map(v => 
            Array.isArray(v) ? [...v] : v
        );
        return cloned;
    }

    toString() {
        return `TimeSamples(${this.times.length} samples, ${this.interpolation} interpolation)`;
    }
}