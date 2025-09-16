// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Basic I/O operations for Crate reader - Implementation

#include "crate-io.hh"
#include "common-macros.inc"
#include "integerCoding.h"
#include "str-util.hh"
#include "tiny-format.hh"

#define kTag "[CrateIO]"

#define CHECK_MEMORY_USAGE(__nbytes) \
  MEMORY_BUDGET_CHECK(memory_manager_, (__nbytes), kTag)

#define REDUCE_MEMORY_USAGE(__nbytes) \
  memory_manager_.Release(__nbytes)

namespace tinyusdz {
namespace crate {

bool CrateIOHelper::ReadIndex(crate::Index* i) {
  if (!i) {
    PushError("nullptr passed to ReadIndex");
    return false;
  }

  if (!_sr->read(sizeof(crate::Index), sizeof(crate::Index),
                 reinterpret_cast<uint8_t*>(i))) {
    PushError(fmt::format("Failed to read Index data."));
    return false;
  }

  return true;
}

bool CrateIOHelper::ReadIndices(std::vector<crate::Index>* indices) {
  if (!indices) {
    PushError("nullptr passed to ReadIndices");
    return false;
  }

  uint64_t n;
  if (!_sr->read8(&n)) {
    PushError("Failed to read the number of Indices.");
    return false;
  }

  if (n == 0) {
    indices->clear();
    return true;
  }

  if (n > std::numeric_limits<size_t>::max()) {
    PushError(fmt::format("Too many indices: {}", n));
    return false;
  }

  CHECK_MEMORY_USAGE(n * sizeof(crate::Index));

  indices->resize(static_cast<size_t>(n));
  if (!_sr->read(sizeof(crate::Index), n * sizeof(crate::Index),
                 reinterpret_cast<uint8_t*>(indices->data()))) {
    REDUCE_MEMORY_USAGE(n * sizeof(crate::Index));
    PushError("Failed to read Indices data.");
    return false;
  }

  return true;
}

bool CrateIOHelper::ReadString(std::string* s) {
  if (!s) {
    PushError("nullptr passed to ReadString");
    return false;
  }

  uint64_t n;
  if (!_sr->read8(&n)) {
    PushError("Failed to read string length");
    return false;
  }

  if (n == 0) {
    s->clear();
    return true;
  }

  if (n > std::numeric_limits<size_t>::max()) {
    PushError(fmt::format("String too long: {}", n));
    return false;
  }

  size_t len = static_cast<size_t>(n);
  CHECK_MEMORY_USAGE(len);

  s->resize(len);
  if (!_sr->read(len, len, reinterpret_cast<uint8_t*>(&(*s)[0]))) {
    REDUCE_MEMORY_USAGE(len);
    PushError("Failed to read string data");
    return false;
  }

  // Security: Check for null bytes in string
  if (s->find('\0') != std::string::npos) {
    PushWarn("String contains null bytes");
  }

  return true;
}

bool CrateIOHelper::ReadValueRep(crate::ValueRep* rep) {
  if (!rep) {
    PushError("nullptr passed to ReadValueRep");
    return false;
  }

  if (!_sr->read8(&rep->payload)) {
    PushError("Failed to read ValueRep payload");
    return false;
  }

  if (rep->IsInlined()) {
    // No additional data for inlined values
    return true;
  }

  // For non-inlined values, read the offset
  if (!_sr->read8(&rep->data)) {
    PushError("Failed to read ValueRep data offset");
    return false;
  }

  return true;
}

bool CrateIOHelper::ReadReference(Reference* d) {
  if (!d) {
    PushError("nullptr passed to ReadReference");
    return false;
  }

  // Read asset path index
  crate::Index assetPathIndex;
  if (!ReadIndex(&assetPathIndex)) {
    PushError("Failed to read Reference asset path index");
    return false;
  }

  // Read prim path index  
  crate::Index primPathIndex;
  if (!ReadIndex(&primPathIndex)) {
    PushError("Failed to read Reference prim path index");
    return false;
  }

  // Read layer offset
  LayerOffset offset;
  if (!ReadLayerOffset(&offset)) {
    PushError("Failed to read Reference layer offset");
    return false;
  }

  // Read custom data
  CustomDataType customData;
  if (!ReadCustomData(&customData)) {
    PushError("Failed to read Reference custom data");
    return false;
  }

  // TODO: Convert indices to actual paths using parent CrateReader
  // For now, store raw indices
  d->asset_path = std::to_string(assetPathIndex.value);
  if (primPathIndex.value != 0) {
    d->prim_path = Path("/" + std::to_string(primPathIndex.value), "");
  }
  d->layerOffset = offset;
  d->customData = customData;

  return true;
}

bool CrateIOHelper::ReadPayload(Payload* d) {
  if (!d) {
    PushError("nullptr passed to ReadPayload");
    return false;
  }

  // Read asset path index
  crate::Index assetPathIndex;
  if (!ReadIndex(&assetPathIndex)) {
    PushError("Failed to read Payload asset path index");
    return false;
  }

  // Read prim path index
  crate::Index primPathIndex;
  if (!ReadIndex(&primPathIndex)) {
    PushError("Failed to read Payload prim path index");
    return false;
  }

  // Read layer offset
  LayerOffset offset;
  if (!ReadLayerOffset(&offset)) {
    PushError("Failed to read Payload layer offset");
    return false;
  }

  // TODO: Convert indices to actual paths using parent CrateReader
  d->asset_path = std::to_string(assetPathIndex.value);
  if (primPathIndex.value != 0) {
    d->prim_path = Path("/" + std::to_string(primPathIndex.value), "");
  }
  d->layer_offset = offset;

  return true;
}

bool CrateIOHelper::ReadLayerOffset(LayerOffset* d) {
  if (!d) {
    PushError("nullptr passed to ReadLayerOffset");
    return false;
  }

  double offset, scale;
  if (!_sr->read_double(&offset)) {
    PushError("Failed to read LayerOffset offset value");
    return false;
  }
  if (!_sr->read_double(&scale)) {
    PushError("Failed to read LayerOffset scale value");
    return false;
  }

  *d = LayerOffset(offset, scale);
  return true;
}

bool CrateIOHelper::ReadLayerOffsetArray(std::vector<LayerOffset>* d) {
  if (!d) {
    PushError("nullptr passed to ReadLayerOffsetArray");
    return false;
  }

  uint64_t n;
  if (!_sr->read8(&n)) {
    PushError("Failed to read LayerOffset array size");
    return false;
  }

  if (n == 0) {
    d->clear();
    return true;
  }

  if (n > std::numeric_limits<size_t>::max() / sizeof(LayerOffset)) {
    PushError(fmt::format("LayerOffset array too large: {}", n));
    return false;
  }

  size_t count = static_cast<size_t>(n);
  CHECK_MEMORY_USAGE(count * sizeof(LayerOffset));

  d->resize(count);
  for (size_t i = 0; i < count; ++i) {
    if (!ReadLayerOffset(&(*d)[i])) {
      REDUCE_MEMORY_USAGE(count * sizeof(LayerOffset));
      PushError(fmt::format("Failed to read LayerOffset at index {}", i));
      return false;
    }
  }

  return true;
}

bool CrateIOHelper::ReadPathArray(std::vector<Path>* d) {
  if (!d) {
    PushError("nullptr passed to ReadPathArray");
    return false;
  }

  uint64_t n;
  if (!_sr->read8(&n)) {
    PushError("Failed to read Path array size");
    return false;
  }

  if (n == 0) {
    d->clear();
    return true;
  }

  if (n > std::numeric_limits<size_t>::max()) {
    PushError(fmt::format("Path array too large: {}", n));
    return false;
  }

  size_t count = static_cast<size_t>(n);
  d->reserve(count);

  for (size_t i = 0; i < count; ++i) {
    crate::Index pathIndex;
    if (!ReadIndex(&pathIndex)) {
      PushError(fmt::format("Failed to read Path index at {}", i));
      return false;
    }
    
    // TODO: Convert index to actual path using parent CrateReader
    // For now, create placeholder path
    d->push_back(Path(fmt::format("/path_{}", pathIndex.value), ""));
  }

  return true;
}

bool CrateIOHelper::ReadVariantSelectionMap(VariantSelectionMap* d) {
  if (!d) {
    PushError("nullptr passed to ReadVariantSelectionMap");
    return false;
  }

  uint64_t n;
  if (!_sr->read8(&n)) {
    PushError("Failed to read VariantSelectionMap size");
    return false;
  }

  if (n == 0) {
    d->clear();
    return true;
  }

  if (n > std::numeric_limits<size_t>::max()) {
    PushError(fmt::format("VariantSelectionMap too large: {}", n));
    return false;
  }

  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    std::string key, value;
    if (!ReadString(&key)) {
      PushError(fmt::format("Failed to read variant key at index {}", i));
      return false;
    }
    if (!ReadString(&value)) {
      PushError(fmt::format("Failed to read variant value at index {}", i));
      return false;
    }
    
    (*d)[key] = value;
  }

