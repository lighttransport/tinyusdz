#include <vector>
#include <cstdint>
#include <algorithm>
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>

class MemoryAllocator {
private:
    std::vector<std::vector<uint8_t>> allocated_chunks;
    std::vector<uint8_t> reserved_space;

public:
    size_t allocate_100mb() {
        const size_t size = 100 * 1024 * 1024; // 100MB
        allocated_chunks.emplace_back(size, 0);
        return allocated_chunks.size();
    }
    
    size_t allocate_105mb() {
        const size_t size = 105 * 1024 * 1024; // 105MB
        allocated_chunks.emplace_back(size, 0);
        return allocated_chunks.size();
    }
    
    size_t allocate_20mb() {
        const size_t size = 20 * 1024 * 1024; // 20MB
        allocated_chunks.emplace_back(size, 0);
        return allocated_chunks.size();
    }
    
    size_t reserve_space(size_t mb_size) {
        const size_t size = mb_size * 1024 * 1024;
        reserved_space.reserve(size);
        reserved_space.resize(size, 0);
        return reserved_space.size();
    }
    
    void clear_reserve() {
        std::vector<uint8_t> empty_vector;
        reserved_space.swap(empty_vector);
    }
    
    size_t get_reserved_size() const {
        return reserved_space.size();
    }
    
    void clear_all() {
        allocated_chunks.clear();
        clear_reserve();
    }
    
    bool release_chunk(size_t index) {
        if (index >= allocated_chunks.size()) {
            return false;
        }
        std::vector<uint8_t> empty_vector;
        allocated_chunks[index].swap(empty_vector);
        return true;
    }
    
    void compact_chunks() {
        allocated_chunks.erase(
            std::remove_if(allocated_chunks.begin(), allocated_chunks.end(),
                          [](const std::vector<uint8_t>& chunk) { return chunk.empty(); }),
            allocated_chunks.end());
    }
    
    size_t get_total_allocated() const {
        size_t total = 0;
        for (const auto& chunk : allocated_chunks) {
            total += chunk.size();
        }
        return total;
    }
    
    size_t get_chunk_count() const {
        return allocated_chunks.size();
    }
    
    size_t get_chunk_size(size_t index) const {
        if (index >= allocated_chunks.size()) {
            return 0;
        }
        return allocated_chunks[index].size();
    }
};

EMSCRIPTEN_BINDINGS(memory_test) {
    emscripten::class_<MemoryAllocator>("MemoryAllocator")
        .constructor<>()
        .function("allocate_100mb", &MemoryAllocator::allocate_100mb)
        .function("allocate_105mb", &MemoryAllocator::allocate_105mb)
        .function("allocate_20mb", &MemoryAllocator::allocate_20mb)
        .function("reserve_space", &MemoryAllocator::reserve_space)
        .function("clear_reserve", &MemoryAllocator::clear_reserve)
        .function("get_reserved_size", &MemoryAllocator::get_reserved_size)
        .function("clear_all", &MemoryAllocator::clear_all)
        .function("release_chunk", &MemoryAllocator::release_chunk)
        .function("compact_chunks", &MemoryAllocator::compact_chunks)
        .function("get_total_allocated", &MemoryAllocator::get_total_allocated)
        .function("get_chunk_count", &MemoryAllocator::get_chunk_count)
        .function("get_chunk_size", &MemoryAllocator::get_chunk_size);
}