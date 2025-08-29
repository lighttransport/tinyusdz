// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment Inc.
//
// Chunked data reader implementation

#include "chunk-reader.hh"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace tinyusdz {

ChunkReader::ChunkReader(size_t total_size, const Config& config)
    : config_(config), total_size_(total_size), current_offset_(0), current_cache_size_(0) {
  // Validate configuration
  if (config_.chunk_size == 0) {
    config_.chunk_size = 1024 * 1024; // 1MB default
  }
  
  if (config_.max_chunks == 0) {
    config_.max_chunks = 16;
  }
  
  if (config_.max_buffer_size == 0) {
    config_.max_buffer_size = 16 * 1024 * 1024; // 16MB default
  }
  
  // Ensure max_buffer_size is at least one chunk
  if (config_.max_buffer_size < config_.chunk_size) {
    config_.max_buffer_size = config_.chunk_size;
  }
  
  // Calculate actual max chunks based on buffer size
  size_t max_chunks_by_buffer = config_.max_buffer_size / config_.chunk_size;
  if (max_chunks_by_buffer < config_.max_chunks) {
    config_.max_chunks = max_chunks_by_buffer;
  }
}

ChunkReader::~ChunkReader() {
  // Cleanup is automatic with smart pointers
}

nonstd::expected<ChunkReader::ReadResult, std::string> ChunkReader::Read(size_t offset, size_t size, uint8_t* buffer) {
  ReadResult result;
  result.fully_cached = true;
  result.bytes_available = 0;
  
  // Error checking
  if (offset >= total_size_) {
    return nonstd::make_unexpected("Read offset " + std::to_string(offset) + 
                                   " exceeds EOF (" + std::to_string(total_size_) + ")");
  }
  
  // Check if read size exceeds maximum allowed
  size_t max_read_size = config_.chunk_size * config_.max_chunks;
  if (size > max_read_size) {
    return nonstd::make_unexpected("Read size " + std::to_string(size) + 
                                   " exceeds maximum allowed (" + std::to_string(max_read_size) + ")");
  }
  
  // Adjust size if it goes beyond EOF
  size_t actual_size = std::min(size, total_size_ - offset);
  
  // Calculate which chunks are needed
  size_t start_chunk = GetChunkId(offset);
  size_t end_offset = offset + actual_size - 1;
  size_t end_chunk = GetChunkId(end_offset);
  
  // Check which chunks are not in cache
  {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    for (size_t chunk_id = start_chunk; chunk_id <= end_chunk; ++chunk_id) {
      if (chunk_cache_.find(chunk_id) == chunk_cache_.end()) {
        result.required_chunks.push_back(chunk_id);
        result.fully_cached = false;
      }
    }
    
    // Calculate bytes available from cache
    if (!result.fully_cached) {
      // Find contiguous cached chunks from the start
      for (size_t chunk_id = start_chunk; chunk_id <= end_chunk; ++chunk_id) {
        if (chunk_cache_.find(chunk_id) != chunk_cache_.end()) {
          size_t chunk_start = chunk_id * config_.chunk_size;
          size_t chunk_end = std::min(chunk_start + config_.chunk_size, total_size_);
          
          if (chunk_id == start_chunk) {
            // First chunk - may be partial
            size_t offset_in_chunk = GetChunkOffset(offset);
            result.bytes_available += std::min(chunk_end - chunk_start - offset_in_chunk, actual_size);
          } else if (result.bytes_available > 0) {
            // Only count if contiguous with previous chunks
            size_t prev_end = offset + result.bytes_available;
            if (prev_end >= chunk_start) {
              result.bytes_available += std::min(chunk_end - chunk_start, actual_size - result.bytes_available);
            } else {
              break; // Not contiguous
            }
          }
        } else {
          break; // Missing chunk breaks contiguity
        }
      }
    } else {
      result.bytes_available = actual_size;
    }
  }
  
  // If buffer is provided and all data is cached, perform the read
  if (buffer && result.fully_cached) {
    auto read_result = ReadDirect(offset, size, buffer, false);
    if (!read_result) {
      return nonstd::make_unexpected(read_result.error());
    }
  }
  
  // Update statistics
  stats_.total_reads++;
  if (result.fully_cached) {
    stats_.cache_hits++;
  } else {
    stats_.cache_misses++;
  }
  
  return result;
}

