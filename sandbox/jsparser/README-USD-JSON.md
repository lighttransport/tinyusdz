# USD ↔ JSON Bidirectional Conversion

A comprehensive JavaScript implementation for converting between USD (Universal Scene Description) data structures and JSON format. This enables easier integration with web applications, APIs, and JavaScript-based USD processing pipelines.

## Overview

The USD-JSON conversion system provides:

- **Bidirectional Conversion**: Convert USD data to JSON and back to USD
- **Format Support**: Works with both USDA (ASCII) and USDC (binary) formats
- **Schema Validation**: JSON schema validation for USD data structures
- **Type Preservation**: Maintains USD type information in JSON representation
- **Time Samples**: Full support for animated/time-varying data
- **Metadata Support**: Preserves USD metadata and attributes
- **Configurable Output**: Customizable conversion options

## Files

### Core Components
- `usd-json-converter.js` - Main conversion engine
- `usd-json-schema.js` - JSON schema validation
- `usd-json-test.js` - Conversion test suite
- `usd-json-schema-test.js` - Schema validation tests
- `usd-json-example.js` - Usage examples and demos

### Documentation
- `README-USD-JSON.md` - This comprehensive documentation

## Features

### USD to JSON Conversion

Converts USD layer data to structured JSON:

```javascript
const { UsdJsonConverter } = require('./usd-json-converter.js');
const { UsdaParser } = require('./usda-parser.js');

// Parse USD content
const parser = new UsdaParser(usdaContent);
const layer = parser.parse();

// Convert to JSON
const converter = new UsdJsonConverter({
    includeMetadata: true,
    preserveTypes: true,
    includeTimeSamples: true
});

const jsonData = converter.usdToJson(layer, 'usda');
console.log(JSON.stringify(jsonData, null, 2));
```

### JSON to USD Conversion

Converts JSON back to USD format:

```javascript
// Convert JSON back to USDA text
const usdaText = converter.jsonToUsd(jsonData, 'usda');

// Verify the generated USD is valid
const verifyParser = new UsdaParser(usdaText);
const verifyLayer = verifyParser.parse();
```

### Schema Validation

Validates JSON structure against USD schema:

```javascript
const { UsdJsonSchemaValidator } = require('./usd-json-schema.js');

const validator = new UsdJsonSchemaValidator();
const result = validator.validateUsdJson(jsonData);

if (result.valid) {
    console.log('✓ Valid USD JSON');
} else {
    result.errors.forEach(err => console.log(`Error: ${err}`));
}
```

## JSON Schema Format

### Basic Structure

```json
{
  "format": "usda|usdc",
  "version": "1.0",
  "metadata": { /* layer metadata */ },
  "prims": { /* primitive data */ },
  "timeSamples": { /* animation data */ },
  "statistics": { /* conversion stats */ }
}
```

### USDA Format Representation

For USDA files, prims contain hierarchical primitive data:

```json
{
  "format": "usda",
  "prims": {
    "name": "Sphere",
    "type": "Sphere", 
    "specifier": "def",
    "attributes": {
      "radius": {
        "type": "float",
        "value": { "type": "number", "value": 1.0 }
      }
    },
    "children": {
      "childPrim": { /* nested prim */ }
    }
  }
}
```

### USDC Format Representation

For USDC files, prims contain binary format data:

```json
{
  "format": "usdc",
  "prims": {
    "tokens": ["def", "Sphere", "radius"],
    "strings": ["material_name"],
    "fields": [
      {
        "index": 0,
        "token": 2,
        "tokenName": "radius",
        "valueRep": {
          "typeId": 8,
          "isInlined": true,
          "payload": "1065353216"
        }
      }
    ],
    "specs": [ /* spec definitions */ ],
    "paths": [ /* path definitions */ ]
  }
}
```

### Value Representation

USD values are represented with type information:

```json
{
  "type": "number|string|array|tuple|dictionary",
  "value": /* actual value */,
  "originalType": true  /* optional, preserves USD type info */
}
```

### Time Samples

Animation data is preserved in time samples:

```json
{
  "timeSamples": {
    "0": {
      "type": "time_samples",
      "interpolation": "linear",
      "samples": [
        {
          "time": 0.0,
          "value": { "type": "tuple", "value": [0, 0, 0] }
        },
        {
          "time": 1.0, 
          "value": { "type": "tuple", "value": [5, 2, 0] }
        }
      ]
    }
  }
}
```

## Configuration Options

### Converter Options

