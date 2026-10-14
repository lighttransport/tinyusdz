// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "prim-spec-variant-data.hh"

#include <memory>
#include <string>
#include <vector>

/// Fill a stronger dictionary's missing keys from a weaker one
/// (AOUSD §6.6.2 / §12.2). A key where one side is a dictionary and the other
/// is not is a TYPE CONFLICT: the stronger opinion correctly wins, but the
/// weaker subtree is silently shadowed — when `conflicts` is provided, the
/// dotted key path of each such collision is recorded so callers can surface
/// a diagnostic.
void MergeWeakerDictionaryValue(
    Value* stronger, const Value& weaker,
    std::vector<std::string>* conflicts = nullptr,
    const std::string& key_prefix = std::string());

void MergeWeakerExtensionFields(
    std::vector<DecodedExtensionField>* stronger,
    const std::vector<DecodedExtensionField>& weaker);

void MergeWeakerRawFields(
    std::vector<std::pair<std::string, std::string>>* stronger,
    const std::vector<std::pair<std::string, std::string>>& weaker);

/// Apply an authored string list-op to a weaker effective list. This preserves
/// the authored sublists above while providing one shared effective-order rule
/// for composition and consumers such as value clips.
/// Apply an `ordered` (reorder) sublist to `list` with pxr SdfListOp
/// semantics (_ReorderKeysHelper): each ordered item present drags along the
/// contiguous run of non-ordered items that follow it, sequences are emitted
/// in the authored ordered order, and items before any ordered item stay at
/// the front.
void ApplyStringListOrder(const std::vector<std::string> &order,
                                 std::vector<std::string> *list);

std::vector<std::string> ApplyStringListOp(
    const StringListOps &edits,
    const std::vector<std::string> &weaker);

/// Cold PrimSpec metadata: fields that are empty on the vast majority of prims
/// (no docs / variants / relocates / apiSchemas / instancing / list-op edits).
/// Held behind a lazily-allocated unique_ptr on PrimMetadata so an ordinary
/// prim pays only 8 pointer bytes instead of ~190 bytes of empty std::string /
/// std::vector heads (Phase 8.1 footprint split).
struct PrimMetadataCold {
  std::string doc;
  std::string comment;
  bool doc_authored = false;
  bool comment_authored = false;
  std::string kind;         // model kind (component/group/assembly/...)
  bool kind_authored = false;
  std::string displayName;  // UI display name
  bool display_name_authored = false;
  std::string permission;   // "public"/"private" (pxr SdfPermission)
  std::vector<std::string> displayGroupOrder;
  bool displayGroupOrderAuthored = false;
  // When non-empty (set by composition), this prim is an instance whose
  // children come from the prototype prim at this path (no duplicated subtree).
  std::string instance_prototype;
  std::vector<std::string> apiSchemas;
  bool apiSchemasAuthored = false;
  StringListOps apiSchemaEdits;
  // Authored list-op qualifier for apiSchemas: "" (bare/explicit),
  // "prepend", "append" or "delete" — pxr's own convention is prepend.
  std::string apiSchemasQualifier;
  // Variant set definitions.
  std::vector<PrimVariantSetData> variantSets;
  StringListOps variantSetNameEdits;
  // Multiple variant selections (set -> selection); composed in addition to the
  // legacy single `variantSelection` field on PrimMetadata.
  std::vector<std::pair<std::string, std::string>> variantSelections;
  bool variantSelectionsAuthored = false;
  // Relocates: namespace renames (absolute source path -> absolute target path).
  std::vector<std::pair<std::string, std::string>> relocates;
  bool relocatesAuthored = false;
  // Dictionary-valued metadata (each a Dictionary Value; empty when unauthored).
  Value customData;
  Value assetInfo;
  Value sdrMetadata;
  Value clips;
  StringListOps clipSetEdits;
  // Composed-output only (never parsed or serialized): properties whose
  // strongest authored opinion came from a composition source WEAKER than the
  // source that introduced the clips metadata. Value-clip resolution takes
  // precedence over these (LVRPS: local > clips > references/payloads).
  std::vector<PropNameId> clipShadowedProps;
  bool customDataAuthored = false;
  bool assetInfoAuthored = false;
  bool sdrMetadataAuthored = false;
  bool clipsAuthored = false;
  // Authored namespace ordering fields (`reorder nameChildren/properties`).
  std::vector<std::string> primOrder;
  std::vector<std::string> propertyOrder;
  bool primOrderAuthored = false;
  bool propertyOrderAuthored = false;
  // Unknown (unmodeled) prim metadata, preserved as (key, raw source text of
  // the value) in authored order so the USDA writer can re-emit it verbatim
  // (the legacy parser preserves unknown prim metadata; dropping it loses
  // pipeline-specific opinions like `sceneName`).
  std::vector<std::pair<std::string, std::string>> unknownMeta;
  std::vector<DecodedExtensionField> unknownFields;
  // Arc list-op qualifiers (Phase 7 S5); null unless authored.
  std::unique_ptr<ArcListOps> arc_edits;

