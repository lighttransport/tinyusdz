/**
 * Example usage of the USDC JavaScript parser
 */

const { UsdcParser } = require('./usdc-parser.js');
const { UsdcValueReader } = require('./usdc-value-reader.js');
const fs = require('fs');

function parseUsdcFile(filename) {
    console.log(`Parsing USDC file: ${filename}\n`);
    
    try {
        // Read file data
        const fileData = fs.readFileSync(filename);
        console.log(`File size: ${fileData.length} bytes`);
        
        // Create parser
        const parser = new UsdcParser(fileData);
        
        // Parse the file
        const layer = parser.parse();
        
        if (!layer) {
            console.error('Parse failed!');
            parser.getErrors().forEach(err => console.error(`  ${err.toString()}`));
            return null;
        }
        
        console.log('Parse successful!\n');
        
        // Print warnings if any
        const warnings = parser.getWarnings();
        if (warnings.length > 0) {
            console.log('Warnings:');
            warnings.forEach(warning => console.log(`  ${warning}`));
            console.log('');
        }
        
        return { parser, layer };
        
    } catch (error) {
        console.error(`Error reading file: ${error.message}`);
        return null;
    }
}

function analyzeUsdcLayer(parser, layer) {
    console.log('=== USDC Layer Analysis ===\n');
    
    // Basic statistics
    console.log('Layer Statistics:');
    console.log(`  Tokens: ${layer.tokens.length}`);
    console.log(`  Strings: ${layer.strings.length}`);
    console.log(`  Fields: ${layer.fields.length}`);
    console.log(`  Field Sets: ${layer.fieldSets.length}`);
    console.log(`  Specs: ${layer.specs.length}`);
    console.log(`  Paths: ${layer.paths.length}`);
    console.log(`  Memory used: ${(parser.memoryUsed / 1024 / 1024).toFixed(2)} MB\n`);
    
    // Show some tokens
    if (layer.tokens.length > 0) {
        console.log('Sample Tokens:');
        const maxTokens = Math.min(10, layer.tokens.length);
        for (let i = 0; i < maxTokens; i++) {
            console.log(`  [${i}] "${layer.tokens[i]}"`);
        }
        if (layer.tokens.length > maxTokens) {
            console.log(`  ... and ${layer.tokens.length - maxTokens} more`);
        }
        console.log('');
    }
    
    // Show some strings
    if (layer.strings.length > 0) {
        console.log('Sample Strings:');
        const maxStrings = Math.min(5, layer.strings.length);
        for (let i = 0; i < maxStrings; i++) {
            const str = layer.strings[i];
            const displayStr = str.length > 50 ? str.substring(0, 50) + '...' : str;
            console.log(`  [${i}] "${displayStr}"`);
        }
        if (layer.strings.length > maxStrings) {
            console.log(`  ... and ${layer.strings.length - maxStrings} more`);
        }
        console.log('');
    }
    
    // Show some paths
    if (layer.paths.length > 0) {
        console.log('Sample Paths:');
        const maxPaths = Math.min(10, layer.paths.length);
        for (let i = 0; i < maxPaths; i++) {
            const path = layer.paths[i];
            console.log(`  [${i}] ${path.toString()}`);
        }
        if (layer.paths.length > maxPaths) {
            console.log(`  ... and ${layer.paths.length - maxPaths} more`);
        }
        console.log('');
    }
    
    // Analyze fields
    if (layer.fields.length > 0) {
        console.log('Sample Fields:');
        const maxFields = Math.min(10, layer.fields.length);
        for (let i = 0; i < maxFields; i++) {
            const field = layer.fields[i];
            const tokenName = parser.getToken(field.token);
            const valueRep = field.valueRep;
            
            console.log(`  [${i}] ${tokenName}:`);
            console.log(`    Type: ${valueRep.getTypeId()}`);
            console.log(`    Array: ${valueRep.isArray()}`);
            console.log(`    Inlined: ${valueRep.isInlined()}`);
            console.log(`    Compressed: ${valueRep.isCompressed()}`);
            console.log(`    Payload: 0x${valueRep.getPayload().toString(16)}`);
        }
        if (layer.fields.length > maxFields) {
            console.log(`  ... and ${layer.fields.length - maxFields} more`);
        }
        console.log('');
    }
    
    // Analyze specs
    if (layer.specs.length > 0) {
        console.log('Sample Specs:');
        const maxSpecs = Math.min(5, layer.specs.length);
        for (let i = 0; i < maxSpecs; i++) {
            const spec = layer.specs[i];
            const pathStr = parser.getPath(spec.path);
            
            console.log(`  [${i}] Path: ${pathStr}`);
            console.log(`    Field Set: ${spec.fieldSet}`);
            console.log(`    Spec Type: ${spec.specType}`);
            
            // Show fields in this spec's fieldset
            if (spec.fieldSet < layer.fieldSets.length) {
                const fieldSet = layer.fieldSets[spec.fieldSet];
                console.log(`    Fields: [${fieldSet.fields.slice(0, 5).join(', ')}${fieldSet.fields.length > 5 ? '...' : ''}]`);
            }
        }
        if (layer.specs.length > maxSpecs) {
            console.log(`  ... and ${layer.specs.length - maxSpecs} more`);
        }
        console.log('');
    }
}

