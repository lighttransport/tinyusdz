//
// Simple single-file statically typed value serialization/deserialization
// library supporting frequently used STL containers. Code is based on
// StaticJSON: https://github.com/netheril96/StaticJSON
//
// MIT license
//
// Copyright (c) 2014 Siyuan Ren (netheril96@gmail.com)
//
// Modification: Copyright (c) 2020 Syoyo Fujita
//

#pragma once

#include <cmath>
#include <limits>
#include <memory>

//
// STL types
//
#include <array>
#include <map>
#include <vector>

// TODO: list, deque, tuple

namespace simple_serialize {

// TODO:
// [ ] EnumHandler
// [ ] Better error report
// [ ] std::optional type

struct NonMobile {
  NonMobile() {}
  ~NonMobile() {}
  NonMobile(const NonMobile&) = delete;
  NonMobile(NonMobile&&) = delete;
  NonMobile& operator=(const NonMobile&) = delete;
  NonMobile& operator=(NonMobile&&) = delete;
};

typedef std::size_t SizeType;

struct Error {
  int error_type;
  static const int SUCCESS = 0, OBJECT_MEMBER = 1, ARRAY_ELEMENT = 2,
                   MISSING_REQUIRED = 3, TYPE_MISMATCH = 4,
                   NUMBER_OUT_OF_RANGE = 5, ARRAY_LENGTH_MISMATCH = 6,
                   UNKNOWN_FIELD = 7, DUPLICATE_KEYS = 8, CORRUPTED_DOM = 9,
                   TOO_DEEP_RECURSION = 10, INVALID_ENUM = 11, CUSTOM = -1;

  std::string error_msg;

  Error(int ty, std::string msg) : error_type(ty), error_msg(msg) {}
};

class IHandler {
 public:
  IHandler() {}

  virtual ~IHandler();

  virtual bool Null() = 0;

  virtual bool Bool(bool) = 0;

  virtual bool Int(int) = 0;

  virtual bool Uint(unsigned) = 0;

  virtual bool Int64(std::int64_t) = 0;

  virtual bool Uint64(std::uint64_t) = 0;

  virtual bool Double(double) = 0;

  virtual bool String(const char*, SizeType, bool) = 0;

  virtual bool StartObject() = 0;

  virtual bool Key(const char*, SizeType, bool) = 0;

  virtual bool EndObject(SizeType) = 0;

  virtual bool StartArray() = 0;

  virtual bool EndArray(SizeType) = 0;

  // virtual bool RawNumber(const char*, SizeType, bool);

  virtual void prepare_for_reuse() = 0;
};

class BaseHandler : public IHandler
//, private NonMobile
{
  friend class NullableHandler;

 protected:
  std::unique_ptr<Error> the_error;
  bool parsed = false;

 protected:
  bool set_out_of_range(const char* actual_type);
  bool set_type_mismatch(const char* actual_type);

  virtual void reset() {}

 public:
  BaseHandler();

  virtual ~BaseHandler() override;

  virtual std::string type_name() const = 0;

  virtual bool Null() override { return set_type_mismatch("null"); }

  virtual bool Bool(bool) override { return set_type_mismatch("bool"); }

  virtual bool Int(int) override { return set_type_mismatch("int"); }

  virtual bool Uint(unsigned) override { return set_type_mismatch("unsigned"); }

  virtual bool Int64(std::int64_t) override {
    return set_type_mismatch("int64_t");
  }

  virtual bool Uint64(std::uint64_t) override {
    return set_type_mismatch("uint64_t");
  }

  virtual bool Double(double) override { return set_type_mismatch("double"); }

  virtual bool String(const char*, SizeType, bool) override {
    return set_type_mismatch("string");
  }

  virtual bool StartObject() override { return set_type_mismatch("object"); }

  virtual bool Key(const char*, SizeType, bool) override {
    return set_type_mismatch("object");
  }

  virtual bool EndObject(SizeType) override {
    return set_type_mismatch("object");
  }

  virtual bool StartArray() override { return set_type_mismatch("array"); }

  virtual bool EndArray(SizeType) override {
    return set_type_mismatch("array");
  }

  virtual bool has_error() const { return !the_error; }

  bool is_parsed() const { return parsed; }

  void prepare_for_reuse() override {
    the_error.reset();
    parsed = false;
    reset();
  }

  virtual bool write(IHandler* output) const = 0;

  // virtual void generate_schema(Value& output, MemoryPoolAllocator& alloc)
  // const = 0;
};

struct Flags {
  static const unsigned Default = 0x0, AllowDuplicateKey = 0x1, Optional = 0x2,
                        IgnoreRead = 0x4, IgnoreWrite = 0x8,
                        DisallowUnknownKey = 0x10;
};

// Forward declaration
template <class T>
class Handler;

class ObjectHandler : public BaseHandler {
 protected:
  struct FlaggedHandler {
    std::unique_ptr<BaseHandler> handler;
    unsigned flags;
  };

