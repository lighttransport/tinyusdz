const MemoryTestModule = require('./memory_test.js');

function formatBytes(bytes) {
    return (bytes / 1024 / 1024).toFixed(2) + ' MB';
}

function printMemoryUsage(label) {
    const usage = process.memoryUsage();
    console.log(`\n=== ${label} ===`);
    console.log(`RSS (Resident Set Size): ${formatBytes(usage.rss)}`);
    console.log(`Heap Total: ${formatBytes(usage.heapTotal)}`);
    console.log(`Heap Used: ${formatBytes(usage.heapUsed)}`);
    console.log(`External: ${formatBytes(usage.external)}`);
}

function printAllocatorStatus(allocator, label) {
    console.log(`\n--- ${label} ---`);
    console.log(`Reserved: ${formatBytes(allocator.get_reserved_size())}`);
    console.log(`Chunks: ${allocator.get_chunk_count()}`);
    console.log(`Total allocated: ${formatBytes(allocator.get_total_allocated())}`);
    for (let i = 0; i < allocator.get_chunk_count(); i++) {
        console.log(`  Chunk ${i}: ${formatBytes(allocator.get_chunk_size(i))}`);
    }
}

async function runReserveTest() {
    console.log('Testing with 150MB reserve: reserve 150MB → 100MB → 20MB → free 100MB → 105MB');
    console.log('Loading WASM module...');
    const Module = await MemoryTestModule();
    
    printMemoryUsage('Initial Memory Usage');
    
    const allocator = new Module.MemoryAllocator();
    
    // Step 0: Reserve 150MB
    console.log('\n=== STEP 0: Reserve 150MB ===');
    const reserved = allocator.reserve_space(150);
    console.log(`Reserved: ${formatBytes(reserved)}`);
    printAllocatorStatus(allocator, 'After 150MB reserve');
    printMemoryUsage('Memory after 150MB reserve');
    
    // Step 1: Allocate 100MB
    console.log('\n=== STEP 1: Allocate 100MB ===');
    allocator.allocate_100mb();
    printAllocatorStatus(allocator, 'After 100MB allocation');
    printMemoryUsage('Memory after 100MB (within reserved space)');
    
    // Step 2: Allocate 20MB
    console.log('\n=== STEP 2: Allocate 20MB ===');
    allocator.allocate_20mb();
    printAllocatorStatus(allocator, 'After 20MB allocation (total: 120MB + 150MB reserve)');
    printMemoryUsage('Memory after 20MB (within reserved space)');
    
    // Step 3: Free first chunk (100MB)
    console.log('\n=== STEP 3: Free 100MB (chunk 0) ===');
    const released = allocator.release_chunk(0);
    console.log(`Release successful: ${released}`);
    printAllocatorStatus(allocator, 'After freeing 100MB chunk');
    printMemoryUsage('Memory after freeing 100MB');
    
    // Step 4: Allocate 105MB
    console.log('\n=== STEP 4: Allocate 105MB ===');
    allocator.allocate_105mb();
    printAllocatorStatus(allocator, 'After 105MB allocation');
    printMemoryUsage('Memory after 105MB (should fit in reserved space)');
    
    // Step 5: Clear reserve to see memory behavior
    console.log('\n=== STEP 5: Clear reserve space ===');
    allocator.clear_reserve();
    printAllocatorStatus(allocator, 'After clearing reserve');
    printMemoryUsage('Memory after clearing reserve');
    
    console.log('\n=== SUMMARY ===');
    console.log('Expected behavior: With 150MB pre-reserved, all allocations should fit');
    console.log('without additional heap growth, reducing fragmentation.');
    
    console.log('\nReserve test completed!');
}

runReserveTest().catch(console.error);