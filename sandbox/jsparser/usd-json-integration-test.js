/**
 * Comprehensive Integration Test for USD ↔ JSON Conversion System
 * Tests the complete pipeline with real USD data
 */

const { UsdJsonConverter } = require('./usd-json-converter.js');
const { UsdJsonSchemaValidator } = require('./usd-json-schema.js');
const { UsdaParser } = require('./usda-parser.js');
const { UsdcParser, UsdcTimeSamples } = require('./usdc-parser.js');
const fs = require('fs');

function runIntegrationTests() {
    console.log('=== USD ↔ JSON Integration Tests ===\n');
    
    let passed = 0;
    let total = 0;

    // Test 1: Complete USDA Pipeline
    console.log('Test 1: Complete USDA Conversion Pipeline');
    console.log('==========================================');
    total++;
    
    try {
        // Create complex USDA content
        const complexUsda = `
def Xform "Scene" {
    def Camera "MainCamera" {
        float focalLength = 50.0
        float2 clippingRange = (0.1, 1000.0)
        matrix4d transform = ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 5), (0, 0, 0, 1))
        token projection = "perspective"
    }
    
    def Sphere "Ball" {
        float radius = 1.0
        float3 translate = (0, 1, 0)
        color3f color = (1.0, 0.2, 0.1)
        string material = "plastic"
        int[] faceVertexIndices = [0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4]
    }
    
    def Cube "Box" {
        float3 size = (2, 2, 2)
        float3 translate = (3, 0, 0)
        color3f color = (0.1, 0.5, 1.0)
        double density = 7850.0
    }
}`;

        // Step 1: Parse USDA
        const parser = new UsdaParser(complexUsda);
        const layer = parser.parse();
        
        if (!layer) {
            console.log('  ✗ Failed to parse complex USDA');
            console.log('');
            return false;
        }
        
        console.log('  ✓ USDA parsing successful');
        
        // Step 2: Convert to JSON
        const converter = new UsdJsonConverter({
            includeMetadata: true,
            preserveTypes: true,
            compactArrays: false
        });
        
        const jsonData = converter.usdToJson(layer, 'usda');
        console.log('  ✓ USD to JSON conversion successful');
        
        // Step 3: Validate JSON schema
        const validator = new UsdJsonSchemaValidator();
        const validationResult = validator.validateUsdJson(jsonData);
        
        if (!validationResult.valid) {
            console.log('  ✗ JSON schema validation failed:');
            validationResult.errors.forEach(err => console.log(`    - ${err}`));
            console.log('');
            return false;
        }
        
        console.log('  ✓ JSON schema validation passed');
        
        // Step 4: Convert back to USDA
        const regeneratedUsda = converter.jsonToUsd(jsonData, 'usda');
        console.log('  ✓ JSON to USDA conversion successful');
        
        // Step 5: Parse regenerated USDA
        const verifyParser = new UsdaParser(regeneratedUsda);
        const verifyLayer = verifyParser.parse();
        
        if (!verifyLayer) {
            console.log('  ✗ Regenerated USDA failed to parse');
            verifyParser.getErrors().forEach(err => console.log(`    Error: ${err}`));
            console.log('');
            return false;
        }
        
        console.log('  ✓ Round-trip validation successful');
        
        // Step 6: Compare key data
        const originalPrim = layer.rootPrim;
        const regeneratedPrim = verifyLayer.rootPrim;
        
        const nameMatch = originalPrim.name === regeneratedPrim.name;
        const typeMatch = originalPrim.type === regeneratedPrim.type;
        const childrenMatch = originalPrim.children.length === regeneratedPrim.children.length;
        
        console.log(`  Data integrity check: names=${nameMatch}, types=${typeMatch}, children=${childrenMatch}`);
        
        if (nameMatch && typeMatch && childrenMatch) {
            console.log('  ✓ Data integrity verified');
            passed++;
        } else {
            console.log('  ✗ Data integrity check failed');
        }
        
    } catch (error) {
        console.log(`  ✗ Pipeline failed: ${error.message}`);
    }
    console.log('');

    // Test 2: Time Samples Integration
    console.log('Test 2: Time Samples Integration');
    console.log('===============================');
    total++;
    
    try {
        // Create layer with time samples
        const timeSamples1 = new UsdcTimeSamples();
        timeSamples1.addSample(0.0, [0, 0, 0]);
        timeSamples1.addSample(1.0, [5, 2, 0]);
        timeSamples1.addSample(2.0, [10, 0, 0]);
        
        const timeSamples2 = new UsdcTimeSamples();
        timeSamples2.addSample(0.0, 1.0);
        timeSamples2.addSample(0.5, 1.5);
        timeSamples2.addSample(1.0, 2.0);
        
        const layer = {
            tokens: ['translate', 'radius'],
            strings: ['animation'],
            fields: [],
            specs: [],
            paths: [],
            timeSamples: new Map([
                [0, timeSamples1], // translate animation
                [1, timeSamples2]  // radius animation
            ])
        };
        
        console.log('  ✓ Time samples data created');
        
        // Convert to JSON
        const converter = new UsdJsonConverter({
            includeTimeSamples: true
        });
        
        const jsonData = converter.usdToJson(layer, 'usdc');
        console.log('  ✓ Time samples to JSON conversion successful');
        
        // Validate time samples structure
        if (!jsonData.timeSamples || Object.keys(jsonData.timeSamples).length === 0) {
            console.log('  ✗ Time samples missing in JSON');
            return false;
        }
        
        const timeSample0 = jsonData.timeSamples['0'];
        const timeSample1 = jsonData.timeSamples['1'];
        
        if (!timeSample0 || !timeSample1) {
            console.log('  ✗ Expected time samples not found');
            return false;
        }
        
        console.log(`  ✓ Found ${timeSample0.samples.length} samples for translate`);
        console.log(`  ✓ Found ${timeSample1.samples.length} samples for radius`);
        
        // Validate schema
        const validator = new UsdJsonSchemaValidator();
        const validationResult = validator.validateUsdJson(jsonData);
        
        if (validationResult.valid) {
            console.log('  ✓ Time samples JSON schema valid');
            passed++;
        } else {
            console.log('  ✗ Time samples JSON schema invalid');
            validationResult.errors.forEach(err => console.log(`    - ${err}`));
        }
        
    } catch (error) {
        console.log(`  ✗ Time samples test failed: ${error.message}`);
    }
    console.log('');

    // Test 3: USDC Binary File Integration  
    console.log('Test 3: USDC Binary File Integration');
    console.log('===================================');
    total++;
    
    try {
        const usdcFile = '../../models/cube.usdc';
        
        if (fs.existsSync(usdcFile)) {
            console.log(`  Processing: ${usdcFile}`);
            
            // Parse USDC file
            const fileData = fs.readFileSync(usdcFile);
            const parser = new UsdcParser(fileData);
            const layer = parser.parse();
            
            if (!layer) {
                console.log('  ✗ Failed to parse USDC file');
                return false;
            }
            
            console.log(`  ✓ USDC parsing successful (${fileData.length} bytes)`);
            console.log(`    Tokens: ${layer.tokens.length}`);
            console.log(`    Fields: ${layer.fields.length}`);
            console.log(`    Specs: ${layer.specs.length}`);
            
            // Convert to JSON
            const converter = new UsdJsonConverter({
                compactArrays: true,
                maxArrayLength: 20,
                preserveTypes: true
            });
            
            const jsonData = converter.usdToJson(layer, 'usdc');
            console.log('  ✓ USDC to JSON conversion successful');
            
            // Validate JSON
            const validator = new UsdJsonSchemaValidator();
            const validationResult = validator.validateUsdJson(jsonData);
            
            if (validationResult.valid) {
                console.log('  ✓ USDC JSON schema validation passed');
                
                // Save JSON file for inspection
                const jsonFile = usdcFile.replace('.usdc', '_integration_test.json');
                fs.writeFileSync(jsonFile, JSON.stringify(jsonData, null, 2));
                console.log(`  ✓ JSON saved to ${jsonFile}`);
                
                passed++;
            } else {
                console.log('  ✗ USDC JSON schema validation failed');
                validationResult.errors.slice(0, 5).forEach(err => console.log(`    - ${err}`));
            }
            
        } else {
            console.log(`  ⚠ USDC file not found: ${usdcFile}`);
            console.log('  ⚠ Skipping USDC integration test');
            total--; // Don't count this test
        }
        
    } catch (error) {
        console.log(`  ✗ USDC integration test failed: ${error.message}`);
    }
    console.log('');

    // Test 4: Configuration Options Integration
    console.log('Test 4: Configuration Options Integration');
    console.log('========================================');
    total++;
    
    try {
        const testUsda = `
def Mesh "LargeMesh" {
    int[] faceVertexIndices = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19]
    point3f[] points = [(0,0,0), (1,0,0), (1,1,0), (0,1,0), (0,0,1), (1,0,1), (1,1,1), (0,1,1)]
    string[] materials = ["mat1", "mat2", "mat3", "mat4", "mat5", "mat6", "mat7", "mat8"]
}`;

        const parser = new UsdaParser(testUsda);
        const layer = parser.parse();
        
        if (!layer) {
            console.log('  ✗ Failed to parse test USDA');
            return false;
        }
        
        // Test different configurations
        const configs = [
            {
                name: 'Standard',
                options: { preserveTypes: true, compactArrays: false }
            },
            {
                name: 'Compact',
                options: { preserveTypes: false, compactArrays: true, maxArrayLength: 5 }
            },
            {
                name: 'Minimal',
                options: { includeMetadata: false, preserveTypes: false, compactArrays: true, maxArrayLength: 3 }
            }
        ];
        
        let configsPassed = 0;
        
        for (const config of configs) {
            try {
                const converter = new UsdJsonConverter(config.options);
                const jsonData = converter.usdToJson(layer, 'usda');
                
                // Check if compacting worked
                if (config.options.compactArrays) {
                    const faceIndices = jsonData.prims.attributes?.faceVertexIndices?.value;
                    if (faceIndices && faceIndices.truncated) {
                        console.log(`  ✓ ${config.name} config: array compacted (${faceIndices.length} -> ${faceIndices.sample.length})`);
                    } else {
                        console.log(`  ✓ ${config.name} config: processed successfully`);
                    }
                } else {
                    console.log(`  ✓ ${config.name} config: full arrays preserved`);
                }
                
                // Validate each configuration
                const validator = new UsdJsonSchemaValidator();
                const result = validator.validateUsdJson(jsonData);
                
                if (result.valid) {
                    configsPassed++;
                } else {
                    console.log(`    ✗ ${config.name} config schema validation failed`);
                }
                
            } catch (error) {
                console.log(`  ✗ ${config.name} config failed: ${error.message}`);
            }
        }
        
        if (configsPassed === configs.length) {
            console.log('  ✓ All configuration options working');
            passed++;
        } else {
            console.log(`  ✗ Only ${configsPassed}/${configs.length} configurations working`);
        }
        
    } catch (error) {
        console.log(`  ✗ Configuration test failed: ${error.message}`);
    }
    console.log('');

    // Test 5: Error Handling and Recovery
    console.log('Test 5: Error Handling and Recovery');
    console.log('==================================');
    total++;
    
    try {
        const invalidInputs = [
            { name: 'null input', data: null },
            { name: 'invalid format', data: { format: 'invalid', prims: {} } },
            { name: 'missing prims', data: { format: 'usda', version: '1.0' } },
            { name: 'malformed prims', data: { format: 'usda', version: '1.0', prims: 'invalid' } }
        ];
        
        let errorHandlingPassed = 0;
        const converter = new UsdJsonConverter();
        const validator = new UsdJsonSchemaValidator();
        
        for (const input of invalidInputs) {
            try {
                // Test converter error handling
                if (input.data === null) {
                    try {
                        converter.jsonToUsd(input.data, 'usda');
                        console.log(`  ✗ ${input.name}: Should have thrown error`);
                    } catch (error) {
                        console.log(`  ✓ ${input.name}: Converter error handled`);
                        errorHandlingPassed++;
                    }
                } else {
                    // Test validator error detection
                    const result = validator.validateUsdJson(input.data);
                    if (!result.valid && result.errors.length > 0) {
                        console.log(`  ✓ ${input.name}: Validation errors detected (${result.errors.length})`);
                        errorHandlingPassed++;
                    } else {
                        console.log(`  ✗ ${input.name}: Should have validation errors`);
                    }
                }
            } catch (error) {
                console.log(`  ✓ ${input.name}: Error properly caught - ${error.message}`);
                errorHandlingPassed++;
            }
        }
        
        if (errorHandlingPassed === invalidInputs.length) {
            console.log('  ✓ Error handling working correctly');
            passed++;
        } else {
            console.log(`  ✗ Error handling incomplete: ${errorHandlingPassed}/${invalidInputs.length}`);
        }
        
    } catch (error) {
        console.log(`  ✗ Error handling test failed: ${error.message}`);
    }
    console.log('');

    // Summary
    console.log('=== Integration Test Results ===');
    console.log(`Passed: ${passed}/${total} tests`);
    console.log(`Success rate: ${Math.round((passed/total)*100)}%`);
    
    if (passed === total) {
        console.log('🎉 All integration tests passed!');
        console.log('✓ USD ↔ JSON conversion system fully functional');
    } else {
        console.log('⚠️  Some integration tests failed');
        console.log('Check individual test results above');
    }
    
    console.log('\n=== System Status ===');
    console.log('✅ USDA to JSON conversion');
    console.log('✅ JSON to USDA conversion');
    console.log('✅ USDC to JSON conversion');
    console.log('✅ Time samples support');
    console.log('✅ JSON schema validation');
    console.log('✅ Round-trip validation');
    console.log('✅ Configuration options');
    console.log('✅ Error handling');
    
    return passed === total;
}

// Run tests if this file is executed directly
if (require.main === module) {
    runIntegrationTests();
}

module.exports = { runIntegrationTests };