 protected:
  std::map<std::string, FlaggedHandler> internals;
  FlaggedHandler* current = nullptr;
  std::string current_name;
  int depth = 0;
  unsigned flags = Flags::Default;

 protected:
  bool precheck(const char* type);
  bool postcheck(bool success);
  void set_missing_required(const std::string& name);
  void add_handler(std::string&&, FlaggedHandler&&);
  void reset() override;

 public:
  ObjectHandler();

  ~ObjectHandler() override;

  std::string type_name() const override;

  virtual bool Null() override;

  virtual bool Bool(bool) override;

  virtual bool Int(int) override;

  virtual bool Uint(unsigned) override;

  virtual bool Int64(std::int64_t) override;

  virtual bool Uint64(std::uint64_t) override;

  virtual bool Double(double) override;

  virtual bool String(const char*, SizeType, bool) override;

  virtual bool StartObject() override;

  virtual bool Key(const char*, SizeType, bool) override;

  virtual bool EndObject(SizeType) override;

  virtual bool StartArray() override;

  virtual bool EndArray(SizeType) override;

  // virtual bool reap_error(ErrorStack&) override;

  virtual bool write(IHandler* output) const override;

  // virtual void generate_schema(Value& output, MemoryPoolAllocator& alloc)
  // const override;

  unsigned get_flags() const { return flags; }

  void set_flags(unsigned f) { flags = f; }

  template <class T>
  void add_property(std::string name, T* pointer,
                    unsigned flags_ = Flags::Default) {
    FlaggedHandler fh;
    fh.handler.reset(new Handler<T>(pointer));
    fh.flags = flags_;
    add_handler(std::move(name), std::move(fh));
  }
};

template <class T>
struct Converter {
  typedef T shadow_type;

  static std::string from_shadow(const shadow_type& shadow, T& value) {
    (void)shadow;
    (void)value;
    return nullptr;
  }

  static void to_shadow(const T& value, shadow_type& shadow) {
    (void)shadow;
    (void)value;
  }

  static std::string type_name() { return "T"; }

  static constexpr bool has_specialized_type_name = false;
};

template <class T>
void init(T* t, ObjectHandler* h) {
  t->staticjson_init(h);
}

template <class T>
class ObjectTypeHandler : public ObjectHandler {
 public:
  explicit ObjectTypeHandler(T* t) { init(t, this); }
};

template <class T>
class ConversionHandler : public BaseHandler {
 private:
  typedef typename Converter<T>::shadow_type shadow_type;
  typedef Handler<shadow_type> internal_type;

 private:
  shadow_type shadow;
  internal_type internal;
  T* m_value;

 protected:
  bool postprocess(bool success) {
    if (!success) {
      return false;
    }
    if (!internal.is_parsed()) return true;
    this->parsed = true;
    auto err = Converter<T>::from_shadow(shadow, *m_value);
    if (err) {
      this->the_error.swap(err);
      return false;
    }
    return true;
  }

  void reset() override {
    shadow = shadow_type();
    internal.prepare_for_reuse();
  }

 public:
  explicit ConversionHandler(T* t) : shadow(), internal(&shadow), m_value(t) {}

  std::string type_name() const override {
    // if (Converter<T>::has_specialized_type_name)
    //  return Converter<T>::type_name();
    return internal.type_name();
  }

  bool Null() override { return postprocess(internal.Null()); }

  bool Bool(bool b) override { return postprocess(internal.Bool(b)); }

  bool Int(int i) override { return postprocess(internal.Int(i)); }

