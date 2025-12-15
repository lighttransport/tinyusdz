// SPDX-License-Identifier: Apache-2.0
// Originally Copyright 2017 Pixar
// Modified for LightUSD
//
// Integer compression implementation
// Based on USD's integer coding implementation

#include "lightusd/integer_coding.hh"
#include "lightusd/lz4_compression.hh"

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <unordered_map>

namespace lightusd {
namespace v1 {

/*

These integer coding & compression routines are tailored for what are typically
lists of indexes into other tables.  The binary "usdc" file format has lots of
these in its "structural sections" that define the object hierarchy.

The basic idea is to take a contiguous list of 32-bit integers and encode them
in a buffer that is not only smaller, but also still quite compressible by a
general compression algorithm, and then compress that buffer to produce a final
result.  Decompression proceeds by going in reverse.  The general compressor is
LZ4 via TfFastCompression.  The integer coding scheme implemented here is
described below.

We encode a list of integers as follows.  First we transform the input to
produce a new list of integers where each element is the difference between it
and the previous integer in the input sequence.  This is the sequence we encode.
Next we find the most common value in the sequence and write it to the output.
Then we write 2-bit codes, one for each integer, classifying it.  Finally we
write a variable length section of integer data.  The decoder uses the 2-bit
codes to understand how to interpret this variable length data.

*/

namespace {

template <class Int>
inline typename std::enable_if<
    std::is_integral<Int>::value &&
    std::is_unsigned<Int>::value &&
    sizeof(Int) == 4,
    int32_t>::type
_Signed(Int x)
{
    if (x <= static_cast<uint32_t>(INT32_MAX))
        return static_cast<int32_t>(x);

    if (x >= static_cast<uint32_t>(INT32_MIN))
        return static_cast<int32_t>(x - INT32_MIN) + INT32_MIN;

    return 0;
}

template <class Int>
inline typename std::enable_if<
    std::is_integral<Int>::value &&
    std::is_signed<Int>::value &&
    sizeof(Int) == 4,
    int32_t>::type
_Signed(Int x)
{
    return x;
}

template <class Int>
inline typename std::enable_if<
    std::is_integral<Int>::value &&
    std::is_unsigned<Int>::value &&
    sizeof(Int) == 8,
    int64_t>::type
_Signed(Int x)
{
    if (x <= static_cast<uint64_t>(INT64_MAX))
        return static_cast<int64_t>(x);

    if (x >= static_cast<uint64_t>(INT64_MIN))
        return static_cast<int64_t>(x - INT64_MIN) + INT64_MIN;

    return 0;
}

template <class Int>
inline typename std::enable_if<
    std::is_integral<Int>::value &&
    std::is_signed<Int>::value &&
    sizeof(Int) == 8,
    int64_t>::type
_Signed(Int x)
{
    return x;
}

template <class T>
inline char *_WriteBits(char *p, T val)
{
    memcpy(p, &val, sizeof(val));
    return p + sizeof(val);
}

template <class T>
inline T _ReadBits(char const *&p)
{
    T ret;
    memcpy(&ret, p, sizeof(ret));
    p += sizeof(ret);
    return ret;
}

template <class Int>
constexpr size_t
_GetEncodedBufferSize(size_t numInts)
{
    // Calculate encoded integer size.
    return numInts ?
        /* commonValue */ (sizeof(Int)) +
        /* numCodesBytes */ ((numInts * 2 + 7) / 8) +
        /* maxIntBytes */ (numInts * sizeof(Int))
        : 0;
}

template <class Int>
struct _SmallTypes
{
    typedef typename std::conditional<
        sizeof(Int) == 4, int8_t, int16_t>::type SmallInt;
    typedef typename std::conditional<
        sizeof(Int) == 4, int16_t, int32_t>::type MediumInt;
};

template <int N, class Iterator>
void _EncodeNHelper(
    Iterator &cur,
    typename std::iterator_traits<Iterator>::value_type commonValue,
    typename std::make_signed<
        typename std::iterator_traits<Iterator>::value_type
    >::type &prevVal,
    char *&codesOut,
    char *&vintsOut) {

    using Int = typename std::iterator_traits<Iterator>::value_type;
    using SInt = typename std::make_signed<Int>::type;
    using SmallInt = typename _SmallTypes<Int>::SmallInt;
    using MediumInt = typename _SmallTypes<Int>::MediumInt;

    static_assert(1 <= N && N <= 4, "");

    enum Code { Common, Small, Medium, Large };

    auto getCode = [commonValue](SInt x) {
        std::numeric_limits<SmallInt> smallLimit;
        std::numeric_limits<MediumInt> mediumLimit;
        if (x == _Signed(commonValue)) { return Common; }
        if (x >= smallLimit.min() && x <= smallLimit.max()) { return Small; }
        if (x >= mediumLimit.min() && x <= mediumLimit.max()) { return Medium; }
        return Large;
    };

    uint8_t codeByte = 0;
    for (int i = 0; i != N; ++i) {
        SInt val = _Signed(*cur) - prevVal;
        prevVal = _Signed(*cur++);
        Code code = getCode(val);
        codeByte |= (code << (2 * i));
        switch (code) {
        default:
        case Common:
            break;
        case Small:
            vintsOut = _WriteBits(vintsOut, static_cast<SmallInt>(val));
            break;
        case Medium:
            vintsOut = _WriteBits(vintsOut, static_cast<MediumInt>(val));
            break;
        case Large:
            vintsOut = _WriteBits(vintsOut, _Signed(val));
            break;
        };
    }
    codesOut = _WriteBits(codesOut, codeByte);
}

template <int N, class Iterator>
void _DecodeNHelper(
    char const *&codesIn,
    char const *&vintsIn,
    typename std::iterator_traits<Iterator>::value_type commonValue,
    typename std::make_signed<
        typename std::iterator_traits<Iterator>::value_type
    >::type &prevVal,
    Iterator &output)
{
    using Int = typename std::iterator_traits<Iterator>::value_type;
    using SInt = typename std::make_signed<Int>::type;
    using SmallInt = typename _SmallTypes<Int>::SmallInt;
    using MediumInt = typename _SmallTypes<Int>::MediumInt;

    enum Code { Common, Small, Medium, Large };

    uint8_t codeByte = *codesIn++;
    for (int i = 0; i != N; ++i) {
        switch ((codeByte & (3 << (2 * i))) >> (2 * i)) {
        default:
        case Common:
            prevVal += commonValue;
            break;
        case Small:
            prevVal += _ReadBits<SmallInt>(vintsIn);
            break;
        case Medium:
            prevVal += _ReadBits<MediumInt>(vintsIn);
            break;
        case Large:
            prevVal += _ReadBits<SInt>(vintsIn);
            break;
        }
        *output++ = static_cast<Int>(prevVal);
    }
}

template <class Int>
size_t
_EncodeIntegers(Int const *begin, size_t numInts, char *output)
{
    using SInt = typename std::make_signed<Int>::type;

    if (numInts == 0)
        return 0;

    // First find the most common element value.
    SInt commonValue = 0;
    {
        size_t commonCount = 0;
        std::unordered_map<SInt, size_t> counts;
        SInt prevVal = 0;
        for (Int const *cur = begin, *end = begin + numInts;
             cur != end; ++cur) {
            SInt val = _Signed(*cur) - prevVal;
            const size_t count = ++counts[val];
            if (count > commonCount) {
                commonValue = val;
                commonCount = count;
            } else if (count == commonCount && val > commonValue) {
                // Take the largest common value in case of a tie -- this gives
                // the biggest potential savings in the encoded stream.
                commonValue = val;
            }
            prevVal = _Signed(*cur);
        }
    }

    // Now code the values.

    // Write most common value.
    char *p = _WriteBits(output, commonValue);
    char *codesOut = p;
    char *vintsOut = p + (numInts * 2 + 7) / 8;

    Int const *cur = begin;
    SInt prevVal = 0;
    while (numInts >= 4) {
        _EncodeNHelper<4>(cur, commonValue, prevVal, codesOut, vintsOut);
        numInts -= 4;
    }
    switch (numInts) {
    case 0: default: break;
    case 1: _EncodeNHelper<1>(cur, commonValue, prevVal, codesOut, vintsOut);
        break;
    case 2: _EncodeNHelper<2>(cur, commonValue, prevVal, codesOut, vintsOut);
        break;
    case 3: _EncodeNHelper<3>(cur, commonValue, prevVal, codesOut, vintsOut);
        break;
    };

    return vintsOut - output;
}

template <class Int>
size_t _DecodeIntegers(char const *data, size_t numInts, Int *result)
{
    using SInt = typename std::make_signed<Int>::type;

    auto commonValue = _ReadBits<SInt>(data);

    size_t numCodesBytes = (numInts * 2 + 7) / 8;
    char const *codesIn = data;
    char const *vintsIn = data + numCodesBytes;

    SInt prevVal = 0;
    auto intsLeft = numInts;
    while (intsLeft >= 4) {
        _DecodeNHelper<4>(codesIn, vintsIn, commonValue, prevVal, result);
        intsLeft -= 4;
    }
    switch (intsLeft) {
    case 0: default: break;
    case 1: _DecodeNHelper<1>(codesIn, vintsIn, commonValue, prevVal, result);
        break;
    case 2: _DecodeNHelper<2>(codesIn, vintsIn, commonValue, prevVal, result);
        break;
    case 3: _DecodeNHelper<3>(codesIn, vintsIn, commonValue, prevVal, result);
        break;
    };

    return numInts;
}

template <class Int>
size_t
_CompressIntegers(Int const *begin, size_t numInts, char *output, std::string *err)
{
    // Working space.
    std::unique_ptr<char[]>
        encodeBuffer(new char[_GetEncodedBufferSize<Int>(numInts)]);

    // Encode first.
    size_t encodedSize = _EncodeIntegers(begin, numInts, encodeBuffer.get());

    (void)encodedSize;
    (void)output;
    (void)err;
    // Compression not implemented yet
    return 0;
}

template <class Int>
size_t _DecompressIntegers(char const *compressed, size_t compressedSize,
                           Int *ints, size_t numInts, std::string *err, char *workingSpace)
{
    // Working space.
    size_t workingSpaceSize =
        IntegerCompression::GetDecompressionWorkingSpaceSize(numInts);
    std::unique_ptr<char[]> tmpSpace;
    if (!workingSpace) {
        tmpSpace.reset(new char[workingSpaceSize]);
        workingSpace = tmpSpace.get();
    }

    size_t decompSz = LZ4Compression::DecompressFromBuffer(
        compressed, workingSpace, compressedSize, workingSpaceSize, err);

    if (decompSz == 0)
        return 0;

    return _DecodeIntegers(workingSpace, numInts, ints);
}


} // anon

////////////////////////////////////////////////////////////////////////
// 32 bit.

size_t
IntegerCompression::GetCompressedBufferSize(size_t numInts)
{
    return LZ4Compression::GetCompressedBufferSize(
        _GetEncodedBufferSize<int32_t>(numInts));
}

size_t
IntegerCompression::GetDecompressionWorkingSpaceSize(size_t numInts)
{
    return _GetEncodedBufferSize<int32_t>(numInts);
}

size_t
IntegerCompression::CompressToBuffer(
    int32_t const *ints, size_t numInts, char *compressed, std::string *err)
{
    return _CompressIntegers(ints, numInts, compressed, err);
}

size_t
IntegerCompression::CompressToBuffer(
    uint32_t const *ints, size_t numInts, char *compressed, std::string *err)
{
    return _CompressIntegers(ints, numInts, compressed, err);
}

size_t
IntegerCompression::DecompressFromBuffer(
    char const *compressed, size_t compressedSize,
    int32_t *ints, size_t numInts, std::string *err, char *workingSpace)
{
    return _DecompressIntegers(compressed, compressedSize,
                               ints, numInts, err, workingSpace);
}

size_t
IntegerCompression::DecompressFromBuffer(
    char const *compressed, size_t compressedSize,
    uint32_t *ints, size_t numInts, std::string *err, char *workingSpace)
{
    return _DecompressIntegers(compressed, compressedSize,
                               ints, numInts, err, workingSpace);
}

////////////////////////////////////////////////////////////////////////
// 64 bit.

size_t
IntegerCompression64::GetCompressedBufferSize(size_t numInts)
{
    return LZ4Compression::GetCompressedBufferSize(
        _GetEncodedBufferSize<int64_t>(numInts));
}

size_t
IntegerCompression64::GetDecompressionWorkingSpaceSize(size_t numInts)
{
    return _GetEncodedBufferSize<int64_t>(numInts);
}

size_t
IntegerCompression64::CompressToBuffer(
    int64_t const *ints, size_t numInts, char *compressed, std::string *err)
{
    return _CompressIntegers(ints, numInts, compressed, err);
}

size_t
IntegerCompression64::CompressToBuffer(
    uint64_t const *ints, size_t numInts, char *compressed, std::string *err)
{
    return _CompressIntegers(ints, numInts, compressed, err);
}

size_t
IntegerCompression64::DecompressFromBuffer(
    char const *compressed, size_t compressedSize,
    int64_t *ints, size_t numInts, std::string *err, char *workingSpace)
{
    return _DecompressIntegers(compressed, compressedSize,
                               ints, numInts, err, workingSpace);
}

size_t
IntegerCompression64::DecompressFromBuffer(
    char const *compressed, size_t compressedSize,
    uint64_t *ints, size_t numInts, std::string *err, char *workingSpace)
{
    return _DecompressIntegers(compressed, compressedSize,
                               ints, numInts, err, workingSpace);
}

} // namespace v1
} // namespace lightusd
