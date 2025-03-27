#pragma once
#include <functional>
#include <string>

namespace usdBVHAnimPlugin {
//! A small class for parsing strings.
struct Parse {
    //! A pointer to the beginning of the input string.
    char const* m_Begin = nullptr;

    //! A pointer to the end of the input string.
    char const* m_End = nullptr;

    //! Returns `true` if the Parse object is still valid, or `false` otherwise.
    //! This method can be used to determine whether an error has occurred during
    //! parsing (if it has, an invalid `Parse` object will be returned).
    //!
    //! A `Parse` object is considered valid if it still refers to a string - that is,
    //! that its `m_Begin` and `m_End` pointers are still valid, and ordered such that
    //! `m_Begin <= m_End`. Note that a `Parse` object that has successfully reached
    //! the end of its input string is considered valid.
    operator bool() const
    {
        return m_Begin && m_End && m_Begin <= m_End;
    }

    //! Parse a single character from the string. If that character is matched,
    //! a new `Parse` object is returned beginning at the next character. Otherwise,
    //! an invalid `Parse` object is returned.
    Parse Char(char value) const
    {
        if (m_Begin && m_End && m_Begin < m_End && *m_Begin == value) {
            return Parse { m_Begin + 1, m_End };
        }
        return Parse {};
    }

    //! Parse a sub-string. If the string is matched, a new `Parse` object is returned
    //! beginning at the next character. Otherwise, an invalid `Parse` object is returned.
    Parse String(const char* str) const
    {
        if (!str) {
            return {};
        }
        Parse result = *this;
        while (str && *str != '\0') {
            result = result.Char(*str);
            ++str;
        }
        return result;
    }

    //! Parse any of the given characters. If any of the characters were matched, a
    //! `Parse` object is returned beginning at the next character. Otherwise, an invalid
    //! `Parse` object is returned.
    Parse AnyOf(const char* chars) const
    {
        while (chars && *chars) {
            auto result = Char(*chars);
            if (result) {
                return result;
            }
            ++chars;
        }
        return {};
    }

    //! Parse any of the given items, where each item is given as a function that
    //! should either return a valid `Parse` object if successful, or an invalid
    //! `Parse` object if unsuccessful. This function returns as soon as one of the
    //! given functions returns a valid `Parse` object, with this function also returning
    //! the valid `Parse` object.
    Parse AnyOf(std::initializer_list<std::function<Parse(Parse const&)>> const& args) const
    {
        for (auto const& item : args) {
            auto result = item(*this);
            if (result) {
                return result;
            }
        }
        return {};
    }

    //! Parse a minimum of the given number of items, where each item is described
    //! by a function that should either return a valid `Parse` object if successful,
    //! or an invalid `Parse` object if unsuccessful.
    //!
    //! This function returns a `Parse` object beginning immediately after the items
    //! that were successfully parsed, or an invalid `Parse` object if the given number
    //! of items couldn't be parsed.
    //!
    //! Note that 0 is a valid minimum number of items, and can be used in cases where
    //! the given items are entirely optional.
    Parse AtLeast(size_t n, std::function<Parse(Parse)> const& function) const
    {
        Parse cursor = *this;
        while (cursor && n > 0) {
            cursor = function(cursor);
            --n;
        }
        if (!cursor || n > 0) {
            return {};
        }
        while (cursor) {
            Parse next = function(cursor);
            if (next) {
                cursor = next;
            } else {
                break;
            }
        }
        return cursor;
    }

    //! Returns a new `Parse` object that starts immediately after matching any
    //! of the given characters, effectively skipping over them.
    Parse Skip(char const* chars) const
    {
        return AtLeast(0, [=](Parse const& cursor) { return cursor.AnyOf(chars); });
    }

    //! Calls the given function with this `Parse` object given as an argument. The
    //! function is expected to attempt to parse some of the input string, and return
    //! a new `Parse` object starting at the remaining portion of the string. This
    //! function will return this `Parse` object, and capture the portion of the string
    //! that was parsed to the given `result` string.
    //!
    //! The given function can also be permitted to return an invalid `Parse` object,
    //! in which case, the `result` string will be emptied and the invalid `Parse` object
    //! will be returned by this function.
    Parse Capture(std::string& result, std::function<Parse(Parse)> const& function) const
    {
        Parse next = function(*this);
        if (next) {
            result = std::string(m_Begin, next.m_Begin - m_Begin);
            return next;
        } else {
            return {};
        }
    }
};
} // namespace usdBVHAnimPlugin