```javascript
const converter = new UsdJsonConverter({
  // Metadata handling
  includeMetadata: true,        // Include USD metadata
  includeTimeSamples: true,     // Include animation data
  
  // Type handling  
  preserveTypes: true,          // Preserve USD type information
  
  // Array handling
  compactArrays: false,         // Truncate large arrays
  maxArrayLength: 1000,         // Maximum array length before truncation
  
  // Structure limits
  maxDepth: 20,                 // Maximum nesting depth
  
  // Output formatting
  indent: 2,                    // JSON indentation
  sortKeys: false,              // Sort JSON keys
  
  // USD defaults
  defaultSpecifier: 'def',      // Default prim specifier
  defaultVariability: 'varying' // Default attribute variability
});
```

### Compact Array Mode

For large arrays, use compact mode to avoid huge JSON files:

```javascript
const converter = new UsdJsonConverter({
  compactArrays: true,
  maxArrayLength: 10
});

// Large arrays become:
{
  "type": "array",
  "length": 10000,
  "sample": [/* first 10 elements */],
  "truncated": true
}
```

## Usage Examples

### Example 1: Basic USDA to JSON

```javascript
const usdaContent = `
def Sphere "ball" {
    float radius = 1.0
    float3 translate = (0, 1, 2)
    color3f color = (1.0, 0.5, 0.0)
}`;

const parser = new UsdaParser(usdaContent);
const layer = parser.parse();
const converter = new UsdJsonConverter();
const json = converter.usdToJson(layer, 'usda');
```

### Example 2: JSON to USDA Round-trip

```javascript
// Start with JSON
const jsonData = {
  format: 'usda',
  version: '1.0',
  prims: {
    name: 'TestSphere',
    type: 'Sphere',
    attributes: {
      radius: {
        type: 'float',
        value: { type: 'number', value: 2.0 }
      }
    }
  }
};

// Convert to USDA
const converter = new UsdJsonConverter();
const usdaText = converter.jsonToUsd(jsonData, 'usda');

// Parse the generated USDA
const parser = new UsdaParser(usdaText);
const layer = parser.parse();
```

### Example 3: USDC Binary to JSON

```javascript
const fs = require('fs');

// Read USDC file
const fileData = fs.readFileSync('model.usdc');
const parser = new UsdcParser(fileData);
const layer = parser.parse();

// Convert to JSON
const converter = new UsdJsonConverter({
  compactArrays: true  // Handle large binary arrays
});
const json = converter.usdToJson(layer, 'usdc');

// Save as JSON
fs.writeFileSync('model.json', JSON.stringify(json, null, 2));
```

### Example 4: Time Samples Processing

```javascript
// Create time samples
const timeSamples = new UsdcTimeSamples();
timeSamples.addSample(0.0, [0, 0, 0]);
timeSamples.addSample(1.0, [5, 2, 0]);

const layer = {
  timeSamples: new Map([[0, timeSamples]]),
  // ... other layer data
};

// Convert with time samples
const converter = new UsdJsonConverter({
  includeTimeSamples: true
});
const json = converter.usdToJson(layer, 'usdc');

// Access animation data
const animation = json.timeSamples[0];
console.log(`${animation.samples.length} keyframes`);
```

### Example 5: Schema Validation

```javascript
const { UsdJsonSchemaValidator } = require('./usd-json-schema.js');

// Validate converted JSON
const validator = new UsdJsonSchemaValidator();
const result = validator.validateUsdJson(jsonData);

if (result.valid) {
  console.log('✓ Valid USD JSON format');
} else {
  console.log('✗ Validation errors:');
  result.errors.forEach(err => console.log(`  - ${err}`));
}

// Get detailed summary
const summary = validator.getValidationSummary(result);
console.log(summary.summary);
```

## Supported USD Features

### Data Types
- ✅ Primitive types: `bool`, `int`, `float`, `double`, `string`, `token`
- ✅ Vector types: `float2`, `float3`, `float4`, `double2`, `double3`, `double4`
- ✅ Color types: `color3f`, `color4f`
- ✅ Point types: `point3f`, `point3d`
- ✅ Arrays and tuples
- ✅ Dictionaries
- ✅ Time samples (animation data)

### Primitive Features
- ✅ Primitive declarations (`def`, `over`, `class`)
- ✅ Nested primitives
- ✅ Attributes with types and values
- ✅ Metadata on prims and attributes
- ✅ Variability specifications

### USDC Binary Format
- ✅ Tokens and strings
- ✅ Fields and field sets
- ✅ Specs and paths
- ✅ Value representations
- ✅ Compressed data handling

### Advanced Features
- ✅ Time-varying data (time samples)
- ✅ Type preservation
- ✅ Round-trip conversion
- ✅ Schema validation
- ✅ Configurable output

## Current Limitations

### USD Features Not Supported
- ❌ References and payloads
- ❌ Variants and variant sets
- ❌ Relationships
- ❌ Layer composition
- ❌ Complex schemas validation

### JSON to USDC
- ❌ JSON to USDC binary generation (complex binary format)
- ❌ Binary data reconstruction
- ❌ Compressed section recreation

