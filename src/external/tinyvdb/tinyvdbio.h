//////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2018 Syoyo Fujita
// Copyright (c) 2012-2018 DreamWorks Animation LLC
//
// All rights reserved. This software is distributed under the
// Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
//
// Redistributions of source code must retain the above copyright
// and license notice and the following restrictions and disclaimer.
//
// *     Neither the name of Syoyo Fujita and DreamWorks Animation nor the names
// of its contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// IN NO EVENT SHALL THE COPYRIGHT HOLDERS' AND CONTRIBUTORS' AGGREGATE
// LIABILITY FOR ALL CLAIMS REGARDLESS OF THEIR BASIS EXCEED US$250.00.
//
///////////////////////////////////////////////////////////////////////////
#ifndef TINY_VDB_IO_H_
#define TINY_VDB_IO_H_

#include <array>
#include <bitset>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// tinyusdz patch: the upstream library printed copious `std::cout` debug traces
// on every load. They are routed through TINYVDB_LOG, which discards output
// unless TINYVDBIO_ENABLE_DEBUG_PRINT is defined. (`std::cout` was replaced with
// TINYVDB_LOG throughout the source.) Defined here, before first use.
#ifdef TINYVDBIO_ENABLE_DEBUG_PRINT
#define TINYVDB_LOG std::cout
#else
namespace tinyvdb {
struct TinyVDBNullStream {
  template <typename T>
  TinyVDBNullStream &operator<<(const T &) {
    return *this;
  }
  // Handle stream manipulators such as std::endl.
  TinyVDBNullStream &operator<<(std::ostream &(*)(std::ostream &)) {
    return *this;
  }
};
static TinyVDBNullStream g_tinyvdb_null_stream;
}  // namespace tinyvdb
#define TINYVDB_LOG ::tinyvdb::g_tinyvdb_null_stream
#endif

namespace tinyvdb {

#if __cplusplus > 199711L
// C++11
typedef uint64_t tinyvdb_uint64;
typedef int64_t tinyvdb_int64;
#else
// Although `long long` is not a standard type pre C++11, assume it is defined
// as a compiler's extension.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++11-long-long"
#endif
typedef unsigned long long tinyvdb_uint64;
typedef long long tinyvdb_int64;
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#endif

// For OpenVDB code compatibility
typedef unsigned int int32;
typedef tinyvdb_uint64 int64;


// TODO(syoyo): Move to IMPLEMENTATION
#define TINYVDBIO_ASSERT(x) assert(x)


#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#pragma clang diagnostic ignored "-Wc++11-long-long"
#endif

typedef struct {
  unsigned int file_version;
  unsigned int major_version;
  unsigned int minor_version;
  // bool has_grid_offsets;
  bool is_compressed;
  bool half_precision;
  std::string uuid;
  tinyvdb_uint64 offset_to_data;  // Byte offset to VDB data
} VDBHeader;

typedef struct {
} VDBMeta;

typedef enum {
  TINYVDBIO_SUCCESS,
  TINYVDBIO_ERROR_INVALID_FILE,
  TINYVDBIO_ERROR_INVALID_HEADER,
  TINYVDBIO_ERROR_INVALID_DATA,
  TINYVDBIO_ERROR_INVALID_ARGUMENT,
  TINYVDBIO_ERROR_UNIMPLEMENTED
} VDBStatus;

// forward decl.
class StreamReader;
class StreamWriter;
struct DeserializeParams;


/// Return the number of on bits in the given 8-bit value.
inline int32 CountOn(unsigned char v) {
// Simple LUT:
#ifndef _MSC_VER  // Visual C++ doesn't guarantee thread-safe initialization of
                  // local statics
  static
#endif
      /// @todo Move this table and others into, say, Util.cc
      const unsigned char numBits[256] = {
#define COUNTONB2(n) n, n + 1, n + 1, n + 2
#define COUNTONB4(n) \
  COUNTONB2(n), COUNTONB2(n + 1), COUNTONB2(n + 1), COUNTONB2(n + 2)
#define COUNTONB6(n) \
  COUNTONB4(n), COUNTONB4(n + 1), COUNTONB4(n + 1), COUNTONB4(n + 2)
          COUNTONB6(0), COUNTONB6(1), COUNTONB6(1), COUNTONB6(2)};
  return numBits[v];
#undef COUNTONB6
#undef COUNTONB4
#undef COUNTONB2

  // Sequentially clear least significant bits
  // int32 c;
  // for (c = 0; v; c++)  v &= v - 0x01U;
  // return c;

  // This version is only fast on CPUs with fast "%" and "*" operations
  // return (v * UINT64_C(0x200040008001) & UINT64_C(0x111111111111111)) % 0xF;
}

/// Return the number of off bits in the given 8-bit value.
inline int32 CountOff(unsigned char v) {
  return CountOn(static_cast<unsigned char>(~v));
}

/// Return the number of on bits in the given 32-bit value.
inline int32 CountOn(int32 v) {
  v = v - ((v >> 1) & 0x55555555U);
  v = (v & 0x33333333U) + ((v >> 2) & 0x33333333U);
  return (((v + (v >> 4)) & 0xF0F0F0FU) * 0x1010101U) >> 24;
}

/// Return the number of off bits in the given 32-bit value.
inline int32 CountOff(int32 v) { return CountOn(~v); }

/// Return the number of on bits in the given 64-bit value.
inline int32 CountOn(int64 v) {
  v = v - ((v >> 1) & 0x5555555555555555);
  v = (v & 0x3333333333333333) + ((v >> 2) & 0x3333333333333333);
  return static_cast<int32>(
      (((v + (v >> 4)) & 0xF0F0F0F0F0F0F0F) * 0x101010101010101) >> 56);
}

/// Return the number of off bits in the given 64-bit value.
inline int32 CountOff(int64 v) { return CountOn(~v); }

/// Return the least significant on bit of the given 8-bit value.
inline int32 FindLowestOn(unsigned char v) {
  TINYVDBIO_ASSERT(v);
#ifndef _MSC_VER  // Visual C++ doesn't guarantee thread-safe initialization of
                  // local statics
  static
#endif
      const unsigned char DeBruijn[8] = {0, 1, 6, 2, 7, 5, 4, 3};
  return DeBruijn[static_cast<unsigned char>((v & -v) * 0x1DU) >> 5];
}

/// Return the least significant on bit of the given 32-bit value.
inline int32 FindLowestOn(int32 v) {
  TINYVDBIO_ASSERT(v);
  // return ffs(v);
#ifndef _MSC_VER  // Visual C++ doesn't guarantee thread-safe initialization of
                  // local statics
  static
#endif
      const unsigned char DeBruijn[32] = {
          0,  1,  28, 2,  29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4,  8,
          31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6,  11, 5,  10, 9};
  return DeBruijn[int32((v & -v) * 0x077CB531U) >> 27];
}

/// Return the least significant on bit of the given 64-bit value.
inline int32 FindLowestOn(int64 v) {
  TINYVDBIO_ASSERT(v);
  // return ffsll(v);
#ifndef _MSC_VER  // Visual C++ doesn't guarantee thread-safe initialization of
                  // local statics
  static
#endif
      const unsigned char DeBruijn[64] = {
          0,  1,  2,  53, 3,  7,  54, 27, 4,  38, 41, 8,  34, 55, 48, 28,
          62, 5,  39, 46, 44, 42, 22, 9,  24, 35, 59, 56, 49, 18, 29, 11,
          63, 52, 6,  26, 37, 40, 33, 47, 61, 45, 43, 21, 23, 58, 17, 10,
          51, 25, 36, 32, 60, 20, 57, 16, 50, 31, 19, 15, 30, 14, 13, 12,
      };
  return DeBruijn[int64((v & -v) * 0x022FDD63CC95386D) >> 58];
}

/// Return the most significant on bit of the given 32-bit value.
inline int32 FindHighestOn(int32 v) {
#ifndef _MSC_VER  // Visual C++ doesn't guarantee thread-safe initialization of
                  // local statics
  static
#endif
      const unsigned char DeBruijn[32] = {
          0, 9,  1,  10, 13, 21, 2,  29, 11, 14, 16, 18, 22, 25, 3, 30,
          8, 12, 20, 28, 15, 17, 24, 7,  19, 27, 23, 6,  26, 5,  4, 31};
  v |= v >> 1;  // first round down to one less than a power of 2
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  return DeBruijn[int32(v * 0x07C4ACDDU) >> 27];
}

////////////////////////////////////////

/// internal Per-node indicator byte that specifies what additional metadata
/// is stored to permit reconstruction of inactive values
enum {
  /*0*/ NO_MASK_OR_INACTIVE_VALS,  // no inactive vals, or all inactive vals are
                                   // +background
  /*1*/ NO_MASK_AND_MINUS_BG,      // all inactive vals are -background
  /*2*/ NO_MASK_AND_ONE_INACTIVE_VAL,  // all inactive vals have the same
                                       // non-background val
  /*3*/ MASK_AND_NO_INACTIVE_VALS,     // mask selects between -background and
                                       // +background
  /*4*/ MASK_AND_ONE_INACTIVE_VAL,  // mask selects between backgd and one other
                                    // inactive val
  /*5*/ MASK_AND_TWO_INACTIVE_VALS,  // mask selects between two non-background
                                     // inactive vals
  /*6*/ NO_MASK_AND_ALL_VALS  // > 2 inactive vals, so no mask compression at
                              // all
};

// TODO(syoyo): remove

/// @brief Bit mask for the internal and leaf nodes of VDB. This
/// is a 64-bit implementation.
///
/// @note A template specialization for Log2Dim=1 and Log2Dim=2 are
/// given below.
// template <int Log2Dim>
class NodeMask {
 public:
  // static_assert(Log2Dim > 2, "expected NodeMask template specialization, got
  // base template");
  int32 LOG2DIM;
  int32 DIM;
  int32 SIZE;
  int32 WORD_COUNT;

  // static const int32 LOG2DIM = Log2Dim;
  // static const int32 DIM = 1 << Log2Dim;
  // static const int32 SIZE = 1 << 3 * Log2Dim;
  // static const int32 WORD_COUNT = SIZE >> 6;  // 2^6=64
  // using Word = int64;
  typedef int64 Word;

 private:
  // The bits are represented as a linear array of Words, and the
  // size of a Word is 32 or 64 bits depending on the platform.
  // The BIT_MASK is defined as the number of bits in a Word - 1
  // static const int32 BIT_MASK   = sizeof(void*) == 8 ? 63 : 31;
  // static const int32 LOG2WORD   = BIT_MASK == 63 ? 6 : 5;
  // static const int32 WORD_COUNT = SIZE >> LOG2WORD;
  // using Word = boost::mpl::if_c<BIT_MASK == 63, int64, int32>::type;

  std::vector<Word> mWords;  // only member data!

 public:

  NodeMask() {
    LOG2DIM = 0;
    DIM = 0;
    SIZE = 0;
    WORD_COUNT = 0;
  }

  void Alloc(int32 log2dim) {
    LOG2DIM = log2dim;
    DIM = 1 << log2dim;
    SIZE = 1 << 3 * log2dim;
    WORD_COUNT = SIZE >> 6;  // 2^6=64

    mWords.resize(WORD_COUNT);

    this->setOff();
  }

  /// Default constructor sets all bits off
  NodeMask(int32 log2dim) {
    LOG2DIM = log2dim;
    DIM = 1 << log2dim;
    SIZE = 1 << 3 * log2dim;
    WORD_COUNT = SIZE >> 6;  // 2^6=64

    mWords.resize(WORD_COUNT);

    this->setOff();
  }
  /// All bits are set to the specified state
  NodeMask(int32 log2dim, bool on) {
    LOG2DIM = log2dim;
    DIM = 1 << log2dim;
    SIZE = 1 << 3 * log2dim;
    WORD_COUNT = SIZE >> 6;  // 2^6=64

    this->set(on);
  }
  /// Copy constructor
  NodeMask(const NodeMask &other) { *this = other; }
  /// Destructor
  ~NodeMask() {}
  /// Assignment operator
  NodeMask &operator=(const NodeMask &other) {
    LOG2DIM = other.LOG2DIM;
    DIM = other.DIM;
    SIZE = other.SIZE;
    WORD_COUNT = other.WORD_COUNT;

    mWords = other.mWords;
    return *this;
    // int32 n = WORD_COUNT;
    // const Word* w2 = other.mWords;
    // for (Word *w1 = mWords; n--; ++w1, ++w2) *w1 = *w2;
  }

#if 0
    using OnIterator = OnMaskIterator<NodeMask>;
    using OffIterator = OffMaskIterator<NodeMask>;
    using DenseIterator = DenseMaskIterator<NodeMask>;

    OnIterator beginOn() const       { return OnIterator(this->findFirstOn(),this); }
    OnIterator endOn() const         { return OnIterator(SIZE,this); }
    OffIterator beginOff() const     { return OffIterator(this->findFirstOff(),this); }
    OffIterator endOff() const       { return OffIterator(SIZE,this); }
    DenseIterator beginDense() const { return DenseIterator(0,this); }
    DenseIterator endDense() const   { return DenseIterator(SIZE,this); }
#endif

  bool operator==(const NodeMask &other) const {
    int n = int(WORD_COUNT);
    for (const Word *w1 = mWords.data(), *w2 = other.mWords.data();
         n-- && *w1++ == *w2++;)
      ;
    return n == -1;
  }

  bool operator!=(const NodeMask &other) const { return !(*this == other); }

  //
  // Bitwise logical operations
  //

  /// @brief Apply a functor to the words of the this and the other mask.
  ///
  /// @details An example that implements the "operator&=" method:
  /// @code
  /// struct Op { inline void operator()(W &w1, const W& w2) const { w1 &= w2; }
  /// };
  /// @endcode
  template <typename WordOp>
  const NodeMask &foreach (const NodeMask &other, const WordOp &op) {
    Word *w1 = mWords.data();
    const Word *w2 = other.mWords.data();
    for (int32 n = WORD_COUNT; n--; ++w1, ++w2) op(*w1, *w2);
    return *this;
  }
  template <typename WordOp>
  const NodeMask &foreach (const NodeMask &other1, const NodeMask &other2,
                           const WordOp &op) {
    Word *w1 = mWords.data();
    const Word *w2 = other1.mWords.data(), *w3 = other2.mWords.data();
    for (int32 n = WORD_COUNT; n--; ++w1, ++w2, ++w3) op(*w1, *w2, *w3);
    return *this;
  }
  template <typename WordOp>
  const NodeMask &foreach (const NodeMask &other1, const NodeMask &other2,
                           const NodeMask &other3, const WordOp &op) {
    Word *w1 = mWords.data();
    const Word *w2 = other1.mWords.data(), *w3 = other2.mWords.data(),
               *w4 = other3.mWords.data();
    for (int32 n = WORD_COUNT; n--; ++w1, ++w2, ++w3, ++w4)
      op(*w1, *w2, *w3, *w4);
    return *this;
  }
  /// @brief Bitwise intersection
  const NodeMask &operator&=(const NodeMask &other) {
    Word *w1 = mWords.data();
    const Word *w2 = other.mWords.data();
    for (int32 n = WORD_COUNT; n--; ++w1, ++w2) *w1 &= *w2;
    return *this;
  }
  /// @brief Bitwise union
  const NodeMask &operator|=(const NodeMask &other) {
    Word *w1 = mWords.data();
    const Word *w2 = other.mWords.data();
    for (int32 n = WORD_COUNT; n--; ++w1, ++w2) *w1 |= *w2;
    return *this;
  }
  /// @brief Bitwise difference
  const NodeMask &operator-=(const NodeMask &other) {
    Word *w1 = mWords.data();
    const Word *w2 = other.mWords.data();
    for (int32 n = WORD_COUNT; n--; ++w1, ++w2) *w1 &= ~*w2;
    return *this;
  }
  /// @brief Bitwise XOR
  const NodeMask &operator^=(const NodeMask &other) {
    Word *w1 = mWords.data();
    const Word *w2 = other.mWords.data();
    for (int32 n = WORD_COUNT; n--; ++w1, ++w2) *w1 ^= *w2;
    return *this;
  }
  NodeMask operator!() const {
    NodeMask m(*this);
    m.toggle();
    return m;
  }
  NodeMask operator&(const NodeMask &other) const {
    NodeMask m(*this);

    m &= other;
    return m;
  }
  NodeMask operator|(const NodeMask &other) const {
    NodeMask m(*this);
    m |= other;
    return m;
  }
  NodeMask operator^(const NodeMask &other) const {
    NodeMask m(*this);
    m ^= other;
    return m;
  }