  return true;
}

bool CrateIOHelper::ReadCustomData(CustomDataType* d) {
  if (!d) {
    PushError("nullptr passed to ReadCustomData");
    return false;
  }

  // Read type indicator
  uint8_t typeIndicator;
  if (!_sr->read1(&typeIndicator)) {
    PushError("Failed to read CustomData type indicator");
    return false;
  }

  if (typeIndicator == 0) {
    // Empty custom data
    d->clear();
    return true;
  }

  // For now, store as empty dict
  // Full implementation would parse the dictionary structure
  d->clear();
  
  PushWarn("CustomData parsing not fully implemented");
  return true;
}

// ListOp implementations
bool CrateIOHelper::ReadTokenListOp(ListOp<value::token>* d) {
  if (!d) {
    PushError("nullptr passed to ReadTokenListOp");
    return false;
  }

  uint8_t flags;
  if (!_sr->read1(&flags)) {
    PushError("Failed to read ListOp flags");
    return false;
  }

  bool isExplicit = (flags & 0x01) != 0;
  d->SetExplicit(isExplicit);

  // Read each list component based on flags
  auto readTokenList = [this](std::vector<value::token>* list) -> bool {
    uint64_t n;
    if (!_sr->read8(&n)) {
      PushError("Failed to read token list size");
      return false;
    }
    
    list->clear();
    for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
      crate::Index tokenIndex;
      if (!ReadIndex(&tokenIndex)) {
        return false;
      }
      // TODO: Convert index to actual token
      list->push_back(value::token(fmt::format("token_{}", tokenIndex.value)));
    }
    return true;
  };

