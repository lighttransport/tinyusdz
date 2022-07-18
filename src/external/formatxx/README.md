# formatxx

C++ string formatting library.

## Authors

Sean Middleditch <sean@middleditch.us>

## About

formatxx is a modern C++ string formatting library. Its intended goals are to offer fast compilation
times, minimal binary bloat, and reasonable speed. Final measurements of these goals are not yet
available; the library is still in preliminary development.

The library supports writing primitive types as well as user-defined types into string formatting
buffers. The libray has as little dependence on the C++ standard library as possible, which is
intended to make it a very light header to include throughout a larger C++ project.

The included string writers allow for formatting into a `std::string`-compatible type, a fixed-
size buffer with guaranteed no allocations, or a buffer that initially has a fixed-size buffer
but can grow to accomodate larger strings. The combination of these buffers allow for easy use
in three major cases: quick creation of `std::string` values, use in assert handlers that
cannot allocate, and use in log systems where allocation should be avoided but is allowed when
necessary. Users can easily write their own buffer systems as well.

The underlying method of operation of formatxx is to collect a list of arguments via variadic
templates, lookup a `format_value` function for each of those arguments, and then pass the format
string, an array of format functions, and an array of `void` pointers to arguments into the
the actual formatting function. Builtin C++ types go through a slightly different mechanism for
the sake of avoiding excessing ADL noise. The header mechanisms that generate these lists of
functions and pointers are intended to be light-weight on the compiler to the extent reasonable.
The actual formatting work is all implemented in a source file and not the header, to keep the
header small and cheap to include.

## Usage

formatxx can write into user-defined buffers, or a user may use one of the provided buffer
types. Formatting is support for any type that has an appropriate `format_value` free function with
the signature `void format_value(formatxx::IWriter&, TheType, formatxx::format_options const&)`.
For instance:

```c++
#include <formatxx/format.h>
#include <string>

struct Foo { int value };
	
void format_value(formatxx::writer& out, Foo const& foo, formatxx::format_options const& opts) {
	format_to(out, "Foo({})", foo.value);
}
	
int main() {
	std::cout << formatxx::format_as<std::string>("testing {0}", Foo{123});
}
```

The above will print `testing Foo(123)` to standard output.

The `options` argument are additional options passed to the formatter. These are parsed from
format string options. In the `format_to` case user-provided arguments may be provided,
otherwise they are interpreted by Python or printf rules.

The functions `formatxx::parse_format_spec` and `formatxx::parse_printf_spec` can be used
to interpret format specs from string inputs.

The `formatxx::format_as<ResultT>(string_view, ...)` template can be used
for formatting a series of arguments into any result type that implements a `string`-like
`append` method. Including `formatxx/std_string.h` also provides a `format_string` function
that defaults to returning `std::string` results.

The `formatxx::format_to(formatxx::writer&, string_view, ...)` template can be used to
write into a write buffer. This is the recommended way of formatting.

The provided write buffers are:
- `formatxx::append_writer<StringT>` - writes to a `string`-like object using `append`.
- `fmt::container_writer<ContainerT>` - writes to a container using `insert` at the end.
- `fmt::span_writer<CharT>` - writes to a pre-allocated buffer.

All three of the provided write buffers guarantee NUL-terminated strings, but support
use with string types that are not NUL-terminated (another important use case for
formatxx).

## History and Design Notes

The library that motivated this author to write formatxx is the excellent
[fmtlib](https://github.com/fmtlib/fmt) (previously cppformat) by Victor Zverovich and
contributors. fmtlib does much of what this library does, has excellent runtime speed, and
responsive and talented authors. Unfortunately at the time the cppformat library failed to
meet a few needs that formatxx seeks to address: it had relied on some heavy standard headers,
its error handling mechanisms were and are limited to C++ exceptions or `abort`, the
compilation time overhead was large, and its support for formatting user-defined types relied
on complicated and expensive mechanisms like `std::ostream` wrappers. For those reasons,
research into formatxx began.

The initial design did not use a `format_value` function for each type. Instead, an enumeration
for the basic categories of primitives (`bool`, `signed long`, `double`, etc.) was computed via
a template, values would be cast into their desired "native" representation, and that would
be passed into the format function. Unfortunately, this added a lot of heavy template machinery
to the header: the enum selection, a `std::tuple` for storing the converted inputs, etc. It
also proved to be difficult to get good support for `format_value` functions for user-defined
types with clean and concise error messages.

We also try to very heavily minimize header dependencies, especially on certain problematic
C++ headers. For example, the core library tries to avoid including `<string>` due to that
header's particularly heavy cost in some implementations. This likewise precludes formatxx
from using other standard library types like `std::string_view` or so on. In some cases we
_try_ to avoid standard headers but fallback on including them for wide compatibility; for
example, we attempt to use the common `__builtin_strlen` support on major compilers but will
fallback to `std::char_traits` when necessary.

The current header relies on a function template wrapper around the real formatting functions.
This is one template more than is desired that will lead to object file bloat, and for
unoptimized debug builds essentially means that all `format_value` functions get an extra
unnecessary "trampoline" that can be very heavyweight with many compilers' debug checks
enabled. Ideally, these templates would be externed or converted away from wrappers. One
solution would be to make the templates' functions for primitive types directly be the
formatters for those types and then make `format_value` be the template wrapper (since it is
expected to be more rarely used; it mostly exists to keep the same API between primitives and
user-defined types for generic user code).

Each `format_value` is responsible currently for its own formatting and even its own format
specifier parsing. This is not necessarily ideal and may change in the long run to standardize
better for at least things like alignment and padding.

A particular limitation with `printf`-like features right now is that the argument array
abstracts away integral arguments such that argument precision (e.g. `%.*s`) cannot be
supported. A fix for this may be to bind a set of simple types instead of binding format
functions directly to instead form a simple struct with unique identifiers for primitive types
and only bind the format functions for user-provided types. This would allow the format code
to parse out integral types efficiently, and possibly also better-optimize primitive formatting.

To the point possible, we use modern C++ and only work with recent compilers. In some cases,
we're held back to slightly older compilers. We currently require C++17 support and so require
recent Visual C++ 19.x (2015 SP 3 or later), Clang 3.9, and GCC 6.3

A final note is that floating point formatting currently bounces through `snprintf` which
necessarily means that our floating point performance is slower and more complicated than
calling the CRT directly. Writing a correct floating point formatter is incredibly complex. C++
now includes helpers like `to_chars` but it is not yet widely available on enough standard
library implementations for us to rely on exclusively.

## To Do

- Performance pass
  - Benchmarks
- Correctness to wide/unicode char support
  - u8/u16/u32?
  - maybe just remove?

## Copying

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