  /// Return the byte size of this NodeMask
  int32 memUsage() const {
    return static_cast<int32>(WORD_COUNT * sizeof(Word));
  }
  /// Return the total number of on bits
  int32 countOn() const {
    int32 sum = 0, n = WORD_COUNT;
    TINYVDB_LOG << "cnt = " << n << ", sz = " << mWords.size() << std::endl;
    for (const Word *w = mWords.data(); n--; ++w) sum += CountOn(*w);
    return sum;
  }
  /// Return the total number of on bits
  int32 countOff() const { return SIZE - this->countOn(); }
  /// Set the <i>n</i>th  bit on
  void setOn(int32 n) {
    TINYVDBIO_ASSERT((n >> 6) < WORD_COUNT);
    mWords[n >> 6] |= Word(1) << (n & 63);
  }
  /// Set the <i>n</i>th bit off
  void setOff(int32 n) {
    TINYVDBIO_ASSERT((n >> 6) < WORD_COUNT);
    mWords[n >> 6] &= ~(Word(1) << (n & 63));
  }
  /// Set the <i>n</i>th bit to the specified state
  void set(int32 n, bool On) { On ? this->setOn(n) : this->setOff(n); }
  /// Set all bits to the specified state
  void set(bool on) {
    const Word state = on ? ~Word(0) : Word(0);
    int32 n = WORD_COUNT;
    for (Word *w = mWords.data(); n--; ++w) *w = state;
  }
  /// Set all bits on
  void setOn() {
    int32 n = WORD_COUNT;
    for (Word *w = mWords.data(); n--; ++w) *w = ~Word(0);
  }
  /// Set all bits off
  void setOff() {
    int32 n = WORD_COUNT;
    for (Word *w = mWords.data(); n--; ++w) *w = Word(0);
  }
  /// Toggle the state of the <i>n</i>th bit
  void toggle(int32 n) {
    TINYVDBIO_ASSERT((n >> 6) < WORD_COUNT);
    mWords[n >> 6] ^= Word(1) << (n & 63);
  }
  /// Toggle the state of all bits in the mask
  void toggle() {
    int32 n = WORD_COUNT;
    for (Word *w = mWords.data(); n--; ++w) *w = ~*w;
  }
  /// Set the first bit on
  void setFirstOn() { this->setOn(0); }
  /// Set the last bit on
  void setLastOn() { this->setOn(SIZE - 1); }
  /// Set the first bit off
  void setFirstOff() { this->setOff(0); }
  /// Set the last bit off
  void setLastOff() { this->setOff(SIZE - 1); }
  /// Return @c true if the <i>n</i>th bit is on
  bool isOn(int32 n) const {
    TINYVDBIO_ASSERT((n >> 6) < WORD_COUNT);
    return 0 != (mWords[n >> 6] & (Word(1) << (n & 63)));
  }
  /// Return @c true if the <i>n</i>th bit is off
  bool isOff(int32 n) const { return !this->isOn(n); }
  /// Return @c true if all the bits are on
  bool isOn() const {
    int n = int(WORD_COUNT);
    for (const Word *w = mWords.data(); n-- && *w++ == ~Word(0);)
      ;
    return n == -1;
  }
  /// Return @c true if all the bits are off
  bool isOff() const {
    int n = int(WORD_COUNT);
    for (const Word *w = mWords.data(); n-- && *w++ == Word(0);)
      ;
    return n == -1;
  }
  /// Return @c true if bits are either all off OR all on.
  /// @param isOn Takes on the values of all bits if the method
  /// returns true - else it is undefined.
  bool isConstant(bool &isOn) const {
    isOn = (mWords[0] == ~Word(0));  // first word has all bits on
    if (!isOn && mWords[0] != Word(0)) return false;  // early out
    const Word *w = mWords.data() + 1, *n = mWords.data() + WORD_COUNT;
    while (w < n && *w == mWords[0]) ++w;
    return w == n;
  }
  int32 findFirstOn() const {
    int32 n = 0;
    const Word *w = mWords.data();
    for (; n < WORD_COUNT && !*w; ++w, ++n)
      ;
    return n == WORD_COUNT ? SIZE : (n << 6) + FindLowestOn(*w);
  }
  int32 findFirstOff() const {
    int32 n = 0;
    const Word *w = mWords.data();
    for (; n < WORD_COUNT && !~*w; ++w, ++n)
      ;
    return n == WORD_COUNT ? SIZE : (n << 6) + FindLowestOn(~*w);
  }

  //@{
  /// Return the <i>n</i>th word of the bit mask, for a word of arbitrary size.
  template <typename WordT>
  WordT getWord(int n) const {
    TINYVDBIO_ASSERT(n * 8 * sizeof(WordT) < SIZE);
    return reinterpret_cast<const WordT *>(mWords)[n];
  }
  template <typename WordT>
  WordT &getWord(int n) {
    TINYVDBIO_ASSERT(n * 8 * sizeof(WordT) < SIZE);
    return reinterpret_cast<WordT *>(mWords)[n];
  }
  //@}

  void save(std::ostream &os) const {
    os.write(reinterpret_cast<const char *>(mWords.data()), this->memUsage());
  }
  bool load(StreamReader *sr);

  void seek(std::istream &is) const {
    is.seekg(this->memUsage(), std::ios_base::cur);
  }
  /// @brief simple print method for debugging
  void printInfo(std::ostream &os = std::cout) const {
    os << "NodeMask: Dim=" << DIM << " Log2Dim=" << LOG2DIM
       << " Bit count=" << SIZE << " word count=" << WORD_COUNT << std::endl;
  }
  void printBits(std::ostream &os = std::cout, int32 max_out = 80u) const {
    const int32 n = (SIZE > max_out ? max_out : SIZE);
    for (int32 i = 0; i < n; ++i) {
      if (!(i & 63))
        os << "||";
      else if (!(i % 8))
        os << "|";
      os << this->isOn(i);
    }
    os << "|" << std::endl;
  }
  void printAll(std::ostream &os = std::cout, int32 max_out = 80u) const {
    this->printInfo(os);
    this->printBits(os, max_out);
  }

  int32 findNextOn(int32 start) const {
    int32 n = start >> 6;              // initiate
    if (n >= WORD_COUNT) return SIZE;  // check for out of bounds
    int32 m = start & 63;
    Word b = mWords[n];
    if (b & (Word(1) << m)) return start;          // simpel case: start is on
    b &= ~Word(0) << m;                            // mask out lower bits
    while (!b && ++n < WORD_COUNT) b = mWords[n];  // find next none-zero word
    return (!b ? SIZE : (n << 6) + FindLowestOn(b));  // catch last word=0
  }

  int32 findNextOff(int32 start) const {
    int32 n = start >> 6;              // initiate
    if (n >= WORD_COUNT) return SIZE;  // check for out of bounds
    int32 m = start & 63;
    Word b = ~mWords[n];
    if (b & (Word(1) << m)) return start;           // simpel case: start is on
    b &= ~Word(0) << m;                             // mask out lower bits
    while (!b && ++n < WORD_COUNT) b = ~mWords[n];  // find next none-zero word
    return (!b ? SIZE : (n << 6) + FindLowestOn(b));  // catch last word=0
  }
};  // NodeMask

template <std::size_t N>
class BitMask {
 public:
  BitMask() {}
  ~BitMask() {}

  ///
  /// Loads bit mask value from stream reader.
  ///
  bool load(StreamReader *sr);

 private:
  std::bitset<N> mask_;
};

class GridDescriptor {
 public:
  GridDescriptor();
  GridDescriptor(const std::string &name, const std::string &grid_type,
                 bool save_float_as_half = false);
  // GridDescriptor(const GridDescriptor &rhs);
  // GridDescriptor& operator=(const GridDescriptor &rhs);
  //~GridDescriptor();

  const std::string &GridName() const { return grid_name_; }

  bool IsInstance() const { return !instance_parent_name_.empty(); }

  bool SaveFloatAsHalf() const { return save_float_as_half_; }

  tinyvdb_uint64 GridPos() const { return grid_pos_; }

  tinyvdb_uint64 BlockPos() const { return block_pos_; }

  tinyvdb_uint64 EndPos() const { return end_pos_; }

  const std::string &GridType() const { return grid_type_; }

  // Only dense FloatTree grids are decodable; other grid types (point-index,
  // vector, bool, ...) are parsed at the descriptor level but their data is
  // skipped rather than failing the whole file.
  bool IsSupported() const {
    return grid_type_.compare("Tree_float_5_4_3") == 0;
  }

  static std::string AddSuffix(const std::string &name, int n);
  static std::string StripSuffix(const std::string &name);

  ///
  /// Read GridDescriptor from a stream.
  ///
  bool Read(StreamReader *sr, const unsigned int file_version,
            std::string *err);

 private:
  std::string grid_name_;
  std::string unique_name_;
  std::string instance_parent_name_;
  std::string grid_type_;

  bool save_float_as_half_;  // use fp16?
  tinyvdb_uint64 grid_pos_;
  tinyvdb_uint64 block_pos_;
  tinyvdb_uint64 end_pos_;
};

typedef enum {
  NODE_TYPE_ROOT = 0,
  NODE_TYPE_INTERNAL = 1,
  NODE_TYPE_LEAF = 2,
  NODE_TYPE_INVALID = 3
} NodeType;

typedef enum {
  VALUE_TYPE_NULL = 0,
  VALUE_TYPE_FLOAT = 1,
  VALUE_TYPE_HALF = 2,
  VALUE_TYPE_BOOL = 3,
  VALUE_TYPE_DOUBLE = 4,
  VALUE_TYPE_INT = 5,
  VALUE_TYPE_STRING = 6
} ValueType;

static size_t GetValueTypeSize(const ValueType type) {
  if (type == VALUE_TYPE_FLOAT) {
    return sizeof(float);
  } else if (type == VALUE_TYPE_HALF) {
    return sizeof(short);
  } else if (type == VALUE_TYPE_BOOL) {
    return 1;
  } else if (type == VALUE_TYPE_DOUBLE) {
    return sizeof(double);
  } else if (type == VALUE_TYPE_STRING) {
    // string is not supported in this function.
    // Use Value::Size() instead.
    return 0;
  }
  return 0;
}

// Simple class to represent value object
class Value {
 public:
  Value() : type_(VALUE_TYPE_NULL) {}

  explicit Value(bool b) : type_(VALUE_TYPE_BOOL) { boolean_value_ = b; }
  explicit Value(float f) : type_(VALUE_TYPE_FLOAT) { float_value_ = f; }
  explicit Value(double d) : type_(VALUE_TYPE_DOUBLE) { double_value_ = d; }
  explicit Value(int n) : type_(VALUE_TYPE_INT) { int_value_ = n; }
  explicit Value(const std::string &str) : type_(VALUE_TYPE_STRING) {
    string_value_ = str;
  }

  ValueType Type() const { return type_; }

  bool IsBool() const { return (type_ == VALUE_TYPE_BOOL); }
  bool IsFloat() const { return (type_ == VALUE_TYPE_FLOAT); }
  bool IsDouble() const { return (type_ == VALUE_TYPE_DOUBLE); }
  bool IsInt() const { return (type_ == VALUE_TYPE_INT); }
  bool IsString() const { return (type_ == VALUE_TYPE_STRING); }

  // Accessor
  template <typename T>
  const T &Get() const;
  template <typename T>
  T &Get();

  size_t Size() const {
    size_t len = 0;
    switch (type_) {
      case VALUE_TYPE_BOOL:
        len = 1;
        break;
      case VALUE_TYPE_HALF:
        len = sizeof(short);
        break;
      case VALUE_TYPE_INT:
        len = sizeof(int);
        break;
      case VALUE_TYPE_FLOAT:
        len = sizeof(float);
        break;
      case VALUE_TYPE_DOUBLE:
        len = sizeof(double);
        break;
      case VALUE_TYPE_STRING:
        len = string_value_.size();
        break;
      case VALUE_TYPE_NULL:
        len = 0;
        break;
    }

    return len;
  }

 protected:
  ValueType type_;

  int int_value_;
  float float_value_;
  double double_value_;
  bool boolean_value_;
  std::string string_value_;
};

#define TINYVDB_VALUE_GET(ctype, var)             \
  template <>                                     \
  inline const ctype &Value::Get<ctype>() const { \
    return var;                                   \
  }                                               \
  template <>                                     \
  inline ctype &Value::Get<ctype>() {             \
    return var;                                   \
  }
TINYVDB_VALUE_GET(bool, boolean_value_)
TINYVDB_VALUE_GET(double, double_value_)
TINYVDB_VALUE_GET(int, int_value_)
TINYVDB_VALUE_GET(float, float_value_)
TINYVDB_VALUE_GET(std::string, string_value_)
#undef TINYVDB_VALUE_GET

static std::ostream &operator<<(std::ostream &os, const Value &value) {
  if (value.Type() == VALUE_TYPE_NULL) {
    os << "NULL";
  } else if (value.Type() == VALUE_TYPE_BOOL) {
    os << value.Get<bool>();
  } else if (value.Type() == VALUE_TYPE_FLOAT) {
    os << value.Get<float>();
  } else if (value.Type() == VALUE_TYPE_INT) {
    os << value.Get<int>();
  } else if (value.Type() == VALUE_TYPE_DOUBLE) {
    os << value.Get<double>();
  }

  return os;
}

static Value Negate(const Value &value) {
  if (value.Type() == VALUE_TYPE_NULL) {
    return value;
  } else if (value.Type() == VALUE_TYPE_BOOL) {
    return Value(value.Get<bool>() ? false : true);
  } else if (value.Type() == VALUE_TYPE_FLOAT) {
    return Value(-value.Get<float>());
  } else if (value.Type() == VALUE_TYPE_INT) {
    return Value(-value.Get<int>());
  } else if (value.Type() == VALUE_TYPE_DOUBLE) {
    return Value(-value.Get<double>());
  }

  // ???
  return value;
}

class TreeDesc;


class TreeDesc
{
 public:
  TreeDesc();
  ~TreeDesc();

 private:
  TreeDesc *child_tree_desc_;
};

class NodeInfo {
 public:
  NodeInfo(NodeType node_type, ValueType value_type, int32 log2dim)
      : node_type_(node_type), value_type_(value_type), log2dim_(log2dim) {}

  NodeType node_type() const { return node_type_; }

  ValueType value_type() const { return value_type_; }

  int32 log2dim() const { return log2dim_; }

 private:
  NodeType node_type_;
  ValueType value_type_;
  int32 log2dim_;
};

///
/// Stores layout of grid hierarchy.
///
class GridLayoutInfo {
 public:
  GridLayoutInfo() {}
  //~GridLayoutInfo() {}

  void Add(const NodeInfo &node_info) {
    node_infos_.push_back(node_info);
  }

  const NodeInfo &GetNodeInfo(int level) const {
    TINYVDBIO_ASSERT(level <= int(node_infos_.size()));
    return node_infos_[size_t(level)];
  }

  int NumLevels() const {
    return int(node_infos_.size());
  }

  std::vector<NodeInfo> node_infos_;
};

class InternalOrLeafNode;

class Node {
 public:
  Node(const GridLayoutInfo &layout_info) : grid_layout_info_(layout_info) {}
  Node &operator=(const Node &rhs) {
    grid_layout_info_ = rhs.grid_layout_info_;
    return (*this);
  }
  Node(const Node &rhs) : grid_layout_info_(rhs.grid_layout_info_) {
  }

  virtual ~Node();

  virtual bool ReadTopology(StreamReader *sr, int level, const DeserializeParams &params,
                            std::string *warn, std::string *err) = 0;

  virtual bool ReadBuffer(StreamReader *sr, int level, const DeserializeParams &params,
                          std::string *warn, std::string *err) = 0;

 protected:
  GridLayoutInfo grid_layout_info_;
};

Node::~Node() {}

///
/// InternalOrLeaf node represents bifurcation or leaf node.
///
class InternalOrLeafNode : public Node {
 public:
  // static const int LOG2DIM = Log2Dim,  // log2 of tile count in one
  // dimension
  //    TOTAL = Log2Dim +
  //            ChildNodeType::TOTAL,  // log2 of voxel count in one dimension
  //    DIM = 1 << TOTAL,              // total voxel count in one dimension
  //    NUM_VALUES =
  //        1 << (3 * Log2Dim),  // total voxel count represented by this node
  //    LEVEL = 1 + ChildNodeType::LEVEL;  // level 0 = leaf
  // static const int64 NUM_VOXELS =
  //    uint64_t(1) << (3 * TOTAL);  // total voxel count represented by this
  //    node
  // static const int NUM_VALUES = 1 << (3 * Log2Dim); // total voxel count
  // represented by this node

