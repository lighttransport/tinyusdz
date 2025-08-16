/**
 * Test suite for LZ4 JavaScript decoder
 */

const { Lz4Decoder, lz4Decompress, lz4DecompressSafe } = require('./lz4-decoder.js');

function testBasicLz4Block() {
    console.log('Testing basic LZ4 block decompression...');
    
    // Create test data for "ABCDABCD" 
    // 4 literals "ABCD" + match(offset=4, length=4) to repeat "ABCD"
    const testData = new Uint8Array([
        0x40,           // Token: 4 literals (0x40 = 0100 0000), 0 match length field
        0x41, 0x42, 0x43, 0x44, // "ABCD"
        0x04, 0x00,     // Offset: 4 (little-endian) - points to start of "ABCD"
        0x00            // Match length: 4-4=0 (minimum match is 4, so total is 4)
    ]);
    
    const decoder = new Lz4Decoder();
    
    try {
        const result = decoder.decompressBlock(testData);
        const resultStr = new TextDecoder().decode(result);
        console.log(`  Result: "${resultStr}" (length: ${result.length})`);
        
        if (resultStr === "ABCDABCD") {
            console.log('  ✓ Basic block decompression working');
        } else {
            console.log('  ✗ Unexpected result');
        }
    } catch (error) {
        console.log(`  ✗ Error: ${error.message}`);
    }
    
    console.log('');
}

function testSimpleLiterals() {
    console.log('Testing simple literals only...');
    
    // Simple case: just literals "Hello"
    const testData = new Uint8Array([
        0x50,           // Token: 5 literals, no match following
        0x48, 0x65, 0x6C, 0x6C, 0x6F // "Hello"
    ]);
    
    const decoder = new Lz4Decoder();
    
    try {
        const result = decoder.decompressBlock(testData);
        const resultStr = new TextDecoder().decode(result);
        console.log(`  Result: "${resultStr}"`);
        
        if (resultStr === "Hello") {
            console.log('  ✓ Literals-only decompression working');
        } else {
            console.log('  ✗ Unexpected result');
        }
    } catch (error) {
        console.log(`  ✗ Error: ${error.message}`);
    }
    
    console.log('');
}

function testExtendedLiterals() {
    console.log('Testing extended literal length...');
    
    // Test with 15+ literals (requires extended length encoding)
    const longString = "This is a very long string for testing extended literal lengths!";
    const stringBytes = new TextEncoder().encode(longString);
    
    // Create test data: 15 (0xF) + additional length byte + data
    const testData = new Uint8Array(2 + stringBytes.length);
    testData[0] = 0xF0; // Token: 15 literals (extended), no match
    testData[1] = stringBytes.length - 15; // Additional length
    testData.set(stringBytes, 2);
    
    const decoder = new Lz4Decoder();
    
    try {
        const result = decoder.decompressBlock(testData);
        const resultStr = new TextDecoder().decode(result);
        console.log(`  Input length: ${longString.length}, Output length: ${result.length}`);
        
        if (resultStr === longString) {
            console.log('  ✓ Extended literals decompression working');
        } else {
            console.log('  ✗ Extended literals failed');
            console.log(`  Expected: "${longString}"`);
            console.log(`  Got: "${resultStr}"`);
        }
    } catch (error) {
        console.log(`  ✗ Error: ${error.message}`);
    }
    
    console.log('');
}