nonstd::expected<size_t, std::string> ChunkReader::ReadDirect(size_t offset, size_t size, uint8_t* buffer, bool force_load) {
  if (!buffer) {
    return nonstd::make_unexpected("Buffer is null");
  }
  
  if (offset >= total_size_) {
    return nonstd::make_unexpected("Offset beyond end of stream");
  }
  
  // Adjust size if it goes beyond the stream
  size_t actual_size = std::min(size, total_size_ - offset);
  size_t bytes_read = 0;
  
  // Read data that may span multiple chunks
  while (bytes_read < actual_size) {
    size_t current_offset = offset + bytes_read;
    size_t chunk_id = GetChunkId(current_offset);
    size_t offset_in_chunk = GetChunkOffset(current_offset);
    
    // Get the chunk (from cache or load it if force_load is true)
    std::shared_ptr<Chunk> chunk;
    
    if (force_load) {
      auto chunk_result = GetChunk(chunk_id);
      if (!chunk_result) {
        return nonstd::make_unexpected(chunk_result.error());
      }
      chunk = chunk_result.value();
    } else {
      // Only use cached chunks
      std::lock_guard<std::mutex> lock(cache_mutex_);
      auto it = chunk_cache_.find(chunk_id);
      if (it == chunk_cache_.end()) {
        // Chunk not in cache and force_load is false
        break;
      }
      chunk = it->second.chunk;
    }
    
    // Calculate how much to read from this chunk
    size_t remaining_in_chunk = chunk->size - offset_in_chunk;
    size_t to_read = std::min(remaining_in_chunk, actual_size - bytes_read);
    
    // Copy data from chunk to buffer
    std::memcpy(buffer + bytes_read, chunk->data.data() + offset_in_chunk, to_read);
    bytes_read += to_read;
    
    // Update statistics
    stats_.bytes_read += to_read;
  }
  
  // Update current position
  current_offset_ = offset + bytes_read;
  
  // Prefetch next chunks if enabled
  if (config_.enable_prefetch && bytes_read > 0 && force_load) {
    Prefetch(current_offset_, config_.chunk_size * config_.prefetch_distance);
  }
  
  return bytes_read;
}

nonstd::expected<bool, std::string> ChunkReader::ReadByte(size_t offset, uint8_t* value) {
  if (!value) {
    return nonstd::make_unexpected("Value pointer is null");
  }
  
  auto result = ReadDirect(offset, 1, value);
  if (!result) {
    return nonstd::make_unexpected(result.error());
  }
  
  if (result.value() != 1) {
    return nonstd::make_unexpected("Failed to read byte");
  }
  
  return true;
}

nonstd::expected<bool, std::string> ChunkReader::Read2(size_t offset, uint16_t* value) {
  if (!value) {
    return nonstd::make_unexpected("Value pointer is null");
  }
  
  uint8_t buffer[2];
  auto result = ReadDirect(offset, 2, buffer);
  if (!result) {
    return nonstd::make_unexpected(result.error());
  }
  
  if (result.value() != 2) {
    return nonstd::make_unexpected("Failed to read 2 bytes");
  }
  
  // Assume little-endian for now (can be made configurable)
  *value = static_cast<uint16_t>(buffer[0]) | 
           (static_cast<uint16_t>(buffer[1]) << 8);
  
  return true;
}

nonstd::expected<bool, std::string> ChunkReader::Read4(size_t offset, uint32_t* value) {
  if (!value) {
    return nonstd::make_unexpected("Value pointer is null");
  }
  
  uint8_t buffer[4];
  auto result = ReadDirect(offset, 4, buffer);
  if (!result) {
    return nonstd::make_unexpected(result.error());
  }
  
  if (result.value() != 4) {
    return nonstd::make_unexpected("Failed to read 4 bytes");
  }
  
  // Assume little-endian for now
  *value = static_cast<uint32_t>(buffer[0]) |
           (static_cast<uint32_t>(buffer[1]) << 8) |
           (static_cast<uint32_t>(buffer[2]) << 16) |
           (static_cast<uint32_t>(buffer[3]) << 24);
  
  return true;
}

nonstd::expected<bool, std::string> ChunkReader::Read8(size_t offset, uint64_t* value) {
  if (!value) {
    return nonstd::make_unexpected("Value pointer is null");
  }
  
  uint8_t buffer[8];
  auto result = ReadDirect(offset, 8, buffer);
  if (!result) {
    return nonstd::make_unexpected(result.error());
  }
  
  if (result.value() != 8) {
    return nonstd::make_unexpected("Failed to read 8 bytes");
  }
  
  // Assume little-endian for now
  *value = static_cast<uint64_t>(buffer[0]) |
           (static_cast<uint64_t>(buffer[1]) << 8) |
           (static_cast<uint64_t>(buffer[2]) << 16) |
           (static_cast<uint64_t>(buffer[3]) << 24) |
           (static_cast<uint64_t>(buffer[4]) << 32) |
           (static_cast<uint64_t>(buffer[5]) << 40) |
           (static_cast<uint64_t>(buffer[6]) << 48) |
           (static_cast<uint64_t>(buffer[7]) << 56);
  
  return true;
}

bool ChunkReader::Seek(size_t offset) {
  if (offset > total_size_) {
    return false;
  }
  current_offset_ = offset;
  return true;
}

void ChunkReader::Prefetch(size_t offset, size_t size) {
  // Calculate which chunks to prefetch
  size_t start_chunk = GetChunkId(offset);
  size_t end_offset = std::min(offset + size, total_size_);
  size_t end_chunk = GetChunkId(end_offset);
  
  // Prefetch chunks asynchronously (could be improved with actual async loading)
  for (size_t chunk_id = start_chunk; chunk_id <= end_chunk; ++chunk_id) {
    // Try to load the chunk if not already cached
    if (!IsChunkCached(chunk_id)) {
      LoadChunk(chunk_id); // Ignore errors during prefetch
    }
  }
}

