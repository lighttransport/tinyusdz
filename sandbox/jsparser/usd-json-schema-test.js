/**
 * Test suite for USD JSON Schema Validation
 */

const { UsdJsonSchemaValidator } = require('./usd-json-schema.js');

function testValidUsdaJson() {
    console.log('Testing valid USDA JSON...');
    
    const validJson = {
        format: 'usda',
        version: '1.0',
        metadata: {
            upAxis: {
                type: 'string',
                value: 'Y'
            }
        },
        prims: {
            name: 'TestSphere',
            type: 'Sphere',
            specifier: 'def',
            attributes: {
                radius: {
                    type: 'float',
                    value: {
                        type: 'number',
                        value: 1.0
                    }
                },
                color: {
                    type: 'color3f',
                    value: {
                        type: 'tuple',
                        value: [
                            { type: 'number', value: 1.0 },
                            { type: 'number', value: 0.5 },
                            { type: 'number', value: 0.0 }
                        ]
                    }
                }
            }
        },
        timeSamples: {},
        statistics: {
            generatedAt: '2023-08-16T12:00:00.000Z',
            tokenCount: 10
        }
    };

    const validator = new UsdJsonSchemaValidator();
    const result = validator.validateUsdJson(validJson);
    const summary = validator.getValidationSummary(result);

    console.log(`  Valid: ${summary.isValid}`);
    console.log(`  Errors: ${summary.errorCount}`);
    console.log(`  Warnings: ${summary.warningCount}`);
    
    if (summary.errors.length > 0) {
        console.log('  Error details:');
        summary.errors.forEach(err => console.log(`    - ${err}`));
    }

    return summary.isValid;
}

function testInvalidUsdaJson() {
    console.log('\nTesting invalid USDA JSON...');
    
    const invalidJson = {
        format: 'invalid_format', // Invalid format
        // missing version
        prims: {
            // missing name
            type: 'Sphere',
            attributes: {
                radius: {
                    // missing type
                    value: {
                        type: 'number',
                        value: 1.0
                    }
                }
            }
        }
    };

    const validator = new UsdJsonSchemaValidator();
    const result = validator.validateUsdJson(invalidJson);
    const summary = validator.getValidationSummary(result);

    console.log(`  Valid: ${summary.isValid}`);
    console.log(`  Errors: ${summary.errorCount}`);
    console.log(`  Warnings: ${summary.warningCount}`);
    
    if (summary.errors.length > 0) {
        console.log('  Expected errors found:');
        summary.errors.forEach(err => console.log(`    - ${err}`));
    }

    return !summary.isValid && summary.errorCount > 0;
}

function testValidUsdcJson() {
    console.log('\nTesting valid USDC JSON...');
    
    const validJson = {
        format: 'usdc',
        version: '1.0',
        prims: {
            tokens: ['def', 'Sphere', 'radius'],
            strings: ['test_sphere'],
            fields: [
                {
                    index: 0,
                    token: 2,
                    tokenName: 'radius',
                    valueRep: {
                        data: '1152921504606846976',
                        typeId: 8,
                        payload: '1065353216',
                        isArray: false,
                        isInlined: true,
                        isCompressed: false
                    }
                }
            ],
            specs: [
                {
                    index: 0,
                    path: 0,
                    pathString: '/Sphere',
                    fieldSet: 0,
                    specType: 1,
                    fields: [0]
                }
            ],
            paths: [
                {
                    index: 0,
                    isAbsolute: true,
                    elements: ['', 'Sphere'],
                    pathString: '/Sphere'
                }
            ]
        },
        timeSamples: {},
        statistics: {
            generatedAt: '2023-08-16T12:00:00.000Z',
            tokenCount: 3,
            fieldCount: 1,
            specCount: 1
        }
    };

    const validator = new UsdJsonSchemaValidator();
    const result = validator.validateUsdJson(validJson);
    const summary = validator.getValidationSummary(result);

    console.log(`  Valid: ${summary.isValid}`);
    console.log(`  Errors: ${summary.errorCount}`);
    console.log(`  Warnings: ${summary.warningCount}`);
    
    if (summary.errors.length > 0) {
        console.log('  Error details:');
        summary.errors.forEach(err => console.log(`    - ${err}`));
    }

    return summary.isValid;
}

function testTimeSamplesValidation() {
    console.log('\nTesting time samples validation...');
    
    const jsonWithTimeSamples = {
        format: 'usdc',
        version: '1.0',
        prims: {
            tokens: ['translate']
        },
        timeSamples: {
            '0': {
                type: 'time_samples',
                interpolation: 'linear',
                samples: [
                    {
                        time: 0.0,
                        value: {
                            type: 'tuple',
                            value: [
                                { type: 'number', value: 0.0 },
                                { type: 'number', value: 0.0 },
                                { type: 'number', value: 0.0 }
                            ]
                        }
                    },
                    {
                        time: 1.0,
                        value: {
                            type: 'tuple',
                            value: [
                                { type: 'number', value: 5.0 },
                                { type: 'number', value: 2.0 },
                                { type: 'number', value: 0.0 }
                            ]
                        }
                    }
                ]
            }
        }
    };

    const validator = new UsdJsonSchemaValidator();
    const result = validator.validateUsdJson(jsonWithTimeSamples);
    const summary = validator.getValidationSummary(result);

    console.log(`  Valid: ${summary.isValid}`);
    console.log(`  Errors: ${summary.errorCount}`);
    console.log(`  Time samples count: ${Object.keys(jsonWithTimeSamples.timeSamples).length}`);
    
    if (summary.errors.length > 0) {
        console.log('  Error details:');
        summary.errors.forEach(err => console.log(`    - ${err}`));
    }

    return summary.isValid;
}