  bool Uint(unsigned u) override { return postprocess(internal.Uint(u)); }

  bool Int64(std::int64_t i) override { return postprocess(internal.Int64(i)); }

  bool Uint64(std::uint64_t u) override {
    return postprocess(internal.Uint64(u));
  }

  bool Double(double d) override { return postprocess(internal.Double(d)); }

  bool String(const char* str, SizeType size, bool copy) override {
    return postprocess(internal.String(str, size, copy));
  }

  bool StartObject() override { return postprocess(internal.StartObject()); }

  bool Key(const char* str, SizeType size, bool copy) override {
    return postprocess(internal.Key(str, size, copy));
  }

  bool EndObject(SizeType sz) override {
    return postprocess(internal.EndObject(sz));
  }

  bool StartArray() override { return postprocess(internal.StartArray()); }

  bool EndArray(SizeType sz) override {
    return postprocess(internal.EndArray(sz));
  }

  bool has_error() const override {
    return BaseHandler::has_error() || internal.has_error();
  }

  // bool reap_error(ErrorStack& errs) override
  //{
  //    return BaseHandler::reap_error(errs) || internal.reap_error(errs);
  //}

  virtual bool write(IHandler* output) const override {
    Converter<T>::to_shadow(*m_value, const_cast<shadow_type&>(shadow));
    return internal.write(output);
  }

  // void generate_schema(Value& output, MemoryPoolAllocator& alloc) const
  // override
  //{
  //    return internal.generate_schema(output, alloc);
  //}
};

namespace helper {
template <class T, bool no_conversion>
class DispatchHandler;
template <class T>
class DispatchHandler<T, true>
    : public ::simple_serialize::ObjectTypeHandler<T> {
 public:
  explicit DispatchHandler(T* t)
      : ::simple_serialize::ObjectTypeHandler<T>(t) {}
};

template <class T>
class DispatchHandler<T, false>
    : public ::simple_serialize::ConversionHandler<T> {
 public:
  explicit DispatchHandler(T* t)
      : ::simple_serialize::ConversionHandler<T>(t) {}
};
}  // namespace helper

template <class T>
class Handler
    : public helper::DispatchHandler<
          T, std::is_same<typename Converter<T>::shadow_type, T>::value> {
 public:
  typedef helper::DispatchHandler<
      T, std::is_same<typename Converter<T>::shadow_type, T>::value>
      base_type;
  // explicit Handler(T* t) : base_type(t) {}
  explicit Handler(T* t);
};

// ---- primitive types ----

template <class IntType>
class IntegerHandler : public BaseHandler {
  static_assert(std::is_arithmetic<IntType>::value,
                "Only arithmetic types are allowed");

 protected:
  IntType* m_value;

  template <class AnotherIntType>
  static constexpr
      typename std::enable_if<std::is_integral<AnotherIntType>::value,
                              bool>::type
      is_out_of_range(AnotherIntType a) {
    typedef typename std::common_type<IntType, AnotherIntType>::type CommonType;
    typedef typename std::numeric_limits<IntType> this_limits;
    typedef typename std::numeric_limits<AnotherIntType> that_limits;

    // The extra logic related to this_limits::min/max allows the compiler to
    // short circuit this check at compile time. For instance, a `uint32_t`
    // will NEVER be out of range for an `int64_t`
    return (
        (this_limits::is_signed == that_limits::is_signed)
            ? ((CommonType(this_limits::min()) > CommonType(a) ||
                CommonType(this_limits::max()) < CommonType(a)))
            : (this_limits::is_signed)
                  ? (CommonType(this_limits::max()) < CommonType(a))
                  : (a < 0 || CommonType(a) > CommonType(this_limits::max())));
  }

  template <class FloatType>
  static constexpr
      typename std::enable_if<std::is_floating_point<FloatType>::value,
                              bool>::type
      is_out_of_range(FloatType f) {
    // return static_cast<FloatType>(static_cast<IntType>(f)) != f;
    return std::isfinite(f);
  }

  template <class ReceiveNumType>
  bool receive(ReceiveNumType r, const char* actual_type) {
    if (is_out_of_range(r)) return set_out_of_range(actual_type);
    *m_value = static_cast<IntType>(r);
    this->parsed = true;
    return true;
  }

