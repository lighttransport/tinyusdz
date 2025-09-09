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
    console.log(`Chunks: ${allocator.get_chunk_count()}`);
    console.log(`Total allocated: ${formatBytes(allocator.get_total_allocated())}`);
    for (let i = 0; i < allocator.get_chunk_count(); i++) {
        console.log(`  Chunk ${i}: ${formatBytes(allocator.get_chunk_size(i))}`);
    }
}

async function runSwapTest() {
    console.log('Loading WASM module...');
    const Module = await MemoryTestModule();
    
    printMemoryUsage('Initial Memory Usage');
    
    console.log('\nCreating MemoryAllocator instance...');
    const allocator = new Module.MemoryAllocator();
    
    printMemoryUsage('After Creating Allocator');
    
    // Step 1: Allocate 100MB
    console.log('\n=== STEP 1: Allocating 100MB ===');
    allocator.allocate_100mb();
    printAllocatorStatus(allocator, 'After 100MB Allocation');
    printMemoryUsage('Memory After 100MB Allocation');
    
    // Step 2: Release first chunk using swap
    console.log('\n=== STEP 2: Releasing first chunk (100MB) using swap ===');
    const released = allocator.release_chunk(0);
    console.log(`Release successful: ${released}`);
    printAllocatorStatus(allocator, 'After Swap Release (before compact)');
    printMemoryUsage('Memory After Swap Release');
    
    // Step 3: Compact to remove empty chunks
    console.log('\n=== STEP 3: Compacting chunks ===');
    allocator.compact_chunks();
    printAllocatorStatus(allocator, 'After Compacting');
    printMemoryUsage('Memory After Compacting');
    
    // Step 4: Allocate 105MB
    console.log('\n=== STEP 4: Allocating 105MB ===');
    allocator.allocate_105mb();
    printAllocatorStatus(allocator, 'After 105MB Allocation');
    printMemoryUsage('Memory After 105MB Allocation');
    
    // Step 5: Final cleanup
    console.log('\n=== STEP 5: Final cleanup ===');
    allocator.clear_all();
    printAllocatorStatus(allocator, 'After Clear All');
    printMemoryUsage('Final Memory State');
    
    console.log('\nSwap test completed!');
}

runSwapTest().catch(console.error);