# wise_enum [![Build Status](https://travis-ci.com/quicknir/wise_enum.svg?branch=master)](https://travis-ci.com/quicknir/wise_enum) [![Build status](https://ci.appveyor.com/api/projects/status/h91ay4xrbsdrq6gk/branch/master?svg=true)](https://ci.appveyor.com/project/quicknir/wise-enum/branch/master)


> Because reflection makes you wise, not smart

`wise_enum` is a standalone smart enum library for C++11/14/17. It supports
all of the standard functionality that you would expect from a smart enum class
in C++:
 - Tells you the number of enumerators
 - Lets you iterate over all enum values
 - Converts string to enum, and enum to string
 - Does everything in an idiomatic C++ way (friendly to generic programming,
   compile time programming, etc)

### Examples

Let's look at a bit of code. You can declare an enum like this:

```cpp
// Equivalent to enum Color {GREEN = 2, RED};
WISE_ENUM(Color, (GREEN, 2), RED)
```

You can also declare an enum class instead of an enum, specify the storage
explicitly, declare an enum nested inside a class, or even adapt an already
declared enum:

```cpp
// Equivalent to enum class MoreColor : int64_t {BLUE, BLACK = 1};
WISE_ENUM_CLASS((MoreColor, int64_t), BLUE, (BLACK, 1))

// Inside a class, must use a different macro, but still works
struct Bar {
    WISE_ENUM_MEMBER(Foo, BUZ)
};

// Adapt an existing enum you don't control so it works with generic code
namespace another_lib {
enum class SomebodyElse { FIRST, SECOND };
}
WISE_ENUM_ADAPT(another_lib::SomebodyElse, FIRST, SECOND)
```

You can ask the enum how many enumerators it has:

```cpp
static_assert(wise_enum::size<Color> == 2, "");
```

Iterate over the enumerators:

```cpp
std::cerr << "Enum values and names:\n";
for (auto e : wise_enum::range<Color>) {
  std::cerr << static_cast<int>(e.value) << " " << e.name << "\n";
}
```

Convert between strings and enums:

```cpp
// Convert any enum to a string
std::cerr << wise_enum::to_string(Color::RED) << "\n";

// Convert any string to an optional<enum>
auto x1 = wise_enum::from_string<Color>("GREEN");
auto x2 = wise_enum::from_string<Color>("Greeeeeeen");

assert(x1.value() == Color::GREEN);
assert(!x2);
```

Check whether something is a wise enum at compile time:
```cpp
static_assert(wise_enum::is_wise_enum_v<Color>, "");
static_assert(!wise_enum::is_wise_enum_v<int>, "");
enum flub { blub, glub };
static_assert(!wise_enum::is_wise_enum_v<flub>, "");
```

### Design

It has a few notable design choices.

First, when you use one of the macros to declare your enum, what gets declared
(among other things) is exactly the vanilla enum (or enum class) that you would
expect. Not an enum like class, or anything like that. That means that
`wise_enum`s can be used exactly like regular enums in non-reflective contexts,
because they are regular enums. They can be used as non-type template
parameters, and they can be used in switch case statements, unlike any user
defined type. This also means that upgrading a regular enum already widely used
(non-reflectively) in your codebase to a wise enum is never a breaking change.
No strange behavior, or edge cases, when used with other third party libraries
(e.g. serialization), or standard library type traits.

Second, all the functionality in defining enums is preserved. You can define
`enum` or `enum class`es, set storage explicitly or let it be implicit, define
the value for an enumeration, or allow it to be determined implicitly. You can
also define enums nested in classes, which isn't supported in some smart enum
libraries.

Third, it's quite compile-time programming friendly. Everything is `constexpr`,
and a type trait is provided. This makes it easy to handle wise enums in a
specific way in generic code. For example, if you have a logger, it can't
intelligently log a vanilla enum without more information, but it can log a wise
enum, so you can use the type trait to handle them differently (with no
performance cost of course).

Fourth, it's careful with regards to performance and generated assembly. It
makes zero heap allocations and does zero dynamic initialization, and does not
use exceptions. The enum -> string is an optimal switch-case. String -> enum is
currently a linear search; this may be changed in the future (most alternatives
are not trivial to implement without doing heap allocations or dynamic
initialization).

### Quick Comparison of Alternatives

