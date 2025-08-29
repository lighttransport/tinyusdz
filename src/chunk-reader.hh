// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment Inc.
//
// Chunked data reader for streaming USD data parsing with LRU buffer management
#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "nonstd/expected.hpp"

namespace tinyusdz {

///
/// Chunked data reader for efficient streaming and parsing of large USD files.
/// This reader divides data into fixed-size chunks and manages them using an LRU cache.
/// When a requested chunk is not in cache, it triggers a callback to load the data.
///
/// Key features:
/// - Fixed-size chunks for predictable memory usage
/// - LRU (Least Recently Used) cache management
/// - Configurable maximum buffer size
/// - Asynchronous chunk loading via callbacks
/// - Thread-safe operations
///
class ChunkReader {
public:
  ///
  /// Chunk data structure
  ///
  struct Chunk {
    size_t chunk_id;                      ///< Unique identifier for the chunk
    size_t offset;                        ///< Offset in the original data stream
    size_t size;                          ///< Actual data size (may be less than chunk_size for last chunk)
    std::vector<uint8_t> data;           ///< Chunk data buffer
    bool is_loaded;                      ///< Whether the chunk is currently loaded
    
    Chunk() : chunk_id(0), offset(0), size(0), is_loaded(false) {}
    Chunk(size_t id, size_t off, size_t sz) 
      : chunk_id(id), offset(off), size(sz), data(sz), is_loaded(false) {}
  };

  ///
  /// Callback function type for chunk requests.
  /// Called when a chunk needs to be loaded that is not in the cache.
  /// 
  /// @param[in] chunk_id The ID of the requested chunk
  /// @param[in] offset The byte offset in the stream
  /// @param[in] size The size of data to read
  /// @param[out] data Buffer to fill with chunk data
  /// @return true if chunk was successfully loaded, false otherwise
  ///
  using ChunkRequestCallback = std::function<bool(size_t chunk_id, size_t offset, size_t size, uint8_t* data)>;

  ///
  /// Configuration for the chunk reader
  ///
  struct Config {
    size_t chunk_size;        ///< Size of each chunk in bytes (default: 1MB)
    size_t max_chunks;        ///< Maximum number of chunks to keep in memory
    size_t max_buffer_size;   ///< Maximum total buffer size in bytes (default: 16MB)
    bool enable_prefetch;     ///< Enable prefetching of adjacent chunks
    size_t prefetch_distance; ///< Number of chunks to prefetch ahead
    
    Config() 
      : chunk_size(1024 * 1024)
      , max_chunks(16)
      , max_buffer_size(16 * 1024 * 1024)
      , enable_prefetch(true)
      , prefetch_distance(2) {}
  };

  ///
  /// Statistics for monitoring chunk reader performance
  ///
  struct Stats {
    size_t total_reads;          ///< Total number of read operations
    size_t cache_hits;           ///< Number of cache hits
    size_t cache_misses;         ///< Number of cache misses
    size_t chunks_loaded;        ///< Total chunks loaded
    size_t chunks_evicted;       ///< Total chunks evicted from cache
    size_t bytes_read;           ///< Total bytes read
    
    Stats() 
      : total_reads(0)
      , cache_hits(0)
      , cache_misses(0)
      , chunks_loaded(0)
      , chunks_evicted(0)
      , bytes_read(0) {}
    
    double hit_rate() const {
      if (total_reads == 0) return 0.0;
      return static_cast<double>(cache_hits) / total_reads;
    }
  };

  ///
  /// Constructor
  ///
  /// @param[in] total_size Total size of the data stream in bytes
  /// @param[in] config Configuration parameters
  ///
  ChunkReader(size_t total_size, const Config& config = Config());
  
  ///
  /// Destructor
  ///
  ~ChunkReader();

  ///
  /// Set the chunk request callback
  ///
  /// @param[in] callback Function to call when a chunk needs to be loaded
  ///
  void SetChunkRequestCallback(ChunkRequestCallback callback) {
    chunk_request_callback_ = callback;
  }

  ///
  /// Information about required chunks for a read operation
  ///
  struct ReadResult {
    std::vector<size_t> required_chunks;  ///< List of chunk IDs needed but not in cache
    size_t bytes_available;                ///< Number of bytes that can be read from cache
    bool fully_cached;                     ///< True if all data is in cache
  };

  ///
  /// Read data from the stream
  ///
  /// @param[in] offset Byte offset to start reading from
  /// @param[in] size Number of bytes to read
  /// @param[out] buffer Buffer to store the read data (optional, can be null to just check)
  /// @return Expected containing ReadResult with required chunks info, or error string
  ///
  /// Error conditions:
  /// - Read exceeds EOF (total_size)
  /// - Read size exceeds chunk_size * max_chunks
  /// - Invalid offset
  ///
  /// If buffer is null, only returns information about required chunks without reading.
  /// If buffer is provided and all data is cached, performs the read.
  /// If buffer is provided but data is not cached, returns required chunks list.
  ///
  nonstd::expected<ReadResult, std::string> Read(size_t offset, size_t size, uint8_t* buffer = nullptr);

  ///
  /// Read data from the stream (legacy interface for compatibility)
  ///
  /// @param[in] offset Byte offset to start reading from
  /// @param[in] size Number of bytes to read
  /// @param[out] buffer Buffer to store the read data
  /// @param[in] force_load If true, loads required chunks via callback
  /// @return Expected containing number of bytes read, or error string
  ///
  nonstd::expected<size_t, std::string> ReadDirect(size_t offset, size_t size, uint8_t* buffer, bool force_load = true);