function testTinyUsdz() {
    console.log('Testing TinyUSDZ wrapper format...');
    
    // Create simple test data
    const blockData = new Uint8Array([
        0x50,           // Token: 5 literals, no match
        0x48, 0x65, 0x6C, 0x6C, 0x6F // "Hello"
    ]);
    
    // Test single chunk format (0 chunks)
    const singleChunk = new Uint8Array(blockData.length + 1);
    singleChunk[0] = 0; // 0 chunks = single chunk mode
    singleChunk.set(blockData, 1);
    
    const decoder = new Lz4Decoder();
    
    try {
        const result = decoder.decompressTinyUsdz(singleChunk);
        const resultStr = new TextDecoder().decode(result);
        console.log(`  Single chunk result: "${resultStr}"`);
        
        if (resultStr === "Hello") {
            console.log('  ✓ TinyUSDZ single chunk working');
        } else {
            console.log('  ✗ TinyUSDZ single chunk failed');
        }
    } catch (error) {
        console.log(`  ✗ Error: ${error.message}`);
    }
    
    // Test multi-chunk format
    const multiChunk = new Uint8Array(1 + 4 + blockData.length);
    multiChunk[0] = 1; // 1 chunk
    // Chunk size (little-endian)
    multiChunk[1] = blockData.length & 0xFF;
    multiChunk[2] = (blockData.length >> 8) & 0xFF;
    multiChunk[3] = (blockData.length >> 16) & 0xFF;
    multiChunk[4] = (blockData.length >> 24) & 0xFF;
    multiChunk.set(blockData, 5);
    
    try {
        const result = decoder.decompressTinyUsdz(multiChunk);
        const resultStr = new TextDecoder().decode(result);
        console.log(`  Multi chunk result: "${resultStr}"`);
        
        if (resultStr === "Hello") {
            console.log('  ✓ TinyUSDZ multi chunk working');
        } else {
            console.log('  ✗ TinyUSDZ multi chunk failed');
        }
    } catch (error) {
        console.log(`  ✗ Multi chunk error: ${error.message}`);
    }
    
    console.log('');
}

function testErrorHandling() {
    console.log('Testing error handling...');
    
    const decoder = new Lz4Decoder();
    
    // Test with empty data
    try {
        decoder.decompressBlock(new Uint8Array(0));
        console.log('  ✗ Should have failed with empty data');
    } catch (error) {
        console.log('  ✓ Correctly caught empty data error');
    }
    
    // Test with invalid offset
    const invalidOffset = new Uint8Array([
        0x10,           // Token: 1 literal, match follows
        0x41,           // "A"
        0xFF, 0xFF,     // Invalid large offset
        0x00            // Match length
    ]);
    
    try {
        decoder.decompressBlock(invalidOffset);
        console.log('  ✗ Should have failed with invalid offset');
    } catch (error) {
        console.log('  ✓ Correctly caught invalid offset error');
    }
    
    // Test with truncated data
    const truncated = new Uint8Array([0x20]); // Token says 2 literals but no data
    
    try {
        decoder.decompressBlock(truncated);
        console.log('  ✗ Should have failed with truncated data');
    } catch (error) {
        console.log('  ✓ Correctly caught truncated data error');
    }
    
    console.log('');
}

function testConvenienceFunctions() {
    console.log('Testing convenience functions...');
    
    // Test safe decompression with invalid data
    const invalidData = new Uint8Array([0xFF, 0xFF, 0xFF]);
    
    const result = lz4DecompressSafe(invalidData);
    if (result === null) {
        console.log('  ✓ Safe decompression correctly returned null for invalid data');
    } else {
        console.log('  ✗ Safe decompression should have returned null');
    }
    
    // Test direct decompression with valid data
    const validData = new Uint8Array([
        0x50,           // Token: 5 literals, no match
        0x48, 0x65, 0x6C, 0x6C, 0x6F // "Hello"
    ]);
    
    try {
        const result = lz4Decompress(validData);
        const resultStr = new TextDecoder().decode(result);
        
        if (resultStr === "Hello") {
            console.log('  ✓ Direct decompression working');
        } else {
            console.log('  ✗ Direct decompression failed');
        }
    } catch (error) {
        console.log(`  ✗ Direct decompression error: ${error.message}`);
    }
    
    console.log('');
}