void ChunkReader::ClearCache() {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  chunk_cache_.clear();
  lru_list_.clear();
  current_cache_size_ = 0;
}

ChunkReader::Stats ChunkReader::GetStats() const {
  return stats_;
}

void ChunkReader::ResetStats() {
  stats_ = Stats();
}

bool ChunkReader::IsChunkCached(size_t chunk_id) const {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  return chunk_cache_.find(chunk_id) != chunk_cache_.end();
}

size_t ChunkReader::GetCacheSize() const {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  return current_cache_size_;
}

size_t ChunkReader::GetCachedChunkCount() const {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  return chunk_cache_.size();
}

nonstd::expected<std::shared_ptr<ChunkReader::Chunk>, std::string> 
ChunkReader::LoadChunk(size_t chunk_id) {
  // Calculate chunk parameters
  size_t chunk_offset = chunk_id * config_.chunk_size;
  if (chunk_offset >= total_size_) {
    return nonstd::make_unexpected("Chunk offset beyond stream size");
  }
  
  size_t chunk_size = std::min(config_.chunk_size, total_size_ - chunk_offset);
  
  // Create new chunk
  auto chunk = std::make_shared<Chunk>(chunk_id, chunk_offset, chunk_size);
  
  // Call the callback to load data
  if (!chunk_request_callback_) {
    return nonstd::make_unexpected("No chunk request callback set");
  }
  
  if (!chunk_request_callback_(chunk_id, chunk_offset, chunk_size, chunk->data.data())) {
    return nonstd::make_unexpected("Failed to load chunk from callback");
  }
  
  chunk->is_loaded = true;
  
  // Update statistics
  stats_.chunks_loaded++;
  
  return chunk;
}

nonstd::expected<std::shared_ptr<ChunkReader::Chunk>, std::string> 
ChunkReader::GetChunk(size_t chunk_id) {
  std::lock_guard<std::mutex> lock(cache_mutex_);
  
  // Check if chunk is in cache
  auto it = chunk_cache_.find(chunk_id);
  if (it != chunk_cache_.end()) {
    // Cache hit
    stats_.cache_hits++;
    
    // Update LRU order
    UpdateLRU(chunk_id);
    
    return it->second.chunk;
  }
  
  // Cache miss
  stats_.cache_misses++;
  
  // Need to load the chunk
  // First, release the lock to avoid blocking during I/O
  cache_mutex_.unlock();
  auto chunk_result = LoadChunk(chunk_id);
  cache_mutex_.lock();
  
  if (!chunk_result) {
    return chunk_result;
  }
  
  auto chunk = chunk_result.value();
  
  // Check if we need to evict chunks
  while (IsCacheFull() || 
         (current_cache_size_ + chunk->size > config_.max_buffer_size)) {
    EvictLRU();
  }
  
  // Add to cache
  CacheEntry entry;
  entry.chunk = chunk;
  
  // Add to front of LRU list (most recently used)
  lru_list_.push_front(chunk_id);
  entry.lru_iterator = lru_list_.begin();
  
  chunk_cache_[chunk_id] = entry;
  current_cache_size_ += chunk->size;
  
  return chunk;
}

void ChunkReader::UpdateLRU(size_t chunk_id) {
  // Note: caller must hold cache_mutex_
  auto it = chunk_cache_.find(chunk_id);
  if (it == chunk_cache_.end()) {
    return;
  }
  
  // Remove from current position in LRU list
  lru_list_.erase(it->second.lru_iterator);
  
  // Add to front (most recently used)
  lru_list_.push_front(chunk_id);
  it->second.lru_iterator = lru_list_.begin();
}

void ChunkReader::EvictLRU() {
  // Note: caller must hold cache_mutex_
  if (lru_list_.empty()) {
    return;
  }
  
  // Get least recently used chunk
  size_t chunk_id = lru_list_.back();
  lru_list_.pop_back();
  
  // Remove from cache
  auto it = chunk_cache_.find(chunk_id);
  if (it != chunk_cache_.end()) {
    current_cache_size_ -= it->second.chunk->size;
    chunk_cache_.erase(it);
    
    // Update statistics
    stats_.chunks_evicted++;
  }
}

bool ChunkReader::IsCacheFull() const {
  // Note: caller must hold cache_mutex_
  return chunk_cache_.size() >= config_.max_chunks;
}

nonstd::expected<size_t, std::string> ChunkReader::LoadChunks(const std::vector<size_t>& chunk_ids) {
  size_t loaded_count = 0;
  
  for (size_t chunk_id : chunk_ids) {
    // Check if already cached
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      if (chunk_cache_.find(chunk_id) != chunk_cache_.end()) {
        continue; // Already cached
      }
    }
    
    // Load the chunk
    auto result = GetChunk(chunk_id);
    if (result) {
      loaded_count++;
    } else {
      return nonstd::make_unexpected("Failed to load chunk " + std::to_string(chunk_id) + ": " + result.error());
    }
  }
  
  return loaded_count;
}

} // namespace tinyusdz