/**
 * Test suite for USDC JavaScript parser
 */

const { UsdcParser, UsdcHeader } = require('./usdc-parser.js');
const { BinaryReader } = require('./binary-reader.js');
const fs = require('fs');

function createMockUsdcHeader() {
    // Create a minimal USDC header for testing
    const buffer = new ArrayBuffer(24);
    const view = new DataView(buffer);
    const uint8View = new Uint8Array(buffer);
    
    // Write magic "PXR-USDC"
    const magic = new TextEncoder().encode('PXR-USDC');
    uint8View.set(magic, 0);
    
    // Write version (3 bytes used: major=0, minor=7, patch=1)
    uint8View[8] = 0;   // major
    uint8View[9] = 7;   // minor
    uint8View[10] = 1;  // patch
    // Rest of version bytes are zero
    
    // Write TOC offset (points to end of header for now)
    view.setBigUint64(16, 24n, true); // little-endian
    
    return new Uint8Array(buffer);
}

function createMockTableOfContents() {
    const buffer = new ArrayBuffer(1024);
    const view = new DataView(buffer);
    let offset = 0;
    
    // Number of sections
    view.setBigUint64(offset, 3n, true); // 3 sections
    offset += 8;
    
    // Section 1: TOKENS
    const tokensName = new TextEncoder().encode('TOKENS');
    const tokensNamePadded = new Uint8Array(16);
    tokensNamePadded.set(tokensName);
    new Uint8Array(buffer).set(tokensNamePadded, offset);
    offset += 16;
    view.setBigUint64(offset, 1024n, true); // start
    offset += 8;
    view.setBigUint64(offset, 256n, true);  // size
    offset += 8;
    
    // Section 2: STRINGS  
    const stringsName = new TextEncoder().encode('STRINGS');
    const stringsNamePadded = new Uint8Array(16);
    stringsNamePadded.set(stringsName);
    new Uint8Array(buffer).set(stringsNamePadded, offset);
    offset += 16;
    view.setBigUint64(offset, 1280n, true); // start
    offset += 8;
    view.setBigUint64(offset, 128n, true);  // size
    offset += 8;
    
    // Section 3: FIELDS
    const fieldsName = new TextEncoder().encode('FIELDS');
    const fieldsNamePadded = new Uint8Array(16);
    fieldsNamePadded.set(fieldsName);
    new Uint8Array(buffer).set(fieldsNamePadded, offset);
    offset += 16;
    view.setBigUint64(offset, 1408n, true); // start
    offset += 8;
    view.setBigUint64(offset, 64n, true);   // size
    offset += 8;
    
    return new Uint8Array(buffer, 0, offset);
}

function testBinaryReader() {
    console.log('Testing BinaryReader...');
    
    // Create test data
    const buffer = new ArrayBuffer(32);
    const view = new DataView(buffer);
    
    // Write test values
    view.setUint8(0, 0x42);
    view.setUint16(1, 0x1234, true);
    view.setUint32(3, 0x12345678, true);
    view.setBigUint64(7, 0x123456789ABCDEFn, true);
    view.setFloat32(15, 3.14159, true);
    view.setFloat64(19, 2.718281828, true);
    
    const reader = new BinaryReader(buffer);
    
    // Test reads
    console.log(`  uint8: ${reader.readUint8()} (expected: 66)`);
    console.log(`  uint16: ${reader.readUint16()} (expected: 4660)`);
    console.log(`  uint32: ${reader.readUint32()} (expected: 305419896)`);
    console.log(`  uint64: ${reader.readUint64()} (expected: 1311768467463790319n)`);
    console.log(`  float32: ${reader.readFloat32().toFixed(5)} (expected: 3.14159)`);
    console.log(`  float64: ${reader.readFloat64().toFixed(9)} (expected: 2.718281828)`);
    
    console.log('BinaryReader test completed\n');
}

function testUsdcHeader() {
    console.log('Testing USDC Header parsing...');
    
    const headerData = createMockUsdcHeader();
    const parser = new UsdcParser(headerData);
    
    try {
        const header = parser.parseHeader();
        
        console.log(`  Magic: "${header.magic}" (expected: "PXR-USDC")`);
        console.log(`  Version: [${Array.from(header.version).slice(0, 3).join(', ')}] (expected: [0, 7, 1])`);
        console.log(`  TOC Offset: ${header.tocOffset} (expected: 24n)`);
        
        if (header.magic === 'PXR-USDC' && header.tocOffset === 24n) {
            console.log('  ✓ Header parsing successful');
        } else {
            console.log('  ✗ Header parsing failed');
        }
    } catch (error) {
        console.log(`  ✗ Header parsing error: ${error.message}`);
    }
    
    console.log('');
}

function testTableOfContents() {
    console.log('Testing Table of Contents parsing...');
    
    // Create a complete mock USDC file
    const headerData = createMockUsdcHeader();
    const tocData = createMockTableOfContents();
    
    // Combine header and TOC
    const totalSize = headerData.length + tocData.length;
    const fullBuffer = new Uint8Array(totalSize);
    fullBuffer.set(headerData, 0);
    fullBuffer.set(tocData, headerData.length);
    
    const parser = new UsdcParser(fullBuffer);
    
    try {
        parser.parseHeader();
        const toc = parser.parseTableOfContents();
        
        console.log(`  Number of sections: ${toc.sections.length} (expected: 3)`);
        
        for (let i = 0; i < toc.sections.length; i++) {
            const section = toc.sections[i];
            console.log(`  Section ${i}: ${section.name} (start: ${section.start}, size: ${section.size})`);
        }
        
        // Test section lookup
        const tokensSection = toc.getSection('TOKENS');
        if (tokensSection && tokensSection.name === 'TOKENS') {
            console.log('  ✓ Section lookup successful');
        } else {
            console.log('  ✗ Section lookup failed');
        }
        
    } catch (error) {
        console.log(`  ✗ TOC parsing error: ${error.message}`);
    }
    
    console.log('');
}

