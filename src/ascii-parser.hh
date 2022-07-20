// SPDX-License-Identifier: MIT
// Copyright 2021 - Present, Syoyo Fujita.
//
// USD ASCII parser

#pragma once

#include <clocale>
#include <stack>
#include <functional>

#include "stream-reader.hh"
#include "tinyusdz.hh"

namespace tinyusdz {

namespace ascii {

enum LoadState {
  LOAD_STATE_TOPLEVEL,   // toplevel .usda input
  LOAD_STATE_SUBLAYER,   // .usda is read by 'subLayers'
  LOAD_STATE_REFERENCE,  // .usda is read by `references`
  LOAD_STATE_PAYLOAD,    // .usda is read by `payload`
};

///
/// Test if input file is USDA ascii format.
///
bool IsUSDA(const std::string &filename, size_t max_filesize = 0);



class AsciiParser {

 public:

  struct StageMetas {
    ///
    /// Predefined Stage metas
    ///
    std::vector<std::string> subLayers;
    std::string defaultPrim;
    std::string doc;
    nonstd::optional<Axis> upAxis; // not specified = nullopt
    nonstd::optional<double> metersPerUnit;
    nonstd::optional<double> timeCodesPerSecond;

    value::dict _customData; // `customData`(non-predefined Stage metas).
  };

  struct ParseState {
    int64_t loc{-1};  // byte location in StreamReder
  };

  struct Cursor {
    int row{0};
    int col{0};
  };

  struct ErrorDiagnositc {
    std::string err;
    Cursor cursor;
  };

  void PushError(const std::string &msg) {
    ErrorDiagnositc diag;
    diag.cursor.row = _curr_cursor.row;
    diag.cursor.col = _curr_cursor.col;
    diag.err = msg;
    err_stack.push(diag);
  }

  // This function is used to cancel recent parsing error.
  void PopError() {
    if (!err_stack.empty()) {
      err_stack.pop();
    }
  }

  void PushWarn(const std::string &msg) {
    ErrorDiagnositc diag;
    diag.cursor.row = _curr_cursor.row;
    diag.cursor.col = _curr_cursor.col;
    diag.err = msg;
    warn_stack.push(diag);
  }

  // This function is used to cancel recent parsing warning.
  void PopWarn() {
    if (!warn_stack.empty()) {
      warn_stack.pop();
    }
  }

  bool IsStageMeta(const std::string &name);
  bool IsPrimMeta(const std::string &name);

  class VariableDef {
   public:
    std::string type;
    std::string name;

    VariableDef() = default;

    VariableDef(const std::string &t, const std::string &n)
        : type(t), name(n) {}

    VariableDef(const VariableDef &rhs) = default;

    VariableDef &operator=(const VariableDef &rhs) {
      type = rhs.type;
      name = rhs.name;

      return *this;
    }
  };

  AsciiParser();
  AsciiParser(tinyusdz::StreamReader *sr);

  AsciiParser(const AsciiParser &rhs) = delete;
  AsciiParser(AsciiParser &&rhs) = delete;

  ~AsciiParser();

  ///
  /// Stage Meta construction callback function
  ///
  using StageMetaProcessFunction = std::function<bool(const StageMetas& metas)>;
  
  ///
  /// Register Stage metadatum processing callback function.
  /// Called when after parsing Stage metadatum.
  ///
  void RegisterStageMetaProcessFunction(StageMetaProcessFunction fun) {
    _stage_meta_process_fun = fun;
  }

  ///
  /// Prim construction callback function
  /// TODO: use std::function?
  ///
  using PrimConstructFunction = std::function<bool(const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references)>;

  ///
  /// Register Prim construction callback function.
  /// Example: "Xform", ReconstrctXform
  ///
  void RegisterPrimConstructFunction(const std::string &prim_type, PrimConstructFunction fun) {
    _prim_construct_fun_map[prim_type] = fun;
  }

  ///
  /// Base filesystem directory to search asset files.
  ///
  void SetBaseDir(const std::string &base_dir);

  ///
  /// Set ASCII data stream
  ///
  void SetStream(tinyusdz::StreamReader *sr);

  ///
  /// Check if header data is USDA
  ///
  bool CheckHeader();

  ///
  /// Parser entry point
  ///
  bool Parse(LoadState state = LOAD_STATE_TOPLEVEL);

  // TODO: ParseBasicType?
  bool ParsePurpose(Purpose *result);

  ///
  /// Return true but `value` is set to nullopt for `None`(Attribute Blocked)
  ///
  template <typename T>
  bool ReadBasicType(nonstd::optional<T> *value);

  template <typename T>
  bool ReadBasicType(T *value);

  template<typename T>
  bool ParseMatrix(T *result);

  ///
  /// Parse '(', Sep1By(','), ')'
  ///
  template <typename T, size_t N>
  bool ParseBasicTypeTuple(std::array<T, N> *result);

