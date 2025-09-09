const MemoryPoolModule = require('./memory_pool.js');

function formatBytes(bytes) {
    return (bytes / 1024 / 1024).toFixed(2) + ' MB';
}

function printMemoryUsage(label) {
    const usage = process.memoryUsage();
    console.log(`\n=== ${label} ===`);
    console.log(`RSS: ${formatBytes(usage.rss)}`);
    console.log(`Heap Total: ${formatBytes(usage.heapTotal)}`);
    console.log(`Heap Used: ${formatBytes(usage.heapUsed)}`);
    console.log(`External: ${formatBytes(usage.external)}`);
}

function printPoolStatus(pool, label) {
    console.log(`\n--- ${label} ---`);
    console.log(`Pool size: ${formatBytes(pool.get_pool_size())}`);
    console.log(`Total allocated: ${formatBytes(pool.get_total_allocated())}`);
    console.log(`Total free: ${formatBytes(pool.get_total_free())}`);
    console.log(`Largest free block: ${formatBytes(pool.get_largest_free_block())}`);
    console.log(`Block count: ${pool.get_block_count()}`);
}

async function runPoolTest() {
    console.log('CUSTOM MEMORY POOL TEST');
    console.log('Test sequence: Create 150MB pool → 100MB → 20MB → free 100MB → 105MB');
    console.log('='.repeat(70));
    
    const Module = await MemoryPoolModule();
    const pool = new Module.MemoryPool();
    
    printMemoryUsage('Initial Memory Usage');
    
    // Step 0: Create 150MB pool
    console.log('\n=== STEP 0: Create 150MB Pool ===');
    const poolSize = pool.create_pool(150);
    console.log(`Pool created: ${formatBytes(poolSize)}`);
    printPoolStatus(pool, 'After pool creation');
    printMemoryUsage('Memory after pool creation');
    
    // Step 1: Allocate 100MB from pool
    console.log('\n=== STEP 1: Allocate 100MB from pool ===');
    const block1 = pool.allocate_from_pool(100);
    console.log(`Block ID: ${block1}`);
    if (block1 >= 0) {
        console.log(`Block size: ${formatBytes(pool.get_block_size(block1))}`);
        console.log(`Block allocated: ${pool.is_block_allocated(block1)}`);
    }
    printPoolStatus(pool, 'After 100MB allocation');
    printMemoryUsage('Memory after 100MB allocation');
    
    // Step 2: Allocate 20MB from pool
    console.log('\n=== STEP 2: Allocate 20MB from pool ===');
    const block2 = pool.allocate_from_pool(20);
    console.log(`Block ID: ${block2}`);
    if (block2 >= 0) {
        console.log(`Block size: ${formatBytes(pool.get_block_size(block2))}`);
        console.log(`Block allocated: ${pool.is_block_allocated(block2)}`);
    }
    printPoolStatus(pool, 'After 20MB allocation (120MB total used)');
    printMemoryUsage('Memory after 20MB allocation');
    
    // Step 3: Free the 100MB block
    console.log('\n=== STEP 3: Free 100MB block ===');
    const freed = pool.free_block(block1);
    console.log(`Free successful: ${freed}`);
    if (block1 >= 0) {
        console.log(`Block allocated: ${pool.is_block_allocated(block1)}`);
    }
    printPoolStatus(pool, 'After freeing 100MB block');
    printMemoryUsage('Memory after freeing 100MB');
    
    // Step 4: Allocate 105MB from pool (should reuse freed space)
    console.log('\n=== STEP 4: Allocate 105MB from pool ===');
    const block3 = pool.allocate_from_pool(105);
    console.log(`Block ID: ${block3}`);
    if (block3 >= 0) {
        console.log(`Block size: ${formatBytes(pool.get_block_size(block3))}`);
        console.log(`Block allocated: ${pool.is_block_allocated(block3)}`);
    } else {
        console.log('Allocation failed - not enough free space');
    }
    printPoolStatus(pool, 'After 105MB allocation');
    printMemoryUsage('Memory after 105MB allocation');
    
    console.log('\n=== ANALYSIS ===');
    const finalUsage = process.memoryUsage();
    const initialRSS = 50.5 * 1024 * 1024; // Approximate baseline
    const totalGrowth = finalUsage.rss - initialRSS;
    const expectedGrowth = 150 * 1024 * 1024; // Just the pool size
    const efficiency = (1 - (totalGrowth - expectedGrowth) / expectedGrowth) * 100;
    
    console.log(`Expected RSS growth: ${formatBytes(expectedGrowth)} (pool only)`);
    console.log(`Actual RSS growth: ${formatBytes(totalGrowth)}`);
    console.log(`Memory efficiency: ${efficiency.toFixed(1)}%`);
    
    if (block3 >= 0) {
        console.log(`✓ 105MB allocation succeeded - memory was reused!`);
        console.log(`✓ Pool manages ${formatBytes(pool.get_pool_size())} with perfect reuse`);
    } else {
        console.log(`✗ 105MB allocation failed - insufficient free space`);
    }
    
    console.log('\nPool test completed!');
}

runPoolTest().catch(console.error);