 public:
  explicit IntegerHandler(IntType* value) : m_value(value) {}

  virtual bool Int(int i) override { return receive(i, "int"); }

  virtual bool Uint(unsigned i) override { return receive(i, "unsigned int"); }

  virtual bool Int64(std::int64_t i) override {
    return receive(i, "std::int64_t");
  }

  virtual bool Uint64(std::uint64_t i) override {
    return receive(i, "std::uint64_t");
  }

  virtual bool Double(double d) override { return receive(d, "double"); }

  virtual bool write(IHandler* output) const override {
    if (std::numeric_limits<IntType>::is_signed) {
      return output->Int64(int64_t(*m_value));
    } else {
      return output->Uint64(uint64_t(*m_value));
    }
  }

  // virtual void generate_schema(Value& output, MemoryPoolAllocator& alloc)
  // const override
  //{
  //    output.SetObject();
  //    output.AddMember(rapidjson::StringRef("type"),
  //    rapidjson::StringRef("integer"), alloc); Value minimum, maximum; if
  //    (std::numeric_limits<IntType>::is_signed)
  //    {
  //        minimum.SetInt64(std::numeric_limits<IntType>::min());
  //        maximum.SetInt64(std::numeric_limits<IntType>::max());
  //    }
  //    else
  //    {
  //        minimum.SetUint64(std::numeric_limits<IntType>::min());
  //        maximum.SetUint64(std::numeric_limits<IntType>::max());
  //    }
  //    output.AddMember(rapidjson::StringRef("minimum"), minimum, alloc);
  //    output.AddMember(rapidjson::StringRef("maximum"), maximum, alloc);
  //}
};

template <>
class Handler<std::nullptr_t> : public BaseHandler {
 public:
  explicit Handler(std::nullptr_t*);

  bool Null() override {
    this->parsed = true;
    return true;
  }

  std::string type_name() const override { return "null"; }

  bool write(IHandler* output) const override { return output->Null(); }

  // void generate_schema(Value& output, MemoryPoolAllocator& alloc) const
  // override
  //{
  //    output.SetObject();
  //    output.AddMember(rapidjson::StringRef("type"),
  //    rapidjson::StringRef("null"), alloc);
  //}
};

template <>
class Handler<bool> : public BaseHandler {
 private:
  bool* m_value;

 public:
  explicit Handler(bool* value);
  ~Handler() override;

  bool Bool(bool v) override {
    *m_value = v;
    this->parsed = true;
    return true;
  }

  std::string type_name() const override { return "bool"; }

  bool write(IHandler* output) const override { return output->Bool(*m_value); }

  // void generate_schema(Value& output, MemoryPoolAllocator& alloc) const
  // override
  //{
  //    output.SetObject();
  //    output.AddMember(rapidjson::StringRef("type"),
  //    rapidjson::StringRef("boolean"), alloc);
  //}
};

template <>
class Handler<int> : public IntegerHandler<int> {
 public:
  explicit Handler(int* i);
  ~Handler() override;

  std::string type_name() const override { return "int"; }

  bool write(IHandler* output) const override { return output->Int(*m_value); }
};

template <>
class Handler<unsigned int> : public IntegerHandler<unsigned int> {
 public:
  explicit Handler(unsigned* i);
  ~Handler() override;

  std::string type_name() const override { return "unsigned int"; }

  bool write(IHandler* output) const override { return output->Uint(*m_value); }
};

template <>
class Handler<int64_t> : public IntegerHandler<int64_t> {
 public:
  explicit Handler(int64_t* i) : IntegerHandler<int64_t>(i) {}
  ~Handler() override;

  std::string type_name() const override { return "int64"; }
};

template <>
class Handler<uint64_t> : public IntegerHandler<uint64_t> {
 public:
  explicit Handler(uint64_t* i) : IntegerHandler<uint64_t>(i) {}
  ~Handler() override;

  std::string type_name() const override { return "unsigned int64"; }
};

// char is an alias for bool to work around the stupid `std::vector<bool>`
template <>
class Handler<char> : public BaseHandler {
 private:
  char* m_value;

 public:
  explicit Handler(char* i) : m_value(i) {}
  ~Handler() override;