  ///
  /// Parse '(', Sep1By(','), ')'
  /// Can have `None`
  ///
  template <typename T, size_t N>
  bool ParseBasicTypeTuple(nonstd::optional<std::array<T, N>> *result);

  template <typename T, size_t N>
  bool ParseTupleArray(std::vector<std::array<T, N>> *result);

  ///
  /// Parse the array of tuple. some may be None(e.g. `float3`: [(0, 1, 2),
  /// None, (2, 3, 4), ...] )
  ///
  template <typename T, size_t N>
  bool ParseTupleArray(std::vector<nonstd::optional<std::array<T, N>>> *result);

  template <typename T>
  bool SepBy1BasicType(const char sep, std::vector<T> *result);

  ///
  /// Parse '[', Sep1By(','), ']'
  ///
  template <typename T>
  bool ParseBasicTypeArray(std::vector<nonstd::optional<T>> *result);

  ///
  /// Parse '[', Sep1By(','), ']'
  ///
  template <typename T>
  bool ParseBasicTypeArray(std::vector<T> *result);

  ///
  /// Parses 1 or more occurences of value with basic type 'T', separated by
  /// `sep`
  ///
  template <typename T>
  bool SepBy1BasicType(const char sep,
                       std::vector<nonstd::optional<T>> *result);

  ///
  /// Parses 1 or more occurences of tuple values with type 'T', separated by
  /// `sep`. Allows 'None'
  ///
  template <typename T, size_t N>
  bool SepBy1TupleType(const char sep,
                       std::vector<nonstd::optional<std::array<T, N>>> *result);

  ///
  /// Parses 1 or more occurences of tuple values with type 'T', separated by
  /// `sep`
  ///
  template <typename T, size_t N>
  bool SepBy1TupleType(const char sep, std::vector<std::array<T, N>> *result);

  bool ParseDictElement(std::string *out_key, MetaVariable *out_var);
  bool ParseDict(std::map<std::string, MetaVariable> *out_dict);

  bool MaybeListEditQual(tinyusdz::ListEditQual *qual);

#if 0
  ///
  ///
  ///
  std::string GetDefaultPrimName() const;

  ///
  /// Get parsed toplevel "def" nodes(GPrim)
  ///
  std::vector<GPrim> GetGPrims();
#endif

  ///
  /// Get error message(when `Parse` failed)
  ///
  std::string GetError();

  ///
  /// Get warning message(warnings in `Parse`)
  ///
  std::string GetWarning();

#if 0
  ///
  /// Get as scene
  ///
  const HighLevelScene& GetHighLevelScene() const;
#endif

  // Return the flag if the .usda is read from `references`
  bool IsReferenced() { return _referenced; }

  // Return the flag if the .usda is read from `subLayers`
  bool IsSubLayered() { return _sub_layered; }

  // Return the flag if the .usda is read from `payload`
  bool IsPayloaded() { return _payloaded; }

  // Return true if the .udsa is read in the top layer(stage)
  bool IsToplevel() {
    return !IsReferenced() && !IsSubLayered() && !IsPayloaded();
  }

  bool MaybeNone();
  bool MaybeCustom();

  template <typename T>
  bool MaybeNonFinite(T *out);

  bool LexFloat(std::string *result);

  bool Expect(char expect_c);

  bool ReadStringLiteral(std::string *literal);  // identifier wrapped with '"'
  bool ReadPrimAttrIdentifier(std::string *token);
  bool ReadIdentifier(std::string *token);  // no '"'
  bool ReadPathIdentifier(
      std::string *path_identifier);  // '<' + identifier + '>'

  /// Parse magic
  /// #usda FLOAT
  bool ParseMagicHeader();

  bool SkipWhitespace();
  bool SkipWhitespaceAndNewline();
  bool SkipCommentAndWhitespaceAndNewline();
  bool SkipUntilNewline();

  // bool ParseAttributeMeta();
  bool ParseAttrMeta(AttrMeta *out_meta);

  bool ParsePrimMetas(
      std::map<std::string, std::tuple<ListEditQual, MetaVariable>> *args);

  bool ParseMetaValue(const std::string &vartype, const std::string &varname,
                      MetaVariable *outvar);

  bool ParseStageMetaOpt();
  // Parsed Stage metadatum is stored in this instance.
  bool ParseStageMetas();

  bool ParseCustomMetaValue();

  // TODO: Return Path
  bool ParseReference(Reference *out, bool *triple_deliminated);

  // `#` style comment
  bool ParseSharpComment();

  bool IsSupportedPrimAttrType(const std::string &ty);
  bool IsSupportedPrimType(const std::string &ty);

  bool Eof() { return _sr->eof(); }

  bool ParseRel(Rel *result);
  bool ParseProperty(std::map<std::string, Property> *props);


  //
  // Look***() : Fetch chars but do not change input stream position.
  //

  bool LookChar1(char *c);
  bool LookCharN(size_t n, std::vector<char> *nc);

