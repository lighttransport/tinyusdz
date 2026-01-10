# Refactoring Opportunities

This document outlines potential areas for refactoring in the TinyUSDZ codebase.

## Code Duplication

*   **`prim-types.hh` and `value-types.hh`:** There is significant code duplication in these files, particularly in the operator overloads for `point3h`, `point3f`, and `point3d`. This could be consolidated using templates.

## Large Classes

*   **`Prim` and `Stage`:** These classes have a large number of responsibilities. Consider breaking them down into smaller, more focused classes to improve modularity and maintainability.

## Type-Erased `value::Value`

*   The `value::Value` class uses type erasure, which can impact performance and code clarity. Explore alternatives such as `std::variant` (if C++17 is an option) or a more specialized approach to improve performance and type safety.

## Python Bindings

*   The Python bindings in `python-bindings.cc` could be improved by adding more complete and Pythonic wrappers for the C++ classes and functions.

## Tydra Module

*   The `tydra` module appears to be a separate component for rendering. Consider separating it into its own library to improve modularity.

## C-style Casts

*   Replace C-style casts with C++-style casts (e.g., `static_cast`, `reinterpret_cast`) to improve type safety.

## Use of `std::vector` for Fixed-Size Arrays

*   In cases where `std::vector` is used for fixed-size arrays, it would be more efficient to use `std::array`.

## Lack of Comments

*   Some parts of the code could benefit from more comments to explain the intent and logic, especially in complex areas.