  InternalOrLeafNode(const GridLayoutInfo &grid_layout_info)
      : Node(grid_layout_info) {
    origin_[0] = 0.0f;
    origin_[1] = 0.0f;
    origin_[2] = 0.0f;
    //node_values_.resize(child_mask_.memUsage());

    num_voxels_ = 0;
  }

  InternalOrLeafNode(const InternalOrLeafNode &rhs) :
    Node(rhs.grid_layout_info_)
     {

    origin_[0] = rhs.origin_[0];
    origin_[1] = rhs.origin_[1];
    origin_[2] = rhs.origin_[2];

    child_nodes_ = rhs.child_nodes_;
    child_mask_ = rhs.child_mask_;

    num_voxels_ = rhs.num_voxels_;

    node_values_ = rhs.node_values_;

    value_mask_ = rhs.value_mask_;

    data_ = rhs.data_;
  }

  InternalOrLeafNode& operator=(const InternalOrLeafNode &rhs) {

    origin_[0] = rhs.origin_[0];
    origin_[1] = rhs.origin_[1];
    origin_[2] = rhs.origin_[2];

    child_nodes_ = rhs.child_nodes_;
    child_mask_ = rhs.child_mask_;

    num_voxels_ = rhs.num_voxels_;

    node_values_ = rhs.node_values_;

    value_mask_ = rhs.value_mask_;

    data_ = rhs.data_;

    return (*this);
  }

  //~InternalOrLeafNode();
  

  /// Deep copy function
  InternalOrLeafNode &Copy(const InternalOrLeafNode &rhs);

  ///
  /// @param[in] level Depth of this node(0: root, 1: first intermediate, ...)
  ///
  bool ReadTopology(StreamReader *sr, int level, const DeserializeParams &parms,
                    std::string *warn, std::string *err);

  bool ReadBuffer(StreamReader *sr, int level, const DeserializeParams &params,
                  std::string *warn, std::string *err);


  const std::vector<InternalOrLeafNode> &GetChildNodes() const {
    return child_nodes_;
  }

  std::vector<InternalOrLeafNode> &GetChildNodes() {
    return child_nodes_;
  }

  // tinyusdz patch: expose decoded data for dense grid extraction.
  const NodeMask &GetChildMask() const { return child_mask_; }
  const NodeMask &GetValueMask() const { return value_mask_; }
  // Leaf voxel data (dense, num_voxels values; inactive voxels already filled
  // with the (reconstructed) background/inactive value by ReadMaskValues).
  const std::vector<unsigned char> &GetLeafData() const { return data_; }
  unsigned int GetNumVoxels() const { return num_voxels_; }

 private:

  NodeMask value_mask_;

  // For internal node
  // child nodes are internal or leaf depending on grid_layout_info_[level+1].node_type().
  std::vector<InternalOrLeafNode> child_nodes_;

  NodeMask child_mask_;
  int origin_[3];

  std::vector<ValueType> node_values_;

  // For leaf node

  std::vector<unsigned char> data_;  // Leaf's voxel data.
  unsigned int num_voxels_;

};


class RootNode : public Node {
 public:
  RootNode(const GridLayoutInfo &layout_info)
      : Node(layout_info),
        num_tiles_(0),
        num_children_(0) {

  }
  ~RootNode() {}

  /// Deep copy function
  RootNode &Copy(const RootNode &rhs);

  bool ReadTopology(StreamReader *sr, int level, const DeserializeParams &parms,
                    std::string *warn, std::string *err);

  bool ReadBuffer(StreamReader *sr, int level, const DeserializeParams &params,
                  std::string *warn, std::string *err);

  const std::vector<InternalOrLeafNode> &GetChildNodes() const {
    return child_nodes_;
  }

  std::vector<InternalOrLeafNode> &GetChildNodes() {
    return child_nodes_;
  }

  // tinyusdz patch: expose decoded data for dense grid extraction.
  // child_origins_[k] is the global voxel-space origin of child_nodes_[k]
  // (read from the file but discarded by the upstream code).
  const std::vector<std::array<int, 3> > &GetChildOrigins() const {
    return child_origins_;
  }
  double background_float() const {
    if (background_.IsFloat()) return double(background_.Get<float>());
    if (background_.IsDouble()) return background_.Get<double>();
    return 0.0;
  }

 private:

  std::vector<InternalOrLeafNode> child_nodes_;
  std::vector<std::array<int, 3> > child_origins_;  // tinyusdz patch

  Value background_;  // Background(region of un-interested area) value
  unsigned int num_tiles_;
  unsigned int num_children_;
};

///
/// Simple Voxel node.
/// (integer grid)
///
struct VoxelNode
{
  // local bbox
  // must be dividable by each element of `num_divs` for intermediate node.
  uint32_t bmin[3];
  uint32_t bmax[3];

  bool is_leaf;  

  uint32_t num_divs[3]; // The number of voxel divisions

  //
  // intermediate(branch)
  //
  double background;  // background value(for empty leaf)

  // offset to child VoxelNode
  // 0 = empty leaf
  std::vector<size_t> child_offsets;  // len = num_divs[0] * num_divs[1] * num_divs[2]

  //
  // leaf
  //

  // TODO(syoyo): Support various voxel data type.
  uint32_t num_channels;
  std::vector<float> voxels; // len = num_divs[0] * num_divs[1] * num_divs[2] * num_channels
};

class VoxelTree
{
 public:
  
  ///
  /// Returns tree is valid(got success to build tree?)
  ///
  bool Valid();

  ///
  /// Builds Voxel tree from RootNode class
  ///
  bool Build(const RootNode &root);

  ///
  /// Sample voxel value for a given coordinate.
  /// Returns voxel value or background value when `loc` coordinate is empty. 
  ///
  /// @param[in] loc Sample coordinate.
  /// @param[in] req_channels Required channels of voxel data. 
  /// @param[out] out Sampled voxel value(length = req_channels)
  ///
  void Sample(const uint32_t loc[3], const uint8_t req_channels, float *out);

 private:

  // Build tree recursively.
  void BuildTree(const InternalOrLeafNode& root, int depth);

  bool valid_ = false;

  double bmin_[3]; // bounding min of root voxel node(in world coordinate).
  double bmax_[3]; // bounding max of root voxel node(in world coordinate).
  double pitch_[3];  // voxel pitch at leaf level. Assume all voxel has same pitch size.
  

  std::vector<VoxelNode> nodes_; // [0] = root node
};

#ifdef __clang__
#pragma clang diagnostic pop
#endif

///
/// Parse VDB header from a file.
/// Returns TINYVDBIO_SUCCESS upon success and `header` will be filled.
/// Returns false when failed to parse VDB header and store error message to
/// `err`.
///
VDBStatus ParseVDBHeader(const std::string &filename, VDBHeader *header,
                         std::string *err);

///
/// Parse VDB header from memory.
/// Returns TINYVDBIO_SUCCESS upon success and `header` will be filled.
/// Returns false when failed to parse VDB header and store error message to
/// `err`.
///
VDBStatus ParseVDBHeader(const unsigned char *data, const size_t len,
                         VDBHeader *header, std::string *err);

///
/// Load Grid descriptors from file
///
/// Returns TINYVDBIO_SUCCESS upon success.
/// Returns false when failed to read VDB data and store error message to
/// `err`.
///
VDBStatus ReadGridDescriptors(const std::string &filename,
                              const VDBHeader &header,
                              std::map<std::string, GridDescriptor> *gd_map,
                              std::string *err);

///
/// Load Grid descriptors from memory
///
/// Returns TINYVDBIO_SUCCESS upon success.
/// Returns false when failed to read VDB data and store error message to
/// `err`.
///
VDBStatus ReadGridDescriptors(const unsigned char *data, const size_t data_len,
                              const VDBHeader &header,
                              std::map<std::string, GridDescriptor> *gd_map,
                              std::string *err);

///
/// Load Grid data from file
///
/// Returns TINYVDBIO_SUCCESS upon success.
/// Returns false when failed to read VDB data and store error message to
/// `err`.
///
VDBStatus ReadGrids(const std::string &filename, const VDBHeader &header,
                    const std::map<std::string, GridDescriptor> &gd_map,
                    std::string *warn, std::string *err);

///
/// Load Grid data from memory
///
/// Returns TINYVDBIO_SUCCESS upon success.
/// Returns false when failed to read VDB data and store error message to
/// `err`.
/// Returns warning message tot `warn`.
///
VDBStatus ReadGrids(const unsigned char *data, const size_t data_len,
                    const VDBHeader &header,
                    const std::map<std::string, GridDescriptor> &gd_map,
                    std::string *warn, std::string *err);

///
/// Write VDB data to a file.
///
bool SaveVDB(const std::string &filename, std::string *err);

// ---------------------------------------------------------------------------
// tinyusdz patch: dense float-grid extraction API.
//
// The upstream ReadGrids() decodes a grid but discards it. The functions below
// decode each FloatTree grid into a DENSE float array (active voxels scattered
// in, inactive voxels = background), suitable for direct volume rendering.
//
// Limitations (sufficient for "simple volume" support):
//   - Only float FloatTree grids (Tree4<float,5,4,3>). Half-precision grids
//     (save_float_as_half) and non-float grids are rejected with an error.
//   - Only None / ZIP / ACTIVE_MASK compression (no BLOSC).
//   - Instanced grids are skipped.
// ---------------------------------------------------------------------------
struct DenseFloatGrid {
  std::string name;            // grid name
  int origin[3];               // index-space origin (min active voxel coord)
  int dim[3];                  // voxel dimensions (x, y, z)
  double voxel_size[3];        // world-space size of one voxel
  double world_translation[3]; // world-space translation (frustum/scale-translate)
  float background;            // background (inactive) value
  // Dense voxels, length dim[0]*dim[1]*dim[2].
  // Layout: index = x + dim[0]*(y + dim[1]*z)  (x is contiguous).
  std::vector<float> data;
};

///
/// Load all grids from memory and decode each into a dense float grid.
/// `max_voxels` caps the total voxel count of any single grid (0 = use a
/// built-in default) to bound memory; grids exceeding it produce an error.
///
VDBStatus ReadDenseGrids(const unsigned char *data, const size_t data_len,
                         const VDBHeader &header,
                         const std::map<std::string, GridDescriptor> &gd_map,
                         std::vector<DenseFloatGrid> *grids, size_t max_voxels,
                         std::string *warn, std::string *err);

}  // namespace tinyvdb

#endif // TINY_VDB_IO_H_

#ifdef TINYVDBIO_IMPLEMENTATION

#if !defined(TINYVDBIO_USE_SYSTEM_ZLIB)
// miniz header location defaults to <miniz.h> on the include path; a host project
// can override it by defining TINYVDBIO_MINIZ_INCLUDE before including.
#if !defined(TINYVDBIO_MINIZ_INCLUDE)
#define TINYVDBIO_MINIZ_INCLUDE "miniz.h"
#endif
extern "C" {
#include TINYVDBIO_MINIZ_INCLUDE
}
#endif

#if defined(TINYVDBIO_USE_BLOSC)
#include "blosc.h"
#endif

// Self-contained BLOSC1 decode (OpenVDB's DEFAULT write compression) using a
// vendored LZ4 / zstd. TINYVDBIO_NO_VENDORED_BLOSC disables it (falls back to
// "unsupported"). The LZ4 / zstd header locations default to <lz4.h>/<zstd.h>
// on the include path; a host project can override them by defining
// TINYVDBIO_LZ4_INCLUDE / TINYVDBIO_ZSTD_INCLUDE before including this header.
#if !defined(TINYVDBIO_NO_VENDORED_BLOSC)
#define TINYVDBIO_VENDORED_BLOSC 1
#if !defined(TINYVDBIO_LZ4_INCLUDE)
#define TINYVDBIO_LZ4_INCLUDE "lz4.h"
#endif
#if !defined(TINYVDBIO_ZSTD_INCLUDE)
#define TINYVDBIO_ZSTD_INCLUDE "zstd.h"
#endif
extern "C" {
#include TINYVDBIO_LZ4_INCLUDE
}
#include TINYVDBIO_ZSTD_INCLUDE
#endif

#include <iostream>  // HACK
#include <sstream>
#include <vector>

#ifdef __clang__
#pragma clang diagnostic push
#if __has_warning("-Wzero-as-null-pointer-constant")
#pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
#endif
#endif

namespace tinyvdb {

const int kOPENVDB_MAGIC = 0x56444220;

#ifndef MINIZ_LITTLE_ENDIAN
#define MINIZ_LITTLE_ENDIAN (1)
#endif

///
/// TinyVDBIO's default file version.
///
const unsigned int kTINYVDB_FILE_VERSION = 220;

// File format versions.
// This should be same with OpenVDB's implementation.
// We don't support version less than 220
enum {
  TINYVDB_FILE_VERSION_SELECTIVE_COMPRESSION = 220,
  TINYVDB_FILE_VERSION_FLOAT_FRUSTUM_BBOX = 221,
  TINYVDB_FILE_VERSION_NODE_MASK_COMPRESSION = 222,
  TINYVDB_FILE_VERSION_BLOSC_COMPRESSION = 223,
  TINYVDB_FILE_VERSION_POINT_INDEX_GRID = 223,
  TINYVDB_FILE_VERSION_MULTIPASS_IO = 224
};

enum {
  COMPRESS_NONE = 0,
  COMPRESS_ZIP = 0x1,
  COMPRESS_ACTIVE_MASK = 0x2,
  COMPRESS_BLOSC = 0x4
};

namespace {

// In order not to break backward compatibility with existing VDB files,
// grids stored using 16-bit half floats are flagged by adding the following
// suffix to the grid's type name on output.  The suffix is removed on input
// and the grid's "save float as half" flag set accordingly.
const char *HALF_FLOAT_TYPENAME_SUFFIX = "_HalfFloat";

const char *SEP = "\x1e";  // ASCII "record separator"

}  // namespace

// https://gist.github.com/rygorous/2156668
// Reuse MINIZ_LITTLE_ENDIAN flag from miniz.
union FP32 {
  unsigned int u;
  float f;
  struct {
#if MINIZ_LITTLE_ENDIAN
    unsigned int Mantissa : 23;
    unsigned int Exponent : 8;
    unsigned int Sign : 1;
#else
    unsigned int Sign : 1;
    unsigned int Exponent : 8;
    unsigned int Mantissa : 23;
#endif
  } s;
};

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#endif

union FP16 {
  unsigned short u;
  struct {
#if MINIZ_LITTLE_ENDIAN
    unsigned int Mantissa : 10;
    unsigned int Exponent : 5;
    unsigned int Sign : 1;
#else
    unsigned int Sign : 1;
    unsigned int Exponent : 5;
    unsigned int Mantissa : 10;
#endif
  } s;
};

#ifdef __clang__
#pragma clang diagnostic pop
#endif

static inline FP32 half_to_float(FP16 h) {
  static const FP32 magic = {113 << 23};
  static const unsigned int shifted_exp = 0x7c00
                                          << 13;  // exponent mask after shift
  FP32 o;

  o.u = (h.u & 0x7fffU) << 13U;           // exponent/mantissa bits
  unsigned int exp_ = shifted_exp & o.u;  // just the exponent
  o.u += (127 - 15) << 23;                // exponent adjust

  // handle exponent special cases
  if (exp_ == shifted_exp)    // Inf/NaN?
    o.u += (128 - 16) << 23;  // extra exp adjust
  else if (exp_ == 0)         // Zero/Denormal?
  {
    o.u += 1 << 23;  // extra exp adjust
    o.f -= magic.f;  // renormalize
  }

  o.u |= (h.u & 0x8000U) << 16U;  // sign bit
  return o;
}

static inline FP16 float_to_half_full(FP32 f) {
  FP16 o = {0};

  // Based on ISPC reference code (with minor modifications)
  if (f.s.Exponent == 0)  // Signed zero/denormal (which will underflow)
    o.s.Exponent = 0;
  else if (f.s.Exponent == 255)  // Inf or NaN (all exponent bits set)
  {
    o.s.Exponent = 31;
    o.s.Mantissa = f.s.Mantissa ? 0x200 : 0;  // NaN->qNaN and Inf->Inf
  } else                                      // Normalized number
  {
    // Exponent unbias the single, then bias the halfp
    int newexp = f.s.Exponent - 127 + 15;
    if (newexp >= 31)  // Overflow, return signed infinity
      o.s.Exponent = 31;
    else if (newexp <= 0)  // Underflow
    {
      if ((14 - newexp) <= 24)  // Mantissa might be non-zero
      {
        unsigned int mant = f.s.Mantissa | 0x800000;  // Hidden 1 bit
        o.s.Mantissa = mant >> (14 - newexp);
        if ((mant >> (13 - newexp)) & 1)  // Check for rounding
          o.u++;  // Round, might overflow into exp bit, but this is OK
      }
    } else {
      o.s.Exponent = static_cast<unsigned int>(newexp);
      o.s.Mantissa = f.s.Mantissa >> 13;
      if (f.s.Mantissa & 0x1000)  // Check for rounding
        o.u++;                    // Round, might overflow to inf, this is OK
    }
  }

  o.s.Sign = f.s.Sign;
  return o;
}

// tinyusdz patch: convenience scalar half<->float helpers.
static inline float TinyVDBHalfBitsToFloat(unsigned short bits) {
  FP16 h;
  h.u = bits;
  return half_to_float(h).f;
}
static inline unsigned short TinyVDBFloatToHalfBits(float v) {
  FP32 f;
  f.f = v;
  return float_to_half_full(f).u;
}

static inline void swap2(unsigned short *val) {
  unsigned short tmp = *val;
  unsigned char *dst = reinterpret_cast<unsigned char *>(val);
  unsigned char *src = reinterpret_cast<unsigned char *>(&tmp);

  dst[0] = src[1];
  dst[1] = src[0];
}

static inline void swap4(unsigned int *val) {
  unsigned int tmp = *val;
  unsigned char *dst = reinterpret_cast<unsigned char *>(val);
  unsigned char *src = reinterpret_cast<unsigned char *>(&tmp);

  dst[0] = src[3];
  dst[1] = src[2];
  dst[2] = src[1];
  dst[3] = src[0];
}

static inline void swap4(int *val) {
  int tmp = *val;
  unsigned char *dst = reinterpret_cast<unsigned char *>(val);
  unsigned char *src = reinterpret_cast<unsigned char *>(&tmp);

  dst[0] = src[3];
  dst[1] = src[2];
  dst[2] = src[1];
  dst[3] = src[0];
}

static inline void swap8(tinyvdb::tinyvdb_uint64 *val) {
  tinyvdb::tinyvdb_uint64 tmp = (*val);
  unsigned char *dst = reinterpret_cast<unsigned char *>(val);
  unsigned char *src = reinterpret_cast<unsigned char *>(&tmp);

  dst[0] = src[7];
  dst[1] = src[6];
  dst[2] = src[5];
  dst[3] = src[4];
  dst[4] = src[3];
  dst[5] = src[2];
  dst[6] = src[1];
  dst[7] = src[0];
}

static inline void swap8(tinyvdb::tinyvdb_int64 *val) {
  tinyvdb::tinyvdb_int64 tmp = (*val);
  unsigned char *dst = reinterpret_cast<unsigned char *>(val);
  unsigned char *src = reinterpret_cast<unsigned char *>(&tmp);

  dst[0] = src[7];
  dst[1] = src[6];
  dst[2] = src[5];
  dst[3] = src[4];
  dst[4] = src[3];
  dst[5] = src[2];
  dst[6] = src[1];
  dst[7] = src[0];
}


///
/// Simple stream reader
///
class StreamReader {
 public:
  explicit StreamReader(const uint8_t *binary, const size_t length,
                        const bool swap_endian)
      : binary_(binary), length_(length), swap_endian_(swap_endian), idx_(0) {
    (void)pad_;
  }