  bool Char1(char *c);
  bool CharN(size_t n, std::vector<char> *nc);

  bool Rewind(size_t offset);
  uint64_t CurrLoc();
  bool SeekTo(size_t pos);

  bool PushParserState();
  bool PopParserState(ParseState *state);

  //
  // Valid after ParseStageMetas() --------------
  //
  StageMetas GetStageMetas() const {
    return _stage_metas;
  }

  bool ParseClassBlock();
  bool ParseOverBlock();
  bool ParseDefBlock(uint32_t nestlevel = 0);

  // --------------------------------------------

 private:

  ///
  /// Do common setups. Assume called in ctor.
  ///
  void Setup();

  //template<typename T>
  //bool ParseTimeSampleData(nonstd::optional<T> *out_value);
  
  template<typename T>
  using TimeSampleData = std::vector<std::pair<double, nonstd::optional<T>>>;

  ///
  /// Convert TimeSampleData<T> to TimeSamples(type-erased TimeSample Sdata struct)
  ///
  template<typename T>
  value::TimeSamples ConvertToTimeSamples(
      const TimeSampleData<T> &in);

  template <typename T>
  nonstd::optional<TimeSampleData<T>> TryParseTimeSamples() {
    // timeSamples = '{' (int : T), + '}'

    TimeSampleData<T> data;

    if (!Expect('{')) {
      return nonstd::nullopt;
    }

    if (!SkipWhitespaceAndNewline()) {
      return nonstd::nullopt;
    }

    while (!Eof()) {
      char c;
      if (!Char1(&c)) {
        return nonstd::nullopt;
      }

      if (c == '}') {
        break;
      }

      Rewind(1);

      double timeVal;
      // -inf, inf and nan are handled.
      if (!ReadBasicType(&timeVal)) {
        PushError("Parse time value failed.");
        return nonstd::nullopt;
      }

      if (!SkipWhitespace()) {
        return nonstd::nullopt;
      }

      if (!Expect(':')) {
        return nonstd::nullopt;
      }

      if (!SkipWhitespace()) {
        return nonstd::nullopt;
      }

      nonstd::optional<T> value;
      if (!ReadBasicType(&value)) {
        return nonstd::nullopt;
      }

      // It looks the last item also requires ','
      if (!Expect(',')) {
        return nonstd::nullopt;
      }

      if (!SkipWhitespaceAndNewline()) {
        return nonstd::nullopt;
      }

      data.push_back({timeVal, value});
    }

    return data;
  }


  nonstd::optional<std::tuple<ListEditQual, MetaVariable>> ParsePrimMeta();
  bool ParsePrimAttr(std::map<std::string, Property> *props);

  template <typename T>
  bool ParseBasicPrimAttr(bool array_qual,
                                       const std::string &primattr_name,
                                       PrimAttrib *out_attr);


  bool ParseStageMeta(std::tuple<ListEditQual, MetaVariable> *out);
  nonstd::optional<VariableDef> GetStageMetaDefinition(const std::string &name);

  std::string GetCurrentPath();
  bool PathStackDepth() { return _path_stack.size(); }
  void PushPath(const std::string &p) { _path_stack.push(p); }
  void PopPath() {
    if (!_path_stack.empty()) {
      _path_stack.pop();
    }
  }

  const tinyusdz::StreamReader *_sr = nullptr;

  nonstd::optional<VariableDef> GetPrimMeta(const std::string &arg);

  // "class" defs
  std::map<std::string, Klass> _klasses;
  std::stack<std::string> _path_stack;

#if 0
  // Cache of loaded `references`
  // <filename, {defaultPrim index, list of root nodes in referenced usd file}>
  std::map<std::string, std::pair<uint32_t, std::vector<GPrim>>>
        _reference_cache;

    // toplevel "def" defs
    std::vector<GPrim> _gprims;
#endif

  Cursor _curr_cursor;

  // Supported Prim types
  std::set<std::string> _supported_prim_types;
  std::set<std::string> _supported_prim_attr_types;

  // Supported metadataum for Stage
  std::map<std::string, VariableDef> _supported_stage_metas;

  // Supported metadataum for Prim.
  std::map<std::string, VariableDef> _supported_prim_metas;

  std::stack<ErrorDiagnositc> err_stack;
  std::stack<ErrorDiagnositc> warn_stack;
  std::stack<ParseState> parse_stack;

  float _version{1.0f};

  // load flags
  bool _sub_layered{false};
  bool _referenced{false};
  bool _payloaded{false};

  std::string _base_dir;

  StageMetas _stage_metas;
  
  //
  // Callbacks
  //
  StageMetaProcessFunction _stage_meta_process_fun;
  std::map<std::string, PrimConstructFunction> _prim_construct_fun_map;

  // class Impl;
  // Impl *_impl;
};

}  // namespace ascii

}  // namespace tinyusdz