function demonstrateValueReading(parser, layer) {
    console.log('=== Value Reading Demo ===\n');
    
    if (layer.fields.length === 0) {
        console.log('No fields available for value reading demo');
        return;
    }
    
    const valueReader = new UsdcValueReader(parser);
    
    console.log('Reading field values:');
    const maxDemo = Math.min(5, layer.fields.length);
    
    for (let i = 0; i < maxDemo; i++) {
        const field = layer.fields[i];
        const tokenName = parser.getToken(field.token);
        
        try {
            // Note: This is a simplified demo - real value reading needs proper data sections
            console.log(`  Field "${tokenName}":`);
            console.log(`    Type ID: ${field.valueRep.getTypeId()}`);
            console.log(`    Is inlined: ${field.valueRep.isInlined()}`);
            
            if (field.valueRep.isInlined()) {
                const value = valueReader.readInlinedValue(
                    field.valueRep.getTypeId(), 
                    field.valueRep.getPayload()
                );
                console.log(`    Value: ${JSON.stringify(value.value)} (${value.type})`);
            } else {
                console.log(`    Non-inlined value (would need data section)`);
            }
        } catch (error) {
            console.log(`    Error reading value: ${error.message}`);
        }
        
        console.log('');
    }
}

function createMiniUsdc() {
    console.log('=== Creating Mini USDC for Testing ===\n');
    
    // This creates a minimal USDC file structure for testing
    const headerSize = 24;
    const tocSize = 32; // Just number of sections
    const totalSize = headerSize + tocSize;
    
    const buffer = new ArrayBuffer(totalSize);
    const view = new DataView(buffer);
    const uint8View = new Uint8Array(buffer);
    
    // Write header
    const magic = new TextEncoder().encode('PXR-USDC');
    uint8View.set(magic, 0);
    
    // Version
    uint8View[8] = 0;  // major
    uint8View[9] = 7;  // minor  
    uint8View[10] = 1; // patch
    
    // TOC offset
    view.setBigUint64(16, BigInt(headerSize), true);
    
    // Write minimal TOC (0 sections)
    view.setBigUint64(headerSize, 0n, true);
    
    return new Uint8Array(buffer);
}

function testMiniUsdc() {
    console.log('Testing mini USDC file...');
    
    const miniUsdc = createMiniUsdc();
    const parser = new UsdcParser(miniUsdc);
    
    try {
        const layer = parser.parse();
        if (layer) {
            console.log('✓ Mini USDC parsed successfully');
            analyzeUsdcLayer(parser, layer);
        } else {
            console.log('✗ Mini USDC parse failed');
        }
    } catch (error) {
        console.log(`✗ Mini USDC error: ${error.message}`);
    }
    
    console.log('');
}

// Main execution
function main() {
    console.log('USDC JavaScript Parser Example\n');
    console.log('==============================\n');
    
    // Test with mini USDC first
    testMiniUsdc();
    
    // Try to find and parse real USDC files
    const testFiles = [
        '../../models/simple.usdc',
        '../../models/cube.usdc', 
        '../../models/sphere.usdc',
        '../test.usdc',
        './test.usdc'
    ];
    
    let parsedAny = false;
    
    for (const filename of testFiles) {
        try {
            if (fs.existsSync(filename)) {
                console.log(`Found USDC file: ${filename}`);
                const result = parseUsdcFile(filename);
                
                if (result) {
                    analyzeUsdcLayer(result.parser, result.layer);
                    demonstrateValueReading(result.parser, result.layer);
                    parsedAny = true;
                    break; // Parse just one file for demo
                }
            }
        } catch (error) {
            // Continue to next file
        }
    }
    
    if (!parsedAny) {
        console.log('No USDC files found for demonstration.');
        console.log('To test with real files, place a .usdc file in the models/ directory.');
    }
}

if (require.main === module) {
    main();
}

module.exports = { parseUsdcFile, analyzeUsdcLayer };