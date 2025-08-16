/**
 * Test suite for USD ↔ JSON bidirectional conversion
 */

const { UsdJsonConverter } = require('./usd-json-converter.js');
const { UsdaParser } = require('./usda-parser.js');
const { UsdcParser, UsdcTimeSamples } = require('./usdc-parser.js');

function testUsdaToJson() {
    console.log('Testing USDA to JSON conversion...');
    
    const usdaContent = `
def Sphere "ball" {
    float3 translate = (0, 1, 2)
    float radius = 1.0
    string name = "test_sphere"
    int[] faceVertexIndices = [0, 1, 2, 2, 3, 0]
}

def Cube "box" {
    float3 size = (2, 2, 2)
    color3f color = (1.0, 0.5, 0.0)
}`;

    try {
        // Parse USDA
        const parser = new UsdaParser(usdaContent);
        const layer = parser.parse();
        
        if (!layer) {
            console.log('  ✗ Failed to parse USDA content');
            return false;
        }

        // Convert to JSON
        const converter = new UsdJsonConverter({
            includeMetadata: true,
            preserveTypes: true,
            indent: 2
        });
        
        const jsonData = converter.usdToJson(layer, 'usda');
        
        // Validate JSON structure
        console.log(`  JSON format: ${jsonData.format}`);
        console.log(`  JSON version: ${jsonData.version}`);
        console.log(`  Prims present: ${jsonData.prims ? 'yes' : 'no'}`);
        
        if (jsonData.prims && jsonData.prims.name) {
            console.log(`  Root prim: ${jsonData.prims.name} (${jsonData.prims.type})`);
            
            // Check attributes
            if (jsonData.prims.attributes) {
                const attrCount = Object.keys(jsonData.prims.attributes).length;
                console.log(`  Root attributes: ${attrCount}`);
                
                // Check specific attribute
                if (jsonData.prims.attributes.radius) {
                    const radius = jsonData.prims.attributes.radius;
                    console.log(`  Radius: ${radius.type} = ${JSON.stringify(radius.value)}`);
                }
            }
            
            // Check children
            if (jsonData.prims.children) {
                const childCount = Object.keys(jsonData.prims.children).length;
                console.log(`  Children: ${childCount}`);
            }
        }
        
        console.log('  ✓ USDA to JSON conversion successful');
        return jsonData;
        
    } catch (error) {
        console.log(`  ✗ USDA to JSON conversion failed: ${error.message}`);
        return false;
    }
}

function testJsonToUsda() {
    console.log('\nTesting JSON to USDA conversion...');
    
    // Create test JSON data (without layer metadata for compatibility)
    const jsonData = {
        format: 'usda',
        version: '1.0',
        prims: {
            name: 'TestRoot',
            type: 'Xform',
            specifier: 'def',
            attributes: {
                translate: {
                    type: 'float3',
                    value: {
                        type: 'tuple',
                        value: [
                            { type: 'number', value: 1.0 },
                            { type: 'number', value: 2.0 },
                            { type: 'number', value: 3.0 }
                        ]
                    }
                },
                visibility: {
                    type: 'token',
                    value: { type: 'string', value: 'inherited' }
                }
            },
            children: {
                sphere: {
                    name: 'sphere',
                    type: 'Sphere',
                    specifier: 'def',
                    attributes: {
                        radius: {
                            type: 'float',
                            value: { type: 'number', value: 1.5 }
                        }
                    }
                }
            }
        }
    };

    try {
        const converter = new UsdJsonConverter();
        const usdaText = converter.jsonToUsd(jsonData, 'usda');
        
        console.log('  Generated USDA:');
        console.log('  ' + '='.repeat(40));
        usdaText.split('\n').forEach(line => {
            console.log(`  ${line}`);
        });
        console.log('  ' + '='.repeat(40));
        
        // Try to parse the generated USDA
        const parser = new UsdaParser(usdaText);
        const reparsedLayer = parser.parse();
        
        if (reparsedLayer) {
            console.log('  ✓ Generated USDA is valid and parseable');
            return true;
        } else {
            console.log('  ✗ Generated USDA is not parseable');
            parser.getErrors().forEach(err => console.log(`    Error: ${err.toString()}`));
            return false;
        }
        
    } catch (error) {
        console.log(`  ✗ JSON to USDA conversion failed: ${error.message}`);
        return false;
    }
}

