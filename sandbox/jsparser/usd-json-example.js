/**
 * USD ↔ JSON Conversion Examples
 * Demonstrates bidirectional conversion between USD and JSON formats
 */

const { UsdJsonConverter } = require('./usd-json-converter.js');
const { UsdaParser } = require('./usda-parser.js');
const { UsdcParser, UsdcTimeSamples } = require('./usdc-parser.js');
const fs = require('fs');

console.log('=== USD ↔ JSON Conversion Examples ===\n');

// Example 1: USDA to JSON
console.log('Example 1: Converting USDA to JSON');
console.log('=====================================\n');

const sampleUsda = `
def Sphere "ball" {
    float radius = 1.0
    float3 translate = (0, 1, 2)
    color3f color = (1.0, 0.5, 0.0)
    string name = "basketball"
    int[] faceVertexIndices = [0, 1, 2, 2, 3, 0]
}

def Cube "box" {
    float3 size = (2, 2, 2)
    color3f color = (0.0, 0.5, 1.0)
    string material = "metal"
}`;

try {
    // Parse USDA
    const parser = new UsdaParser(sampleUsda);
    const layer = parser.parse();
    
    if (layer) {
        // Convert to JSON
        const converter = new UsdJsonConverter({
            includeMetadata: true,
            preserveTypes: true,
            indent: 2
        });
        
        const jsonData = converter.usdToJson(layer, 'usda');
        
        console.log('Original USDA:');
        console.log(sampleUsda);
        console.log('\nConverted to JSON:');
        console.log(JSON.stringify(jsonData, null, 2));
        
        // Save to file
        fs.writeFileSync('/tmp/sample.json', JSON.stringify(jsonData, null, 2));
        console.log('\n✓ JSON saved to /tmp/sample.json');
    }
} catch (error) {
    console.error('Error in USDA to JSON conversion:', error.message);
}

console.log('\n' + '='.repeat(50) + '\n');

// Example 2: JSON to USDA
console.log('Example 2: Converting JSON to USDA');
console.log('===================================\n');

const sampleJson = {
    format: 'usda',
    version: '1.0',
    prims: {
        name: 'Scene',
        type: 'Xform',
        specifier: 'def',
        attributes: {
            purpose: {
                type: 'token',
                value: { type: 'string', value: 'default' }
            }
        },
        children: {
            camera: {
                name: 'camera',
                type: 'Camera',
                specifier: 'def',
                attributes: {
                    focalLength: {
                        type: 'float',
                        value: { type: 'number', value: 50.0 }
                    },
                    projection: {
                        type: 'token',
                        value: { type: 'string', value: 'perspective' }
                    }
                }
            },
            light: {
                name: 'light',
                type: 'SphereLight',
                specifier: 'def',
                attributes: {
                    intensity: {
                        type: 'float',
                        value: { type: 'number', value: 1000.0 }
                    },
                    color: {
                        type: 'color3f',
                        value: {
                            type: 'tuple',
                            value: [
                                { type: 'number', value: 1.0 },
                                { type: 'number', value: 1.0 },
                                { type: 'number', value: 0.9 }
                            ]
                        }
                    }
                }
            }
        }
    }
};

try {
    const converter = new UsdJsonConverter();
    const generatedUsda = converter.jsonToUsd(sampleJson, 'usda');
    
    console.log('Source JSON:');
    console.log(JSON.stringify(sampleJson, null, 2));
    console.log('\nConverted to USDA:');
    console.log(generatedUsda);
    
    // Save to file
    fs.writeFileSync('/tmp/generated.usda', generatedUsda);
    console.log('✓ USDA saved to /tmp/generated.usda');
    
    // Verify it can be parsed
    const verifyParser = new UsdaParser(generatedUsda);
    const verifyLayer = verifyParser.parse();
    if (verifyLayer) {
        console.log('✓ Generated USDA is valid and parseable');
    } else {
        console.log('✗ Generated USDA has parsing errors');
    }
} catch (error) {
    console.error('Error in JSON to USDA conversion:', error.message);
}

console.log('\n' + '='.repeat(50) + '\n');

// Example 3: Time Samples to JSON
console.log('Example 3: Converting Time Samples to JSON');
console.log('==========================================\n');