  bool seek_set(const uint64_t offset) {
    if (offset > length_) {
      return false;
    }

    idx_ = offset;
    return true;
  }

  bool seek_from_currect(const int64_t offset) {
    if ((int64_t(idx_) + offset) < 0) {
      return false;
    }

    if (size_t((int64_t(idx_) + offset)) > length_) {
      return false;
    }

    idx_ = size_t(int64_t(idx_) + offset);
    return true;
  }

  size_t read(const size_t n, const uint64_t dst_len, unsigned char *dst) {
    size_t len = n;
    if ((idx_ + len) > length_) {
      len = length_ - idx_;
    }

    if (len > 0) {
      if (dst_len < len) {
        // dst does not have enough space. return 0 for a while.
        return 0;
      }

      memcpy(dst, &binary_[idx_], len);
      idx_ += len;
      return len;

    } else {
      return 0;
    }
  }

  bool read1(unsigned char *ret) {
    if ((idx_ + 1) > length_) {
      return false;
    }

    const unsigned char val = binary_[idx_];

    (*ret) = val;
    idx_ += 1;

    return true;
  }

  bool read_bool(bool *ret) {
    if ((idx_ + 1) > length_) {
      return false;
    }

    const char val = static_cast<const char>(binary_[idx_]);

    (*ret) = bool(val);
    idx_ += 1;

    return true;
  }

  bool read1(char *ret) {
    if ((idx_ + 1) > length_) {
      return false;
    }

    const char val = static_cast<const char>(binary_[idx_]);

    (*ret) = val;
    idx_ += 1;

    return true;
  }

  bool read2(unsigned short *ret) {
    if ((idx_ + 2) > length_) {
      return false;
    }

    unsigned short val =
        *(reinterpret_cast<const unsigned short *>(&binary_[idx_]));

    if (swap_endian_) {
      swap2(&val);
    }

    (*ret) = val;
    idx_ += 2;

    return true;
  }

  bool read4(unsigned int *ret) {
    if ((idx_ + 4) > length_) {
      return false;
    }

    unsigned int val =
        *(reinterpret_cast<const unsigned int *>(&binary_[idx_]));

    if (swap_endian_) {
      swap4(&val);
    }

    (*ret) = val;
    idx_ += 4;

    return true;
  }

  bool read4(int *ret) {
    if ((idx_ + 4) > length_) {
      return false;
    }

    int val = *(reinterpret_cast<const int *>(&binary_[idx_]));

    if (swap_endian_) {
      swap4(&val);
    }

    (*ret) = val;
    idx_ += 4;

    return true;
  }

  bool read8(tinyvdb_uint64 *ret) {
    if ((idx_ + 8) > length_) {
      return false;
    }

    tinyvdb_uint64 val =
        *(reinterpret_cast<const tinyvdb_uint64 *>(&binary_[idx_]));

    if (swap_endian_) {
      swap8(&val);
    }

    (*ret) = val;
    idx_ += 8;

    return true;
  }

  bool read8(tinyvdb_int64 *ret) {
    if ((idx_ + 8) > length_) {
      return false;
    }

    tinyvdb_int64 val =
        *(reinterpret_cast<const tinyvdb_int64 *>(&binary_[idx_]));

    if (swap_endian_) {
      swap8(&val);
    }

    (*ret) = val;
    idx_ += 8;

    return true;
  }

  bool read_float(float *ret) {
    if (!ret) {
      return false;
    }

    float value;
    if (!read4(reinterpret_cast<int *>(&value))) {
      return false;
    }

    (*ret) = value;

    return true;
  }

  bool read_double(double *ret) {
    if (!ret) {
      return false;
    }

    double value;
    if (!read8(reinterpret_cast<tinyvdb_uint64 *>(&value))) {
      return false;
    }

    (*ret) = value;

    return true;
  }

  bool read_value(Value *inout) {
    if (!inout) {
      return false;
    }

    if (inout->Type() == VALUE_TYPE_FLOAT) {
      float value;
      if (!read_float(&value)) {
        return false;
      }

      (*inout) = Value(value);
    } else if (inout->Type() == VALUE_TYPE_INT) {
      int value;
      if (!read4(&value)) {
        return false;
      }

      (*inout) = Value(value);
    } else {
      TINYVDBIO_ASSERT(0);
      return false;
    }

    return true;
  }

  size_t tell() const { return idx_; }

  const uint8_t *data() const { return binary_; }

  bool swap_endian() const { return swap_endian_; }

  size_t size() const { return length_; }