  if (flags & 0x02) {  // Has explicit items
    std::vector<value::token> items;
    if (!readTokenList(&items)) {
      PushError("Failed to read explicit items");
      return false;
    }
    d->SetExplicitItems(items);
  }

  if (flags & 0x04) {  // Has added items
    std::vector<value::token> items;
    if (!readTokenList(&items)) {
      PushError("Failed to read added items");
      return false;
    }
    d->SetAddedItems(items);
  }

  if (flags & 0x08) {  // Has prepended items
    std::vector<value::token> items;
    if (!readTokenList(&items)) {
      PushError("Failed to read prepended items");
      return false;
    }
    d->SetPrependedItems(items);
  }

  if (flags & 0x10) {  // Has appended items
    std::vector<value::token> items;
    if (!readTokenList(&items)) {
      PushError("Failed to read appended items");
      return false;
    }
    d->SetAppendedItems(items);
  }

  if (flags & 0x20) {  // Has deleted items
    std::vector<value::token> items;
    if (!readTokenList(&items)) {
      PushError("Failed to read deleted items");
      return false;
    }
    d->SetDeletedItems(items);
  }

  return true;
}

bool CrateIOHelper::ReadStringListOp(ListOp<std::string>* d) {
  if (!d) {
    PushError("nullptr passed to ReadStringListOp");
    return false;
  }

  uint8_t flags;
  if (!_sr->read1(&flags)) {
    PushError("Failed to read ListOp flags");
    return false;
  }

  bool isExplicit = (flags & 0x01) != 0;
  d->SetExplicit(isExplicit);

  auto readStringList = [this](std::vector<std::string>* list) -> bool {
    uint64_t n;
    if (!_sr->read8(&n)) {
      PushError("Failed to read string list size");
      return false;
    }
    
    list->clear();
    for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
      std::string str;
      if (!ReadString(&str)) {
        return false;
      }
      list->push_back(str);
    }
    return true;
  };

  if (flags & 0x02) {
    std::vector<std::string> items;
    if (!readStringList(&items)) {
      PushError("Failed to read explicit items");
      return false;
    }
    d->SetExplicitItems(items);
  }

  if (flags & 0x04) {
    std::vector<std::string> items;
    if (!readStringList(&items)) {
      PushError("Failed to read added items");
      return false;
    }
    d->SetAddedItems(items);
  }

  if (flags & 0x08) {
    std::vector<std::string> items;
    if (!readStringList(&items)) {
      PushError("Failed to read prepended items");
      return false;
    }
    d->SetPrependedItems(items);
  }

  if (flags & 0x10) {
    std::vector<std::string> items;
    if (!readStringList(&items)) {
      PushError("Failed to read appended items");
      return false;
    }
    d->SetAppendedItems(items);
  }

  if (flags & 0x20) {
    std::vector<std::string> items;
    if (!readStringList(&items)) {
      PushError("Failed to read deleted items");
      return false;
    }
    d->SetDeletedItems(items);
  }

  return true;
}