try {
    // Create time samples data
    const timeSamples = new UsdcTimeSamples();
    timeSamples.addSample(0.0, [0.0, 0.0, 0.0]);    // Initial position
    timeSamples.addSample(1.0, [5.0, 2.0, 0.0]);    // Middle position
    timeSamples.addSample(2.0, [10.0, 0.0, 0.0]);   // Final position
    
    const layer = {
        tokens: ['translate'],
        strings: ['animation'],
        fields: [],
        specs: [],
        paths: [],
        timeSamples: new Map([[0, timeSamples]])
    };

    const converter = new UsdJsonConverter({
        includeTimeSamples: true
    });
    
    const jsonData = converter.usdToJson(layer, 'usdc');
    
    console.log('Time Samples Data:');
    console.log('- 3 keyframes with vector positions');
    console.log('- Linear interpolation');
    console.log('\nConverted to JSON:');
    console.log(JSON.stringify(jsonData.timeSamples, null, 2));
    
    // Test interpolation
    const interpolatedPos = timeSamples.getSampleAtTime(0.5);
    console.log(`\nInterpolated position at t=0.5: [${interpolatedPos.join(', ')}]`);
    
    console.log('✓ Time samples conversion successful');
} catch (error) {
    console.error('Error in time samples conversion:', error.message);
}

console.log('\n' + '='.repeat(50) + '\n');

// Example 4: USDC to JSON (if files available)
console.log('Example 4: Converting USDC files to JSON');
console.log('=========================================\n');

const usdcFiles = [
    '../../models/cube.usdc',
    './test.usdc'
];

for (const filename of usdcFiles) {
    try {
        if (fs.existsSync(filename)) {
            console.log(`Processing: ${filename}`);
            
            const fileData = fs.readFileSync(filename);
            const parser = new UsdcParser(fileData);
            const layer = parser.parse();
            
            if (layer) {
                const converter = new UsdJsonConverter({
                    compactArrays: true,
                    maxArrayLength: 10
                });
                
                const jsonData = converter.usdToJson(layer, 'usdc');
                
                console.log(`  File size: ${fileData.length} bytes`);
                console.log(`  Tokens: ${jsonData.prims.tokens ? jsonData.prims.tokens.length : 0}`);
                console.log(`  Fields: ${jsonData.prims.fields ? jsonData.prims.fields.length : 0}`);
                console.log(`  Specs: ${jsonData.prims.specs ? jsonData.prims.specs.length : 0}`);
                
                // Save JSON version
                const jsonFilename = filename.replace('.usdc', '.json');
                fs.writeFileSync(jsonFilename, JSON.stringify(jsonData, null, 2));
                console.log(`  ✓ JSON saved to ${jsonFilename}`);
                
            } else {
                console.log(`  ✗ Failed to parse ${filename}`);
            }
        } else {
            console.log(`  File not found: ${filename}`);
        }
    } catch (error) {
        console.log(`  ✗ Error processing ${filename}: ${error.message}`);
    }
}

console.log('\n' + '='.repeat(50) + '\n');

// Example 5: Configuration Options
console.log('Example 5: Conversion Configuration Options');
console.log('===========================================\n');

const testUsda = `
def Mesh "largeMesh" {
    int[] faceVertexIndices = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]
    point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0)]
}`;

try {
    const parser = new UsdaParser(testUsda);
    const layer = parser.parse();
    
    if (layer) {
        // Standard conversion
        const converter1 = new UsdJsonConverter({
            preserveTypes: true,
            compactArrays: false
        });
        const json1 = converter1.usdToJson(layer, 'usda');
        
        // Compact conversion
        const converter2 = new UsdJsonConverter({
            preserveTypes: false,
            compactArrays: true,
            maxArrayLength: 5
        });
        const json2 = converter2.usdToJson(layer, 'usda');
        
        console.log('Standard conversion (full arrays):');
        if (json1.prims.attributes && json1.prims.attributes.faceVertexIndices) {
            const attr = json1.prims.attributes.faceVertexIndices;
            console.log(`  Array length: ${attr.value.value ? attr.value.value.length : 'unknown'}`);
        }
        
        console.log('\nCompact conversion (truncated arrays):');
        if (json2.prims.attributes && json2.prims.attributes.faceVertexIndices) {
            const attr = json2.prims.attributes.faceVertexIndices;
            if (attr.value.truncated) {
                console.log(`  Array truncated: ${attr.value.length} total, showing ${attr.value.sample.length} samples`);
            }
        }
        
        console.log('✓ Configuration options demonstrated');
    }
} catch (error) {
    console.error('Error in configuration demo:', error.message);
}

console.log('\n=== USD ↔ JSON Conversion Examples Complete ===');
console.log('\nGenerated files:');
console.log('- /tmp/sample.json (USDA → JSON)');
console.log('- /tmp/generated.usda (JSON → USDA)');
console.log('- *.json (USDC → JSON, if USDC files available)');
console.log('\nFeatures demonstrated:');
console.log('✓ USDA to JSON conversion');
console.log('✓ JSON to USDA conversion');
console.log('✓ Time samples support');
console.log('✓ USDC to JSON conversion');
console.log('✓ Configuration options');
console.log('✓ Round-trip conversion validation');