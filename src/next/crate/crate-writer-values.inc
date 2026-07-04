// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - CrateWriter::Impl VALUE encoding helpers.
// Included inside CrateWriter::Impl private section.

  // ============================================================
  // Value encoding
  // ============================================================

  /// Store data in VALUE section and return its offset.
  /// Returns the offset from the START of the VALUE section.
  uint64_t StoreValueData(const void* data, size_t size, TypeId type) {
    DataBlock block;
    block.type = type;
    block.data.resize(size);
    std::memcpy(block.data.data(), data, size);
    CacheBlockHash(&block);
    return InternBlock(std::move(block));
  }

  // Encode a Value into a ValueRep.
  // For inlinable scalars, payload contains the value directly.
  // For larger values, payload contains an index into value_data_.
  // Returns the ValueRep and records the field's VALUE section offset.
  // Encode a VtDictionary in pxr binary form (OpenUSD WriteMap + _RecursiveWrite
  // of each Write(VtValue)):
  //   [u64 count] then per entry [u32 keyStrIdx][i64 recOffset=8][u64 ValueRep]
  // The recursive forward offset is a constant 8: the ValueRep immediately
  // follows it and the rep's payload is an absolute offset to the value's data
  // block (pxr's reader follows that payload, so no inline data is required).
  // Embedded non-inline reps are relocated by _PatchDictBlock once block offsets
  // are known. The block is patched in place, so it must NOT be deduped/shared
  // (push directly, do not InternBlock).
  ValueRep EncodeDictionary(const Dict& d) {
    size_t entry_count = d.entries.size();
    size_t payload_size = 0;
    if (!safe::add(size_t(8), entry_count * size_t(20), &payload_size)) {
      result_.error = "Dictionary payload size overflow";
      return ValueRep::Make(CrateTypeId::Dictionary, 0, false, false);
    }
    std::vector<uint8_t> blk(payload_size, 0);
    std::memcpy(blk.data(), &entry_count, 8);
    for (size_t i = 0; i < entry_count; ++i) {
      const auto& kv = d.entries[i];
      const size_t off = 8 + (i * size_t(20));
      uint32_t kidx = InternString(kv.first);
      ValueRep vr = EncodeValue(kv.second);  // recursive (nested dicts/arrays)
      std::memcpy(blk.data() + off, &kidx, 4);
      int64_t rec_offset = 8;
      std::memcpy(blk.data() + off + 4, &rec_offset, 8);
      uint64_t vr_raw = vr.raw();
      std::memcpy(blk.data() + off + 12, &vr_raw, 8);
    }
    uint64_t idx = value_data_.size();
    value_data_.push_back({TypeId::Dictionary, std::move(blk)});
    return ValueRep::Make(CrateTypeId::Dictionary, idx, false, false);
  }

  ValueRep EncodeValue(const Value& val) {
    if (val.is_block()) {
      return ValueRep::Make(CrateTypeId::ValueBlock, 0, false, true);
    }

    if (val.is_dictionary()) {
      const Dict* d = val.as_dictionary();
      return d ? EncodeDictionary(*d)
               : ValueRep::Make(CrateTypeId::Dictionary, 0, false, false);
    }

    TypeId type_id = val.type_id();
    CrateTypeId crate_type = ToCrateTypeId(type_id);
    bool is_array = val.is_array();

    if (crate_type == CrateTypeId::Invalid) {
      if (!val.is_empty() && !val.is_block()) {
        const char* type_name = GetTypeName(type_id);
        result_.error = std::string("unsupported value type: ") +
                        (type_name ? type_name : "invalid");
      }
      return ValueRep::Make(CrateTypeId::Invalid, 0, is_array, false);
    }

    // Fast path: copy an unmodified, crate-backed array straight through from
    // its source buffer, skipping decode -> re-encode of large payloads. This
    // must run before any accessor below (which would materialize the array).
    if (is_array && val.is_lazy() && !val.is_dirty()) {
      ValueRep pt;
      if (TryPassThrough(val, &pt)) {
        ++arrays_passed_through_;
        return pt;
      }
      ++arrays_reencoded_;  // fell back to full decode + re-encode below
    }

    // Handle inline scalars
    if (!is_array && options_.inline_small_values) {
      uint64_t payload = 0;
      bool can_inline = false;

      switch (type_id) {
        case TypeId::Bool: {
          const bool* v = val.as_bool();
          if (v) { payload = *v ? 1 : 0; can_inline = true; }
          break;
        }
        case TypeId::Int: {
          const int32_t* v = val.as_int();
          if (v) { payload = static_cast<uint64_t>(*v); can_inline = true; }
          break;
        }
        case TypeId::UInt: {
          const uint32_t* v = val.as_uint();
          if (v) { payload = *v; can_inline = true; }
          break;
        }
        case TypeId::Float: {
          const float* v = val.as_float();
          if (v) { uint32_t bits; std::memcpy(&bits, v, 4); payload = bits; can_inline = true; }
          break;
        }
        case TypeId::Token: {
          const std::string* s = val.as_token();
          if (s) { payload = InternToken(*s); can_inline = true; }
          break;
        }
        default:
          break;
      }

      if (can_inline) {
        return ValueRep::Make(crate_type, payload, is_array, true);
      }
    }

    // String / AssetPath via string index (not inline)
    if (type_id == TypeId::String) {
      const std::string* s = val.as_string();
      if (s) {
        uint32_t str_idx = InternString(*s);
        return ValueRep::Make(crate_type, str_idx, false, true);
      }
    }
    if (type_id == TypeId::AssetPath) {
      const std::string* s = val.as_asset_path();
      if (s) {
        // AssetPath: type stays AssetPath; payload is a token index (the path
        // string interned as a token), matching the reader's UnpackAssetPath.
        uint32_t tok_idx = InternToken(*s);
        return ValueRep::Make(CrateTypeId::AssetPath, tok_idx, false, true);
      }
    }
    if (type_id == TypeId::Token) {
      const std::string* s = val.as_token();
      if (s) {
        uint32_t tok_idx = InternToken(*s);
        return ValueRep::Make(CrateTypeId::Token, tok_idx, false, true);
      }
    }

    // Scalar Double / TimeCode (always stored in VALUE section, not inlined).
    // MUST be scalar-only: an array that fell through the pass-through fast
    // path above would otherwise be encoded as one "double" read from
    // raw_data(), which for a lazy array is the LazyArrayRef control struct
    // (uninitialized padding included), not element data.
    if (!is_array && (type_id == TypeId::Double || type_id == TypeId::TimeCode)) {
      const void* raw = val.raw_data();
      const double* v = static_cast<const double*>(raw);
      if (v) {
        uint64_t data_idx = StoreValueData(v, sizeof(double), type_id);
        return ValueRep::Make(crate_type, data_idx, false, false);
      }
      return ValueRep::Make(crate_type, 0, false, false);
    }

    // Non-inline scalars: store in data section
    if (!is_array) {
      size_t elem_size = CrateValueSize(crate_type, false);
      if (elem_size > 0 && elem_size <= 128) {
        uint8_t tmp[128];
        uint64_t data_idx = value_data_.size();  // placeholder
        auto copy_scalar = [&](size_t size, const void* src) {
          if (src) {
            std::memcpy(tmp, src, size);
            data_idx = StoreValueData(tmp, size, type_id);
          }
        };
        switch (type_id) {
          case TypeId::Half:
            copy_scalar(sizeof(uint16_t), val.raw_data());
            break;
          case TypeId::Half2:
          case TypeId::Texcoord2h:
            copy_scalar(2u * sizeof(uint16_t), val.raw_data());
            break;
          case TypeId::Half3:
          case TypeId::Point3h:
          case TypeId::Vector3h:
          case TypeId::Normal3h:
          case TypeId::Color3h:
          case TypeId::Texcoord3h:
            copy_scalar(3u * sizeof(uint16_t), val.raw_data());
            break;
          case TypeId::Half4:
          case TypeId::Color4h:
          case TypeId::Quath:
            copy_scalar(4u * sizeof(uint16_t), val.raw_data());
            break;
          case TypeId::Float2:
          case TypeId::Texcoord2f:
            copy_scalar(2u * sizeof(float), val.raw_data());
            break;
          case TypeId::Int64: {
            const int64_t* v = val.as_int64();
            copy_scalar(sizeof(*v), v);
            break;
          }
          case TypeId::UInt64: {
            const uint64_t* v = val.as_uint64();
            copy_scalar(sizeof(*v), v);
            break;
          }
          case TypeId::Float3:
          case TypeId::Point3f:
          case TypeId::Vector3f:
          case TypeId::Normal3f:
          case TypeId::Color3f:
          case TypeId::Texcoord3f:
            copy_scalar(3u * sizeof(float), val.raw_data());
            break;
          case TypeId::Float4:
          case TypeId::Color4f:
          case TypeId::Quatf:
            copy_scalar(4u * sizeof(float), val.raw_data());
            break;
          case TypeId::Double2:
            copy_scalar(2u * sizeof(double), val.raw_data());
            break;
          case TypeId::Double3:
          case TypeId::Point3d:
          case TypeId::Vector3d:
          case TypeId::Normal3d:
          case TypeId::Color3d:
          case TypeId::Texcoord3d:
            copy_scalar(3u * sizeof(double), val.raw_data());
            break;
          case TypeId::Double4:
          case TypeId::Quatd:
          case TypeId::Color4d:
            copy_scalar(4u * sizeof(double), val.raw_data());
            break;
          case TypeId::Matrix2f: {
            const void* p = val.raw_data();
            if (p) {
              const float* fp = static_cast<const float*>(p);
              double d[4];
              for (size_t i = 0; i < 4; ++i) d[i] = static_cast<double>(fp[i]);
              std::memcpy(tmp, d, 4u * sizeof(double));
              data_idx = StoreValueData(tmp, 4u * sizeof(double), type_id);
            }
            break;
          }
          case TypeId::Matrix3f: {
            const void* p = val.raw_data();
            if (p) {
              const float* fp = static_cast<const float*>(p);
              double d[9];
              for (size_t i = 0; i < 9; ++i) d[i] = static_cast<double>(fp[i]);
              std::memcpy(tmp, d, 9u * sizeof(double));
              data_idx = StoreValueData(tmp, 9u * sizeof(double), type_id);
            }
            break;
          }
          case TypeId::Matrix4f: {
            const void* p = val.raw_data();
            if (p) {
              const float* fp = static_cast<const float*>(p);
              double d[16];
              for (size_t i = 0; i < 16; ++i) d[i] = static_cast<double>(fp[i]);
              std::memcpy(tmp, d, 16u * sizeof(double));
              data_idx = StoreValueData(tmp, 16u * sizeof(double), type_id);
            }
            break;
          }
          case TypeId::Matrix2d:
          case TypeId::Matrix3d:
          case TypeId::Matrix4d: {
            const void* p = val.raw_data();
            if (p) {
              const size_t size = (type_id == TypeId::Matrix2d)
                                     ? 4u * sizeof(double)
                                     : (type_id == TypeId::Matrix3d) ? 9u * sizeof(double)
                                                                     : 16u * sizeof(double);
              copy_scalar(size, p);
            }
            break;
          }
          default:
            break;
        }
        return ValueRep::Make(crate_type, data_idx, false, false);
      }
      return ValueRep::Make(crate_type, 0, false, false);
    }

    // Array types: store count + elements in data section
    if (is_array) {
      uint64_t count = 0;
      bool data_compressed = false;  // whether the array data was compressed
      size_t elem_size = CrateValueSize(crate_type, false);
      (void)elem_size;

      // Collect array data
      std::vector<uint8_t> arr_data;
      auto resize_counted_payload = [&](uint64_t item_count, size_t item_size) -> bool {
        size_t bytes = 0;
        size_t total = 0;
        if (!safe::mul(item_count, item_size, &bytes) ||
            !safe::add(size_t(8), bytes, &total)) {
          result_.error = "Array payload size overflow";
          return false;
        }
        arr_data.resize(total);
        std::memcpy(arr_data.data(), &item_count, 8);
        return true;
      };
      auto copy_counted_payload = [&](uint64_t item_count, const void* data,
                                    size_t item_size) -> bool {
        if (!resize_counted_payload(item_count, item_size)) {
          return false;
        }
        size_t bytes = 0;
        if (!safe::mul(item_count, item_size, &bytes)) {
          result_.error = "Array payload size overflow";
          return false;
        }
        if (bytes > 0) {
          std::memcpy(arr_data.data() + 8, data, bytes);
        }
        return true;
      };

      switch (type_id) {
        case TypeId::Float: {
          const std::vector<float>* arr = val.as_float_array();
          if (arr) {
            count = arr->size();
            if (!copy_counted_payload(count, arr->data(), sizeof(float))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
          }
          break;
        }
        case TypeId::Int: {
          const std::vector<int32_t>* arr = val.as_int_array();
          if (arr) {
            count = arr->size();
            if (options_.compress_arrays && count >= 16) {
              // Compressed integer array: [u64 count][u64 comp_size][LZ4+delta data]
              std::vector<uint32_t> tmp(count);
              for (size_t k = 0; k < count; ++k) tmp[k] = static_cast<uint32_t>((*arr)[k]);
              std::vector<uint8_t> delta = EncodeDeltaU32(tmp.data(), count);
              CompressResult cr = CompressCrateBlob(delta.data(), delta.size());
              if (cr.success) {
                size_t payload_size = 0;
                size_t total_size = 0;
                if (!safe::add(size_t(8), cr.data.size(), &payload_size) ||
                    !safe::add(size_t(8), payload_size, &total_size)) {
                  result_.error = "Compressed integer array payload size overflow";
                  return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
                }
                arr_data.resize(total_size);
                std::memcpy(arr_data.data(), &count, 8);
                uint64_t csz = static_cast<uint64_t>(cr.data.size());
                std::memcpy(arr_data.data() + 8, &csz, 8);
                std::memcpy(arr_data.data() + 16, cr.data.data(), cr.data.size());
                data_compressed = true;
              } else {
                // Fall back to uncompressed
                if (!copy_counted_payload(count, arr->data(), sizeof(int32_t))) {
                  return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
                }
              }
            } else {
              if (!copy_counted_payload(count, arr->data(), sizeof(int32_t))) {
                return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
              }
            }
          }
          break;
        }
        case TypeId::Double: {
          const std::vector<double>* arr = val.as_double_array();
          if (arr) {
            count = arr->size();
            if (!copy_counted_payload(count, arr->data(), sizeof(double))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
          } else {
            // Fallback: try reading as float array and convert
            const std::vector<float>* farr = val.as_float_array();
            if (farr) {
              count = farr->size();
              if (!resize_counted_payload(count, sizeof(double))) {
                return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
              }
              double* dptr = reinterpret_cast<double*>(arr_data.data() + 8);
              for (size_t i = 0; i < count; ++i) dptr[i] = static_cast<double>((*farr)[i]);
            }
          }
          break;
        }
        case TypeId::Int64: {
          const std::vector<int64_t>* arr = val.as_int64_array();
          if (arr) {
            count = arr->size();
            if (!copy_counted_payload(count, arr->data(), sizeof(int64_t))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
          }
          break;
        }
        case TypeId::UInt: {
          const std::vector<uint32_t>* arr = val.as_uint_array();
          if (arr) {
            count = arr->size();
            if (!copy_counted_payload(count, arr->data(), sizeof(uint32_t))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
          }
          break;
        }
        case TypeId::UInt64: {
          const std::vector<uint64_t>* arr = val.as_uint64_array();
          if (arr) {
            count = arr->size();
            if (!copy_counted_payload(count, arr->data(), sizeof(uint64_t))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
          }
          break;
        }
        case TypeId::Bool: {
          const std::vector<uint8_t>* arr = val.as_bool_array();
          if (arr) {
            count = arr->size();
            if (!copy_counted_payload(count, arr->data(), sizeof(uint8_t))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
          }
          break;
        }
        case TypeId::Token: {
          const std::vector<TfToken>* arr = val.as_token_array();
          if (arr) {
            count = arr->size();
            // Store token indices
            if (!resize_counted_payload(count, sizeof(uint32_t))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
            for (size_t k = 0; k < count; ++k) {
              uint32_t idx = InternToken((*arr)[k].sv());
              std::memcpy(arr_data.data() + 8 + k * 4, &idx, 4);
            }
          }
          break;
        }
        // Vector / quat / matrix arrays: flat float or double buffer. comps =
        // scalars per element (Vec2f=2, Vec4f/Quatf=4, Matrix4d=16, ...).
        case TypeId::Float2:
        case TypeId::Float3:
        case TypeId::Float4:
        case TypeId::Point3f:
        case TypeId::Vector3f:
        case TypeId::Normal3f:
        case TypeId::Color3f:
        case TypeId::Texcoord2f:
        case TypeId::Texcoord3f:
        case TypeId::Color4f:
        case TypeId::Quatf: {
          const std::vector<float>* arr = val.as_float_array();
          const uint32_t comps = ArrayComps(type_id);
          if (arr && comps && arr->size() % comps == 0) {
            count = arr->size() / comps;
            if (!copy_counted_payload(arr->size(), arr->data(), sizeof(float))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
            std::memcpy(arr_data.data(), &count, 8);
          }
          break;
        }
        case TypeId::Half:
        case TypeId::Half2:
        case TypeId::Half3:
        case TypeId::Half4:
        case TypeId::Point3h:
        case TypeId::Vector3h:
        case TypeId::Normal3h:
        case TypeId::Color3h:
        case TypeId::Color4h:
        case TypeId::Texcoord2h:
        case TypeId::Texcoord3h:
        case TypeId::Quath: {
          const std::vector<float>* arr = val.as_float_array();
          const uint32_t comps = ArrayComps(type_id);
          if (arr && comps && arr->size() % comps == 0) {
            count = arr->size() / comps;
            if (!resize_counted_payload(arr->size(), sizeof(uint16_t))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
            std::memcpy(arr_data.data(), &count, 8);
            for (size_t i = 0; i < arr->size(); ++i) {
              uint16_t h = FloatToHalf((*arr)[i]);
              std::memcpy(arr_data.data() + 8 + i * sizeof(uint16_t), &h,
                          sizeof(uint16_t));
            }
          }
          break;
        }
        case TypeId::Matrix2f:
        case TypeId::Matrix3f:
        case TypeId::Matrix4f: {
          const std::vector<float>* arr = val.as_float_array();
          const uint32_t comps = ArrayComps(type_id);
          if (arr && comps && arr->size() % comps == 0) {
            count = arr->size() / comps;
            if (!resize_counted_payload(arr->size(), sizeof(double))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
            std::memcpy(arr_data.data(), &count, 8);
            for (size_t i = 0; i < arr->size(); ++i) {
              double d = static_cast<double>((*arr)[i]);
              std::memcpy(arr_data.data() + 8 + i * sizeof(double), &d,
                          sizeof(double));
            }
          }
          break;
        }
        case TypeId::Double2:
        case TypeId::Double3:
        case TypeId::Double4:
        case TypeId::Point3d:
        case TypeId::Vector3d:
        case TypeId::Normal3d:
        case TypeId::Color3d:
        case TypeId::Texcoord3d:
        case TypeId::Quatd:
        case TypeId::Matrix2d:
        case TypeId::Matrix3d:
        case TypeId::Matrix4d: {
          const std::vector<double>* arr = val.as_double_array();
          const uint32_t comps = ArrayComps(type_id);
          if (arr && comps && arr->size() % comps == 0) {
            count = arr->size() / comps;
            if (!copy_counted_payload(arr->size(), arr->data(), sizeof(double))) {
              return ValueRep::Make(CrateTypeId::Invalid, 0, false, false);
            }
            std::memcpy(arr_data.data(), &count, 8);
          }
          break;
        }
        default:
          break;
      }

      // Empty array (count == 0): pxrUSD encodes this as payload 0 with NO data
      // block (a count=0 block makes pxr/legacy readers reject it). The next
      // reader also treats payload==0 as an empty array.
      if (count == 0) {
        return ValueRep::Make(crate_type, 0, true, false);
      }

      if (!arr_data.empty()) {
        DataBlock block;
        block.type = type_id;
        block.data = std::move(arr_data);
        CacheBlockHash(&block);
        uint64_t idx = InternBlock(std::move(block));
        // Return non-inline value rep (index will be resolved to offset later)
        return ValueRep::Make(crate_type, idx, true, false, data_compressed);
      }
    }

    return ValueRep::Make(crate_type, 0, is_array, false);
  }

  // Build fields and specs from layer