  PrimMetadataCold();
  ~PrimMetadataCold();
  PrimMetadataCold(const PrimMetadataCold &o);
  PrimMetadataCold &operator=(const PrimMetadataCold &) = delete;
};

/// PrimSpec metadata. Hot fields (flags, the four composition-arc lists, the
/// legacy single variant selection, and the layer offset) are inline; cold
/// fields live in a lazily-allocated PrimMetadataCold. The cold-field accessors
/// keep the old names, so call sites change only by gaining `()`. Const reads
/// never allocate (return a shared empty); mutable access allocates the ext on
/// first touch. Copyable with a deep copy of the ext.
struct PrimMetadata {
  bool active = true;
  bool hidden = false;
  // Authored-ness for the two bools above: plain bools cannot distinguish
  // "authored true/false" from "default", and fill-absent composition needs
  // to know (a weaker spec's DEFAULT active=true must not clobber a stronger
  // authored active=false).
  bool active_authored = false;
  bool hidden_authored = false;
  bool loaded = true;  // runtime PCP state; not authored/serialized metadata
  bool instanceable_authored = false;  // instanceable=false is a real opinion
  bool instanceable = false;  // when true (with arcs), the prim is an instance

  // Composition arcs (stored as paths for lazy resolution).
  std::vector<std::string> references;
  std::vector<std::string> payloads;
  std::vector<std::string> inherits;
  std::vector<std::string> specializes;

  // Legacy single "variantSet=selection" (kept inline; the plural list of
  // selections lives in the ext).
  std::string variantSelection;

  // Layer offset (applied at evaluation time). First = offset, second = scale.
  std::pair<double, double> layer_offset = {0.0, 1.0};

  PrimMetadata();
  ~PrimMetadata();
  PrimMetadata(PrimMetadata&&) noexcept;
  PrimMetadata& operator=(PrimMetadata&&) noexcept;
  PrimMetadata(const PrimMetadata& other);
  PrimMetadata& operator=(const PrimMetadata& other);

  bool has_ext() const;
  void ensure_ext();
  const PrimMetadataCold *ext() const;

  // Arc list-op edits (Phase 7 S5). Const returns null when none authored (the
  // inline arc vectors are then implicit explicit lists); mutable allocates.
  const ArcListOps *arc_edits() const;
  /// Drop all arc list-op edits (used when composition consumes the arcs).
  void clear_arc_edits();
  ArcListOps &ensure_arc_edits();