The best known alternative is probably [Better
Enums](https://github.com/aantron/better-enums). The biggest issue with Better
Enums is simply that its macros don't actually create enums or enum classes, but
enum like classes. This carries all of the disadvantages discussed in the
previous section, and for me was just a deal breaker. There are also more minor
issues like not being able to define a nested enum, having a lower default
enumeration limit. Conversely, I'm not aware of any advantages, except one: it
does support C++03, which wise enum never will, so it's a good choice for older
codebases.

A recent interesting alternative is [Meta
Enum](https://github.com/therocode/meta_enum). This does declare actual
enum/enum classes. As benefits, it doesn't have any limit on the number of
enumerations by design, and it doesn't require different macros for declaring
enums nested inside classes. As far as I can tell though, the approach means
that it can't support switch case generation (e.g. for to_string), nor can it
support 11. It currently only seems to support 17 but 14 support may be
possible.

As far as I saw, neither library has something like the adapt macro, though I
think either one could add it pretty easily. There are other implementations,
but most of the ones I've seen are very clearly very short projects, lacking
support for basic features (e.g. controlling enum values) and documentation.

Overall, I feel like wise enum is the best choice for an enum library for a
typical, modern C++ codebase.

If any of this information is incorrect, please let me know and I'll make
correcting it the highest priority.

### Version differences

Wise enum tries to target each language version idiomatically. In 11, template
variables, which are the recommended interface in 14/17 are not available so using
the typical class template static interface instead is necessary. Many functions lose
`constexpr` in 11. The difference between 14 and 17 is mostly in the types used,
discussed in the next section.

### Types and Customizations

There are two types that you can customize in wise_enum, by defining macros: the
optional type, and the string type.

| Type          | 11/14 default       | 17 default       | customize macro       | type alias |
| ------------- | ----------------- | ----------      | ---------------      | --- |
| optional      | `wise_enum::optional` | `std::optional`    | `WISE_ENUM_OPTIONAL_TYPE`    | `wise_enum::optional_type<T>` |
| string        | `const char *`        | `std::string_view` | `WISE_ENUM_STRING_TYPE` | `wise_enum::string_type` |

If you only support 17, the defaults should be fine. If you're on 11/14, the
defaults are fine as well, but if you want to be forward compatible I'd consider
rounding up a string_view implementation somewhere and using that. Otherwise,
since `const char*` and `string_view` don't have the same interface, you may
have breakages when upgrading. Finally, if you're supporting both 11/14 and 17 I'd
definitely define both macros so the same type is used in your builds.

You can define the macro either in your build system, or by having a stub header
that defines them and then includes `wise_enum.h`, and only including via the
stub.

You can also customize the use of exceptions. If you use the CMake option
`NO_EXCEPTIONS` or otherwise define the macro `WISE_ENUM_NO_EXCEPT`, then
wise_enum should be fully compatible with `-fno-exceptions`. The API never
directly throws exceptions anyhow; the only change in behavior is that the
provided `optional` (and `compact_optional`) will abort rather than throw on
`value()` calls if the optional is empty, similar to the behavior of most (all?)
standard library optional implementations when exceptions are disabled.

### Extra Features

Over time I'd like to leverage some of the capabilities of wise enum to do other useful enum related things.

#### Compact optional

I have a compact optional implementation included now in wise enum. The key point is that it uses compile time reflection to statically verified that the sentinel value used to indicate the absence of an enum, is not a value used for any of the enumerators. If you add an enumerator to an enum used in a compact optional, and the value of the enum is the sentinel, you get a compilation error.

#### Switch case "lifts"

One problem where C++ gives you little recourse is when you have a runtime value that
you want to lift into a compile time value. Consider the following:

```cpp
template <MyEnum E>
class MyDerived : MyInterface {...};

unique_ptr<MyInterface> make(MyEnum e);
```

It's hard to write `make` without boilerplate in the general case. You need to manually
switch case over all enumerator values, and in each case put the compile time constant
into the template. Wise enum will shortly (exact interface being worked out) provide
a facility that takes an enum and a lambda, does the switch case over all values
internally, and calls the lambda making the enum available as a compile time constant.

Coming soon!

#### Enum Sets
Planned for the future.

### Limitations

There are some known limitations:

 - If there are enumerators with the same value, then `to_string` will not work.
   You can declare the enum and use all the other API. This is both because it
   doesn't jive at all with the implementation, and even conceptually it's not
   clear how you would handle a conversion to string since multiple strings
   would be associated with the same value.
 - By default, you are limited to 256 enumerations. If you need more, simply run
   the `create_generated` script to create a file with as many as you need, and
   replace `wise_enum_generated` with that file. The default limit may be raised
   or lowered based on feedback. An alternative solution here would be to create
   headers in advance that raise this number, but leave the onus on the user to
   include them (so users who don't need a large number aren't slowed down)

### Q&A

##### Why didn't I use `BOOST_PP`?
I started with it, but the limit on sequences was very disappointing (64) and
there didn't seem to be any easy way to change it. So then I started down the
codegen route, and once there, I wasn't using very much. I know there are always
people who prefer to avoid the dependency, so I decided to drop it.