bool CrateIOHelper::ReadPathListOp(ListOp<Path>* d) {
  if (!d) {
    PushError("nullptr passed to ReadPathListOp");
    return false;
  }

  uint8_t flags;
  if (!_sr->read1(&flags)) {
    PushError("Failed to read ListOp flags");
    return false;
  }

  bool isExplicit = (flags & 0x01) != 0;
  d->SetExplicit(isExplicit);

  auto readPathList = [this](std::vector<Path>* list) -> bool {
    uint64_t n;
    if (!_sr->read8(&n)) {
      PushError("Failed to read path list size");
      return false;
    }
    
    list->clear();
    for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
      crate::Index pathIndex;
      if (!ReadIndex(&pathIndex)) {
        return false;
      }
      // TODO: Convert index to actual path
      list->push_back(Path(fmt::format("/path_{}", pathIndex.value), ""));
    }
    return true;
  };

  if (flags & 0x02) {
    std::vector<Path> items;
    if (!readPathList(&items)) {
      PushError("Failed to read explicit items");
      return false;
    }
    d->SetExplicitItems(items);
  }

  if (flags & 0x04) {
    std::vector<Path> items;
    if (!readPathList(&items)) {
      PushError("Failed to read added items");
      return false;
    }
    d->SetAddedItems(items);
  }

  if (flags & 0x08) {
    std::vector<Path> items;
    if (!readPathList(&items)) {
      PushError("Failed to read prepended items");
      return false;
    }
    d->SetPrependedItems(items);
  }

  if (flags & 0x10) {
    std::vector<Path> items;
    if (!readPathList(&items)) {
      PushError("Failed to read appended items");
      return false;
    }
    d->SetAppendedItems(items);
  }

  if (flags & 0x20) {
    std::vector<Path> items;
    if (!readPathList(&items)) {
      PushError("Failed to read deleted items");
      return false;
    }
    d->SetDeletedItems(items);
  }

  return true;
}

}  // namespace crate
}  // namespace tinyusdz