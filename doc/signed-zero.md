# Signed zero in OpenUSD and AOUSD Core

This note records how IEEE-754 signed zero is handled by the OpenUSD reference
implementation and by AOUSD Core Specification 1.0.1. It is intended to guide
TinyUSDZ readers, writers, and converters that require bit-exact floating-point
round trips.

The source review used OpenUSD revision
`2095fafafd033fa23386d7ec6d58c7cc33974518`. Paths below are relative to the
TinyUSDZ workspace root.

## Summary

- OpenUSD's USDA parser and writer preserve an authored negative zero.
- OpenUSD Crate can store the IEEE sign bit unchanged in direct scalar,
  vector, and matrix representations, but its scalar floating-point array
  compression may collapse signed zero.
- OpenUSD numerical equality and hashing deliberately treat `-0.0` and `+0.0`
  as equal.
- Matrix composition, inversion, decomposition, and reconstruction are
  numerical operations, not bit-preserving transformations. They may change
  the sign of zero.
- AOUSD Core defines floating-point types through IEEE-754 and requires exact
  authored floating-point values, but does not explicitly state whether
  positive and negative zero must be distinguished for conformance.

Consequently, a direct USDA/USDC value round trip can preserve signed zero, but
a semantic transform round trip cannot in general promise bit-exact signed-zero
preservation.

## OpenUSD behavior

### USDA writing preserves negative zero

OpenUSD formats `float` and `double` values with its bundled double-conversion
library:

- [`OpenUSD/pxr/base/tf/stringUtils.cpp`](../../OpenUSD/pxr/base/tf/stringUtils.cpp)
  creates `DoubleToStringConverter` with `NO_FLAGS` and uses its shortest-value
  conversion for `TfStringify(float)` and `TfStringify(double)`.
- [`OpenUSD/pxr/base/tf/pxrDoubleConversion/double-to-string.h`](../../OpenUSD/pxr/base/tf/pxrDoubleConversion/double-to-string.h)
  documents `UNIQUE_ZERO` as the option that converts `-0.0` to `0.0`.

Because OpenUSD does not enable `UNIQUE_ZERO`, the negative sign is retained.
The resulting shortest USDA spelling is `-0` rather than `-0.0`.

### USDA parsing explicitly preserves `-0`

The parser treats the token `-0` specially:

- [`OpenUSD/pxr/usd/sdf/textParserHelpers.cpp`](../../OpenUSD/pxr/usd/sdf/textParserHelpers.cpp)
  constructs `double(-0.0)` when the input token is exactly `-0`.
- [`OpenUSD/pxr/usd/sdf/parserHelpers.h`](../../OpenUSD/pxr/usd/sdf/parserHelpers.h)
  explains that `-0` is stored as a double because this is the only way to
  preserve signed zero; an integral parser value has no signed zero.

Decimal and exponent forms such as `-0.0` and `-0e0` go through the normal
floating-point conversion path, which also observes their sign.

### USDC Crate usually stores the representation, with an array exception

[`OpenUSD/pxr/usd/sdf/crateFile.cpp`](../../OpenUSD/pxr/usd/sdf/crateFile.cpp)
classifies arithmetic types, `GfHalf`, Gf vectors, Gf matrices, and Gf
quaternions as bitwise-readable/writable types. Scalar floats may also be
stored inline by copying their bytes into a Crate value representation.

Therefore a directly stored float, vector, or matrix transferred without
numerical modification can carry the negative-zero sign bit through USDC. Gf
vector and matrix arrays also take the general bitwise array path. This
statement applies to value storage; it does not make later computations
bit-exact.

There is an important exception for `VtArray<half>`, `VtArray<float>`, and
`VtArray<double>`. Starting with Crate 0.6, arrays with at least 16 elements may
use special floating-point compression:

- If every element compares equal to an `int32_t`, OpenUSD converts the array
  to compressed integers. Negative zero passes this numerical test and becomes
  integer zero, so its sign is lost on decoding.
- Otherwise OpenUSD may construct a lookup table using `std::find`, which uses
  floating-point equality. Positive and negative zero therefore share an
  entry, and the decoded sign is whichever zero representation entered the
  lookup table first.
- Arrays not selected for either compression form are written contiguously and
  retain their bits.

See the floating-point array writer near
[`_WritePossiblyCompressedArray`](../../OpenUSD/pxr/usd/sdf/crateFile.cpp).
Thus OpenUSD itself does **not** guarantee bit-exact signed-zero retention for
scalar floating-point arrays. A bit-exact TinyUSDZ writer must reject those
compression choices when the zero sign would change.

### Equality and hashing intentionally collapse the distinction

OpenUSD does not treat the sign of zero as part of ordinary value identity:

- [`OpenUSD/pxr/base/gf/matrix4d.cpp`](../../OpenUSD/pxr/base/gf/matrix4d.cpp)
  implements `GfMatrix4d::operator==` with element-wise floating-point `==`.
  Thus matrices that differ only by zero signs compare equal.