 private:
  const uint8_t *binary_;
  const size_t length_;
  bool swap_endian_;
  char pad_[7];
  uint64_t idx_;
};

static Value ReadValue(StreamReader *sr, const ValueType type) {
  if (type == VALUE_TYPE_NULL) {
    return Value();
  } else if (type == VALUE_TYPE_BOOL) {
    char value;
    sr->read1(&value);
    return Value(value);
  } else if (type == VALUE_TYPE_FLOAT) {
    float value;
    sr->read_float(&value);
    return Value(value);
  } else if (type == VALUE_TYPE_INT) {
    int value;
    sr->read4(&value);
    return Value(value);
  } else if (type == VALUE_TYPE_DOUBLE) {
    double value;
    sr->read_double(&value);
    return Value(value);
  }
  // ???
  return Value();
}

struct DeserializeParams {
  unsigned int file_version;
  unsigned int compression_flags;
  bool half_precision;
  char __pad__[7];
  Value background;
};

static inline std::string ReadString(StreamReader *sr) {
  unsigned int size = 0;
  sr->read4(&size);
  if (size > 0) {
    std::string buffer(size, ' ');
    sr->read(size, size, reinterpret_cast<unsigned char *>(&buffer[0]));
    return buffer;
  }
  return std::string();
}

static inline void WriteString(std::ostream &os, const std::string &name) {
  unsigned int size = static_cast<unsigned int>(name.size());
  os.write(reinterpret_cast<char *>(&size), sizeof(unsigned int));
  os.write(&name[0], size);
}

static inline bool ReadMetaBool(StreamReader *sr) {
  char c = 0;
  unsigned int size;
  sr->read4(&size);
  if (size == 1) {
    sr->read(1, 1, reinterpret_cast<unsigned char *>(&c));
  }
  return bool(c);
}

static inline float ReadMetaFloat(StreamReader *sr) {
  float f = 0.0f;
  unsigned int size;
  sr->read4(&size);
  if (size == sizeof(float)) {
    sr->read4(reinterpret_cast<unsigned int *>(&f));
  }
  return f;
}

static inline void ReadMetaVec3i(StreamReader *sr, int v[3]) {
  unsigned int size;
  sr->read4(&size);
  if (size == 3 * sizeof(int)) {
    sr->read4(&v[0]);
    sr->read4(&v[1]);
    sr->read4(&v[2]);
  }
}

static inline void ReadMetaVec3d(StreamReader *sr, double v[3]) {
  unsigned int size;
  sr->read4(&size);
  if (size == 3 * sizeof(double)) {
    sr->read_double(&v[0]);
    sr->read_double(&v[1]);
    sr->read_double(&v[2]);
  }
}

static inline tinyvdb_int64 ReadMetaInt64(StreamReader *sr) {
  unsigned int size;
  tinyvdb_int64 i64 = 0;
  sr->read4(&size);
  if (size == sizeof(tinyvdb_int64)) {
    sr->read8(reinterpret_cast<tinyvdb_uint64 *>(&i64));
  }
  return i64;
}

static inline void ReadVec3d(StreamReader *sr, double v[3]) {
  sr->read_double(&v[0]);
  sr->read_double(&v[1]);
  sr->read_double(&v[2]);
}

// https://stackoverflow.com/questions/874134/find-if-string-ends-with-another-string-in-c
static inline bool EndsWidth(std::string const &value,
                             std::string const &ending) {
  if (ending.size() > value.size()) return false;
  return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

template <std::size_t N>
bool BitMask<N>::load(StreamReader *sr) {
  std::vector<unsigned char> buf(mask_.size() / 8);

  sr->read(mask_.size(), mask_.size(), buf.data());

  // Reconstruct bit mask
  // TODO(syoyo): endian
  for (size_t j = 0; j < mask_.size() / 8; j++) {
    for (size_t i = 0; i < 8; i++) {
      unsigned char bit = (buf[j] >> i) & 0x1;
      mask_.set(j * 8 + i, bit);
    }
  }

  return true;
}

static bool DecompressZip(unsigned char *dst,
                          unsigned long *uncompressed_size /* inout */,
                          const unsigned char *src, unsigned long src_size) {
  if ((*uncompressed_size) == src_size) {
    // Data is not compressed.
    memcpy(dst, src, src_size);
    return true;
  }
  std::vector<unsigned char> tmpBuf(*uncompressed_size);

#if defined(TINYVDBIO_USE_SYSTEM_ZLIB)
  int ret = uncompress(&tmpBuf.at(0), uncompressed_size, src, src_size);
  if (Z_OK != ret) {
    return false;
  }
#else
  int ret = mz_uncompress(&tmpBuf.at(0), uncompressed_size, src, src_size);
  if (MZ_OK != ret) {
    return false;
  }
#endif

  // tinyusdz patch: upstream cast the POINTER `uncompressed_size` to size_t
  // (a huge value) instead of dereferencing it, reading far past `tmpBuf` and
  // crashing on any ZIP-compressed buffer. Use the decompressed byte count.
  memcpy(dst, tmpBuf.data(), size_t(*uncompressed_size));

  return true;
}

#if defined(TINYVDBIO_USE_BLOSC)
static bool DecompressBlosc(unsigned char *dst, unsigned long uncompressed_size,
                            const unsigned char *src, unsigned long src_size) {
  if (uncompressed_size == src_size) {
    // Data is not compressed.
    memcpy(dst, src, src_size);
    return true;
  }

  const int numUncompressedBytes = blosc_decompress_ctx(
      /*src=*/src, /*dest=*/dst, src_size, /*numthreads=*/1);

  if (numUncompressedBytes < 1) {
    TINYVDB_LOG << "numUncompressedBytes = " << numUncompressedBytes << std::endl;
    return false;
  }

  if (numUncompressedBytes != int(src_size)) {
    TINYVDB_LOG << "aaa" << std::endl;
    return false;
  }

  return true;
}
#endif

#if defined(TINYVDBIO_VENDORED_BLOSC)
// tinyusdz patch: decode a Blosc1 container (the format c-blosc / OpenVDB write)
// into `dst` (`dst_size` bytes). Supports the LZ4 and zstd internal codecs (what
// OpenVDB uses) and no-codec/memcpy, with optional byte-shuffle. Bit-shuffle and
// the blosclz/snappy/zlib internal codecs are not supported. Returns false on an
// unsupported/malformed container.
//
// Container layout (little-endian):
//   [0]=version [1]=versionlz [2]=flags [3]=typesize
//   [4:8)=nbytes [8:12)=blocksize [12:16)=cbytes
//   flags: bit0 byte-shuffle, bit1 memcpyed, bit2 bit-shuffle, bits5-7 codec
//          (0 blosclz, 1 lz4, 2 snappy, 3 zlib, 4 zstd)
//   after the header: int32 bstarts[nblocks]; each block = nstreams sub-streams,
//   each [int32 csize][csize compressed bytes] -> neblock = bsize/nstreams bytes.
static bool DecompressBlosc1(unsigned char *dst, size_t dst_size,
                             const unsigned char *src, size_t src_size,
                             std::string *err) {
  if (src_size < 16) {
    if (err) (*err) += "Blosc: truncated header.\n";
    return false;
  }
  const unsigned char flags = src[2];
  unsigned char typesize = src[3];
  uint32_t nbytes = 0, blocksize = 0, cbytes = 0;
  std::memcpy(&nbytes, src + 4, 4);
  std::memcpy(&blocksize, src + 8, 4);
  std::memcpy(&cbytes, src + 12, 4);

  const bool doshuffle = (flags & 0x01) != 0;
  const bool memcpyed = (flags & 0x02) != 0;
  const bool dobitshuffle = (flags & 0x04) != 0;
  const int codec = (flags >> 5) & 0x07;
  (void)cbytes;

  if (dobitshuffle) {
    if (err) (*err) += "Blosc: bit-shuffle not supported.\n";
    return false;
  }
  if (nbytes != dst_size) {
    if (err) (*err) += "Blosc: nbytes mismatch.\n";
    return false;
  }
  if (typesize == 0) typesize = 1;

  // memcpyed blocks store the ORIGINAL (already-unshuffled) data, so the shuffle
  // flag does not apply — copy straight to dst.
  if (memcpyed) {
    if (16 + size_t(nbytes) > src_size) return false;
    std::memcpy(dst, src + 16, nbytes);
    return true;
  }

  if (blocksize == 0) {
    if (err) (*err) += "Blosc: zero block size.\n";
    return false;
  }

  const int nblocks = int((size_t(nbytes) + blocksize - 1) / blocksize);
  const unsigned char *src_end = src + src_size;

  // c-blosc applies the byte-shuffle filter INDEPENDENTLY within each block, so
  // each block must be decoded into a block-local temp and un-shuffled on its
  // own. (A global un-shuffle only happens to be correct for single-block
  // buffers.) The split streams of a block ARE its shuffle partitions.
  std::vector<unsigned char> blk;
  for (int b = 0; b < nblocks; b++) {
    int32_t bstart = 0;
    std::memcpy(&bstart, src + 16 + size_t(b) * 4, 4);
    if (bstart < 0 || size_t(bstart) >= src_size) return false;
    const unsigned char *bsrc = src + bstart;
    const size_t boff = size_t(b) * blocksize;
    const size_t bsize = std::min(size_t(blocksize), size_t(nbytes) - boff);

    // The compressed extent of this block is [bstart, next-bstart) (or to the
    // end of the buffer for the last block).
    size_t block_end = src_size;
    if (b + 1 < nblocks) {
      int32_t bnext = 0;
      std::memcpy(&bnext, src + 16 + size_t(b + 1) * 4, 4);
      if (bnext >= 0 && size_t(bnext) <= src_size && size_t(bnext) >= size_t(bstart))
        block_end = size_t(bnext);
    }
    const size_t block_compr = block_end - size_t(bstart);

    // Auto-detect the per-block stream count. c-blosc stores a block as EITHER
    // one stream covering the whole block, OR `typesize` independently
    // compressed shuffle-partition streams. The compressor's split heuristic
    // varies by c-blosc version (block-size and element-count thresholds differ),
    // so derive the choice from the data: if the first stream's framed length
    // fills the whole block extent it is unsplit, else it is split typesize-ways.
    int nstreams = 1;
    if (doshuffle && typesize > 1 && block_compr >= 4) {
      int32_t csize0 = 0;
      std::memcpy(&csize0, bsrc, 4);
      if (csize0 >= 0 && (size_t(4) + size_t(csize0)) != block_compr)
        nstreams = int(typesize);
    }

    // Decode straight into dst when there is no shuffle; otherwise into a temp
    // that we then un-shuffle into dst.
    unsigned char *shuf = dst + boff;
    if (doshuffle) {
      blk.resize(bsize);
      shuf = blk.data();
    }

    const size_t neblock = bsize / size_t(nstreams);
    unsigned char *bdst = shuf;
    for (int s = 0; s < nstreams; s++) {
      if (bsrc + 4 > src_end) return false;
      int32_t csize = 0;
      std::memcpy(&csize, bsrc, 4);
      bsrc += 4;
      if (csize < 0 || bsrc + csize > src_end) return false;
      const size_t elen = (s == nstreams - 1) ? (bsize - neblock * s) : neblock;
      if (size_t(csize) == elen) {
        std::memcpy(bdst, bsrc, elen);  // stored uncompressed
      } else if (codec == 1) {          // LZ4
        int got = LZ4_decompress_safe(reinterpret_cast<const char *>(bsrc),
                                      reinterpret_cast<char *>(bdst), csize,
                                      int(elen));
        if (got != int(elen)) {
          if (err) (*err) += "Blosc: LZ4 block decode failed.\n";
          return false;
        }
      } else if (codec == 4) {  // zstd
        size_t got = ZSTD_decompress(bdst, elen, bsrc, size_t(csize));
        if (got != elen) {
          if (err) (*err) += "Blosc: zstd block decode failed.\n";
          return false;
        }
      } else {
        if (err)
          (*err) += "Blosc: internal codec not supported (only LZ4/zstd).\n";
        return false;
      }
      bsrc += csize;
      bdst += elen;
    }

    // Reverse the byte-shuffle filter within this block.
    if (doshuffle) {
      const size_t nelem = bsize / typesize;
      for (size_t j = 0; j < typesize; j++) {
        const unsigned char *srcj = shuf + j * nelem;
        for (size_t i = 0; i < nelem; i++)
          dst[boff + i * typesize + j] = srcj[i];
      }
      const size_t tail = bsize - nelem * typesize;  // un-shuffled remainder
      if (tail) std::memcpy(dst + boff + nelem * typesize,
                            shuf + nelem * typesize, tail);
    }
  }
  return true;
}
#endif  // TINYVDBIO_VENDORED_BLOSC

static bool ReadAndDecompressData(StreamReader *sr, unsigned char *dst_data,
                                  size_t element_size, size_t count,
                                  unsigned int compression_mask,
                                  std::string *warn, std::string *err) {

  (void)warn;

  if (compression_mask & COMPRESS_BLOSC) {
    // Read the size of the compressed data.
    // A negative size indicates uncompressed data.
    tinyvdb_int64 numCompressedBytes;
    sr->read8(&numCompressedBytes);

    TINYVDB_LOG << "numCompressedBytes " << numCompressedBytes << std::endl;
    // Guard against a corrupt / misaligned length: a compressed buffer can never
    // exceed the bytes remaining in the stream. Without this a garbage value
    // (e.g. from a stream-position drift) triggers a multi-exabyte allocation
    // and std::bad_alloc instead of a clean decode error.
    if (numCompressedBytes > 0 &&
        size_t(numCompressedBytes) > sr->size() - sr->tell()) {
      if (err)
        (*err) += "Blosc: compressed size exceeds remaining stream (corrupt "
                  "or unsupported VDB).\n";
      return false;
    }
    if (numCompressedBytes <= 0) {
      if (dst_data == NULL) {
        // seek over this data.
        sr->seek_set(sr->tell() + element_size * count);
      } else {
        sr->read(element_size * count, element_size * count, dst_data);
      }
    } else {
      const unsigned long uncompressed_size = element_size * count;
      std::vector<unsigned char> buf;
      buf.resize(size_t(numCompressedBytes));

      if (!sr->read(size_t(numCompressedBytes), size_t(numCompressedBytes),
                    buf.data())) {
        if (err) {
          (*err) += "Failed to read num compressed bytes in ReadAndDecompressData.\n";
        }
        return false;
      }

      if (dst_data != NULL) {
#if defined(TINYVDBIO_VENDORED_BLOSC)
        // tinyusdz patch: self-contained Blosc1 decode (LZ4/zstd) — no external
        // c-blosc dependency. This is OpenVDB's DEFAULT write compression.
        if (!DecompressBlosc1(dst_data, size_t(uncompressed_size), buf.data(),
                              size_t(numCompressedBytes), err)) {
          if (err) (*err) += "Failed to decode BLOSC data.\n";
          return false;
        }
#elif defined(TINYVDBIO_USE_BLOSC)
        if (!DecompressBlosc(dst_data, uncompressed_size, buf.data(),
                             size_t(numCompressedBytes))) {
          if (err) (*err) += "Failed to decode BLOSC data.\n";
          return false;
        }
#else
        if (err) (*err) += "Decoding BLOSC is not supported in this build.\n";
        return false;
#endif
      }
    }

    TINYVDB_LOG << "blosc decode ok" << std::endl;

  } else if (compression_mask & COMPRESS_ZIP) {
    // Read the size of the compressed data.
    // A negative size indicates uncompressed data.
    tinyvdb_int64 numZippedBytes;
    sr->read8(&numZippedBytes);
    TINYVDB_LOG << "numZippedBytes = " << numZippedBytes << std::endl;

    if (numZippedBytes > 0 &&
        size_t(numZippedBytes) > sr->size() - sr->tell()) {
      if (err)
        (*err) += "Zip: compressed size exceeds remaining stream (corrupt "
                  "or unsupported VDB).\n";
      return false;
    }
    if (numZippedBytes <= 0) {
      if (dst_data == NULL) {
        // seek over this data.
        sr->seek_set(sr->tell() + element_size * count);
      } else {
        sr->read(element_size * count, element_size * count, dst_data);
      }
    } else {
      unsigned long uncompressed_size = element_size * count;
      std::vector<unsigned char> buf;
      buf.resize(size_t(numZippedBytes));

      if (!sr->read(size_t(numZippedBytes), size_t(numZippedBytes),
                    buf.data())) {
        if (err) {
          (*err) += "Failed to read num zipped bytes in ReadAndDecompressData.\n";
        }
        return false;
      }

      if (!DecompressZip(dst_data, &uncompressed_size, buf.data(),
                         size_t(numZippedBytes))) {
        if (err) {
          (*err) += "Failed to decode zip data.\n";
        }
        return false;
      }
    }
  } else {
    TINYVDB_LOG << "HACK: uncompressed" << std::endl;
    TINYVDB_LOG << "  elem_size = " << element_size << ", count = " << count << std::endl;
    if (dst_data == NULL) {
      // seek over this data.
      sr->seek_set(sr->tell() + element_size * count);
    } else {
      sr->read(element_size * count, element_size * count, dst_data);
    }
  }

  if (sr->swap_endian()) {
    if (element_size == 2) {
      unsigned short *ptr = reinterpret_cast<unsigned short *>(dst_data);
      for (size_t i = 0; i < count; i++) {
        swap2(ptr + i);
      }
    } else if (element_size == 4) {
      unsigned int *ptr = reinterpret_cast<unsigned int *>(dst_data);
      for (size_t i = 0; i < count; i++) {
        swap4(ptr + i);
      }
    } else if (element_size == 8) {
      tinyvdb_uint64 *ptr = reinterpret_cast<tinyvdb_uint64 *>(dst_data);
      for (size_t i = 0; i < count; i++) {
        swap8(ptr + i);
      }
    }
  }

  return true;
}

static bool ReadValues(StreamReader *sr, const unsigned int compression_flags,
                       size_t num_values, ValueType value_type,
                       std::vector<unsigned char> *values, std::string *warn, std::string *err) {
  // usually fp16 or fp32
  TINYVDBIO_ASSERT((value_type == VALUE_TYPE_FLOAT) || (value_type == VALUE_TYPE_HALF));

  // Advance stream position when destination buffer is null.
  const bool seek = (values == NULL);

  // read data.
  if (seek) {
    // should not be 'seek' at the monent.
    TINYVDBIO_ASSERT(0);
  } else {
    if (!ReadAndDecompressData(sr, values->data(), GetValueTypeSize(value_type),
                               num_values, compression_flags, warn, err)) {
      return false;
    }
  }

  return true;
}

static bool ReadMaskValues(StreamReader *sr,
                           const unsigned int compression_flags,
                           const unsigned int file_version,
                           const Value background, size_t num_values,
                           ValueType value_type, NodeMask value_mask,
                           std::vector<unsigned char> *values,
                           const bool half_precision,
                           std::string *warn,
                           std::string *err) {
  // Advance stream position when destination buffer is null.
  const bool seek = (values == NULL);

  const bool mask_compressed = compression_flags & COMPRESS_ACTIVE_MASK;

  // tinyusdz patch: in a save-float-as-half grid EVERY float value is stored on
  // disk as a 16-bit half -- not only the leaf voxels but also the INTERNAL node
  // tile values (which the layout types as FLOAT). Reading those tiles as 4-byte
  // floats over-reads the stream and misaligns the whole tree (this is what broke
  // dense half fog volumes like bunny_cloud, whose internal nodes carry active
  // tiles). `file_elem` is the on-disk element size; `dst_elem` is the in-memory
  // size; `promote` means decode 2-byte half -> 4-byte float on store.
  const bool is_half = (value_type == VALUE_TYPE_HALF) ||
                       (half_precision && value_type == VALUE_TYPE_FLOAT);
  const size_t dst_elem = GetValueTypeSize(value_type);
  const size_t file_elem = is_half ? 2 : dst_elem;
  const bool promote = is_half && (dst_elem == 4);

  char per_node_flag = NO_MASK_AND_ALL_VALS;
  if (file_version >= TINYVDB_FILE_VERSION_NODE_MASK_COMPRESSION) {
    if (seek && !mask_compressed) {
      // selection mask and/or inactive value is saved.
      sr->seek_set(sr->tell() + 1);  // advance 1 byte.
    } else {
      sr->read1(&per_node_flag);
    }
  }

  Value inactiveVal1 = background;
  Value inactiveVal0 =
      ((per_node_flag == NO_MASK_OR_INACTIVE_VALS) ? background
                                                   : Negate(background));

  if (per_node_flag == NO_MASK_AND_ONE_INACTIVE_VAL ||
      per_node_flag == MASK_AND_ONE_INACTIVE_VAL ||
      per_node_flag == MASK_AND_TWO_INACTIVE_VALS) {
    // tinyusdz patch: the inactive value(s) are stored at the on-disk element
    // size -- HALF (2 bytes) for save-float-as-half grids, not float. The
    // upstream read_value() always read a 4-byte float, misaligning the stream.
    if (seek) {
      sr->seek_set(sr->tell() + file_elem);
    } else if (is_half) {
      unsigned short h = 0;
      sr->read2(&h);
      inactiveVal0 = Value(TinyVDBHalfBitsToFloat(h));
    } else {
      sr->read_value(&inactiveVal0);
    }

    if (per_node_flag == MASK_AND_TWO_INACTIVE_VALS) {
      // Read the second of two distinct inactive values.
      if (seek) {
        sr->seek_set(sr->tell() + file_elem);
      } else if (is_half) {
        unsigned short h = 0;
        sr->read2(&h);
        inactiveVal1 = Value(TinyVDBHalfBitsToFloat(h));
      } else {
        sr->read_value(&inactiveVal1);
      }
    }
  }

  NodeMask selection_mask(value_mask.LOG2DIM);
  if (per_node_flag == MASK_AND_NO_INACTIVE_VALS ||
      per_node_flag == MASK_AND_ONE_INACTIVE_VAL ||
      per_node_flag == MASK_AND_TWO_INACTIVE_VALS) {
    // For use in mask compression (only), read the bitmask that selects
    // between two distinct inactive values.
    if (seek) {
      sr->seek_set(sr->tell() + selection_mask.memUsage());
    } else {
      selection_mask.load(sr);
    }
  }

  size_t read_count = num_values;

  if (mask_compressed && per_node_flag != NO_MASK_AND_ALL_VALS &&
      file_version >= TINYVDB_FILE_VERSION_NODE_MASK_COMPRESSION) {
    read_count = size_t(value_mask.countOn());
    TINYVDB_LOG << "3 metadata. read count = " << read_count << std::endl;
  }

  TINYVDB_LOG << "read num = " << read_count << std::endl;

  std::vector<unsigned char> tmp_buf(read_count * file_elem);

  // Read the active-value buffer. tinyusdz patch: OpenVDB's HalfReader::read has
  // an `if (count < 1) return;` guard, so a half-precision value buffer emits
  // NOTHING when the active count is zero (e.g. an internal node whose tiles are
  // all inactive in a flood-filled level set). The non-half path always runs
  // through readData/bloscFromStream, which writes the 8-byte length header even
  // for count 0. So skip the read ONLY for the half + count-0 case; otherwise
  // always read. (This matches the reference tinyvdb C implementation and is
  // codec/version agnostic.)
  const bool skip_zero = is_half && (read_count == 0);
  if (!skip_zero) {
    if (!ReadAndDecompressData(sr, tmp_buf.data(), file_elem,
                               size_t(read_count), compression_flags, warn,
                               err)) {
      return false;
    }
  }

  if (seek) {
    return true;
  }

  // Scatter the active values into the dense node buffer, promoting 2-byte half
  // -> 4-byte float when `promote` (save-float-as-half), else a plain copy.
  // If mask compression is enabled and the number of active values read into
  // the temp buffer is smaller than the size of the destination buffer,
  // then there are missing (inactive) values.
  if (mask_compressed && read_count != num_values) {
    // Restore inactive values, using the background value and, if available,
    // the inside/outside mask.  (For fog volumes, the destination buffer is
    // assumed to be initialized to background value zero, so inactive values
    // can be ignored.)
    for (size_t destIdx = 0, tempIdx = 0; destIdx < selection_mask.SIZE;
         ++destIdx) {
      if (value_mask.isOn(int32(destIdx))) {
        // Copy a saved active value into this node's buffer.
        if (promote) {
          unsigned short hv;
          memcpy(&hv, &tmp_buf.at(tempIdx * file_elem), 2);
          float fv = TinyVDBHalfBitsToFloat(hv);
          memcpy(&values->at(destIdx * dst_elem), &fv, 4);
        } else {
          memcpy(&values->at(destIdx * dst_elem), &tmp_buf.at(tempIdx * file_elem),
                 dst_elem);
        }
        ++tempIdx;
      } else {
        // Reconstruct an unsaved inactive value and copy it into this node's
        // buffer.
        // tinyusdz patch: write the actual TYPED inactive value. The upstream
        // code memcpy'd raw bytes from the `Value` object (whose first member
        // is `type_`, not the float payload), corrupting every inactive voxel.
        const Value &iv =
            selection_mask.isOn(int32(destIdx)) ? inactiveVal1 : inactiveVal0;
        float fv = iv.IsFloat() ? iv.Get<float>()
                                : (iv.IsDouble() ? float(iv.Get<double>())
                                                 : 0.0f);
        if (value_type == VALUE_TYPE_HALF) {
          unsigned short hv = TinyVDBFloatToHalfBits(fv);
          memcpy(&values->at(destIdx * dst_elem), &hv, dst_elem);
        } else {
          memcpy(&values->at(destIdx * dst_elem), &fv, dst_elem);
        }
      }
    }
  } else if (promote) {
    for (size_t i = 0; i < num_values; ++i) {
      unsigned short hv;
      memcpy(&hv, &tmp_buf.at(i * 2), 2);
      float fv = TinyVDBHalfBitsToFloat(hv);
      memcpy(&values->at(i * 4), &fv, 4);
    }
  } else {
    memcpy(values->data(), tmp_buf.data(), num_values * dst_elem);
  }

  return true;
}

bool NodeMask::load(StreamReader *sr) {
  //TINYVDB_LOG << "DBG: mWords memUsage = " << this->memUsage() << std::endl;
  //TINYVDB_LOG << "DBG: mWords.size = " << mWords.size() << std::endl;

  bool ret = sr->read(this->memUsage(), this->memUsage(),
                      reinterpret_cast<unsigned char *>(mWords.data()));

  return ret;
}

bool RootNode::ReadTopology(StreamReader *sr, int level, const DeserializeParams &params,
                            std::string *warn, std::string *err) {
  TINYVDB_LOG << "Root background loc " << sr->tell() << std::endl;

  // Read background value;
  background_ = ReadValue(sr, grid_layout_info_.GetNodeInfo(level).value_type());

  TINYVDB_LOG << "background : " << background_
            << ", size = " << GetValueTypeSize(grid_layout_info_.GetNodeInfo(level).value_type())
            << std::endl;

  sr->read4(&num_tiles_);
  sr->read4(&num_children_);

  if ((num_tiles_ == 0) && (num_children_ == 0)) {
    return false;
  }

  TINYVDB_LOG << "num_tiles " << num_tiles_ << std::endl;
  TINYVDB_LOG << "num_children " << num_children_ << std::endl;

  // Read tiles.
  for (unsigned int n = 0; n < num_tiles_; n++) {
    int vec[3];
    Value value;
    bool active;

    sr->read4(&vec[0]);
    sr->read4(&vec[1]);
    sr->read4(&vec[2]);
    value = ReadValue(sr, grid_layout_info_.GetNodeInfo(level).value_type());
    sr->read_bool(&active);

    TINYVDB_LOG << "[" << n << "] vec = (" << vec[0] << ", " << vec[1] << ", "
              << vec[2] << "), value = " << value << ", active = " << active
              << std::endl;
  }

  // Read child nodes.
  for (unsigned int n = 0; n < num_children_; n++) {
    int vec[3];
    sr->read4(&vec[0]);
    sr->read4(&vec[1]);
    sr->read4(&vec[2]);

    // Child should be InternalOrLeafNode type
    TINYVDBIO_ASSERT((level + 1) < grid_layout_info_.NumLevels());
    TINYVDBIO_ASSERT(grid_layout_info_.GetNodeInfo(level+1).node_type() == NODE_TYPE_INTERNAL);

    InternalOrLeafNode child_node(grid_layout_info_);
    if (!child_node.ReadTopology(sr, /* level */level + 1, params, warn, err)) {
      return false;
    }

    child_nodes_.push_back(child_node);

    // tinyusdz patch: retain the child's global voxel-space origin (Coord),
    // which the upstream code read but discarded. Needed to place leaves.
    std::array<int, 3> origin = {{vec[0], vec[1], vec[2]}};
    child_origins_.push_back(origin);
  }

  return true;
}

bool RootNode::ReadBuffer(StreamReader *sr, int level, const DeserializeParams &params,
                          std::string *warn, std::string *err) {
  TINYVDB_LOG << "root readbuffer pos " << sr->tell() << std::endl;

  // Recursive call
  for (size_t i = 0; i < num_children_; i++) {
    if (!child_nodes_[i].ReadBuffer(sr, level + 1, params, warn, err)) {
      if (err) {
        (*err) += "Failed to read buffer.\n";
      }
      return false;
    }
    TINYVDB_LOG << "ReadBuffer done. child_node[" << i << "]" << std::endl;
  }

  return true;
}

bool InternalOrLeafNode::ReadTopology(StreamReader *sr,
                                const int level,
                                const DeserializeParams &params,
                                std::string *warn, std::string *err) {
  (void)params;

  int node_type = grid_layout_info_.GetNodeInfo(level).node_type();

  if (node_type == NODE_TYPE_INTERNAL) {

#if 0  // API3
    {

      int buffer_count;
      sr->read4(&buffer_count);
      if (buffer_count != 1) {
        // OPENVDB_LOG_WARN("multi-buffer trees are no longer supported");
      }
    }
#endif

    TINYVDB_LOG << "topo: buffer count offt = " << sr->tell() << std::endl;
    TINYVDB_LOG << "readtopo: level = " << level << std::endl;

    child_mask_.Alloc(grid_layout_info_.GetNodeInfo(level).log2dim());
    child_mask_.load(sr);
    TINYVDB_LOG << "topo: child mask buffer count offt = " << sr->tell()
              << std::endl;

    value_mask_.Alloc(grid_layout_info_.GetNodeInfo(level).log2dim());
    value_mask_.load(sr);
    TINYVDB_LOG << "topo: value mask buffer count offt = " << sr->tell()
              << std::endl;

    const bool old_version =
        params.file_version < TINYVDB_FILE_VERSION_NODE_MASK_COMPRESSION;

    int NUM_VALUES =
        1 << (3 * grid_layout_info_.GetNodeInfo(level)
                      .log2dim());  // total voxel count represented by this node

    TINYVDB_LOG << "num value_mask = " << value_mask_.SIZE << std::endl;
    TINYVDB_LOG << "NUM_VALUES = " << NUM_VALUES << std::endl;

    // Older version will have less values
    const int num_values =
        (old_version ? int(child_mask_.countOff()) : NUM_VALUES);

    {
      std::vector<unsigned char> values;
      values.resize(GetValueTypeSize(grid_layout_info_.GetNodeInfo(level).value_type()) *
                    size_t(num_values));

      if (!ReadMaskValues(sr, params.compression_flags, params.file_version,
                          params.background, size_t(num_values),
                          grid_layout_info_.GetNodeInfo(level).value_type(), value_mask_, &values,
                          params.half_precision, warn, err)) {
        if (err) {
          std::stringstream ss;
          ss << "Failed to read mask values in ReadTopology. level = " << level << std::endl;
          (*err) += ss.str();
        }

        return false;
      }

      // Copy values from the array into this node's table.
      if (old_version) {
        TINYVDBIO_ASSERT(size_t(num_values) <= node_values_.size());

        // loop over child flags is off.
        int n = 0;
        for (int32 i = 0; i < int32(NUM_VALUES); i++) {
          if (child_mask_.isOff(i)) {
            // mNodes[iter.pos()].setValue(values[n++]);
            n++;
          }
        }
        TINYVDBIO_ASSERT(n == num_values);
      } else {
        // loop over child flags is off.
        for (int32 i = 0; i < int32(NUM_VALUES); i++) {
          if (child_mask_.isOff(i)) {
            // mNodes[iter.pos()].setValue(values[iter.pos());
          }
        }
      }
    }

    TINYVDB_LOG << "SIZE = " << child_mask_.SIZE << std::endl;
    child_nodes_.resize(child_mask_.SIZE, grid_layout_info_);
    

    // loop over child node
    for (int32 i = 0; i < child_mask_.SIZE; i++) {
      if (child_mask_.isOn(i)) {
        //if (node_desc_.child_node_desc_) {
        if (1) { // HACK
          TINYVDBIO_ASSERT(i < child_nodes_.size());
          //child_nodes_[i] = new InternalOrLeafNode
          if (!child_nodes_[i].ReadTopology(sr, level + 1, params, warn, err)) {
            return false;
          }
        } else { // leaf
          // TODO: add to child.
          TINYVDBIO_ASSERT(0);
        } 
      }
    }

  return true;

  } else { // leaf

    value_mask_.Alloc(grid_layout_info_.GetNodeInfo(level).log2dim());
    bool ret = value_mask_.load(sr);

    if (!ret) {
      if (err) {
        (*err) += "Failed to load value mask in leaf node.\n";
      }
      return false;
    }

    num_voxels_ = 1 << (3 * grid_layout_info_.GetNodeInfo(level).log2dim());

    return true;

  }

}

bool InternalOrLeafNode::ReadBuffer(StreamReader *sr, int level, const DeserializeParams &params,
                              std::string *warn, std::string *err) {

  int node_type = grid_layout_info_.GetNodeInfo(level).node_type();
  TINYVDB_LOG << "internalOrLeaf : read buffer" << std::endl;

  if (node_type == NODE_TYPE_INTERNAL) {
    size_t count = 0;
    for (int32 i = 0; i < child_mask_.SIZE; i++) {
      if (child_mask_.isOn(i)) {
        TINYVDB_LOG << "InternalOrLeafNode.ReadBuffer[" << count << "]" << std::endl;
        // TODO: FIXME
        if (!child_nodes_[i].ReadBuffer(sr, level + 1, params, warn, err)) {
          return false;
        }
        count++;
      }
    }

    return true;

  } else { // leaf
    TINYVDBIO_ASSERT(node_type == NODE_TYPE_LEAF);
    char num_buffers = 1;

    TINYVDB_LOG << "LeafNode.ReadBuffer pos = " << sr->tell() << std::endl;
    TINYVDB_LOG << " value_mask_.memUsage = " << value_mask_.memUsage() << std::endl;

    // Seek over the value mask.
    sr->seek_from_currect(value_mask_.memUsage());

    TINYVDB_LOG << "is pos = " << sr->tell() << std::endl;

    if (params.file_version < TINYVDB_FILE_VERSION_NODE_MASK_COMPRESSION) {
      int coord[3];

      // Read coordinate origin and num buffers.
      sr->read4(&coord[0]);
      sr->read4(&coord[1]);
      sr->read4(&coord[2]);

      sr->read1(&num_buffers);
      TINYVDBIO_ASSERT(num_buffers == 1);

    }

    // TODO(syoyo): clipBBox check.

    TINYVDBIO_ASSERT(num_voxels_ > 0);

    const ValueType value_type = grid_layout_info_.GetNodeInfo(level).value_type();

    // tinyusdz patch: decode leaf voxels DENSELY via ReadMaskValues. The
    // upstream inline path only read per_node_flag + values and ignored the
    // inactive-value(s) and selection mask that OpenVDB writes for active-mask
    // compressed leaves (the default), which misaligned the stream and
    // corrupted every subsequent leaf. ReadMaskValues reads per_node_flag,
    // any inactive values, the selection mask, the (possibly compacted) active
    // values, and scatters them back into a dense `num_voxels_` buffer with
    // inactive voxels reconstructed from the background/inactive value.
    data_.resize(size_t(num_voxels_) * GetValueTypeSize(value_type));

    bool ret = ReadMaskValues(sr, params.compression_flags, params.file_version,
                              params.background, size_t(num_voxels_), value_type,
                              value_mask_, &data_, params.half_precision, warn,
                              err);

    return ret;
  }
}

GridDescriptor::GridDescriptor()
    : save_float_as_half_(false), grid_pos_(0), block_pos_(0), end_pos_(0) {}

GridDescriptor::GridDescriptor(const std::string &name,
                               const std::string &grid_type,
                               bool save_float_as_half)
    : grid_name_(StripSuffix(name)),
      unique_name_(name),
      grid_type_(grid_type),
      save_float_as_half_(save_float_as_half),
      grid_pos_(0),
      block_pos_(0),
      end_pos_(0) {}

// GridDescriptor::GridDescriptor(const GridDescriptor &rhs) {
//}

// GridDescriptor::~GridDescriptor() {}

std::string GridDescriptor::AddSuffix(const std::string &name, int n) {
  std::ostringstream ss;
  ss << name << SEP << n;
  return ss.str();
}

std::string GridDescriptor::StripSuffix(const std::string &name) {
  return name.substr(0, name.find(SEP));
}

bool GridDescriptor::Read(StreamReader *sr, const unsigned int file_version,
                          std::string *err) {
  (void)file_version;

  unique_name_ = ReadString(sr);
  grid_name_ = StripSuffix(unique_name_);

  grid_type_ = ReadString(sr);

  if (EndsWidth(grid_type_, HALF_FLOAT_TYPENAME_SUFFIX)) {
    save_float_as_half_ = true;
    // strip suffix
    std::string tmp =
        grid_type_.substr(0, grid_type_.find(HALF_FLOAT_TYPENAME_SUFFIX));
    grid_type_ = tmp;
  }

  // tinyusdz patch: do NOT fail here on an unsupported grid type. The descriptor
  // layout (instance parent + grid/block/end offsets below) is identical for all
  // grid types, so we read it fully and let the grid-data reader skip grids whose
  // type we can't decode (see GridDescriptor::IsSupported). This lets a multi-grid
  // file (e.g. density FloatTree + a vector/point grid) still load its float grids
  // instead of failing outright.
  (void)err;

  TINYVDB_LOG << "grid_type = " << grid_type_ << std::endl;
  TINYVDB_LOG << "half = " << save_float_as_half_ << std::endl;

  {
    instance_parent_name_ = ReadString(sr);
    TINYVDB_LOG << "instance_parent_name = " << instance_parent_name_
              << std::endl;
  }

  // Create the grid of the type if it has been registered.
  // if (!GridBase::isRegistered(mGridType)) {
  //    OPENVDB_THROW(LookupError, "Cannot read grid." <<
  //        " Grid type " << mGridType << " is not registered.");
  //}
  // else
  // GridBase::Ptr grid = GridBase::createGrid(mGridType);
  // if (grid) grid->setSaveFloatAsHalf(mSaveFloatAsHalf);

  // Read in the offsets.
  sr->read8(&grid_pos_);
  sr->read8(&block_pos_);
  sr->read8(&end_pos_);

  TINYVDB_LOG << "grid_pos = " << grid_pos_ << std::endl;
  TINYVDB_LOG << "block_pos = " << block_pos_ << std::endl;
  TINYVDB_LOG << "end_pos = " << end_pos_ << std::endl;

  return true;
}

static bool ReadMeta(StreamReader *sr) {
  // Read the number of metadata items.
  int count = 0;
  sr->read4(&count);

  if (count > 1024) {
    // Too many meta values.
    return false;
  }

  TINYVDB_LOG << "meta_count = " << count << std::endl;

  for (int i = 0; i < count; i++) {
    std::string name = ReadString(sr);

    // read typename string
    std::string type_name = ReadString(sr);

    TINYVDB_LOG << "meta[" << i << "] name = " << name
              << ", type_name = " << type_name << std::endl;

    if (type_name.compare("string") == 0) {
      std::string value = ReadString(sr);

      TINYVDB_LOG << "  value = " << value << std::endl;

    } else if (type_name.compare("vec3i") == 0) {
      int v[3];
      ReadMetaVec3i(sr, v);

      TINYVDB_LOG << "  value = " << v[0] << ", " << v[1] << ", " << v[2]
                << std::endl;

    } else if (type_name.compare("vec3d") == 0) {
      double v[3];
      ReadMetaVec3d(sr, v);

      TINYVDB_LOG << "  value = " << v[0] << ", " << v[1] << ", " << v[2]
                << std::endl;

    } else if (type_name.compare("bool") == 0) {
      bool b = ReadMetaBool(sr);

      TINYVDB_LOG << "  value = " << b << std::endl;

    } else if (type_name.compare("float") == 0) {
      float f = ReadMetaFloat(sr);

      TINYVDB_LOG << "  value = " << f << std::endl;

    } else if (type_name.compare("int64") == 0) {
      tinyvdb_int64 i64 = ReadMetaInt64(sr);

      TINYVDB_LOG << "  value = " << i64 << std::endl;

    } else {
      // Unknown metadata
      int num_bytes;
      sr->read4(&num_bytes);

      TINYVDB_LOG << "  unknown value. size = " << num_bytes << std::endl;

      std::vector<char> data;
      data.resize(size_t(num_bytes));
      sr->read(size_t(num_bytes), tinyvdb_uint64(num_bytes),
               reinterpret_cast<unsigned char *>(data.data()));
    }
  }

  return true;
}

static bool ReadGridDescriptors(StreamReader *sr,
                                const unsigned int file_version,
                                std::map<std::string, GridDescriptor> *gd_map) {
  // Read the number of grid descriptors.
  int count = 0;
  sr->read4(&count);

  TINYVDB_LOG << "grid descriptor counts = " << count << std::endl;

  for (int i = 0; i < count; ++i) {
    // Read the grid descriptor.
    GridDescriptor gd;
    std::string err;
    bool ret = gd.Read(sr, file_version, &err);
    if (!ret) {
      return false;
    }

    //  // Add the descriptor to the dictionary.
    (*gd_map)[gd.GridName()] = gd;

    // Skip forward to the next descriptor.
    sr->seek_set(gd.EndPos());
  }

  return true;
}

// tinyusdz patch: out-params `voxel_size_out` (world-space voxel size) and
// `translation_out` (world-space origin offset) may be null. Also reads the
// extra translation vector for UniformScaleTranslateMap, which the upstream
// code omitted (it under-read the stream and misaligned the following tree
// data for translated grids).
static bool ReadTransform(StreamReader *sr, double voxel_size_out[3],
                          double translation_out[3], std::string *err) {
  // Read the type name.
  std::string type = ReadString(sr);

  TINYVDB_LOG << "transform type = " << type << std::endl;

  double scale_values[3];
  double voxel_size[3];
  double scale_values_inverse[3];
  double inv_scale_squared[3];
  double inv_twice_scale[3];
  double translation[3] = {0.0, 0.0, 0.0};

  // ScaleMap / ScaleTranslateMap (and their Uniform* subclasses) all serialize
  // the same way: scale, voxelSize, scaleInverse, invScaleSqr, invTwiceScale,
  // and — for the *Translate* variants — an extra translation vector.
  const bool is_scale_map =
      (type.compare("UniformScaleMap") == 0) || (type.compare("ScaleMap") == 0);
  const bool is_scale_translate_map =
      (type.compare("UniformScaleTranslateMap") == 0) ||
      (type.compare("ScaleTranslateMap") == 0);

  if (is_scale_map || is_scale_translate_map) {
    TINYVDB_LOG << "offt = " << sr->tell() << std::endl;

    scale_values[0] = scale_values[1] = scale_values[2] = 0.0;
    voxel_size[0] = voxel_size[1] = voxel_size[2] = 0.0;
    scale_values_inverse[0] = scale_values_inverse[1] =
        scale_values_inverse[2] = 0.0;
    inv_scale_squared[0] = inv_scale_squared[1] = inv_scale_squared[2] = 0.0;
    inv_twice_scale[0] = inv_twice_scale[1] = inv_twice_scale[2] = 0.0;

    ReadVec3d(sr, scale_values);
    ReadVec3d(sr, voxel_size);
    ReadVec3d(sr, scale_values_inverse);
    ReadVec3d(sr, inv_scale_squared);
    ReadVec3d(sr, inv_twice_scale);

    // ScaleTranslateMap serializes an additional translation vector.
    if (is_scale_translate_map) {
      ReadVec3d(sr, translation);
    }

    TINYVDB_LOG << "voxel_size " << voxel_size[0] << ", " << voxel_size[1] << ", "
              << voxel_size[2] << std::endl;

    if (voxel_size_out) {
      voxel_size_out[0] = voxel_size[0];
      voxel_size_out[1] = voxel_size[1];
      voxel_size_out[2] = voxel_size[2];
    }
    if (translation_out) {
      translation_out[0] = translation[0];
      translation_out[1] = translation[1];
      translation_out[2] = translation[2];
    }
  } else {
    if (err) {
      (*err) = "Transform type `" + type + "' is not supported.\n";
    }
    return false;
  }

  return true;
}

static unsigned int ReadGridCompression(StreamReader *sr,
                                        unsigned int file_version) {
  unsigned int compression = COMPRESS_NONE;
  if (file_version >= TINYVDB_FILE_VERSION_NODE_MASK_COMPRESSION) {
    sr->read4(&compression);
  }
  return compression;
}

static bool ReadGrid(StreamReader *sr, const unsigned int file_version,
                     const bool half_precision, const GridDescriptor &gd,
                     std::string *warn, std::string *err) {
  // read compression per grid(optional)
  unsigned int grid_compression = ReadGridCompression(sr, file_version);

  DeserializeParams params;
  params.file_version = file_version;
  params.compression_flags = grid_compression;
  params.half_precision = half_precision;

  // read meta
  if (!ReadMeta(sr)) {
    return false;
  }

  // read transform
  if (!ReadTransform(sr, NULL, NULL, err)) {
    return false;
  }

  // read topology
  if (!gd.IsInstance()) {
    GridLayoutInfo layout_info;

    // TODO(syoyo): Construct node hierarchy based on header description.
    NodeInfo root(NODE_TYPE_ROOT, VALUE_TYPE_FLOAT, 0);
    NodeInfo intermediate1(NODE_TYPE_INTERNAL, VALUE_TYPE_FLOAT, 5);
    NodeInfo intermediate2(NODE_TYPE_INTERNAL, VALUE_TYPE_FLOAT, 4);
    NodeInfo leaf(NODE_TYPE_LEAF, VALUE_TYPE_FLOAT, 3);

    layout_info.Add(root);
    layout_info.Add(intermediate1);
    layout_info.Add(intermediate2);
    layout_info.Add(leaf);

    RootNode root_node(layout_info);

    // TreeBase
    {
      int buffer_count;
      sr->read4(&buffer_count);
      if (buffer_count != 1) {
        if (warn) {
          (*warn) += "multi-buffer trees are no longer supported.";
        }
      }
    }

    if (!root_node.ReadTopology(sr, /* level */0, params, warn, err)) {
      return false;
    }

    // TODO(syoyo): Consider bbox(ROI)
    if (!root_node.ReadBuffer(sr, /* level */0, params, warn, err)) {
      return false;
    }

    TINYVDB_LOG << "end = " << sr->tell() << std::endl;

  } else {
    // TODO
    TINYVDBIO_ASSERT(0);
  }

  // Move to grid position
  sr->seek_set(tinyvdb_uint64(gd.GridPos()));
    
  return true;
}

VDBStatus ParseVDBHeader(const std::string &filename, VDBHeader *header,
                         std::string *err) {
  if (header == NULL) {
    if (err) {
      (*err) = "Invalid function arguments";
    }
    return TINYVDBIO_ERROR_INVALID_ARGUMENT;
  }

  // TODO(Syoyo): Load only header region.
  std::vector<unsigned char> data;
  {
    std::ifstream ifs(filename.c_str(), std::ifstream::binary);
    if (!ifs) {
      if (err) {
        (*err) = "File not found or cannot open file : " + filename;
      }
      return TINYVDBIO_ERROR_INVALID_FILE;
    }

    // TODO(syoyo): Use mmap
    ifs.seekg(0, ifs.end);
    size_t sz = static_cast<size_t>(ifs.tellg());
    if (tinyvdb_int64(sz) < 0) {
      // Looks reading directory, not a file.
      if (err) {
        (*err) += "Looks like filename is a directory : \"" + filename + "\"\n";
      }
      return TINYVDBIO_ERROR_INVALID_FILE;
    }

    if (sz < 16) {
      // ???
      if (err) {
        (*err) +=
            "File size too short. Looks like this file is not a VDB : \"" +
            filename + "\"\n";
      }
      return TINYVDBIO_ERROR_INVALID_FILE;
    }

    data.resize(sz);

    ifs.seekg(0, ifs.beg);
    ifs.read(reinterpret_cast<char *>(&data.at(0)),
             static_cast<std::streamsize>(sz));
  }

  VDBStatus status = ParseVDBHeader(data.data(), data.size(), header, err);
  return status;
}

static bool IsBigEndian(void) {
  union {
    unsigned int i;
    char c[4];
  } bint = {0x01020304};

  return bint.c[0] == 1;
}

VDBStatus ParseVDBHeader(const unsigned char *data, const size_t len,
                         VDBHeader *header, std::string *err) {
  tinyvdb_int64 magic;

  // OpenVDB stores data in little endian manner(e.g. x86).
  // Swap bytes for big endian architecture(e.g. Power, SPARC)
  bool swap_endian = IsBigEndian();

  StreamReader sr(data, len, swap_endian);

  // [0:7] magic number
  if (!sr.read8(&magic)) {
    if (err) {
      (*err) += "Failed to read magic number.\n";
    }
    return TINYVDBIO_ERROR_INVALID_HEADER;
  }

  if (magic == kOPENVDB_MAGIC) {
    TINYVDB_LOG << "bingo!" << std::endl;
  } else {
    if (err) {
      (*err) += "Invalid magic number for VDB.\n";
    }
    return TINYVDBIO_ERROR_INVALID_HEADER;
  }

  // [8:11] version
  unsigned int file_version = 0;
  if (!sr.read4(&file_version)) {
    if (err) {
      (*err) += "Failed to read file version.\n";
    }
    return TINYVDBIO_ERROR_INVALID_HEADER;
  }

  TINYVDB_LOG << "File version: " << file_version << std::endl;

  if (file_version < TINYVDB_FILE_VERSION_SELECTIVE_COMPRESSION) {
    if (err) {
      (*err) =
          "VDB file version earlier than "
          "TINYVDB_FILE_VERSION_SELECTIVE_COMPRESSION(220) is not supported.";
    }
    return TINYVDBIO_ERROR_UNIMPLEMENTED;
  }

  // Read the library version numbers (not stored prior to file format version
  // 211).
  unsigned int major_version = 0;
  unsigned int minor_version = 0;
  if (file_version >= 211) {
    sr.read4(&major_version);
    TINYVDB_LOG << "major version : " << major_version << std::endl;
    sr.read4(&minor_version);
    TINYVDB_LOG << "minor version : " << minor_version << std::endl;
  }

  // Read the flag indicating whether the stream supports partial reading.
  // (Versions prior to 212 have no flag because they always supported partial
  // reading.)
  char has_grid_offsets = 0;
  if (file_version >= 212) {
    sr.read1(&has_grid_offsets);
    TINYVDB_LOG << "InputHasGridOffsets = "
              << (has_grid_offsets ? " yes " : " no ") << std::endl;
  }

  if (!has_grid_offsets) {
    // Unimplemened.
    if (err) {
      (*err) = "VDB withoput grid offset is not supported in TinyVDBIO.";
    }
    return TINYVDBIO_ERROR_UNIMPLEMENTED;
  }

  // 5) Read the flag that indicates whether data is compressed.
  //    (From version 222 on, compression information is stored per grid.)
  // mCompression = DEFAULT_COMPRESSION_FLAGS;
  // if (file_version < TINYVDB_FILE_VERSION_BLOSC_COMPRESSION) {
  //    // Prior to the introduction of Blosc, ZLIB was the default compression
  //    scheme. mCompression = (COMPRESS_ZIP | COMPRESS_ACTIVE_MASK);
  //}
  char is_compressed = 0;
  if (file_version >= TINYVDB_FILE_VERSION_SELECTIVE_COMPRESSION &&
      file_version < TINYVDB_FILE_VERSION_NODE_MASK_COMPRESSION) {
    sr.read1(&is_compressed);
    TINYVDB_LOG << "Compression : " << (is_compressed != 0 ? "zip" : "none")
              << std::endl;
    if (file_version >= TINYVDB_FILE_VERSION_BLOSC_COMPRESSION) {
    }
  }

  // 6) Read uuid.
  {
    // ASCII UUID = 32 chars + 4 '-''s = 36 bytes.
    char uuid[36];
    sr.read(36, 36, reinterpret_cast<unsigned char *>(uuid));
    std::string uuid_string = std::string(uuid, 36);
    // TODO(syoyo): Store UUID somewhere.
    TINYVDB_LOG << "uuid ASCII: " << uuid_string << std::endl;
    header->uuid = std::string(uuid, 36);
  }

  header->file_version = file_version;
  header->major_version = major_version;
  header->minor_version = minor_version;
  // header->has_grid_offsets = has_grid_offsets;
  header->is_compressed = is_compressed;
  header->offset_to_data = sr.tell();

  return TINYVDBIO_SUCCESS;
}

VDBStatus ReadGridDescriptors(const std::string &filename,
                              const VDBHeader &header,
                              std::map<std::string, GridDescriptor> *gd_map,
                              std::string *err) {
  std::vector<unsigned char> data;
  {
    std::ifstream ifs(filename.c_str(), std::ifstream::binary);
    if (!ifs) {
      if (err) {
        (*err) = "File not found or cannot open file : " + filename;
      }
      return TINYVDBIO_ERROR_INVALID_FILE;
    }

    // TODO(syoyo): Use mmap
    ifs.seekg(0, ifs.end);
    size_t sz = static_cast<size_t>(ifs.tellg());
    if (tinyvdb_int64(sz) < 0) {
      // Looks reading directory, not a file.
      if (err) {
        (*err) += "Looks like filename is a directory : \"" + filename + "\"\n";
      }
      return TINYVDBIO_ERROR_INVALID_FILE;
    }

    if (sz < 16) {
      // ???
      if (err) {
        (*err) +=
            "File size too short. Looks like this file is not a VDB : \"" +
            filename + "\"\n";
      }
      return TINYVDBIO_ERROR_INVALID_FILE;
    }

    data.resize(sz);

    ifs.seekg(0, ifs.beg);
    ifs.read(reinterpret_cast<char *>(&data.at(0)),
             static_cast<std::streamsize>(sz));
  }

  VDBStatus status =
      ReadGridDescriptors(data.data(), data.size(), header, gd_map, err);
  return status;
}

VDBStatus ReadGridDescriptors(const unsigned char *data, const size_t data_len,
                              const VDBHeader &header,
                              std::map<std::string, GridDescriptor> *gd_map,
                              std::string *err) {
  bool swap_endian = IsBigEndian();
  StreamReader sr(data, data_len, swap_endian);

  if (!sr.seek_set(header.offset_to_data)) {
    if (err) {
      (*err) += "Failed to seek into data.\n";
    }
    return TINYVDBIO_ERROR_INVALID_DATA;
  }

  // Read meta data.
  {
    bool ret = ReadMeta(&sr);
    TINYVDB_LOG << "meta: " << ret << std::endl;
  }

  if (!ReadGridDescriptors(&sr, header.file_version, gd_map)) {
    if (err) {
      (*err) += "Failed to read grid descriptors.\n";
    }
    return TINYVDBIO_ERROR_INVALID_DATA;
  }

  return TINYVDBIO_SUCCESS;
}

VDBStatus ReadGrids(const std::string &filename, const VDBHeader &header,
                    const std::map<std::string, GridDescriptor> &gd_map,
                    std::string *warn, std::string *err) {
  std::vector<unsigned char> data;
  {
    std::ifstream ifs(filename.c_str(), std::ifstream::binary);
    if (!ifs) {
      if (err) {
        (*err) = "File not found or cannot open file : " + filename;
      }
      return TINYVDBIO_ERROR_INVALID_FILE;
    }

    // TODO(syoyo): Use mmap
    ifs.seekg(0, ifs.end);
    size_t sz = static_cast<size_t>(ifs.tellg());
    if (tinyvdb_int64(sz) < 0) {
      // Looks reading directory, not a file.
      if (err) {
        (*err) += "Looks like filename is a directory : \"" + filename + "\"\n";
      }
      return TINYVDBIO_ERROR_INVALID_FILE;
    }

    if (sz < 16) {
      // ???
      if (err) {
        (*err) +=
            "File size too short. Looks like this file is not a VDB : \"" +
            filename + "\"\n";
      }
      return TINYVDBIO_ERROR_INVALID_FILE;
    }

    data.resize(sz);

    ifs.seekg(0, ifs.beg);
    ifs.read(reinterpret_cast<char *>(&data.at(0)),
             static_cast<std::streamsize>(sz));
  }

  VDBStatus status = ReadGrids(data.data(), data.size(), header, gd_map, warn, err);
  return status;
}

VDBStatus ReadGrids(const unsigned char *data, const size_t data_len,
                    const VDBHeader &header,
                    const std::map<std::string, GridDescriptor> &gd_map,
                    std::string *warn, std::string *err) {
  bool swap_endian = IsBigEndian();
  StreamReader sr(data, data_len, swap_endian);

  std::map<std::string, GridDescriptor>::const_iterator it(gd_map.begin());
  std::map<std::string, GridDescriptor>::const_iterator itEnd(gd_map.end());

  for (; it != itEnd; it++) {
    const GridDescriptor &gd = it->second;

    sr.seek_set(gd.GridPos());

    if (!ReadGrid(&sr, header.file_version, header.half_precision, gd, warn, err)) {
      if (err) {
        (*err) += "Failed to read Grid data.\n";
      }
      return TINYVDBIO_ERROR_INVALID_DATA;
    }
  }

  return TINYVDBIO_SUCCESS;
}

// ===========================================================================
// tinyusdz patch: dense float-grid extraction (Tree4<float,5,4,3>).
// ===========================================================================

// log2dim per level: [0]=root (unused), [1]/[2]=internal, [3]=leaf.
static const int kTinyVDBLog2Dim[4] = {0, 5, 4, 3};

// Number of voxels (per axis) spanned by one CHILD of a node at `level`.
static int TinyVDBChildSpan(int level) {
  int s = 0;
  for (int l = level + 1; l <= 3; ++l) s += kTinyVDBLog2Dim[l];
  return 1 << s;
}

struct TinyVDBLeafRef {
  int origin[3];
  const InternalOrLeafNode *node;
};

// Recursively walk the decoded tree, computing each leaf's global voxel-space
// origin from the (now retained) per-child origins / slot indices.
static void TinyVDBCollectLeaves(const InternalOrLeafNode &node, int level,
                                 int num_levels, const int origin[3],
                                 std::vector<TinyVDBLeafRef> *out) {
  if (level == num_levels - 1) {
    TinyVDBLeafRef ref;
    ref.origin[0] = origin[0];
    ref.origin[1] = origin[1];
    ref.origin[2] = origin[2];
    ref.node = &node;
    out->push_back(ref);
    return;
  }

  const int L = kTinyVDBLog2Dim[level];
  const int dim_mask = (1 << L) - 1;
  const int child_span = TinyVDBChildSpan(level);
  const NodeMask &cmask = node.GetChildMask();
  const std::vector<InternalOrLeafNode> &children = node.GetChildNodes();

  for (int32 i = 0; i < cmask.SIZE; i++) {
    if (!cmask.isOn(i)) continue;
    if (size_t(i) >= children.size()) continue;
    // OpenVDB linear-offset ordering: x is most significant, z least.
    const int x = (i >> (2 * L)) & dim_mask;
    const int y = (i >> L) & dim_mask;
    const int z = i & dim_mask;
    int child_origin[3] = {origin[0] + x * child_span,
                           origin[1] + y * child_span,
                           origin[2] + z * child_span};
    TinyVDBCollectLeaves(children[size_t(i)], level + 1, num_levels,
                         child_origin, out);
  }
}

// Decode one grid (positioned at gd.GridPos()) into a dense float grid.
static bool ReadDenseGrid(StreamReader *sr, const VDBHeader &header,
                          const GridDescriptor &gd, DenseFloatGrid *out,
                          size_t max_voxels, std::string *warn,
                          std::string *err) {
  // OpenVDB half-compresses only the LEAF voxel buffers (save_float_as_half);
  // the tree topology values (background, tile and inactive values) remain full
  // float. So we keep the root/internal levels float and read leaves as half.
  // NOTE: half-ness is per-GRID (SaveFloatAsHalf); the file-level
  // header.half_precision is a legacy/global hint and must NOT force half for a
  // full-precision grid (doing so reads 2-byte halves out of 4-byte floats).
  const bool is_half = gd.SaveFloatAsHalf();

  if (gd.IsInstance()) {
    if (warn) {
      (*warn) += "Instanced VDB grids are not supported; skipping grid: " +
                 gd.GridName() + ".\n";
    }
    return false;
  }

  const unsigned int file_version = header.file_version;

  // read compression per grid(optional)
  unsigned int grid_compression = ReadGridCompression(sr, file_version);
#if !defined(TINYVDBIO_VENDORED_BLOSC) && !defined(TINYVDBIO_USE_BLOSC)
  if (grid_compression & COMPRESS_BLOSC) {
    if (err) {
      (*err) += "BLOSC-compressed VDB grids are not supported (grid: " +
                gd.GridName() + ").\n";
    }
    return false;
  }
#endif

  DeserializeParams params;
  params.file_version = file_version;
  params.compression_flags = grid_compression;
  // Half-ness is per-GRID (SaveFloatAsHalf), not the file-level
  // header.half_precision hint. In a save-float-as-half grid BOTH the leaf
  // voxels and the internal-node tile values are stored as 16-bit half, so the
  // whole tree must read at half element size (see ReadMaskValues).
  params.half_precision = is_half;

  if (!ReadMeta(sr)) {
    return false;
  }

  double voxel_size[3] = {1.0, 1.0, 1.0};
  double translation[3] = {0.0, 0.0, 0.0};
  if (!ReadTransform(sr, voxel_size, translation, err)) {
    return false;
  }

  GridLayoutInfo layout_info;
  layout_info.Add(NodeInfo(NODE_TYPE_ROOT, VALUE_TYPE_FLOAT, 0));
  layout_info.Add(NodeInfo(NODE_TYPE_INTERNAL, VALUE_TYPE_FLOAT, 5));
  layout_info.Add(NodeInfo(NODE_TYPE_INTERNAL, VALUE_TYPE_FLOAT, 4));
  layout_info.Add(NodeInfo(NODE_TYPE_LEAF,
                           is_half ? VALUE_TYPE_HALF : VALUE_TYPE_FLOAT, 3));
  const int num_levels = layout_info.NumLevels();
  const size_t leaf_elem_size = is_half ? sizeof(unsigned short) : sizeof(float);

  RootNode root_node(layout_info);

  {
    int buffer_count;
    sr->read4(&buffer_count);
    if (buffer_count != 1 && warn) {
      (*warn) += "multi-buffer trees are no longer supported.";
    }
  }

  if (!root_node.ReadTopology(sr, /* level */ 0, params, warn, err)) {
    return false;
  }

  // tinyusdz patch: feed the root background into params so leaf inactive
  // voxels reconstruct correctly during the buffer pass.
  const float background = float(root_node.background_float());
  params.background = Value(background);

  if (!root_node.ReadBuffer(sr, /* level */ 0, params, warn, err)) {
    return false;
  }

  // Walk the tree and collect leaves with global origins.
  std::vector<TinyVDBLeafRef> leaves;
  const std::vector<InternalOrLeafNode> &top = root_node.GetChildNodes();
  const std::vector<std::array<int, 3> > &top_origins =
      root_node.GetChildOrigins();
  for (size_t k = 0; k < top.size(); k++) {
    if (k >= top_origins.size()) break;
    int o[3] = {top_origins[k][0], top_origins[k][1], top_origins[k][2]};
    TinyVDBCollectLeaves(top[k], /* level */ 1, num_levels, o, &leaves);
  }

  out->name = gd.GridName();
  out->background = background;
  for (int a = 0; a < 3; a++) {
    out->voxel_size[a] = voxel_size[a];
    out->world_translation[a] = translation[a];
  }

  if (leaves.empty()) {
    out->origin[0] = out->origin[1] = out->origin[2] = 0;
    out->dim[0] = out->dim[1] = out->dim[2] = 0;
    out->data.clear();
    return true;
  }

  const int leaf_dim = 1 << kTinyVDBLog2Dim[3];  // 8

  int minc[3] = {leaves[0].origin[0], leaves[0].origin[1], leaves[0].origin[2]};
  int maxc[3] = {minc[0] + leaf_dim, minc[1] + leaf_dim, minc[2] + leaf_dim};
  for (size_t i = 0; i < leaves.size(); i++) {
    for (int a = 0; a < 3; a++) {
      if (leaves[i].origin[a] < minc[a]) minc[a] = leaves[i].origin[a];
      if (leaves[i].origin[a] + leaf_dim > maxc[a])
        maxc[a] = leaves[i].origin[a] + leaf_dim;
    }
  }

  const int dim[3] = {maxc[0] - minc[0], maxc[1] - minc[1], maxc[2] - minc[2]};
  const size_t total = size_t(dim[0]) * size_t(dim[1]) * size_t(dim[2]);
  // Default per-grid voxel cap: 640^3 (~262M voxels, ~1 GiB as dense float).
  // Large enough for production fog volumes (e.g. bunny_cloud 584x576x440,
  // explosion) while still rejecting huge narrow-band level sets that should not
  // be densified (dragon/armadillo are 2-2.5 billion voxels). Override per call.
  const size_t cap = (max_voxels == 0) ? (size_t(640) * 640 * 640) : max_voxels;
  if (total > cap) {
    if (err) {
      std::stringstream ss;
      ss << "VDB grid '" << gd.GridName() << "' dense voxel count " << total
         << " (" << dim[0] << "x" << dim[1] << "x" << dim[2]
         << ") exceeds the cap " << cap << ".\n";
      (*err) += ss.str();
    }
    return false;
  }

  out->origin[0] = minc[0];
  out->origin[1] = minc[1];
  out->origin[2] = minc[2];
  out->dim[0] = dim[0];
  out->dim[1] = dim[1];
  out->dim[2] = dim[2];
  out->data.assign(total, background);

  const int leaf_L = kTinyVDBLog2Dim[3];  // 3
  const int leaf_mask = (1 << leaf_L) - 1;
  const size_t num_leaf_voxels = size_t(1) << (3 * leaf_L);  // 512

  for (size_t i = 0; i < leaves.size(); i++) {
    const std::vector<unsigned char> &d = leaves[i].node->GetLeafData();
    if (d.size() < num_leaf_voxels * leaf_elem_size) continue;
    const int ox = leaves[i].origin[0] - minc[0];
    const int oy = leaves[i].origin[1] - minc[1];
    const int oz = leaves[i].origin[2] - minc[2];
    for (size_t n = 0; n < num_leaf_voxels; n++) {
      const int lx = int(n >> (2 * leaf_L)) & leaf_mask;
      const int ly = int(n >> leaf_L) & leaf_mask;
      const int lz = int(n) & leaf_mask;
      float v;
      if (is_half) {
        unsigned short hbits;
        std::memcpy(&hbits, &d[n * leaf_elem_size], sizeof(unsigned short));
        v = TinyVDBHalfBitsToFloat(hbits);
      } else {
        std::memcpy(&v, &d[n * leaf_elem_size], sizeof(float));
      }
      const size_t gx = size_t(ox + lx);
      const size_t gy = size_t(oy + ly);
      const size_t gz = size_t(oz + lz);
      const size_t idx = gx + size_t(dim[0]) * (gy + size_t(dim[1]) * gz);
      out->data[idx] = v;
    }
  }

  return true;
}

VDBStatus ReadDenseGrids(const unsigned char *data, const size_t data_len,
                         const VDBHeader &header,
                         const std::map<std::string, GridDescriptor> &gd_map,
                         std::vector<DenseFloatGrid> *grids, size_t max_voxels,
                         std::string *warn, std::string *err) {
  if (!grids) {
    if (err) (*err) += "ReadDenseGrids: `grids` output is null.\n";
    return TINYVDBIO_ERROR_INVALID_ARGUMENT;
  }

  bool swap_endian = IsBigEndian();
  StreamReader sr(data, data_len, swap_endian);

  std::map<std::string, GridDescriptor>::const_iterator it(gd_map.begin());
  std::map<std::string, GridDescriptor>::const_iterator itEnd(gd_map.end());

  for (; it != itEnd; it++) {
    const GridDescriptor &gd = it->second;
    if (gd.IsInstance()) continue;

    // Skip grids we cannot decode (non-FloatTree: point-index, vector, bool, ...)
    // rather than failing the whole file.
    if (!gd.IsSupported()) {
      if (warn) {
        (*warn) += "Skipping unsupported VDB grid '" + gd.GridName() +
                   "' of type '" + gd.GridType() + "'.\n";
      }
      continue;
    }

    sr.seek_set(gd.GridPos());

    DenseFloatGrid grid;
    if (!ReadDenseGrid(&sr, header, gd, &grid, max_voxels, warn, err)) {
      return TINYVDBIO_ERROR_INVALID_DATA;
    }
    grids->push_back(grid);
  }

  return TINYVDBIO_SUCCESS;
}

static bool WriteVDBHeader(std::ostream &os) {
  // [0:7] magic number
  tinyvdb_int64 magic = kOPENVDB_MAGIC;
  os.write(reinterpret_cast<char *>(&magic), 8);

  // [8:11] version
  unsigned int file_version = kTINYVDB_FILE_VERSION;
  os.write(reinterpret_cast<char *>(&file_version), sizeof(unsigned int));

#if 0  // TODO(syoyo): Implement

  TINYVDB_LOG << "File version: " << file_version << std::endl;

  // Read the library version numbers (not stored prior to file format version
  // 211).
  if (file_version >= 211) {
    unsigned int version;
    ifs.read(reinterpret_cast<char *>(&version), sizeof(unsigned int));
    TINYVDB_LOG << "major version : " << version << std::endl;
    ifs.read(reinterpret_cast<char *>(&version), sizeof(unsigned int));
    TINYVDB_LOG << "minor version : " << version << std::endl;
  }

  // Read the flag indicating whether the stream supports partial reading.
  // (Versions prior to 212 have no flag because they always supported partial
  // reading.)
  char has_grid_offsets = 0;
  if (file_version >= 212) {
    ifs.read(&has_grid_offsets, sizeof(char));
    TINYVDB_LOG << "InputHasGridOffsets = " << (has_grid_offsets ? " yes " : " no ")
              << std::endl;
  }

  // 5) Read the flag that indicates whether data is compressed.
  //    (From version 222 on, compression information is stored per grid.)
  // mCompression = DEFAULT_COMPRESSION_FLAGS;
  // if (file_version < TINYVDB_FILE_VERSION_BLOSC_COMPRESSION) {
  //    // Prior to the introduction of Blosc, ZLIB was the default compression
  //    scheme. mCompression = (COMPRESS_ZIP | COMPRESS_ACTIVE_MASK);
  //}
  char isCompressed = 0;
  if (file_version >= TINYVDB_FILE_VERSION_SELECTIVE_COMPRESSION &&
      file_version < TINYVDB_FILE_VERSION_NODE_MASK_COMPRESSION) {
    ifs.read(&isCompressed, sizeof(char));
    TINYVDB_LOG << "Compression : " << (isCompressed != 0 ? "zip" : "none")
              << std::endl;
  }

  // 6) Read the 16-byte(128-bit) uuid.
  if (file_version >= TINYVDB_FILE_VERSION_BOOST_UUID) {
    // ASCII UUID = 32 chars + 4 '-''s = 36 bytes.
    char uuid[36];
    ifs.read(uuid, 36);
    // TODO(syoyo): Store UUID somewhere.
    TINYVDB_LOG << "uuid: " << uuid << std::endl;
  } else {
    char uuid[16];
    ifs.read(uuid, 16);
    // TODO(syoyo): Store UUID somewhere.
    TINYVDB_LOG << "uuid: " << uuid << std::endl;
  }

  {
    bool ret = ReadMeta(ifs);
    TINYVDB_LOG << "meta: " << ret << std::endl;
  }

  if (has_grid_offsets) {
    ReadGridDescriptors(ifs);
  } else {
  }
#endif

  return true;
}

bool SaveVDB(const std::string &filename, std::string *err) {
  std::ofstream os(filename.c_str(), std::ifstream::binary);

  if (!os) {
    if (err) {
      (*err) = "Failed to open a file to write: " + filename;
    }
    return false;
  }

  WriteVDBHeader(os);
  // if filemane

  return true;
}

void VoxelTree::BuildTree(const InternalOrLeafNode& root, int depth)
{
  (void)root;
  (void)depth; 
}

bool VoxelTree::Build(const RootNode &root)
{
  nodes_.clear();

  // root node
  VoxelNode node;

  nodes_.push_back(node);

  for (size_t i = 0; i < root.GetChildNodes().size(); i++) {
    BuildTree(root.GetChildNodes()[i], 0);
  }

  valid_ = true;
  return true;
}

}  // namespace tinyvdb

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#endif // TINYVDBIO_IMPLEMENTATION
