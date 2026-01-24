#include <vector>
#include <cstdint>
#include <algorithm>
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>

class MemoryPool {
private:
    std::vector<uint8_t> pool_memory;
    struct Block {
        size_t offset;
        size_t size;
        bool is_free;
        
        Block(size_t off, size_t sz, bool free) : offset(off), size(sz), is_free(free) {}
    };
    std::vector<Block> blocks;
    
public:
    size_t create_pool(size_t mb_size) {
        const size_t size = mb_size * 1024 * 1024;
        pool_memory.resize(size);
        blocks.clear();
        blocks.emplace_back(0, size, true);
        return pool_memory.size();
    }
    
    int allocate_from_pool(size_t mb_size) {
        const size_t size = mb_size * 1024 * 1024;
        
        // Find first free block that fits
        for (size_t i = 0; i < blocks.size(); ++i) {
            Block& block = blocks[i];
            if (block.is_free && block.size >= size) {
                // Mark as used
                block.is_free = false;
                
                // If block is larger, split it
                if (block.size > size) {
                    Block new_free_block(block.offset + size, block.size - size, true);
                    blocks.insert(blocks.begin() + i + 1, new_free_block);
                    block.size = size;
                }
                
                // Fill with pattern for verification
                std::fill(pool_memory.begin() + block.offset, 
                         pool_memory.begin() + block.offset + size, 
                         static_cast<uint8_t>(0xAA));
                
                return static_cast<int>(i);
            }
        }
        return -1; // No suitable block found
    }
    
    bool free_block(int block_id) {
        if (block_id < 0 || block_id >= static_cast<int>(blocks.size())) {
            return false;
        }
        
        Block& block = blocks[block_id];
        if (block.is_free) {
            return false; // Already free
        }
        
        block.is_free = true;
        
        // Clear memory for verification
        std::fill(pool_memory.begin() + block.offset, 
                 pool_memory.begin() + block.offset + block.size, 
                 static_cast<uint8_t>(0x00));
        
        // Merge with adjacent free blocks
        merge_free_blocks();
        
        return true;
    }
    
    void merge_free_blocks() {
        // Sort blocks by offset
        std::sort(blocks.begin(), blocks.end(), 
                 [](const Block& a, const Block& b) { return a.offset < b.offset; });
        
        // Merge adjacent free blocks
        for (size_t i = 0; i < blocks.size() - 1; ) {
            Block& current = blocks[i];
            Block& next = blocks[i + 1];
            
            if (current.is_free && next.is_free && 
                current.offset + current.size == next.offset) {
                current.size += next.size;
                blocks.erase(blocks.begin() + i + 1);
            } else {
                ++i;
            }
        }
    }
    
    size_t get_pool_size() const {
        return pool_memory.size();
    }
    
    size_t get_total_allocated() const {
        size_t total = 0;
        for (const auto& block : blocks) {
            if (!block.is_free) {
                total += block.size;
            }
        }
        return total;
    }
    
    size_t get_total_free() const {
        size_t total = 0;
        for (const auto& block : blocks) {
            if (block.is_free) {
                total += block.size;
            }
        }
        return total;
    }
    
    size_t get_block_count() const {
        return blocks.size();
    }
    
    size_t get_largest_free_block() const {
        size_t largest = 0;
        for (const auto& block : blocks) {
            if (block.is_free && block.size > largest) {
                largest = block.size;
            }
        }
        return largest;
    }
    
    bool is_block_allocated(int block_id) const {
        if (block_id < 0 || block_id >= static_cast<int>(blocks.size())) {
            return false;
        }
        return !blocks[block_id].is_free;
    }
    
    size_t get_block_size(int block_id) const {
        if (block_id < 0 || block_id >= static_cast<int>(blocks.size())) {
            return 0;
        }
        return blocks[block_id].size;
    }
    
    void clear_pool() {
        pool_memory.clear();
        blocks.clear();
    }
};

EMSCRIPTEN_BINDINGS(memory_pool) {
    emscripten::class_<MemoryPool>("MemoryPool")
        .constructor<>()
        .function("create_pool", &MemoryPool::create_pool)
        .function("allocate_from_pool", &MemoryPool::allocate_from_pool)
        .function("free_block", &MemoryPool::free_block)
        .function("get_pool_size", &MemoryPool::get_pool_size)
        .function("get_total_allocated", &MemoryPool::get_total_allocated)
        .function("get_total_free", &MemoryPool::get_total_free)
        .function("get_block_count", &MemoryPool::get_block_count)
        .function("get_largest_free_block", &MemoryPool::get_largest_free_block)
        .function("is_block_allocated", &MemoryPool::is_block_allocated)
        .function("get_block_size", &MemoryPool::get_block_size)
        .function("clear_pool", &MemoryPool::clear_pool);
}