function testInvalidHeader() {
    console.log('Testing invalid USDC header...');
    
    // Create invalid header with wrong magic
    const buffer = new ArrayBuffer(24);
    const uint8View = new Uint8Array(buffer);
    
    // Write wrong magic
    const wrongMagic = new TextEncoder().encode('INVALID!');
    uint8View.set(wrongMagic, 0);
    
    const parser = new UsdcParser(buffer);
    
    try {
        parser.parseHeader();
        console.log('  ✗ Should have failed with invalid magic');
    } catch (error) {
        console.log(`  ✓ Correctly caught error: ${error.message}`);
    }
    
    console.log('');
}

function testMemoryLimits() {
    console.log('Testing memory limits...');
    
    const buffer = new ArrayBuffer(24);
    const parser = new UsdcParser(buffer);
    
    try {
        // Test memory limit check
        parser.checkMemoryLimit(1024 * 1024); // 1MB should be OK
        console.log('  ✓ Normal memory allocation accepted');
        
        // This should fail
        parser.checkMemoryLimit(3 * 1024 * 1024 * 1024); // 3GB should fail
        console.log('  ✗ Should have failed with memory limit exceeded');
    } catch (error) {
        console.log(`  ✓ Correctly caught memory limit error: ${error.message}`);
    }
    
    console.log('');
}

function testValueRepresentation() {
    console.log('Testing Value Representation...');
    
    const { UsdcValueRep, UsdcDataType } = require('./usdc-parser.js');
    
    // Create test value representation
    // Format: [type_id:8][flags:8][payload:48]
    const typeId = UsdcDataType.FLOAT;
    const payload = 0x3F800000; // 1.0 as IEEE 754 float
    
    const valueRep = new UsdcValueRep(
        (BigInt(typeId) << 48n) | BigInt(payload)
    );
    
    console.log(`  Type ID: ${valueRep.getTypeId()} (expected: ${typeId})`);
    console.log(`  Payload: 0x${valueRep.getPayload().toString(16)} (expected: 0x${payload.toString(16)})`);
    console.log(`  Is Array: ${valueRep.isArray()} (expected: false)`);
    console.log(`  Is Inlined: ${valueRep.isInlined()} (expected: false)`);
    console.log(`  Is Compressed: ${valueRep.isCompressed()} (expected: false)`);
    
    // Test with flags
    const arrayValueRep = new UsdcValueRep(
        (BigInt(typeId) << 48n) | (1n << 63n) | BigInt(payload) // set array bit
    );
    
    console.log(`  Array Value Is Array: ${arrayValueRep.isArray()} (expected: true)`);
    
    console.log('');
}

function testErrorHandling() {
    console.log('Testing error handling...');
    
    const parser = new UsdcParser(new ArrayBuffer(8)); // Too small
    
    try {
        parser.parseHeader(); // Should fail
        console.log('  ✗ Should have failed with insufficient data');
    } catch (error) {
        console.log(`  ✓ Correctly caught error: ${error.message}`);
        console.log(`  Error position: ${error.position}`);
    }
    
    console.log('');
}

function testFileReading() {
    console.log('Testing file reading (if USDC files available)...');
    
    // Look for test USDC files
    const testFiles = [
        '../../models/simple.usdc',
        '../test.usdc',
        './test.usdc'
    ];
    
    let foundFile = null;
    for (const file of testFiles) {
        try {
            if (fs.existsSync(file)) {
                foundFile = file;
                break;
            }
        } catch (error) {
            // Ignore file system errors
        }
    }
    
    if (foundFile) {
        try {
            console.log(`  Reading USDC file: ${foundFile}`);
            const fileData = fs.readFileSync(foundFile);
            const parser = new UsdcParser(fileData);
            
            const layer = parser.parse();
            if (layer) {
                console.log(`  ✓ Successfully parsed USDC file`);
                console.log(`  Tokens: ${layer.tokens.length}`);
                console.log(`  Strings: ${layer.strings.length}`);
                console.log(`  Fields: ${layer.fields.length}`);
                console.log(`  Specs: ${layer.specs.length}`);
                console.log(`  Paths: ${layer.paths.length}`);
            } else {
                console.log(`  ✗ Failed to parse USDC file`);
                parser.getErrors().forEach(err => console.log(`    Error: ${err.message}`));
            }
        } catch (error) {
            console.log(`  ✗ Error reading file: ${error.message}`);
        }
    } else {
        console.log('  No USDC test files found - skipping file test');
    }
    
    console.log('');
}

// Run all tests
function runTests() {
    console.log('=== USDC JavaScript Parser Tests ===\n');
    
    try {
        testBinaryReader();
        testUsdcHeader();
        testTableOfContents();
        testInvalidHeader();
        testMemoryLimits();
        testValueRepresentation();
        testErrorHandling();
        testFileReading();
        
        console.log('All tests completed!');
    } catch (error) {
        console.error('Test suite failed with error:', error);
    }
}

// Run tests if this file is executed directly
if (require.main === module) {
    runTests();
}

module.exports = { runTests };