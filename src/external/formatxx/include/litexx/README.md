# litexx

Litewight C++ utility library.

## Authors

Sean Middleditch <sean.middleditch+litexx@gmail.com>

## About

Small utility types and features. Explicitly built to compile quuickly and with low symbol
overhead, to a reasonable degree; it doesn't avoid templates just to be trendy, but avoids
features the author doesn't personally need.

Many users might prefer [EASTL](https://github.com/electronicarts/EASTL) if they're looking
for a liteweight alternative to the C++ standard STL.

## History and Design Notes

Written primarily to share a `string_view` implementation between the author's libraries
[formatxx](https://github.com/seanmiddleditch/formatxx) and
[logxx](https://github.com/seanmiddleditch/logxx).

Attempts to optimize reasonably well, even going so far as to use compiler intrinsics where
available. This is especially useful as all three major modern compilers (MSC, GCC, and Clang)
support many of the same intrinics; Clang to be source compatible with GCC, and now MSC to
allow C++17 and later headers to compile with clang-cl.

Many standard library features are left off analogues in litexx; mostly those that the author
does not (yet) use. They may be added if a justifiable use case is found.

## To Do

- span
- zstring_view (cstring_view)
- vector
- extra traits
- box (unique_ptr)
- callable and callable_ref (function/function_ref)
- typelist and utilities

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