  const std::string &doc() const;
  std::string &doc();
  bool doc_authored() const;
  void set_doc_authored(bool authored = true);
  const std::string &comment() const;
  std::string &comment();
  bool comment_authored() const;
  void set_comment_authored(bool authored = true);
  const std::string &kind() const;
  std::string &kind();
  bool kindAuthored() const;
  void setKindAuthored(bool authored = true);
  const std::string &permission() const;
  std::string &permission();
  const std::string &displayName() const;
  std::string &displayName();
  bool displayNameAuthored() const;
  void setDisplayNameAuthored(bool authored = true);
  const std::string &instance_prototype() const;
  std::string &instance_prototype();
  const std::vector<std::string> &apiSchemas() const;
  std::vector<std::string> &apiSchemas();
  bool apiSchemasAuthored() const;
  void setApiSchemasAuthored(bool authored = true);
  const StringListOps &apiSchemaEdits() const;
  StringListOps &apiSchemaEdits();
  const std::string &apiSchemasQualifier() const;
  std::string &apiSchemasQualifier();
  const std::vector<PrimVariantSetData> &variantSets() const;
  std::vector<PrimVariantSetData> &variantSets();
  const StringListOps &variantSetNameEdits() const;
  StringListOps &variantSetNameEdits();
  const StringListOps &clipSetEdits() const;
  StringListOps &clipSetEdits();
  bool primOrderAuthored() const;
  void setPrimOrderAuthored(bool authored = true);
  bool propertyOrderAuthored() const;
  void setPropertyOrderAuthored(bool authored = true);
  const std::vector<std::pair<std::string, std::string>> &variantSelections()
      const;
  std::vector<std::pair<std::string, std::string>> &variantSelections();
  bool variantSelectionsAuthored() const;
  void setVariantSelectionsAuthored(bool authored = true);
  const std::vector<std::pair<std::string, std::string>> &relocates() const;
  std::vector<std::pair<std::string, std::string>> &relocates();
  bool relocatesAuthored() const;
  void setRelocatesAuthored(bool authored = true);
  const Value &customData() const;
  Value &customData();
  bool customDataAuthored() const;
  void setCustomDataAuthored(bool authored = true);
  const Value &assetInfo() const;
  Value &assetInfo();
  bool assetInfoAuthored() const;
  void setAssetInfoAuthored(bool authored = true);
  const Value &sdrMetadata() const;
  Value &sdrMetadata();
  bool sdrMetadataAuthored() const;
  void setSdrMetadataAuthored(bool authored = true);
  const Value &clips() const;
  Value &clips();
  bool clipsAuthored() const;
  void setClipsAuthored(bool authored = true);
  const std::vector<PropNameId> &clipShadowedProps() const;
  std::vector<PropNameId> &clipShadowedProps();
  const std::vector<std::string> &primOrder() const;
  const std::vector<std::string> &displayGroupOrder() const;
  std::vector<std::string> &editDisplayGroupOrder();
  bool displayGroupOrderAuthored() const;
  void setDisplayGroupOrderAuthored(bool authored = true);
  std::vector<std::string> &editPrimOrder();
  const std::vector<std::string> &propertyOrder() const;
  std::vector<std::string> &editPropertyOrder();
  const std::vector<std::pair<std::string, std::string>> &unknownMeta() const;
  const std::vector<DecodedExtensionField> &unknownFields() const;
  std::vector<DecodedExtensionField> &unknownFields();
  std::vector<std::pair<std::string, std::string>> &unknownMeta();

 private:
  std::unique_ptr<PrimMetadataCold> ext_;
};

/// Per-property metadata (the `( ... )` block after an attribute/relationship).
/// Lazily allocated per property in PrimSpec::prop_metas_, so an ordinary
/// property pays nothing. `authored` records which fields were explicitly set so
/// the writer re-emits exactly the authored opinions (e.g. elementSize=1).
struct PropMeta {
  uint32_t authored = 0;
  enum : uint32_t {
    kInterpolation  = 1u << 0,  kElementSize   = 1u << 1,
    kColorSpace     = 1u << 2,  kDisplayName   = 1u << 3,
    kDisplayGroup   = 1u << 4,  kDoc           = 1u << 5,
    kHidden         = 1u << 6,  kRenderType    = 1u << 7,
    kConnectability = 1u << 8,  kOutputName    = 1u << 9,
    kBindMaterialAs = 1u << 10, kKind          = 1u << 11,
    kWeight         = 1u << 12, kUnauthoredIdx = 1u << 13,
    kAllowedTokens  = 1u << 14, kCustomData    = 1u << 15,
    kAssetInfo      = 1u << 16, kSdrMetadata   = 1u << 17,
    kUnknownMeta    = 1u << 18, kComment = 1u << 19,
    kPermission     = 1u << 20,
  };
  // token / string fields
  std::string interpolation;   // constant/uniform/varying/vertex/faceVarying
  std::string colorSpace;
  std::string renderType;
  std::string connectability;
  std::string outputName;
  std::string bindMaterialAs;
  std::string kind;
  std::string displayName;
  std::string displayGroup;
  std::string doc;
  std::string comment;
  std::string permission;  // "public"/"private" (pxr SdfPermission)
  // scalar fields
  int32_t elementSize = 1;
  int32_t unauthoredValuesIndex = -1;
  double  weight = 0.0;
  bool    hidden = false;
  // array / dict fields
  std::vector<std::string> allowedTokens;
  Value customData;
  Value assetInfo;
  Value sdrMetadata;
  // Unknown (unmodeled) property metadata preserved as raw source text,
  // authored order (key -> raw value text); the USDA writer re-emits it
  // verbatim so pipeline-specific opinions round-trip.
  std::vector<std::pair<std::string, std::string>> unknownMeta;
  std::vector<DecodedExtensionField> unknownFields;

  bool empty() const;
};

/// Value storage block
/// Stores property values contiguously for cache efficiency