function testInvalidTimeSamples() {
    console.log('\nTesting invalid time samples...');
    
    const invalidTimeSamples = {
        format: 'usdc',
        version: '1.0',
        prims: { tokens: [] },
        timeSamples: {
            '0': {
                type: 'invalid_type', // Invalid type
                samples: [
                    {
                        // missing time
                        value: { type: 'number', value: 1.0 }
                    },
                    {
                        time: 'invalid_time', // Invalid time type
                        // missing value
                    }
                ]
            }
        }
    };

    const validator = new UsdJsonSchemaValidator();
    const result = validator.validateUsdJson(invalidTimeSamples);
    const summary = validator.getValidationSummary(result);

    console.log(`  Valid: ${summary.isValid}`);
    console.log(`  Errors: ${summary.errorCount}`);
    
    if (summary.errors.length > 0) {
        console.log('  Expected errors found:');
        summary.errors.forEach(err => console.log(`    - ${err}`));
    }

    return !summary.isValid && summary.errorCount > 0;
}

function testAttributeValidation() {
    console.log('\nTesting attribute validation...');
    
    const jsonWithInvalidAttrs = {
        format: 'usda',
        version: '1.0',
        prims: {
            name: 'TestPrim',
            type: 'Sphere',
            attributes: {
                validAttr: {
                    type: 'float',
                    value: { type: 'number', value: 1.0 }
                },
                invalidAttr: {
                    // missing type
                    value: { type: 'number', value: 2.0 }
                },
                invalidVariability: {
                    type: 'string',
                    value: { type: 'string', value: 'test' },
                    variability: 'invalid_variability'
                }
            }
        }
    };

    const validator = new UsdJsonSchemaValidator();
    const result = validator.validateUsdJson(jsonWithInvalidAttrs);
    const summary = validator.getValidationSummary(result);

    console.log(`  Valid: ${summary.isValid}`);
    console.log(`  Errors: ${summary.errorCount}`);
    
    if (summary.errors.length > 0) {
        console.log('  Attribute validation errors:');
        summary.errors.forEach(err => console.log(`    - ${err}`));
    }

    return !summary.isValid && summary.errorCount >= 2; // Should have at least 2 errors
}

function testSchemaGeneration() {
    console.log('\nTesting schema generation...');
    
    const validator = new UsdJsonSchemaValidator();
    const schema = validator.generateExampleSchema();

    console.log(`  Schema title: ${schema.title}`);
    console.log(`  Schema description: ${schema.description}`);
    console.log(`  Has definitions: ${schema.definitions ? 'yes' : 'no'}`);
    console.log(`  Required fields: ${schema.required ? schema.required.join(', ') : 'none'}`);

    const hasBasicStructure = 
        schema.title && 
        schema.description && 
        schema.definitions && 
        schema.required && 
        schema.required.includes('format') &&
        schema.required.includes('prims');

    console.log(`  Schema structure valid: ${hasBasicStructure}`);

    return hasBasicStructure;
}

function runAllTests() {
    console.log('=== USD JSON Schema Validation Tests ===\n');
    
    const tests = [
        { name: 'Valid USDA JSON', func: testValidUsdaJson },
        { name: 'Invalid USDA JSON', func: testInvalidUsdaJson },
        { name: 'Valid USDC JSON', func: testValidUsdcJson },
        { name: 'Time Samples Validation', func: testTimeSamplesValidation },
        { name: 'Invalid Time Samples', func: testInvalidTimeSamples },
        { name: 'Attribute Validation', func: testAttributeValidation },
        { name: 'Schema Generation', func: testSchemaGeneration }
    ];
    
    let passed = 0;
    let total = tests.length;
    
    for (const test of tests) {
        try {
            const result = test.func();
            if (result) {
                console.log(`  ✓ ${test.name} passed`);
                passed++;
            } else {
                console.log(`  ✗ ${test.name} failed`);
            }
        } catch (error) {
            console.log(`  ✗ ${test.name} failed with error: ${error.message}`);
        }
        console.log('');
    }
    
    console.log('=== Schema Validation Test Results ===');
    console.log(`Passed: ${passed}/${total} tests`);
    console.log(`Success rate: ${Math.round((passed/total)*100)}%`);
    
    if (passed === total) {
        console.log('🎉 All schema validation tests passed!');
    } else {
        console.log('⚠️  Some schema validation tests failed');
    }
    
    return passed === total;
}

// Run tests if this file is executed directly
if (require.main === module) {
    runAllTests();
}

module.exports = { runAllTests };