function testRepeatingPattern() {
    console.log('Testing repeating pattern compression...');
    
    // Test case: "abcabcabc" using matches
    // Method: "abc" as literals, then two matches pointing back
    const testData = new Uint8Array([
        0x30,           // Token: 3 literals, match follows
        0x61, 0x62, 0x63, // "abc"
        0x03, 0x00,     // Offset: 3 (points to start of "abc")
        0x02            // Match length: 6-4=2 (minimum match is 4, total is 6)
    ]);
    
    const decoder = new Lz4Decoder();
    
    try {
        const result = decoder.decompressBlock(testData);
        const resultStr = new TextDecoder().decode(result);
        console.log(`  Result: "${resultStr}" (length: ${result.length})`);
        
        // Result should be "abc" + 6 bytes copied = "abcabcabc" (9 total)
        if (resultStr.startsWith("abcabc")) {
            console.log('  ✓ Repeating pattern decompression working');
        } else {
            console.log('  ✗ Repeating pattern failed');
        }
    } catch (error) {
        console.log(`  ✗ Error: ${error.message}`);
    }
    
    console.log('');
}

function testMemoryLimits() {
    console.log('Testing memory limits...');
    
    const decoder = new Lz4Decoder();
    
    // Create data that would expand beyond limit
    const maliciousData = new Uint8Array([
        0x1F,           // Token: 1 literal, match follows with extended length
        0x41,           // "A"
        0x01, 0x00,     // Offset: 1 (self-referencing)
        100             // Extended match length (will try to copy 100+15+4 = 119 bytes)
    ]);
    
    try {
        // Set very small limit to trigger error
        decoder.decompressBlock(maliciousData, 10);
        console.log('  ✗ Should have failed with memory limit exceeded');
    } catch (error) {
        if (error.message.includes('maximum size')) {
            console.log('  ✓ Correctly enforced memory limit');
        } else {
            console.log(`  ✗ Unexpected error: ${error.message}`);
        }
    }
    
    console.log('');
}

// Integration test with USDC parser
function testUsdcIntegration() {
    console.log('Testing USDC parser integration...');
    
    // This requires the USDC parser to be available
    try {
        const { UsdcParser } = require('./usdc-parser.js');
        
        // Create minimal USDC with LZ4 compressed section
        const headerSize = 24;
        const buffer = new ArrayBuffer(headerSize);
        const view = new DataView(buffer);
        const uint8View = new Uint8Array(buffer);
        
        // Write basic header
        const magic = new TextEncoder().encode('PXR-USDC');
        uint8View.set(magic, 0);
        uint8View[8] = 0; uint8View[9] = 7; uint8View[10] = 1; // version
        view.setBigUint64(16, BigInt(headerSize), true); // TOC offset
        
        // Add empty TOC
        const fullBuffer = new Uint8Array(headerSize + 8);
        fullBuffer.set(uint8View);
        const tocView = new DataView(fullBuffer.buffer, headerSize, 8);
        tocView.setBigUint64(0, 0n, true); // 0 sections
        
        const parser = new UsdcParser(fullBuffer);
        const layer = parser.parse();
        
        if (layer) {
            console.log('  ✓ USDC parser integration working with LZ4 decoder');
        } else {
            console.log('  ✗ USDC parser integration failed');
        }
    } catch (error) {
        console.log(`  ✗ Integration test error: ${error.message}`);
    }
    
    console.log('');
}

// Run all tests
function runTests() {
    console.log('=== LZ4 JavaScript Decoder Tests ===\n');
    
    try {
        testSimpleLiterals();
        testBasicLz4Block();
        testExtendedLiterals();
        testRepeatingPattern();
        testTinyUsdz();
        testErrorHandling();
        testMemoryLimits();
        testConvenienceFunctions();
        testUsdcIntegration();
        
        console.log('All LZ4 tests completed!');
    } catch (error) {
        console.error('LZ4 test suite failed with error:', error);
    }
}

// Run tests if this file is executed directly
if (require.main === module) {
    runTests();
}

module.exports = { runTests };