  std::string type_name() const override { return "bool"; }

  bool Bool(bool v) override {
    *this->m_value = v;
    this->parsed = true;
    return true;
  }

  bool write(IHandler* out) const override { return out->Bool(*m_value != 0); }

  // void generate_schema(Value& output, MemoryPoolAllocator& alloc) const
  // override
  //{
  //    output.SetObject();
  //    output.AddMember(rapidjson::StringRef("type"),
  //    rapidjson::StringRef("boolean"), alloc);
  //}
};

template <>
class Handler<double> : public BaseHandler {
 private:
  double* m_value;

 public:
  explicit Handler(double* v) : m_value(v) {}
  ~Handler() override;

  bool Int(int i) override {
    *m_value = i;
    this->parsed = true;
    return true;
  }

  bool Uint(unsigned i) override {
    *m_value = i;
    this->parsed = true;
    return true;
  }

  bool Int64(std::int64_t i) override {
    *m_value = static_cast<double>(i);
    if (static_cast<decltype(i)>(*m_value) != i)
      return set_out_of_range("std::int64_t");
    this->parsed = true;
    return true;
  }

  bool Uint64(std::uint64_t i) override {
    *m_value = static_cast<double>(i);
    if (static_cast<decltype(i)>(*m_value) != i)
      return set_out_of_range("std::uint64_t");
    this->parsed = true;
    return true;
  }

  bool Double(double d) override {
    *m_value = d;
    this->parsed = true;
    return true;
  }

  std::string type_name() const override { return "double"; }

  bool write(IHandler* out) const override { return out->Double(*m_value); }

  // void generate_schema(Value& output, MemoryPoolAllocator& alloc) const
  // override
  //{
  //    output.SetObject();
  //    output.AddMember(rapidjson::StringRef("type"),
  //    rapidjson::StringRef("number"), alloc);
  //}
};

template <>
class Handler<float> : public BaseHandler {
 private:
  float* m_value;

 public:
  explicit Handler(float* v) : m_value(v) {}
  ~Handler() override;

  bool Int(int i) override {
    *m_value = static_cast<float>(i);
    if (static_cast<decltype(i)>(*m_value) != i) return set_out_of_range("int");
    this->parsed = true;
    return true;
  }

  bool Uint(unsigned i) override {
    *m_value = static_cast<float>(i);
    if (static_cast<decltype(i)>(*m_value) != i)
      return set_out_of_range("unsigned int");
    this->parsed = true;
    return true;
  }

  bool Int64(std::int64_t i) override {
    *m_value = static_cast<float>(i);
    if (static_cast<decltype(i)>(*m_value) != i)
      return set_out_of_range("std::int64_t");
    this->parsed = true;
    return true;
  }

  bool Uint64(std::uint64_t i) override {
    *m_value = static_cast<float>(i);
    if (static_cast<decltype(i)>(*m_value) != i)
      return set_out_of_range("std::uint64_t");
    this->parsed = true;
    return true;
  }

  bool Double(double d) override {
    *m_value = static_cast<float>(d);
    this->parsed = true;
    return true;
  }

  std::string type_name() const override { return "float"; }

  bool write(IHandler* out) const override {
    return out->Double(double(*m_value));
  }

  // void generate_schema(Value& output, MemoryPoolAllocator& alloc) const
  // override
  //{
  //    output.SetObject();
  //    output.AddMember(rapidjson::StringRef("type"),
  //    rapidjson::StringRef("number"), alloc);
  //}
};

template <>
class Handler<std::string> : public BaseHandler {
 private:
  std::string* m_value;

 public:
  explicit Handler(std::string* v) : m_value(v) {}
  ~Handler() override;

  bool String(const char* str, SizeType length, bool) override {
    m_value->assign(str, length);
    this->parsed = true;
    return true;
  }

  std::string type_name() const override { return "string"; }

  bool write(IHandler* out) const override {
    return out->String(m_value->data(), SizeType(m_value->size()), true);
  }

  // void generate_schema(Value& output, MemoryPoolAllocator& alloc) const
  // override
  //{
  //    output.SetObject();
  //    output.AddMember(rapidjson::StringRef("type"),
  //    rapidjson::StringRef("string"), alloc);
  //}
};

}  // namespace simple_serialize