function testUsdcToJson() {
    console.log('\nTesting USDC to JSON conversion...');
    
    try {
        // Create a minimal USDC layer for testing
        const layer = {
            tokens: ['def', 'Sphere', 'radius', 'translate'],
            strings: ['test_sphere'],
            fields: [
                {
                    token: 2, // 'radius'
                    valueRep: {
                        data: 0x403F800000000000n, // Represents float 1.0 inlined
                        getTypeId: () => 8, // FLOAT
                        getPayload: () => 0x3F800000n, // 1.0 as bits
                        isArray: () => false,
                        isInlined: () => true,
                        isCompressed: () => false
                    }
                }
            ],
            specs: [
                {
                    path: 0,
                    fieldSet: 0,
                    specType: 1
                }
            ],
            paths: [
                {
                    isAbsolute: true,
                    indices: ['', 'Sphere'],
                    toString: () => '/Sphere'
                }
            ],
            timeSamples: new Map()
        };

        const converter = new UsdJsonConverter({
            includeMetadata: true,
            preserveTypes: true
        });
        
        const jsonData = converter.usdToJson(layer, 'usdc');
        
        console.log(`  JSON format: ${jsonData.format}`);
        console.log(`  Token count: ${jsonData.prims.tokens ? jsonData.prims.tokens.length : 0}`);
        console.log(`  Field count: ${jsonData.prims.fields ? jsonData.prims.fields.length : 0}`);
        console.log(`  Spec count: ${jsonData.prims.specs ? jsonData.prims.specs.length : 0}`);
        
        if (jsonData.prims.fields && jsonData.prims.fields.length > 0) {
            const field = jsonData.prims.fields[0];
            console.log(`  First field token: ${field.tokenName}`);
            console.log(`  First field inlined: ${field.valueRep ? field.valueRep.isInlined : 'unknown'}`);
        }
        
        console.log('  ✓ USDC to JSON conversion successful');
        return true;
        
    } catch (error) {
        console.log(`  ✗ USDC to JSON conversion failed: ${error.message}`);
        return false;
    }
}

function testTimeSamplesToJson() {
    console.log('\nTesting Time Samples to JSON conversion...');
    
    try {
        // Create time samples data
        const timeSamples = new UsdcTimeSamples();
        timeSamples.addSample(0.0, 1.0);
        timeSamples.addSample(1.0, 2.0);
        timeSamples.addSample(2.0, 3.0);
        
        const layer = {
            timeSamples: new Map([[0, timeSamples]]),
            tokens: ['translate'],
            strings: [],
            fields: [],
            specs: [],
            paths: []
        };

        const converter = new UsdJsonConverter({
            includeTimeSamples: true
        });
        
        const jsonData = converter.usdToJson(layer, 'usdc');
        
        console.log(`  Time samples present: ${jsonData.timeSamples ? 'yes' : 'no'}`);
        
        if (jsonData.timeSamples && jsonData.timeSamples[0]) {
            const samples = jsonData.timeSamples[0];
            console.log(`  Sample count: ${samples.samples ? samples.samples.length : 0}`);
            console.log(`  Interpolation: ${samples.interpolation}`);
            
            if (samples.samples && samples.samples.length > 0) {
                const firstSample = samples.samples[0];
                console.log(`  First sample: time=${firstSample.time}, value=${JSON.stringify(firstSample.value)}`);
            }
        }
        
        console.log('  ✓ Time samples to JSON conversion successful');
        return true;
        
    } catch (error) {
        console.log(`  ✗ Time samples to JSON conversion failed: ${error.message}`);
        return false;
    }
}