  ///
  /// Read a single byte
  ///
  /// @param[in] offset Byte offset to read from
  /// @param[out] value The byte value
  /// @return Expected containing true on success, or error string
  ///
  nonstd::expected<bool, std::string> ReadByte(size_t offset, uint8_t* value);

  ///
  /// Read 2 bytes (16-bit value)
  ///
  /// @param[in] offset Byte offset to read from
  /// @param[out] value The 16-bit value
  /// @return Expected containing true on success, or error string
  ///
  nonstd::expected<bool, std::string> Read2(size_t offset, uint16_t* value);

  ///
  /// Read 4 bytes (32-bit value)
  ///
  /// @param[in] offset Byte offset to read from
  /// @param[out] value The 32-bit value
  /// @return Expected containing true on success, or error string
  ///
  nonstd::expected<bool, std::string> Read4(size_t offset, uint32_t* value);

  ///
  /// Read 8 bytes (64-bit value)
  ///
  /// @param[in] offset Byte offset to read from
  /// @param[out] value The 64-bit value
  /// @return Expected containing true on success, or error string
  ///
  nonstd::expected<bool, std::string> Read8(size_t offset, uint64_t* value);

  ///
  /// Seek to a position in the stream
  ///
  /// @param[in] offset Byte offset to seek to
  /// @return true if seek was successful
  ///
  bool Seek(size_t offset);

  ///
  /// Get current position in the stream
  ///
  /// @return Current byte offset
  ///
  size_t Tell() const { return current_offset_; }

  ///
  /// Get total size of the stream
  ///
  /// @return Total size in bytes
  ///
  size_t Size() const { return total_size_; }

  ///
  /// Prefetch chunks starting from the given offset
  ///
  /// @param[in] offset Starting byte offset
  /// @param[in] size Size of data that will be read
  ///
  void Prefetch(size_t offset, size_t size);

  ///
  /// Clear all cached chunks
  ///
  void ClearCache();

  ///
  /// Get statistics about cache performance
  ///
  /// @return Current statistics
  ///
  Stats GetStats() const;

  ///
  /// Reset statistics
  ///
  void ResetStats();

  ///
  /// Check if a chunk is currently in cache
  ///
  /// @param[in] chunk_id The chunk ID to check
  /// @return true if chunk is in cache
  ///
  bool IsChunkCached(size_t chunk_id) const;

  ///
  /// Get the current cache size in bytes
  ///
  /// @return Current cache size
  ///
  size_t GetCacheSize() const;

  ///
  /// Get the number of chunks currently in cache
  ///
  /// @return Number of cached chunks
  ///
  size_t GetCachedChunkCount() const;

  ///
  /// Load specific chunks into cache
  ///
  /// @param[in] chunk_ids List of chunk IDs to load
  /// @return Expected containing number of chunks loaded, or error string
  ///
  nonstd::expected<size_t, std::string> LoadChunks(const std::vector<size_t>& chunk_ids);

  ///
  /// Get maximum readable size in one operation
  ///
  /// @return Maximum bytes that can be read in one operation
  ///
  size_t GetMaxReadSize() const {
    return config_.chunk_size * config_.max_chunks;
  }

private:
  ///
  /// Internal chunk cache entry for LRU management
  ///
  struct CacheEntry {
    std::shared_ptr<Chunk> chunk;
    std::list<size_t>::iterator lru_iterator;
  };

  ///
  /// Calculate chunk ID from byte offset
  ///
  size_t GetChunkId(size_t offset) const {
    return offset / config_.chunk_size;
  }

  ///
  /// Calculate offset within a chunk
  ///
  size_t GetChunkOffset(size_t offset) const {
    return offset % config_.chunk_size;
  }

  ///
  /// Load a chunk into cache
  ///
  /// @param[in] chunk_id The chunk ID to load
  /// @return Expected containing the loaded chunk, or error string
  ///
  nonstd::expected<std::shared_ptr<Chunk>, std::string> LoadChunk(size_t chunk_id);

  ///
  /// Get a chunk from cache or load it
  ///
  /// @param[in] chunk_id The chunk ID to get
  /// @return Expected containing the chunk, or error string
  ///
  nonstd::expected<std::shared_ptr<Chunk>, std::string> GetChunk(size_t chunk_id);

  ///
  /// Update LRU order for a chunk
  ///
  /// @param[in] chunk_id The chunk ID that was accessed
  ///
  void UpdateLRU(size_t chunk_id);

  ///
  /// Evict least recently used chunks if cache is full
  ///
  void EvictLRU();

  ///
  /// Check if cache size exceeds maximum
  ///
  bool IsCacheFull() const;

  // Configuration
  Config config_;
  size_t total_size_;
  size_t current_offset_;

  // Chunk cache with LRU management
  mutable std::mutex cache_mutex_;
  std::unordered_map<size_t, CacheEntry> chunk_cache_;
  std::list<size_t> lru_list_;  // Most recently used at front
  size_t current_cache_size_;

  // Callback for loading chunks
  ChunkRequestCallback chunk_request_callback_;

  // Statistics
  mutable Stats stats_;

  // Disable copy
  ChunkReader(const ChunkReader&) = delete;
  ChunkReader& operator=(const ChunkReader&) = delete;
};

} // namespace tinyusdz