### Performance
- ⚠️ Large file handling could be optimized
- ⚠️ Memory usage for huge datasets
- ⚠️ Streaming/lazy loading not implemented

## Running Tests

### Conversion Tests
```bash
node usd-json-test.js
```

### Schema Validation Tests  
```bash
node usd-json-schema-test.js
```

### Examples and Demos
```bash
node usd-json-example.js
```

### All Tests
```bash
npm test  # If package.json configured
```

## Test Results

### Conversion Tests: 100% Pass Rate
- ✅ USDA to JSON conversion
- ✅ JSON to USDA conversion  
- ✅ USDC to JSON conversion
- ✅ Time samples conversion
- ✅ Round-trip validation
- ✅ JSON validation

### Schema Tests: 100% Pass Rate
- ✅ Valid USDA JSON validation
- ✅ Invalid JSON error detection
- ✅ USDC JSON validation
- ✅ Time samples schema
- ✅ Attribute validation
- ✅ Schema generation

## Integration Examples

### Web Application Integration

```javascript
// In a web app
import { UsdJsonConverter } from './usd-json-converter.js';

class UsdViewer {
  async loadUsdFile(file) {
    const content = await file.text();
    const parser = new UsdaParser(content);
    const layer = parser.parse();
    
    const converter = new UsdJsonConverter();
    const json = converter.usdToJson(layer, 'usda');
    
    this.displayScene(json);
  }
  
  displayScene(usdJson) {
    // Render USD data from JSON
    const prims = this.extractPrims(usdJson.prims);
    this.renderPrims(prims);
  }
}
```

### API Server Integration

```javascript
// Express.js API endpoint
app.post('/api/usd/convert', (req, res) => {
  try {
    const { content, format } = req.body;
    
    const parser = format === 'usda' ? 
      new UsdaParser(content) : 
      new UsdcParser(Buffer.from(content, 'base64'));
    
    const layer = parser.parse();
    
    const converter = new UsdJsonConverter({
      compactArrays: true,
      maxArrayLength: 100
    });
    
    const json = converter.usdToJson(layer, format);
    
    res.json({
      success: true,
      data: json
    });
  } catch (error) {
    res.status(400).json({
      success: false,
      error: error.message
    });
  }
});
```

### Build Pipeline Integration

```javascript
// Build step for USD assets
const fs = require('fs');
const path = require('path');

function processUsdAssets(inputDir, outputDir) {
  const converter = new UsdJsonConverter({
    compactArrays: true,
    includeTimeSamples: true
  });
  
  fs.readdirSync(inputDir)
    .filter(file => file.endsWith('.usda') || file.endsWith('.usdc'))
    .forEach(file => {
      const inputPath = path.join(inputDir, file);
      const outputPath = path.join(outputDir, file.replace(/\.usd[ac]$/, '.json'));
      
      const fileData = fs.readFileSync(inputPath);
      const parser = file.endsWith('.usda') ? 
        new UsdaParser(fileData.toString()) :
        new UsdcParser(fileData);
      
      const layer = parser.parse();
      if (layer) {
        const json = converter.usdToJson(layer, file.endsWith('.usda') ? 'usda' : 'usdc');
        fs.writeFileSync(outputPath, JSON.stringify(json, null, 2));
        console.log(`✓ Converted ${file} to JSON`);
      }
    });
}
```

## Performance Considerations

### Memory Usage
- Large USD files can generate large JSON representations
- Use `compactArrays: true` for files with large arrays
- Monitor memory usage with `converter.options.maxMemoryBudget`

### Processing Speed
- USDA parsing is generally faster than USDC for small files
- USDC is more efficient for large files
- JSON serialization can be slow for huge datasets

### Optimization Tips
```javascript
// For large files
const converter = new UsdJsonConverter({
  compactArrays: true,
  maxArrayLength: 50,
  maxDepth: 10,
  includeMetadata: false  // Skip metadata if not needed
});

// For real-time applications
const converter = new UsdJsonConverter({
  preserveTypes: false,   // Faster conversion
  sortKeys: false,        // Skip sorting
  includeTimeSamples: false  // Skip if not animating
});
```

## License

Based on TinyUSDZ implementation, Apache 2.0 License.

## Contributing

Contributions welcome! Areas for improvement:
- JSON to USDC binary generation
- Performance optimizations
- Additional USD feature support
- Streaming/lazy loading
- Better error recovery

## References

- [USD Documentation](https://openusd.org/)
- [TinyUSDZ Project](https://github.com/lighttransport/tinyusdz)
- [JSON Schema Specification](https://json-schema.org/)
- [USD ASCII Format Specification](https://openusd.org/dev/api/_usd__page__ascii_syntax.html)