function testRoundTripConversion() {
    console.log('\nTesting round-trip conversion (USDA → JSON → USDA)...');
    
    const originalUsda = `
def Sphere "ball" {
    float radius = 1.0
    float3 translate = (0, 1, 2)
}`;

    try {
        // Parse original USDA
        const parser1 = new UsdaParser(originalUsda);
        const layer1 = parser1.parse();
        
        if (!layer1) {
            console.log('  ✗ Failed to parse original USDA');
            return false;
        }

        // Convert to JSON
        const converter = new UsdJsonConverter();
        const jsonData = converter.usdToJson(layer1, 'usda');
        
        // Convert back to USDA
        const generatedUsda = converter.jsonToUsd(jsonData, 'usda');
        
        // Parse generated USDA
        const parser2 = new UsdaParser(generatedUsda);
        const layer2 = parser2.parse();
        
        if (!layer2) {
            console.log('  ✗ Failed to parse generated USDA');
            parser2.getErrors().forEach(err => console.log(`    Error: ${err.toString()}`));
            return false;
        }

        // Compare key properties
        const originalPrim = layer1.rootPrim;
        const generatedPrim = layer2.rootPrim;
        
        const nameMatch = originalPrim.name === generatedPrim.name;
        const typeMatch = originalPrim.type === generatedPrim.type;
        
        console.log(`  Name match: ${nameMatch} (${originalPrim.name} vs ${generatedPrim.name})`);
        console.log(`  Type match: ${typeMatch} (${originalPrim.type} vs ${generatedPrim.type})`);
        
        if (nameMatch && typeMatch) {
            console.log('  ✓ Round-trip conversion successful');
            return true;
        } else {
            console.log('  ⚠ Round-trip conversion partially successful');
            return false;
        }
        
    } catch (error) {
        console.log(`  ✗ Round-trip conversion failed: ${error.message}`);
        return false;
    }
}

function testJsonValidation() {
    console.log('\nTesting JSON validation...');
    
    const converter = new UsdJsonConverter();
    
    // Test valid JSON
    const validJson = {
        format: 'usda',
        version: '1.0',
        prims: { name: 'test', type: 'Sphere' }
    };
    
    const validErrors = converter.validateJsonData(validJson);
    console.log(`  Valid JSON errors: ${validErrors.length}`);
    
    // Test invalid JSON
    const invalidJson = {
        format: 'invalid',
        // missing prims
    };
    
    const invalidErrors = converter.validateJsonData(invalidJson);
    console.log(`  Invalid JSON errors: ${invalidErrors.length}`);
    
    if (invalidErrors.length > 0) {
        console.log('  Validation errors:');
        invalidErrors.forEach(err => console.log(`    - ${err}`));
    }
    
    const validationWorking = (validErrors.length === 0) && (invalidErrors.length > 0);
    console.log(`  ✓ JSON validation ${validationWorking ? 'working' : 'not working properly'}`);
    
    return validationWorking;
}

function runAllTests() {
    console.log('=== USD ↔ JSON Conversion Tests ===\n');
    
    const tests = [
        testUsdaToJson,
        testJsonToUsda,
        testUsdcToJson,
        testTimeSamplesToJson,
        testRoundTripConversion,
        testJsonValidation
    ];
    
    let passed = 0;
    let total = tests.length;
    
    for (const test of tests) {
        try {
            const result = test();
            if (result) {
                passed++;
            }
        } catch (error) {
            console.log(`  ✗ Test failed with error: ${error.message}`);
        }
        console.log(''); // Add spacing between tests
    }
    
    console.log('=== Test Results ===');
    console.log(`Passed: ${passed}/${total} tests`);
    console.log(`Success rate: ${Math.round((passed/total)*100)}%`);
    
    if (passed === total) {
        console.log('🎉 All USD-JSON conversion tests passed!');
    } else {
        console.log('⚠️  Some tests failed - check implementation');
    }
    
    return passed === total;
}

// Run tests if this file is executed directly
if (require.main === module) {
    runAllTests();
}

module.exports = { runAllTests };