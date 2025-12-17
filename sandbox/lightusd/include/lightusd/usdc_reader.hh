// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// USDC (Crate) file reader with lazy value decoding

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>

#include "lightusd/result.hh"
#include "lightusd/path.hh"
#include "lightusd/token.hh"
#include "lightusd/value.hh"
#include "lightusd/crate_format.hh"
#include "lightusd/usdc_fetch.hh"

namespace lightusd {
namespace v1 {

// Forward declarations
class Stage;
class Prim;

/// Options for USDC reading
struct UsdcReaderOptions {
    /// Maximum file size to read (0 = no limit)
    size_t max_file_size = 0;

    /// Maximum memory budget for decompression (0 = no limit)
    size_t max_memory_mb = 0;

    /// Whether to decode values lazily (recommended for large files)
    bool lazy_decode = true;

    /// Whether to validate file structure strictly
    bool strict_validation = false;

    /// Custom warning handler
    std::function<void(const std::string&)> warn_handler;
};

/// USDC file reader with lazy value support
class UsdcReader {
public:
    UsdcReader();
    ~UsdcReader();

    // Non-copyable
    UsdcReader(const UsdcReader&) = delete;
    UsdcReader& operator=(const UsdcReader&) = delete;

    // Movable
    UsdcReader(UsdcReader&&) noexcept;
    UsdcReader& operator=(UsdcReader&&) noexcept;

    // -------------------------------------------------------------------------
    // File Operations
    // -------------------------------------------------------------------------

    /// Read USDC from memory buffer
    Result<void> read(const uint8_t* data, size_t size,
                      const UsdcReaderOptions& options = {});

    /// Read USDC from file path
    Result<void> read_file(const std::string& filepath,
                           const UsdcReaderOptions& options = {});

    /// Check if data appears to be USDC format
    static bool is_usdc(const uint8_t* data, size_t size);

    // -------------------------------------------------------------------------
    // File Information
    // -------------------------------------------------------------------------

    /// Get file version
    crate::CrateVersion version() const;

    /// Get number of tokens
    size_t token_count() const;

    /// Get number of strings
    size_t string_count() const;

    /// Get number of paths
    size_t path_count() const;

    /// Get number of specs
    size_t spec_count() const;

    // -------------------------------------------------------------------------
    // Token/String Access
    // -------------------------------------------------------------------------

    /// Get token by index
    Result<Token> get_token(crate::TokenIndex index) const;

    /// Get string by index
    Result<std::string> get_string(crate::StringIndex index) const;

    // -------------------------------------------------------------------------
    // Path Access
    // -------------------------------------------------------------------------

    /// Get path by index
    Result<Path> get_path(crate::PathIndex index) const;

    /// Get all paths
    std::vector<Path> get_all_paths() const;

    // -------------------------------------------------------------------------
    // Spec Access
    // -------------------------------------------------------------------------

    /// Get spec by index
    Result<crate::Spec> get_spec(size_t index) const;

    /// Get fields for a spec
    Result<std::vector<crate::Field>> get_spec_fields(size_t spec_index) const;

    // -------------------------------------------------------------------------
    // Value Decoding (Lazy)
    // -------------------------------------------------------------------------

    /// Get lazy value for a field
    crate::LazyValue get_lazy_value(const crate::Field& field) const;

    /// Decode a lazy value to actual Value
    Result<Value> decode_value(const crate::LazyValue& lazy) const;

    /// Decode a ValueRep to Value
    Result<Value> decode_value_rep(const crate::ValueRep& rep) const;

    // -------------------------------------------------------------------------
    // Stage Construction
    // -------------------------------------------------------------------------

    /// Reconstruct full stage from parsed data
    Result<Stage> reconstruct_stage() const;

    // -------------------------------------------------------------------------
    // Fetch-on-Demand API
    // -------------------------------------------------------------------------

    /// Set fetch handler for on-demand reading
    /// Once set, use step()/provide_data() or read_with_fetch() to read
    void set_fetch_handler(const lightusd_fetch_handler_t& handler);

    /// Check if using fetch mode (handler is set)
    bool is_fetch_mode() const;

    /// Step state machine - get next required fetch request
    /// @param out_request Filled with the request details
    /// @return Tag of section needed, or LIGHTUSD_FETCH_COUNT when complete
    lightusd_fetch_tag_t step(lightusd_fetch_request_t* out_request);

    /// Provide fetched data to reader
    /// @param tag Which section this data is for
    /// @param data Pointer to fetched data
    /// @param size Size of data in bytes
    /// @return Success or error
    Result<void> provide_data(lightusd_fetch_tag_t tag,
                              const uint8_t* data, size_t size);

    /// Complete an async fetch (for PENDING responses)
    /// @param async_token Token from the pending response
    /// @param status Final status (OK or ERROR)
    /// @param data Pointer to fetched data (if OK)
    /// @param size Size of data (if OK)
    /// @param error_message Error description (if ERROR)
    /// @return Success or error
    Result<void> complete_async(uint64_t async_token,
                                lightusd_fetch_status_t status,
                                const uint8_t* data, size_t size,
                                const char* error_message);

    /// Check if waiting for async data
    bool is_pending() const;

    /// Get current fetch state (for debugging/progress reporting)
    lightusd_fetch_tag_t current_fetch_state() const;

    /// Run fetch loop to completion (for synchronous callbacks)
    /// Calls step() -> callback -> provide_data() until complete
    /// @param options Reader options
    /// @return Success or error
    Result<void> read_with_fetch(const UsdcReaderOptions& options = {});

    // -------------------------------------------------------------------------
    // Error/Warning Access
    // -------------------------------------------------------------------------

    /// Get last error message
    const std::string& error() const;

    /// Get warning messages
    const std::vector<std::string>& warnings() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Check if file is USDC format
bool is_usdc_file(const std::string& filepath);

/// Load USDC file into stage
Result<Stage> load_usdc(const std::string& filepath,
                        const UsdcReaderOptions& options = {});

/// Load USDC from memory into stage
Result<Stage> load_usdc(const uint8_t* data, size_t size,
                        const UsdcReaderOptions& options = {});

} // namespace v1
} // namespace lightusd