- [`OpenUSD/pxr/base/tf/hash.h`](../../OpenUSD/pxr/base/tf/hash.h) explicitly
  canonicalizes both positive and negative zero to the same floating-point
  hash.

Code that needs bit-exact comparison must compare the IEEE representations,
for example with `std::bit_cast` or `memcpy` to a same-width unsigned integer.
It must not rely on `operator==`, approximate comparison, or `TfHash`.

### Transform arithmetic may change zero signs

[`OpenUSD/pxr/base/gf/matrix4d.cpp`](../../OpenUSD/pxr/base/gf/matrix4d.cpp)
implements matrix multiplication as ordinary sequences of floating-point
multiplications and additions. IEEE-754 arithmetic permits the sign of an
exact-zero result to depend on its operands and rounding mode; adding positive
zero contributions commonly changes a negative zero to positive zero.

Other semantic transform operations have the same limitation:

- inverse and matrix multiplication calculate new elements;
- factorization/decomposition initializes and reconstructs matrix elements;
- composing `UsdGeomXformOp` values calculates a result matrix;
- deriving one semantic transform representation from another performs matrix
  arithmetic.

An unchanged authored `matrix4d` value can preserve its bits. A matrix derived
from other transforms is a numerical result and must not be expected to retain
the source matrix's zero signs.

## AOUSD Core Specification 1.0.1

The reviewed document is
[`aousd/aousd_core_spec_1.0.1_2025-12-12.pdf`](../../aousd/aousd_core_spec_1.0.1_2025-12-12.pdf).

### Floating-point types

The foundational type table defines:

- `float` as `f32` per IEEE 754;
- `double` as `f64` per IEEE 754-1985;
- `half` as `f16` per IEEE 754-2008;
- `matrix4d` as a row-major `f64[4,4]` value.

The Crate description for `half` identifies a sign bit and describes exponent
and significand zero as producing zero. The `float` and `double` sections
describe their widths. These definitions admit both IEEE positive and negative
zero encodings, but the specification does not separately define their USD
semantic identity.

### Exact authored values

The compliance and value-resolution sections require floating-point fields to
be exact at authored time samples and spline knots. They do not define
"exact" as bitwise equality and do not mention signed zero in that requirement.

The practical interpretation is therefore limited: the text requires exact
authored numerical results, but by itself does not establish that a conforming
implementation must distinguish `-0.0` from `+0.0`. OpenUSD's parser and
storage behavior is useful reference behavior beyond the explicit wording.

### USDA representation

Section 16.2.5.1 requires a finite double to use the shortest string whose
parsed value is the original value. Its examples include `0.0` becoming `0`.
The USDA number grammar permits a leading minus, so `-0` is valid, but the
section contains no negative-zero example or explicit canonical spelling.

There is an ambiguity here: IEEE-754 gives positive and negative zero different
encodings, while ordinary floating-point equality considers them equal. The
specification does not say which interpretation of "original value" controls
the shortest-string rule for zero. OpenUSD resolves the ambiguity by emitting
and parsing `-0` distinctly.

The specification's discussion of positive and negative zero in the Crate path
index section concerns integer/index portability and is unrelated to
floating-point signed-zero preservation.

## TinyUSDZ implementation guidance

For ordinary USD processing, treating positive and negative zero as equal is
consistent with OpenUSD numerical comparison and hashing. For an explicitly
bit-exact conversion mode:

1. Preserve an authored scalar, vector, quaternion, or matrix by copying its
   representation without arithmetic.
2. Serialize negative zero as `-0` in USDA and restore its sign when parsing.
3. Preserve the IEEE bits in USDC scalar, inline, array, and compressed-array
   paths. In particular, do not integer-compress a negative zero and do not
   merge positive and negative zero in a scalar floating-point lookup table.
4. Use bitwise comparison in round-trip tests. A numerical equality check will
   miss signed-zero changes.
5. Do not claim bit-exact recovery after matrix composition, inversion, or
   decomposition unless an independent authoritative copy of the source value
   exists.

Standard USD matrix attributes can preserve signed zero if an authoritative
source matrix is authored directly and read back directly. If the original
matrix must instead be reconstructed from another semantic transform
representation, standard matrix data alone cannot guarantee a bit-exact
result. That reconstruction necessarily performs arithmetic, and OpenUSD
itself does not preserve zero signs across such operations.

### Current writer policy

TinyUSDZ applies this distinction in its authored-value writers:

- USDA emits `-0` for negative-zero `half`, `float`, and `double` values.
- USDC value deduplication compares authored floating-point payloads by their
  representation, so a positive-zero payload cannot substitute for a
  negative-zero payload.
- Scalar floating-point array compression excludes negative zero from its
  integer codec and keeps the two zero encodings distinct in lookup tables.
- Ordinary value equality and numerical transform operations retain their
  normal numerical semantics; they are not changed into bitwise operations.

The guarantee is therefore about unchanged authored storage, not values
created by evaluation or computation.
