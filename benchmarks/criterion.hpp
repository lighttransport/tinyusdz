
#pragma once

namespace indicators {
enum class Color { grey, red, green, yellow, blue, magenta, cyan, white, unspecified };
}

#pragma once
namespace indicators {
enum class FontStyle { bold, dark, italic, underline, blink, reverse, concealed, crossed };
}

#pragma once

namespace indicators {
enum class ProgressType { incremental, decremental };
}

//!
//! termcolor
//! ~~~~~~~~~
//!
//! termcolor is a header-only c++ library for printing colored messages
//! to the terminal. Written just for fun with a help of the Force.
//!
//! :copyright: (c) 2013 by Ihor Kalnytskyi
//! :license: BSD, see LICENSE for details
//!

#ifndef TERMCOLOR_HPP_
#define TERMCOLOR_HPP_

// the following snippet of code detects the current OS and
// defines the appropriate macro that is used to wrap some
// platform specific things
#if defined(_WIN32) || defined(_WIN64)
#define TERMCOLOR_OS_WINDOWS
#elif defined(__APPLE__)
#define TERMCOLOR_OS_MACOS
#elif defined(__unix__) || defined(__unix)
#define TERMCOLOR_OS_LINUX
#else
#error unsupported platform
#endif

// This headers provides the `isatty()`/`fileno()` functions,
// which are used for testing whether a standart stream refers
// to the terminal. As for Windows, we also need WinApi funcs
// for changing colors attributes of the terminal.
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
#include <unistd.h>
#elif defined(TERMCOLOR_OS_WINDOWS)
#if defined(_MSC_VER)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#endif
#endif

#include <cstdio>
#include <iostream>

namespace termcolor {
// Forward declaration of the `_internal` namespace.
// All comments are below.
namespace _internal {
// An index to be used to access a private storage of I/O streams. See
// colorize / nocolorize I/O manipulators for details.
static int colorize_index = std::ios_base::xalloc();

inline FILE *get_standard_stream(const std::ostream &stream);
inline bool is_colorized(std::ostream &stream);
inline bool is_atty(const std::ostream &stream);

#if defined(TERMCOLOR_OS_WINDOWS)
inline void win_change_attributes(std::ostream &stream, int foreground, int background = -1);
#endif
} // namespace _internal

inline std::ostream &colorize(std::ostream &stream) {
  stream.iword(_internal::colorize_index) = 1L;
  return stream;
}

inline std::ostream &nocolorize(std::ostream &stream) {
  stream.iword(_internal::colorize_index) = 0L;
  return stream;
}

inline std::ostream &reset(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[00m";
#elif defined(TERMCOLOR_OS_WINDOWS)
    _internal::win_change_attributes(stream, -1, -1);
#endif
  }
  return stream;
}

inline std::ostream &bold(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[1m";
#elif defined(TERMCOLOR_OS_WINDOWS)
#endif
  }
  return stream;
}

inline std::ostream &dark(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[2m";
#elif defined(TERMCOLOR_OS_WINDOWS)
#endif
  }
  return stream;
}

inline std::ostream &italic(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[3m";
#elif defined(TERMCOLOR_OS_WINDOWS)
#endif
  }
  return stream;
}

inline std::ostream &underline(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[4m";
#elif defined(TERMCOLOR_OS_WINDOWS)
#endif
  }
  return stream;
}

inline std::ostream &blink(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[5m";
#elif defined(TERMCOLOR_OS_WINDOWS)
#endif
  }
  return stream;
}

inline std::ostream &reverse(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[7m";
#elif defined(TERMCOLOR_OS_WINDOWS)
#endif
  }
  return stream;
}

inline std::ostream &concealed(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[8m";
#elif defined(TERMCOLOR_OS_WINDOWS)
#endif
  }
  return stream;
}

inline std::ostream &crossed(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[9m";
#elif defined(TERMCOLOR_OS_WINDOWS)
#endif
  }
  return stream;
}

template <uint8_t code> inline std::ostream &color(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    char command[12];
    std::snprintf(command, sizeof(command), "\033[38;5;%dm", code);
    stream << command;
#elif defined(TERMCOLOR_OS_WINDOWS)
#endif
  }
  return stream;
}

template <uint8_t code> inline std::ostream &on_color(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    char command[12];
    std::snprintf(command, sizeof(command), "\033[48;5;%dm", code);
    stream << command;
#elif defined(TERMCOLOR_OS_WINDOWS)
#endif
  }
  return stream;
}

template <uint8_t r, uint8_t g, uint8_t b> inline std::ostream &color(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    char command[20];
    std::snprintf(command, sizeof(command), "\033[38;2;%d;%d;%dm", r, g, b);
    stream << command;
#elif defined(TERMCOLOR_OS_WINDOWS)
#endif
  }
  return stream;
}

template <uint8_t r, uint8_t g, uint8_t b> inline std::ostream &on_color(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    char command[20];
    std::snprintf(command, sizeof(command), "\033[48;2;%d;%d;%dm", r, g, b);
    stream << command;
#elif defined(TERMCOLOR_OS_WINDOWS)
#endif
  }
  return stream;
}

inline std::ostream &grey(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[30m";
#elif defined(TERMCOLOR_OS_WINDOWS)
    _internal::win_change_attributes(stream,
                                     0 // grey (black)
    );
#endif
  }
  return stream;
}

inline std::ostream &red(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[31m";
#elif defined(TERMCOLOR_OS_WINDOWS)
    _internal::win_change_attributes(stream, FOREGROUND_RED);
#endif
  }
  return stream;
}

inline std::ostream &green(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[32m";
#elif defined(TERMCOLOR_OS_WINDOWS)
    _internal::win_change_attributes(stream, FOREGROUND_GREEN);
#endif
  }
  return stream;
}

inline std::ostream &yellow(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[33m";
#elif defined(TERMCOLOR_OS_WINDOWS)
    _internal::win_change_attributes(stream, FOREGROUND_GREEN | FOREGROUND_RED);
#endif
  }
  return stream;
}

inline std::ostream &blue(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[34m";
#elif defined(TERMCOLOR_OS_WINDOWS)
    _internal::win_change_attributes(stream, FOREGROUND_BLUE);
#endif
  }
  return stream;
}

inline std::ostream &magenta(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[35m";
#elif defined(TERMCOLOR_OS_WINDOWS)
    _internal::win_change_attributes(stream, FOREGROUND_BLUE | FOREGROUND_RED);
#endif
  }
  return stream;
}

inline std::ostream &cyan(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[36m";
#elif defined(TERMCOLOR_OS_WINDOWS)
    _internal::win_change_attributes(stream, FOREGROUND_BLUE | FOREGROUND_GREEN);
#endif
  }
  return stream;
}

inline std::ostream &white(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[37m";
#elif defined(TERMCOLOR_OS_WINDOWS)
    _internal::win_change_attributes(stream, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
#endif
  }
  return stream;
}

inline std::ostream &on_grey(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[40m";
#elif defined(TERMCOLOR_OS_WINDOWS)
    _internal::win_change_attributes(stream, -1,
                                     0 // grey (black)
    );
#endif
  }
  return stream;
}

inline std::ostream &on_red(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[41m";
#elif defined(TERMCOLOR_OS_WINDOWS)
    _internal::win_change_attributes(stream, -1, BACKGROUND_RED);
#endif
  }
  return stream;
}

inline std::ostream &on_green(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[42m";
#elif defined(TERMCOLOR_OS_WINDOWS)
    _internal::win_change_attributes(stream, -1, BACKGROUND_GREEN);
#endif
  }
  return stream;
}

inline std::ostream &on_yellow(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[43m";
#elif defined(TERMCOLOR_OS_WINDOWS)
    _internal::win_change_attributes(stream, -1, BACKGROUND_GREEN | BACKGROUND_RED);
#endif
  }
  return stream;
}

inline std::ostream &on_blue(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[44m";
#elif defined(TERMCOLOR_OS_WINDOWS)
    _internal::win_change_attributes(stream, -1, BACKGROUND_BLUE);
#endif
  }
  return stream;
}

inline std::ostream &on_magenta(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[45m";
#elif defined(TERMCOLOR_OS_WINDOWS)
    _internal::win_change_attributes(stream, -1, BACKGROUND_BLUE | BACKGROUND_RED);
#endif
  }
  return stream;
}

inline std::ostream &on_cyan(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[46m";
#elif defined(TERMCOLOR_OS_WINDOWS)
    _internal::win_change_attributes(stream, -1, BACKGROUND_GREEN | BACKGROUND_BLUE);
#endif
  }
  return stream;
}

inline std::ostream &on_white(std::ostream &stream) {
  if (_internal::is_colorized(stream)) {
#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
    stream << "\033[47m";
#elif defined(TERMCOLOR_OS_WINDOWS)
    _internal::win_change_attributes(stream, -1,
                                     BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_RED);
#endif
  }

  return stream;
}

//! Since C++ hasn't a way to hide something in the header from
//! the outer access, I have to introduce this namespace which
//! is used for internal purpose and should't be access from
//! the user code.
namespace _internal {
//! Since C++ hasn't a true way to extract stream handler
//! from the a given `std::ostream` object, I have to write
//! this kind of hack.
inline FILE *get_standard_stream(const std::ostream &stream) {
  if (&stream == &std::cout)
    return stdout;
  else if ((&stream == &std::cerr) || (&stream == &std::clog))
    return stderr;

  return nullptr;
}

// Say whether a given stream should be colorized or not. It's always
// true for ATTY streams and may be true for streams marked with
// colorize flag.
inline bool is_colorized(std::ostream &stream) {
  return is_atty(stream) || static_cast<bool>(stream.iword(colorize_index));
}

//! Test whether a given `std::ostream` object refers to
//! a terminal.
inline bool is_atty(const std::ostream &stream) {
  FILE *std_stream = get_standard_stream(stream);

  // Unfortunately, fileno() ends with segmentation fault
  // if invalid file descriptor is passed. So we need to
  // handle this case gracefully and assume it's not a tty
  // if standard stream is not detected, and 0 is returned.
  if (!std_stream)
    return false;

#if defined(TERMCOLOR_OS_MACOS) || defined(TERMCOLOR_OS_LINUX)
  return ::isatty(fileno(std_stream));
#elif defined(TERMCOLOR_OS_WINDOWS)
  return ::_isatty(_fileno(std_stream));
#endif
}

#if defined(TERMCOLOR_OS_WINDOWS)
//! Change Windows Terminal colors attribute. If some
//! parameter is `-1` then attribute won't changed.
inline void win_change_attributes(std::ostream &stream, int foreground, int background) {
  // yeah, i know.. it's ugly, it's windows.
  static WORD defaultAttributes = 0;

  // Windows doesn't have ANSI escape sequences and so we use special
  // API to change Terminal output color. That means we can't
  // manipulate colors by means of "std::stringstream" and hence
  // should do nothing in this case.
  if (!_internal::is_atty(stream))
    return;

  // get terminal handle
  HANDLE hTerminal = INVALID_HANDLE_VALUE;
  if (&stream == &std::cout)
    hTerminal = GetStdHandle(STD_OUTPUT_HANDLE);
  else if (&stream == &std::cerr)
    hTerminal = GetStdHandle(STD_ERROR_HANDLE);

  // save default terminal attributes if it unsaved
  if (!defaultAttributes) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(hTerminal, &info))
      return;
    defaultAttributes = info.wAttributes;
  }

  // restore all default settings
  if (foreground == -1 && background == -1) {
    SetConsoleTextAttribute(hTerminal, defaultAttributes);
    return;
  }

  // get current settings
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (!GetConsoleScreenBufferInfo(hTerminal, &info))
    return;

  if (foreground != -1) {
    info.wAttributes &= ~(info.wAttributes & 0x0F);
    info.wAttributes |= static_cast<WORD>(foreground);
  }

  if (background != -1) {
    info.wAttributes &= ~(info.wAttributes & 0xF0);
    info.wAttributes |= static_cast<WORD>(background);
  }

  SetConsoleTextAttribute(hTerminal, info.wAttributes);
}
#endif // TERMCOLOR_OS_WINDOWS

} // namespace _internal

} // namespace termcolor

#undef TERMCOLOR_OS_WINDOWS
#undef TERMCOLOR_OS_MACOS
#undef TERMCOLOR_OS_LINUX

#endif // TERMCOLOR_HPP_

#pragma once
#include <utility>

#if defined(_WIN32)
#include <windows.h>

namespace indicators {

static inline std::pair<size_t, size_t> terminal_size() {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  int cols, rows;
  GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
  cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
  rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  return {static_cast<size_t>(rows), static_cast<size_t>(cols)};
}

size_t terminal_width() { return terminal_size().second; }

} // namespace indicators

#else

#include <sys/ioctl.h> //ioctl() and TIOCGWINSZ
#include <unistd.h>    // for STDOUT_FILENO

namespace indicators {

static inline std::pair<size_t, size_t> terminal_size() {
  struct winsize size;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);
  return {static_cast<size_t>(size.ws_row), static_cast<size_t>(size.ws_col)};
}

static inline size_t terminal_width() { return terminal_size().second; }

} // namespace indicators

#endif

/*
Activity Indicators for Modern C++
https://github.com/p-ranav/indicators

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
SPDX-License-Identifier: MIT
Copyright (c) 2019 Dawid Pilarski <dawid.pilarski@panicsoftware.com>.

Permission is hereby  granted, free of charge, to any  person obtaining a copy
of this software and associated  documentation files (the "Software"), to deal
in the Software  without restriction, including without  limitation the rights
to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#pragma once

#include <cstddef>
// #include <indicators/color.hpp>
// #include <indicators/font_style.hpp>
// #include <indicators/progress_type.hpp>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace indicators {

namespace details {

template <bool condition> struct if_else;

template <> struct if_else<true> { using type = std::true_type; };

template <> struct if_else<false> { using type = std::false_type; };

template <bool condition, typename True, typename False> struct if_else_type;

template <typename True, typename False> struct if_else_type<true, True, False> {
  using type = True;
};

template <typename True, typename False> struct if_else_type<false, True, False> {
  using type = False;
};

template <typename... Ops> struct conjuction;

template <> struct conjuction<> : std::true_type {};

template <typename Op, typename... TailOps>
struct conjuction<Op, TailOps...>
    : if_else_type<!Op::value, std::false_type, conjuction<TailOps...>>::type {};

template <typename... Ops> struct disjunction;

template <> struct disjunction<> : std::false_type {};

template <typename Op, typename... TailOps>
struct disjunction<Op, TailOps...>
    : if_else_type<Op::value, std::true_type, disjunction<TailOps...>>::type {};

enum class ProgressBarOption {
  bar_width = 0,
  prefix_text,
  postfix_text,
  start,
  end,
  fill,
  lead,
  remainder,
  max_postfix_text_len,
  completed,
  show_percentage,
  show_elapsed_time,
  show_remaining_time,
  saved_start_time,
  foreground_color,
  spinner_show,
  spinner_states,
  font_styles,
  hide_bar_when_complete,
  min_progress,
  max_progress,
  progress_type,
  stream
};

template <typename T, ProgressBarOption Id> struct Setting {
  template <typename... Args,
            typename = typename std::enable_if<std::is_constructible<T, Args...>::value>::type>
  explicit Setting(Args &&... args) : value(std::forward<Args>(args)...) {}
  Setting(const Setting &) = default;
  Setting(Setting &&) = default;

  static constexpr auto id = Id;
  using type = T;

  T value{};
};

template <typename T> struct is_setting : std::false_type {};

template <ProgressBarOption Id, typename T> struct is_setting<Setting<T, Id>> : std::true_type {};

template <typename... Args>
struct are_settings : if_else<conjuction<is_setting<Args>...>::value>::type {};

template <> struct are_settings<> : std::true_type {};

template <typename Setting, typename Tuple> struct is_setting_from_tuple;

template <typename Setting> struct is_setting_from_tuple<Setting, std::tuple<>> : std::true_type {};

template <typename Setting, typename... TupleTypes>
struct is_setting_from_tuple<Setting, std::tuple<TupleTypes...>>
    : if_else<disjunction<std::is_same<Setting, TupleTypes>...>::value>::type {};

template <typename Tuple, typename... Settings>
struct are_settings_from_tuple
    : if_else<conjuction<is_setting_from_tuple<Settings, Tuple>...>::value>::type {};

template <ProgressBarOption Id> struct always_true { static constexpr auto value = true; };

template <ProgressBarOption Id, typename Default> Default &&get_impl(Default &&def) {
  return std::forward<Default>(def);
}

template <ProgressBarOption Id, typename Default, typename T, typename... Args>
auto get_impl(Default && /*def*/, T &&first, Args &&... /*tail*/) ->
    typename std::enable_if<(std::decay<T>::type::id == Id),
                            decltype(std::forward<T>(first))>::type {
  return std::forward<T>(first);
}

template <ProgressBarOption Id, typename Default, typename T, typename... Args>
auto get_impl(Default &&def, T && /*first*/, Args &&... tail) ->
    typename std::enable_if<(std::decay<T>::type::id != Id),
                            decltype(get_impl<Id>(std::forward<Default>(def),
                                                  std::forward<Args>(tail)...))>::type {
  return get_impl<Id>(std::forward<Default>(def), std::forward<Args>(tail)...);
}

template <ProgressBarOption Id, typename Default, typename... Args,
          typename = typename std::enable_if<are_settings<Args...>::value, void>::type>
auto get(Default &&def, Args &&... args)
    -> decltype(details::get_impl<Id>(std::forward<Default>(def), std::forward<Args>(args)...)) {
  return details::get_impl<Id>(std::forward<Default>(def), std::forward<Args>(args)...);
}

template <ProgressBarOption Id> using StringSetting = Setting<std::string, Id>;

template <ProgressBarOption Id> using IntegerSetting = Setting<std::size_t, Id>;

template <ProgressBarOption Id> using BooleanSetting = Setting<bool, Id>;

template <ProgressBarOption Id, typename Tuple, std::size_t counter = 0> struct option_idx;

template <ProgressBarOption Id, typename T, typename... Settings, std::size_t counter>
struct option_idx<Id, std::tuple<T, Settings...>, counter>
    : if_else_type<(Id == T::id), std::integral_constant<std::size_t, counter>,
                   option_idx<Id, std::tuple<Settings...>, counter + 1>>::type {};

template <ProgressBarOption Id, std::size_t counter> struct option_idx<Id, std::tuple<>, counter> {
  static_assert(always_true<(ProgressBarOption)Id>::value, "No such option was found");
};

template <ProgressBarOption Id, typename Settings>
auto get_value(Settings &&settings)
    -> decltype((std::get<option_idx<Id, typename std::decay<Settings>::type>::value>(
        std::declval<Settings &&>()))) {
  return std::get<option_idx<Id, typename std::decay<Settings>::type>::value>(
      std::forward<Settings>(settings));
}

} // namespace details

namespace option {
using BarWidth = details::IntegerSetting<details::ProgressBarOption::bar_width>;
using PrefixText = details::StringSetting<details::ProgressBarOption::prefix_text>;
using PostfixText = details::StringSetting<details::ProgressBarOption::postfix_text>;
using Start = details::StringSetting<details::ProgressBarOption::start>;
using End = details::StringSetting<details::ProgressBarOption::end>;
using Fill = details::StringSetting<details::ProgressBarOption::fill>;
using Lead = details::StringSetting<details::ProgressBarOption::lead>;
using Remainder = details::StringSetting<details::ProgressBarOption::remainder>;
using MaxPostfixTextLen = details::IntegerSetting<details::ProgressBarOption::max_postfix_text_len>;
using Completed = details::BooleanSetting<details::ProgressBarOption::completed>;
using ShowPercentage = details::BooleanSetting<details::ProgressBarOption::show_percentage>;
using ShowElapsedTime = details::BooleanSetting<details::ProgressBarOption::show_elapsed_time>;
using ShowRemainingTime = details::BooleanSetting<details::ProgressBarOption::show_remaining_time>;
using SavedStartTime = details::BooleanSetting<details::ProgressBarOption::saved_start_time>;
using ForegroundColor = details::Setting<Color, details::ProgressBarOption::foreground_color>;
using ShowSpinner = details::BooleanSetting<details::ProgressBarOption::spinner_show>;
using SpinnerStates =
    details::Setting<std::vector<std::string>, details::ProgressBarOption::spinner_states>;
using HideBarWhenComplete =
    details::BooleanSetting<details::ProgressBarOption::hide_bar_when_complete>;
using FontStyles =
    details::Setting<std::vector<FontStyle>, details::ProgressBarOption::font_styles>;
using MinProgress = details::IntegerSetting<details::ProgressBarOption::min_progress>;
using MaxProgress = details::IntegerSetting<details::ProgressBarOption::max_progress>;
using ProgressType = details::Setting<ProgressType, details::ProgressBarOption::progress_type>;
using Stream = details::Setting<std::ostream &, details::ProgressBarOption::stream>;
} // namespace option
} // namespace indicators

#pragma once

#if defined(_MSC_VER)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <cstdio>
#endif

namespace indicators {

#if defined(_MSC_VER)

static inline void show_console_cursor(bool const show) {
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);

  CONSOLE_CURSOR_INFO cursorInfo;

  GetConsoleCursorInfo(out, &cursorInfo);
  cursorInfo.bVisible = show; // set the cursor visibility
  SetConsoleCursorInfo(out, &cursorInfo);
}

#else

static inline void show_console_cursor(bool const show) {
  std::fputs(show ? "\033[?25h" : "\033[?25l", stdout);
}

#endif

} // namespace indicators
#pragma once

#if defined(_MSC_VER)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <iostream>
#endif

namespace indicators {

#ifdef _MSC_VER

static inline void move(int x, int y) {
  auto hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!hStdout)
    return;

  CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
  GetConsoleScreenBufferInfo(hStdout, &csbiInfo);

  COORD cursor;

  cursor.X = csbiInfo.dwCursorPosition.X + x;
  cursor.Y = csbiInfo.dwCursorPosition.Y + y;
  SetConsoleCursorPosition(hStdout, cursor);
}

static inline void move_up(int lines) { move(0, -lines); }
static inline void move_down(int lines) { move(0, -lines); }
static inline void move_right(int cols) { move(cols, 0); }
static inline void move_left(int cols) { move(-cols, 0); }

#else

static inline void move_up(int lines) { std::cout << "\033[" << lines << "A"; }
static inline void move_down(int lines) { std::cout << "\033[" << lines << "B"; }
static inline void move_right(int cols) { std::cout << "\033[" << cols << "C"; }
static inline void move_left(int cols) { std::cout << "\033[" << cols << "D"; }

#endif

} // namespace indicators
#pragma once

// #include <indicators/display_width.hpp>

#include <clocale>
#if __has_include(<codecvt>)
#include <codecvt>
#define INDICATORS_HAVE_CODECVT 1
#endif
#include <cstdlib>
#include <locale>
#include <string>
#include <wchar.h>

namespace unicode {

#if INDICATORS_HAVE_CODECVT
namespace details {

/*
 * This is an implementation of wcwidth() and wcswidth() (defined in
 * IEEE Std 1002.1-2001) for Unicode.
 *
 * http://www.opengroup.org/onlinepubs/007904975/functions/wcwidth.html
 * http://www.opengroup.org/onlinepubs/007904975/functions/wcswidth.html
 *
 * In fixed-width output devices, Latin characters all occupy a single
 * "cell" position of equal width, whereas ideographic CJK characters
 * occupy two such cells. Interoperability between terminal-line
 * applications and (teletype-style) character terminals using the
 * UTF-8 encoding requires agreement on which character should advance
 * the cursor by how many cell positions. No established formal
 * standards exist at present on which Unicode character shall occupy
 * how many cell positions on character terminals. These routines are
 * a first attempt of defining such behavior based on simple rules
 * applied to data provided by the Unicode Consortium.
 *
 * For some graphical characters, the Unicode standard explicitly
 * defines a character-cell width via the definition of the East Asian
 * FullWidth (F), Wide (W), Half-width (H), and Narrow (Na) classes.
 * In all these cases, there is no ambiguity about which width a
 * terminal shall use. For characters in the East Asian Ambiguous (A)
 * class, the width choice depends purely on a preference of backward
 * compatibility with either historic CJK or Western practice.
 * Choosing single-width for these characters is easy to justify as
 * the appropriate long-term solution, as the CJK practice of
 * displaying these characters as double-width comes from historic
 * implementation simplicity (8-bit encoded characters were displayed
 * single-width and 16-bit ones double-width, even for Greek,
 * Cyrillic, etc.) and not any typographic considerations.
 *
 * Much less clear is the choice of width for the Not East Asian
 * (Neutral) class. Existing practice does not dictate a width for any
 * of these characters. It would nevertheless make sense
 * typographically to allocate two character cells to characters such
 * as for instance EM SPACE or VOLUME INTEGRAL, which cannot be
 * represented adequately with a single-width glyph. The following
 * routines at present merely assign a single-cell width to all
 * neutral characters, in the interest of simplicity. This is not
 * entirely satisfactory and should be reconsidered before
 * establishing a formal standard in this area. At the moment, the
 * decision which Not East Asian (Neutral) characters should be
 * represented by double-width glyphs cannot yet be answered by
 * applying a simple rule from the Unicode database content. Setting
 * up a proper standard for the behavior of UTF-8 character terminals
 * will require a careful analysis not only of each Unicode character,
 * but also of each presentation form, something the author of these
 * routines has avoided to do so far.
 *
 * http://www.unicode.org/unicode/reports/tr11/
 *
 * Markus Kuhn -- 2007-05-26 (Unicode 5.0)
 *
 * Permission to use, copy, modify, and distribute this software
 * for any purpose and without fee is hereby granted. The author
 * disclaims all warranties with regard to this software.
 *
 * Latest version: http://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c
 */

struct interval {
  int first;
  int last;
};

/* auxiliary function for binary search in interval table */
static inline int bisearch(wchar_t ucs, const struct interval *table, int max) {
  int min = 0;
  int mid;

  if (ucs < table[0].first || ucs > table[max].last)
    return 0;
  while (max >= min) {
    mid = (min + max) / 2;
    if (ucs > table[mid].last)
      min = mid + 1;
    else if (ucs < table[mid].first)
      max = mid - 1;
    else
      return 1;
  }

  return 0;
}

/* The following two functions define the column width of an ISO 10646
 * character as follows:
 *
 *    - The null character (U+0000) has a column width of 0.
 *
 *    - Other C0/C1 control characters and DEL will lead to a return
 *      value of -1.
 *
 *    - Non-spacing and enclosing combining characters (general
 *      category code Mn or Me in the Unicode database) have a
 *      column width of 0.
 *
 *    - SOFT HYPHEN (U+00AD) has a column width of 1.
 *
 *    - Other format characters (general category code Cf in the Unicode
 *      database) and ZERO WIDTH SPACE (U+200B) have a column width of 0.
 *
 *    - Hangul Jamo medial vowels and final consonants (U+1160-U+11FF)
 *      have a column width of 0.
 *
 *    - Spacing characters in the East Asian Wide (W) or East Asian
 *      Full-width (F) category as defined in Unicode Technical
 *      Report #11 have a column width of 2.
 *
 *    - All remaining characters (including all printable
 *      ISO 8859-1 and WGL4 characters, Unicode control characters,
 *      etc.) have a column width of 1.
 *
 * This implementation assumes that wchar_t characters are encoded
 * in ISO 10646.
 */

static inline int mk_wcwidth(wchar_t ucs) {
  /* sorted list of non-overlapping intervals of non-spacing characters */
  /* generated by "uniset +cat=Me +cat=Mn +cat=Cf -00AD +1160-11FF +200B c" */
  static const struct interval combining[] = {
      {0x0300, 0x036F},   {0x0483, 0x0486},   {0x0488, 0x0489},   {0x0591, 0x05BD},
      {0x05BF, 0x05BF},   {0x05C1, 0x05C2},   {0x05C4, 0x05C5},   {0x05C7, 0x05C7},
      {0x0600, 0x0603},   {0x0610, 0x0615},   {0x064B, 0x065E},   {0x0670, 0x0670},
      {0x06D6, 0x06E4},   {0x06E7, 0x06E8},   {0x06EA, 0x06ED},   {0x070F, 0x070F},
      {0x0711, 0x0711},   {0x0730, 0x074A},   {0x07A6, 0x07B0},   {0x07EB, 0x07F3},
      {0x0901, 0x0902},   {0x093C, 0x093C},   {0x0941, 0x0948},   {0x094D, 0x094D},
      {0x0951, 0x0954},   {0x0962, 0x0963},   {0x0981, 0x0981},   {0x09BC, 0x09BC},
      {0x09C1, 0x09C4},   {0x09CD, 0x09CD},   {0x09E2, 0x09E3},   {0x0A01, 0x0A02},
      {0x0A3C, 0x0A3C},   {0x0A41, 0x0A42},   {0x0A47, 0x0A48},   {0x0A4B, 0x0A4D},
      {0x0A70, 0x0A71},   {0x0A81, 0x0A82},   {0x0ABC, 0x0ABC},   {0x0AC1, 0x0AC5},
      {0x0AC7, 0x0AC8},   {0x0ACD, 0x0ACD},   {0x0AE2, 0x0AE3},   {0x0B01, 0x0B01},
      {0x0B3C, 0x0B3C},   {0x0B3F, 0x0B3F},   {0x0B41, 0x0B43},   {0x0B4D, 0x0B4D},
      {0x0B56, 0x0B56},   {0x0B82, 0x0B82},   {0x0BC0, 0x0BC0},   {0x0BCD, 0x0BCD},
      {0x0C3E, 0x0C40},   {0x0C46, 0x0C48},   {0x0C4A, 0x0C4D},   {0x0C55, 0x0C56},
      {0x0CBC, 0x0CBC},   {0x0CBF, 0x0CBF},   {0x0CC6, 0x0CC6},   {0x0CCC, 0x0CCD},
      {0x0CE2, 0x0CE3},   {0x0D41, 0x0D43},   {0x0D4D, 0x0D4D},   {0x0DCA, 0x0DCA},
      {0x0DD2, 0x0DD4},   {0x0DD6, 0x0DD6},   {0x0E31, 0x0E31},   {0x0E34, 0x0E3A},
      {0x0E47, 0x0E4E},   {0x0EB1, 0x0EB1},   {0x0EB4, 0x0EB9},   {0x0EBB, 0x0EBC},
      {0x0EC8, 0x0ECD},   {0x0F18, 0x0F19},   {0x0F35, 0x0F35},   {0x0F37, 0x0F37},
      {0x0F39, 0x0F39},   {0x0F71, 0x0F7E},   {0x0F80, 0x0F84},   {0x0F86, 0x0F87},
      {0x0F90, 0x0F97},   {0x0F99, 0x0FBC},   {0x0FC6, 0x0FC6},   {0x102D, 0x1030},
      {0x1032, 0x1032},   {0x1036, 0x1037},   {0x1039, 0x1039},   {0x1058, 0x1059},
      {0x1160, 0x11FF},   {0x135F, 0x135F},   {0x1712, 0x1714},   {0x1732, 0x1734},
      {0x1752, 0x1753},   {0x1772, 0x1773},   {0x17B4, 0x17B5},   {0x17B7, 0x17BD},
      {0x17C6, 0x17C6},   {0x17C9, 0x17D3},   {0x17DD, 0x17DD},   {0x180B, 0x180D},
      {0x18A9, 0x18A9},   {0x1920, 0x1922},   {0x1927, 0x1928},   {0x1932, 0x1932},
      {0x1939, 0x193B},   {0x1A17, 0x1A18},   {0x1B00, 0x1B03},   {0x1B34, 0x1B34},
      {0x1B36, 0x1B3A},   {0x1B3C, 0x1B3C},   {0x1B42, 0x1B42},   {0x1B6B, 0x1B73},
      {0x1DC0, 0x1DCA},   {0x1DFE, 0x1DFF},   {0x200B, 0x200F},   {0x202A, 0x202E},
      {0x2060, 0x2063},   {0x206A, 0x206F},   {0x20D0, 0x20EF},   {0x302A, 0x302F},
      {0x3099, 0x309A},   {0xA806, 0xA806},   {0xA80B, 0xA80B},   {0xA825, 0xA826},
      {0xFB1E, 0xFB1E},   {0xFE00, 0xFE0F},   {0xFE20, 0xFE23},   {0xFEFF, 0xFEFF},
      {0xFFF9, 0xFFFB},   {0x10A01, 0x10A03}, {0x10A05, 0x10A06}, {0x10A0C, 0x10A0F},
      {0x10A38, 0x10A3A}, {0x10A3F, 0x10A3F}, {0x1D167, 0x1D169}, {0x1D173, 0x1D182},
      {0x1D185, 0x1D18B}, {0x1D1AA, 0x1D1AD}, {0x1D242, 0x1D244}, {0xE0001, 0xE0001},
      {0xE0020, 0xE007F}, {0xE0100, 0xE01EF}};

  /* test for 8-bit control characters */
  if (ucs == 0)
    return 0;
  if (ucs < 32 || (ucs >= 0x7f && ucs < 0xa0))
    return -1;

  /* binary search in table of non-spacing characters */
  if (bisearch(ucs, combining, sizeof(combining) / sizeof(struct interval) - 1))
    return 0;

  /* if we arrive here, ucs is not a combining or C0/C1 control character */

  return 1 + (ucs >= 0x1100 &&
              (ucs <= 0x115f || /* Hangul Jamo init. consonants */
               ucs == 0x2329 || ucs == 0x232a ||
               (ucs >= 0x2e80 && ucs <= 0xa4cf && ucs != 0x303f) || /* CJK ... Yi */
               (ucs >= 0xac00 && ucs <= 0xd7a3) ||                  /* Hangul Syllables */
               (ucs >= 0xf900 && ucs <= 0xfaff) || /* CJK Compatibility Ideographs */
               (ucs >= 0xfe10 && ucs <= 0xfe19) || /* Vertical forms */
               (ucs >= 0xfe30 && ucs <= 0xfe6f) || /* CJK Compatibility Forms */
               (ucs >= 0xff00 && ucs <= 0xff60) || /* Fullwidth Forms */
               (ucs >= 0xffe0 && ucs <= 0xffe6) || (ucs >= 0x20000 && ucs <= 0x2fffd) ||
               (ucs >= 0x30000 && ucs <= 0x3fffd)));
}

static inline int mk_wcswidth(const wchar_t *pwcs, size_t n) {
  int w, width = 0;

  for (; *pwcs && n-- > 0; pwcs++)
    if ((w = mk_wcwidth(*pwcs)) < 0)
      return -1;
    else
      width += w;

  return width;
}

/*
 * The following functions are the same as mk_wcwidth() and
 * mk_wcswidth(), except that spacing characters in the East Asian
 * Ambiguous (A) category as defined in Unicode Technical Report #11
 * have a column width of 2. This variant might be useful for users of
 * CJK legacy encodings who want to migrate to UCS without changing
 * the traditional terminal character-width behaviour. It is not
 * otherwise recommended for general use.
 */
static inline int mk_wcwidth_cjk(wchar_t ucs) {
  /* sorted list of non-overlapping intervals of East Asian Ambiguous
   * characters, generated by "uniset +WIDTH-A -cat=Me -cat=Mn -cat=Cf c" */
  static const struct interval ambiguous[] = {
      {0x00A1, 0x00A1},    {0x00A4, 0x00A4}, {0x00A7, 0x00A8}, {0x00AA, 0x00AA}, {0x00AE, 0x00AE},
      {0x00B0, 0x00B4},    {0x00B6, 0x00BA}, {0x00BC, 0x00BF}, {0x00C6, 0x00C6}, {0x00D0, 0x00D0},
      {0x00D7, 0x00D8},    {0x00DE, 0x00E1}, {0x00E6, 0x00E6}, {0x00E8, 0x00EA}, {0x00EC, 0x00ED},
      {0x00F0, 0x00F0},    {0x00F2, 0x00F3}, {0x00F7, 0x00FA}, {0x00FC, 0x00FC}, {0x00FE, 0x00FE},
      {0x0101, 0x0101},    {0x0111, 0x0111}, {0x0113, 0x0113}, {0x011B, 0x011B}, {0x0126, 0x0127},
      {0x012B, 0x012B},    {0x0131, 0x0133}, {0x0138, 0x0138}, {0x013F, 0x0142}, {0x0144, 0x0144},
      {0x0148, 0x014B},    {0x014D, 0x014D}, {0x0152, 0x0153}, {0x0166, 0x0167}, {0x016B, 0x016B},
      {0x01CE, 0x01CE},    {0x01D0, 0x01D0}, {0x01D2, 0x01D2}, {0x01D4, 0x01D4}, {0x01D6, 0x01D6},
      {0x01D8, 0x01D8},    {0x01DA, 0x01DA}, {0x01DC, 0x01DC}, {0x0251, 0x0251}, {0x0261, 0x0261},
      {0x02C4, 0x02C4},    {0x02C7, 0x02C7}, {0x02C9, 0x02CB}, {0x02CD, 0x02CD}, {0x02D0, 0x02D0},
      {0x02D8, 0x02DB},    {0x02DD, 0x02DD}, {0x02DF, 0x02DF}, {0x0391, 0x03A1}, {0x03A3, 0x03A9},
      {0x03B1, 0x03C1},    {0x03C3, 0x03C9}, {0x0401, 0x0401}, {0x0410, 0x044F}, {0x0451, 0x0451},
      {0x2010, 0x2010},    {0x2013, 0x2016}, {0x2018, 0x2019}, {0x201C, 0x201D}, {0x2020, 0x2022},
      {0x2024, 0x2027},    {0x2030, 0x2030}, {0x2032, 0x2033}, {0x2035, 0x2035}, {0x203B, 0x203B},
      {0x203E, 0x203E},    {0x2074, 0x2074}, {0x207F, 0x207F}, {0x2081, 0x2084}, {0x20AC, 0x20AC},
      {0x2103, 0x2103},    {0x2105, 0x2105}, {0x2109, 0x2109}, {0x2113, 0x2113}, {0x2116, 0x2116},
      {0x2121, 0x2122},    {0x2126, 0x2126}, {0x212B, 0x212B}, {0x2153, 0x2154}, {0x215B, 0x215E},
      {0x2160, 0x216B},    {0x2170, 0x2179}, {0x2190, 0x2199}, {0x21B8, 0x21B9}, {0x21D2, 0x21D2},
      {0x21D4, 0x21D4},    {0x21E7, 0x21E7}, {0x2200, 0x2200}, {0x2202, 0x2203}, {0x2207, 0x2208},
      {0x220B, 0x220B},    {0x220F, 0x220F}, {0x2211, 0x2211}, {0x2215, 0x2215}, {0x221A, 0x221A},
      {0x221D, 0x2220},    {0x2223, 0x2223}, {0x2225, 0x2225}, {0x2227, 0x222C}, {0x222E, 0x222E},
      {0x2234, 0x2237},    {0x223C, 0x223D}, {0x2248, 0x2248}, {0x224C, 0x224C}, {0x2252, 0x2252},
      {0x2260, 0x2261},    {0x2264, 0x2267}, {0x226A, 0x226B}, {0x226E, 0x226F}, {0x2282, 0x2283},
      {0x2286, 0x2287},    {0x2295, 0x2295}, {0x2299, 0x2299}, {0x22A5, 0x22A5}, {0x22BF, 0x22BF},
      {0x2312, 0x2312},    {0x2460, 0x24E9}, {0x24EB, 0x254B}, {0x2550, 0x2573}, {0x2580, 0x258F},
      {0x2592, 0x2595},    {0x25A0, 0x25A1}, {0x25A3, 0x25A9}, {0x25B2, 0x25B3}, {0x25B6, 0x25B7},
      {0x25BC, 0x25BD},    {0x25C0, 0x25C1}, {0x25C6, 0x25C8}, {0x25CB, 0x25CB}, {0x25CE, 0x25D1},
      {0x25E2, 0x25E5},    {0x25EF, 0x25EF}, {0x2605, 0x2606}, {0x2609, 0x2609}, {0x260E, 0x260F},
      {0x2614, 0x2615},    {0x261C, 0x261C}, {0x261E, 0x261E}, {0x2640, 0x2640}, {0x2642, 0x2642},
      {0x2660, 0x2661},    {0x2663, 0x2665}, {0x2667, 0x266A}, {0x266C, 0x266D}, {0x266F, 0x266F},
      {0x273D, 0x273D},    {0x2776, 0x277F}, {0xE000, 0xF8FF}, {0xFFFD, 0xFFFD}, {0xF0000, 0xFFFFD},
      {0x100000, 0x10FFFD}};

  /* binary search in table of non-spacing characters */
  if (bisearch(ucs, ambiguous, sizeof(ambiguous) / sizeof(struct interval) - 1))
    return 2;

  return mk_wcwidth(ucs);
}

static inline int mk_wcswidth_cjk(const wchar_t *pwcs, size_t n) {
  int w, width = 0;

  for (; *pwcs && n-- > 0; pwcs++)
    if ((w = mk_wcwidth_cjk(*pwcs)) < 0)
      return -1;
    else
      width += w;

  return width;
}

// convert UTF-8 string to wstring
static inline std::wstring utf8_decode(const std::string &str) {
  std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
  return myconv.from_bytes(str);
}

// convert wstring to UTF-8 string
static inline std::string utf8_encode(const std::wstring &str) {
  std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
  return myconv.to_bytes(str);
}

} // namespace details

static inline int display_width(const std::string &input) {
  using namespace unicode::details;
  return mk_wcswidth(utf8_decode(input).c_str(), input.size());
}

static inline int display_width(const std::wstring &input) {
  return details::mk_wcswidth(input.c_str(), input.size());
}

#else

static inline int display_width(const std::string &input) { return input.length(); }

static inline int display_width(const std::wstring &input) { return input.length(); }

#endif

} // namespace unicode

// #include <indicators/setting.hpp>
// #include <indicators/indicators.hpp>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <ostream>
#include <string>
#include <vector>

#include <cassert>
#include <cmath>

namespace indicators {
namespace details {

inline void set_stream_color(std::ostream &os, Color color) {
  switch (color) {
  case Color::grey:
    os << termcolor::grey;
    break;
  case Color::red:
    os << termcolor::red;
    break;
  case Color::green:
    os << termcolor::green;
    break;
  case Color::yellow:
    os << termcolor::yellow;
    break;
  case Color::blue:
    os << termcolor::blue;
    break;
  case Color::magenta:
    os << termcolor::magenta;
    break;
  case Color::cyan:
    os << termcolor::cyan;
    break;
  case Color::white:
    os << termcolor::white;
    break;
  default:
    assert(false);
  }
}

inline void set_font_style(std::ostream &os, FontStyle style) {
  switch (style) {
  case FontStyle::bold:
    os << termcolor::bold;
    break;
  case FontStyle::dark:
    os << termcolor::dark;
    break;
  case FontStyle::italic:
    os << termcolor::italic;
    break;
  case FontStyle::underline:
    os << termcolor::underline;
    break;
  case FontStyle::blink:
    os << termcolor::blink;
    break;
  case FontStyle::reverse:
    os << termcolor::reverse;
    break;
  case FontStyle::concealed:
    os << termcolor::concealed;
    break;
  case FontStyle::crossed:
    os << termcolor::crossed;
    break;
  default:
    break;
  }
}

inline std::ostream &write_duration(std::ostream &os, std::chrono::nanoseconds ns) {
  using namespace std;
  using namespace std::chrono;
  using days = duration<int, ratio<86400>>;
  char fill = os.fill();
  os.fill('0');
  auto d = duration_cast<days>(ns);
  ns -= d;
  auto h = duration_cast<hours>(ns);
  ns -= h;
  auto m = duration_cast<minutes>(ns);
  ns -= m;
  auto s = duration_cast<seconds>(ns);
  if (d.count() > 0)
    os << setw(2) << d.count() << "d:";
  if (h.count() > 0)
    os << setw(2) << h.count() << "h:";
  os << setw(2) << m.count() << "m:" << setw(2) << s.count() << 's';
  os.fill(fill);
  return os;
}

class BlockProgressScaleWriter {
public:
  BlockProgressScaleWriter(std::ostream &os, size_t bar_width) : os(os), bar_width(bar_width) {}

  std::ostream &write(float progress) {
    std::string fill_text{"█"};
    std::vector<std::string> lead_characters{" ", "▏", "▎", "▍", "▌", "▋", "▊", "▉"};
    auto value = std::min(1.0f, std::max(0.0f, progress / 100.0f));
    auto whole_width = std::floor(value * bar_width);
    auto remainder_width = fmod((value * bar_width), 1.0f);
    auto part_width = std::floor(remainder_width * lead_characters.size());
    std::string lead_text = lead_characters[size_t(part_width)];
    if ((bar_width - whole_width - 1) < 0)
      lead_text = "";
    for (size_t i = 0; i < whole_width; ++i)
      os << fill_text;
    os << lead_text;
    for (size_t i = 0; i < (bar_width - whole_width - 1); ++i)
      os << " ";
    return os;
  }

private:
  std::ostream &os;
  size_t bar_width = 0;
};

class ProgressScaleWriter {
public:
  ProgressScaleWriter(std::ostream &os, size_t bar_width, const std::string &fill,
                      const std::string &lead, const std::string &remainder)
      : os(os), bar_width(bar_width), fill(fill), lead(lead), remainder(remainder) {}

  std::ostream &write(float progress) {
    auto pos = static_cast<size_t>(progress * bar_width / 100.0);
    for (size_t i = 0, current_display_width = 0; i < bar_width;) {
      std::string next;

      if (i < pos) {
        next = fill;
        current_display_width = unicode::display_width(fill);
      } else if (i == pos) {
        next = lead;
        current_display_width = unicode::display_width(lead);
      } else {
        next = remainder;
        current_display_width = unicode::display_width(remainder);
      }

      i += current_display_width;

      if (i > bar_width) {
        // `next` is larger than the allowed bar width
        // fill with empty space instead
        os << std::string((bar_width - (i - current_display_width)), ' ');
        break;
      }

      os << next;
    }
    return os;
  }

private:
  std::ostream &os;
  size_t bar_width = 0;
  std::string fill;
  std::string lead;
  std::string remainder;
};

class IndeterminateProgressScaleWriter {
public:
  IndeterminateProgressScaleWriter(std::ostream &os, size_t bar_width, const std::string &fill,
                                   const std::string &lead)
      : os(os), bar_width(bar_width), fill(fill), lead(lead) {}

  std::ostream &write(size_t progress) {
    for (size_t i = 0; i < bar_width;) {
      std::string next;
      size_t current_display_width = 0;

      if (i < progress) {
        next = fill;
        current_display_width = unicode::display_width(fill);
      } else if (i == progress) {
        next = lead;
        current_display_width = unicode::display_width(lead);
      } else {
        next = fill;
        current_display_width = unicode::display_width(fill);
      }

      i += current_display_width;

      if (i > bar_width) {
        // `next` is larger than the allowed bar width
        // fill with empty space instead
        os << std::string((bar_width - (i - current_display_width)), ' ');
        break;
      }

      os << next;
    }
    return os;
  }

private:
  std::ostream &os;
  size_t bar_width = 0;
  std::string fill;
  std::string lead;
};

} // namespace details
} // namespace indicators
#pragma once

// #include <indicators/details/stream_helper.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
// #include <indicators/color.hpp>
// #include <indicators/setting.hpp>
// #include <indicators/terminal_size.hpp>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace indicators {

class ProgressBar {
  using Settings =
      std::tuple<option::BarWidth, option::PrefixText, option::PostfixText, option::Start,
                 option::End, option::Fill, option::Lead, option::Remainder,
                 option::MaxPostfixTextLen, option::Completed, option::ShowPercentage,
                 option::ShowElapsedTime, option::ShowRemainingTime, option::SavedStartTime,
                 option::ForegroundColor, option::FontStyles, option::MinProgress,
                 option::MaxProgress, option::ProgressType, option::Stream>;

public:
  template <typename... Args,
            typename std::enable_if<details::are_settings_from_tuple<
                                        Settings, typename std::decay<Args>::type...>::value,
                                    void *>::type = nullptr>
  explicit ProgressBar(Args &&... args)
      : settings_(
            details::get<details::ProgressBarOption::bar_width>(option::BarWidth{100},
                                                                std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::prefix_text>(option::PrefixText{},
                                                                  std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::postfix_text>(option::PostfixText{},
                                                                   std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::start>(option::Start{"["},
                                                            std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::end>(option::End{"]"},
                                                          std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::fill>(option::Fill{"="},
                                                           std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::lead>(option::Lead{">"},
                                                           std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::remainder>(option::Remainder{" "},
                                                                std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::max_postfix_text_len>(
                option::MaxPostfixTextLen{0}, std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::completed>(option::Completed{false},
                                                                std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::show_percentage>(option::ShowPercentage{false},
                                                                      std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::show_elapsed_time>(
                option::ShowElapsedTime{false}, std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::show_remaining_time>(
                option::ShowRemainingTime{false}, std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::saved_start_time>(
                option::SavedStartTime{false}, std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::foreground_color>(
                option::ForegroundColor{Color::unspecified}, std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::font_styles>(
                option::FontStyles{std::vector<FontStyle>{}}, std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::min_progress>(option::MinProgress{0},
                                                                   std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::max_progress>(option::MaxProgress{100},
                                                                   std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::progress_type>(
                option::ProgressType{ProgressType::incremental}, std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::stream>(option::Stream{std::cout},
                                                             std::forward<Args>(args)...)) {

    // if progress is incremental, start from min_progress
    // else start from max_progress
    const auto type = get_value<details::ProgressBarOption::progress_type>();
    if (type == ProgressType::incremental)
      progress_ = get_value<details::ProgressBarOption::min_progress>();
    else
      progress_ = get_value<details::ProgressBarOption::max_progress>();
  }

  template <typename T, details::ProgressBarOption id>
  void set_option(details::Setting<T, id> &&setting) {
    static_assert(!std::is_same<T, typename std::decay<decltype(details::get_value<id>(
                                       std::declval<Settings>()))>::type>::value,
                  "Setting has wrong type!");
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<id>() = std::move(setting).value;
  }

  template <typename T, details::ProgressBarOption id>
  void set_option(const details::Setting<T, id> &setting) {
    static_assert(!std::is_same<T, typename std::decay<decltype(details::get_value<id>(
                                       std::declval<Settings>()))>::type>::value,
                  "Setting has wrong type!");
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<id>() = setting.value;
  }

  void set_option(
      const details::Setting<std::string, details::ProgressBarOption::postfix_text> &setting) {
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<details::ProgressBarOption::postfix_text>() = setting.value;
    if (setting.value.length() > get_value<details::ProgressBarOption::max_postfix_text_len>()) {
      get_value<details::ProgressBarOption::max_postfix_text_len>() = setting.value.length();
    }
  }

  void
  set_option(details::Setting<std::string, details::ProgressBarOption::postfix_text> &&setting) {
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<details::ProgressBarOption::postfix_text>() = std::move(setting).value;
    auto &new_value = get_value<details::ProgressBarOption::postfix_text>();
    if (new_value.length() > get_value<details::ProgressBarOption::max_postfix_text_len>()) {
      get_value<details::ProgressBarOption::max_postfix_text_len>() = new_value.length();
    }
  }

  void set_progress(size_t new_progress) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      progress_ = new_progress;
    }

    save_start_time();
    print_progress();
  }

  void tick() {
    {
      std::lock_guard<std::mutex> lock{mutex_};
      const auto type = get_value<details::ProgressBarOption::progress_type>();
      if (type == ProgressType::incremental)
        progress_ += 1;
      else
        progress_ -= 1;
    }
    save_start_time();
    print_progress();
  }

  size_t current() {
    std::lock_guard<std::mutex> lock{mutex_};
    return std::min(progress_, size_t(get_value<details::ProgressBarOption::max_progress>()));
  }

  bool is_completed() const { return get_value<details::ProgressBarOption::completed>(); }

  void mark_as_completed() {
    get_value<details::ProgressBarOption::completed>() = true;
    print_progress();
  }

private:
  template <details::ProgressBarOption id>
  auto get_value() -> decltype((details::get_value<id>(std::declval<Settings &>()).value)) {
    return details::get_value<id>(settings_).value;
  }

  template <details::ProgressBarOption id>
  auto get_value() const
      -> decltype((details::get_value<id>(std::declval<const Settings &>()).value)) {
    return details::get_value<id>(settings_).value;
  }

  size_t progress_{0};
  Settings settings_;
  std::chrono::nanoseconds elapsed_;
  std::chrono::time_point<std::chrono::high_resolution_clock> start_time_point_;
  std::mutex mutex_;

  template <typename Indicator, size_t count> friend class MultiProgress;
  template <typename Indicator> friend class DynamicProgress;
  std::atomic<bool> multi_progress_mode_{false};

  void save_start_time() {
    auto &show_elapsed_time = get_value<details::ProgressBarOption::show_elapsed_time>();
    auto &saved_start_time = get_value<details::ProgressBarOption::saved_start_time>();
    auto &show_remaining_time = get_value<details::ProgressBarOption::show_remaining_time>();
    if ((show_elapsed_time || show_remaining_time) && !saved_start_time) {
      start_time_point_ = std::chrono::high_resolution_clock::now();
      saved_start_time = true;
    }
  }

  std::pair<std::string, size_t> get_prefix_text() {
    std::stringstream os;
    os << get_value<details::ProgressBarOption::prefix_text>();
    const auto result = os.str();
    const auto result_size = unicode::display_width(result);
    return {result, result_size};
  }

  std::pair<std::string, size_t> get_postfix_text() {
    std::stringstream os;
    const auto max_progress = get_value<details::ProgressBarOption::max_progress>();

    if (get_value<details::ProgressBarOption::show_percentage>()) {
      os << " "
         << std::min(static_cast<size_t>(static_cast<float>(progress_) / max_progress * 100),
                     size_t(100))
         << "%";
    }

    auto &saved_start_time = get_value<details::ProgressBarOption::saved_start_time>();

    if (get_value<details::ProgressBarOption::show_elapsed_time>()) {
      os << " [";
      if (saved_start_time)
        details::write_duration(os, elapsed_);
      else
        os << "00:00s";
    }

    if (get_value<details::ProgressBarOption::show_remaining_time>()) {
      if (get_value<details::ProgressBarOption::show_elapsed_time>())
        os << "<";
      else
        os << " [";

      if (saved_start_time) {
        auto eta = std::chrono::nanoseconds(
            progress_ > 0 ? static_cast<long long>(elapsed_.count() * max_progress / progress_)
                          : 0);
        auto remaining = eta > elapsed_ ? (eta - elapsed_) : (elapsed_ - eta);
        details::write_duration(os, remaining);
      } else {
        os << "00:00s";
      }

      os << "]";
    } else {
      if (get_value<details::ProgressBarOption::show_elapsed_time>())
        os << "]";
    }

    os << " " << get_value<details::ProgressBarOption::postfix_text>();

    const auto result = os.str();
    const auto result_size = unicode::display_width(result);
    return {result, result_size};
  }

public:
  void print_progress(bool from_multi_progress = false) {
    std::lock_guard<std::mutex> lock{mutex_};

    auto &os = get_value<details::ProgressBarOption::stream>();

    const auto type = get_value<details::ProgressBarOption::progress_type>();
    const auto min_progress = get_value<details::ProgressBarOption::min_progress>();
    const auto max_progress = get_value<details::ProgressBarOption::max_progress>();
    if (multi_progress_mode_ && !from_multi_progress) {
      if ((type == ProgressType::incremental && progress_ >= max_progress) ||
          (type == ProgressType::decremental && progress_ <= min_progress)) {
        get_value<details::ProgressBarOption::completed>() = true;
      }
      return;
    }
    auto now = std::chrono::high_resolution_clock::now();
    if (!get_value<details::ProgressBarOption::completed>())
      elapsed_ = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start_time_point_);

    if (get_value<details::ProgressBarOption::foreground_color>() != Color::unspecified)
      details::set_stream_color(os, get_value<details::ProgressBarOption::foreground_color>());

    for (auto &style : get_value<details::ProgressBarOption::font_styles>())
      details::set_font_style(os, style);

    const auto prefix_pair = get_prefix_text();
    const auto prefix_text = prefix_pair.first;
    const auto prefix_length = prefix_pair.second;
    os << prefix_text;

    os << get_value<details::ProgressBarOption::start>();

    details::ProgressScaleWriter writer{os, get_value<details::ProgressBarOption::bar_width>(),
                                        get_value<details::ProgressBarOption::fill>(),
                                        get_value<details::ProgressBarOption::lead>(),
                                        get_value<details::ProgressBarOption::remainder>()};
    writer.write(double(progress_) / double(max_progress) * 100.0f);

    os << get_value<details::ProgressBarOption::end>();

    const auto postfix_pair = get_postfix_text();
    const auto postfix_text = postfix_pair.first;
    const auto postfix_length = postfix_pair.second;
    os << postfix_text;

    // Get length of prefix text and postfix text
    const auto start_length = get_value<details::ProgressBarOption::start>().size();
    const auto bar_width = get_value<details::ProgressBarOption::bar_width>();
    const auto end_length = get_value<details::ProgressBarOption::end>().size();
    const auto terminal_width = terminal_size().second;
    // prefix + bar_width + postfix should be <= terminal_width
    const int remaining =
        terminal_width - (prefix_length + start_length + bar_width + end_length + postfix_length);
    if (remaining > 0) {
      os << std::string(remaining, ' ') << "\r";
    } else if (remaining < 0) {
      // Do nothing. Maybe in the future truncate postfix with ...
    }
    os.flush();

    if ((type == ProgressType::incremental && progress_ >= max_progress) ||
        (type == ProgressType::decremental && progress_ <= min_progress)) {
      get_value<details::ProgressBarOption::completed>() = true;
    }
    if (get_value<details::ProgressBarOption::completed>() &&
        !from_multi_progress) // Don't std::endl if calling from MultiProgress
      os << termcolor::reset << std::endl;
  }
};

} // namespace indicators

#pragma once

// #include <indicators/color.hpp>
// #include <indicators/details/stream_helper.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
// #include <indicators/setting.hpp>
// #include <indicators/terminal_size.hpp>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <utility>

namespace indicators {

class BlockProgressBar {
  using Settings = std::tuple<option::ForegroundColor, option::BarWidth, option::Start, option::End,
                              option::PrefixText, option::PostfixText, option::ShowPercentage,
                              option::ShowElapsedTime, option::ShowRemainingTime, option::Completed,
                              option::SavedStartTime, option::MaxPostfixTextLen, option::FontStyles,
                              option::MaxProgress, option::Stream>;

public:
  template <typename... Args,
            typename std::enable_if<details::are_settings_from_tuple<
                                        Settings, typename std::decay<Args>::type...>::value,
                                    void *>::type = nullptr>
  explicit BlockProgressBar(Args &&... args)
      : settings_(details::get<details::ProgressBarOption::foreground_color>(
                      option::ForegroundColor{Color::unspecified}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::bar_width>(option::BarWidth{100},
                                                                      std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::start>(option::Start{"["},
                                                                  std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::end>(option::End{"]"},
                                                                std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::prefix_text>(
                      option::PrefixText{""}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::postfix_text>(
                      option::PostfixText{""}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::show_percentage>(
                      option::ShowPercentage{true}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::show_elapsed_time>(
                      option::ShowElapsedTime{false}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::show_remaining_time>(
                      option::ShowRemainingTime{false}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::completed>(option::Completed{false},
                                                                      std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::saved_start_time>(
                      option::SavedStartTime{false}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::max_postfix_text_len>(
                      option::MaxPostfixTextLen{0}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::font_styles>(
                      option::FontStyles{std::vector<FontStyle>{}}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::max_progress>(
                      option::MaxProgress{100}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::stream>(option::Stream{std::cout},
                                                                   std::forward<Args>(args)...)) {}

  template <typename T, details::ProgressBarOption id>
  void set_option(details::Setting<T, id> &&setting) {
    static_assert(!std::is_same<T, typename std::decay<decltype(details::get_value<id>(
                                       std::declval<Settings>()))>::type>::value,
                  "Setting has wrong type!");
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<id>() = std::move(setting).value;
  }

  template <typename T, details::ProgressBarOption id>
  void set_option(const details::Setting<T, id> &setting) {
    static_assert(!std::is_same<T, typename std::decay<decltype(details::get_value<id>(
                                       std::declval<Settings>()))>::type>::value,
                  "Setting has wrong type!");
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<id>() = setting.value;
  }

  void set_option(
      const details::Setting<std::string, details::ProgressBarOption::postfix_text> &setting) {
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<details::ProgressBarOption::postfix_text>() = setting.value;
    if (setting.value.length() > get_value<details::ProgressBarOption::max_postfix_text_len>()) {
      get_value<details::ProgressBarOption::max_postfix_text_len>() = setting.value.length();
    }
  }

  void
  set_option(details::Setting<std::string, details::ProgressBarOption::postfix_text> &&setting) {
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<details::ProgressBarOption::postfix_text>() = std::move(setting).value;
    auto &new_value = get_value<details::ProgressBarOption::postfix_text>();
    if (new_value.length() > get_value<details::ProgressBarOption::max_postfix_text_len>()) {
      get_value<details::ProgressBarOption::max_postfix_text_len>() = new_value.length();
    }
  }

  void set_progress(float value) {
    if (is_completed())
      return;
    {
      std::lock_guard<std::mutex> lock{mutex_};
      progress_ = value;
    }
    save_start_time();
    print_progress();
  }

  void tick() {
    if (is_completed())
      return;
    {
      std::lock_guard<std::mutex> lock{mutex_};
      progress_ += 1;
    }
    save_start_time();
    print_progress();
  }

  size_t current() {
    std::lock_guard<std::mutex> lock{mutex_};
    return std::min(static_cast<size_t>(progress_),
                    size_t(get_value<details::ProgressBarOption::max_progress>()));
  }

  bool is_completed() const { return get_value<details::ProgressBarOption::completed>(); }

  void mark_as_completed() {
    if (!get_value<details::ProgressBarOption::completed>())
      print_progress();
    get_value<details::ProgressBarOption::completed>() = true;
  }

private:
  template <details::ProgressBarOption id>
  auto get_value() -> decltype((details::get_value<id>(std::declval<Settings &>()).value)) {
    return details::get_value<id>(settings_).value;
  }

  template <details::ProgressBarOption id>
  auto get_value() const
      -> decltype((details::get_value<id>(std::declval<const Settings &>()).value)) {
    return details::get_value<id>(settings_).value;
  }

  Settings settings_;
  float progress_{0.0};
  std::chrono::time_point<std::chrono::high_resolution_clock> start_time_point_;
  std::mutex mutex_;

  template <typename Indicator, size_t count> friend class MultiProgress;
  template <typename Indicator> friend class DynamicProgress;
  std::atomic<bool> multi_progress_mode_{false};

  void save_start_time() {
    auto &show_elapsed_time = get_value<details::ProgressBarOption::show_elapsed_time>();
    auto &saved_start_time = get_value<details::ProgressBarOption::saved_start_time>();
    auto &show_remaining_time = get_value<details::ProgressBarOption::show_remaining_time>();
    if ((show_elapsed_time || show_remaining_time) && !saved_start_time) {
      start_time_point_ = std::chrono::high_resolution_clock::now();
      saved_start_time = true;
    }
  }

  std::pair<std::string, size_t> get_prefix_text() {
    std::stringstream os;
    os << get_value<details::ProgressBarOption::prefix_text>();
    const auto result = os.str();
    const auto result_size = unicode::display_width(result);
    return {result, result_size};
  }

  std::pair<std::string, size_t> get_postfix_text() {
    std::stringstream os;
    const auto max_progress = get_value<details::ProgressBarOption::max_progress>();
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start_time_point_);

    if (get_value<details::ProgressBarOption::show_percentage>()) {
      os << " " << std::min(static_cast<size_t>(progress_ / max_progress * 100.0), size_t(100))
         << "%";
    }

    auto &saved_start_time = get_value<details::ProgressBarOption::saved_start_time>();

    if (get_value<details::ProgressBarOption::show_elapsed_time>()) {
      os << " [";
      if (saved_start_time)
        details::write_duration(os, elapsed);
      else
        os << "00:00s";
    }

    if (get_value<details::ProgressBarOption::show_remaining_time>()) {
      if (get_value<details::ProgressBarOption::show_elapsed_time>())
        os << "<";
      else
        os << " [";

      if (saved_start_time) {
        auto eta = std::chrono::nanoseconds(
            progress_ > 0 ? static_cast<long long>(elapsed.count() * max_progress / progress_) : 0);
        auto remaining = eta > elapsed ? (eta - elapsed) : (elapsed - eta);
        details::write_duration(os, remaining);
      } else {
        os << "00:00s";
      }

      os << "]";
    } else {
      if (get_value<details::ProgressBarOption::show_elapsed_time>())
        os << "]";
    }

    os << " " << get_value<details::ProgressBarOption::postfix_text>();

    const auto result = os.str();
    const auto result_size = unicode::display_width(result);
    return {result, result_size};
  }

public:
  void print_progress(bool from_multi_progress = false) {
    std::lock_guard<std::mutex> lock{mutex_};

    auto &os = get_value<details::ProgressBarOption::stream>();

    const auto max_progress = get_value<details::ProgressBarOption::max_progress>();
    if (multi_progress_mode_ && !from_multi_progress) {
      if (progress_ > max_progress) {
        get_value<details::ProgressBarOption::completed>() = true;
      }
      return;
    }

    if (get_value<details::ProgressBarOption::foreground_color>() != Color::unspecified)
      details::set_stream_color(os, get_value<details::ProgressBarOption::foreground_color>());

    for (auto &style : get_value<details::ProgressBarOption::font_styles>())
      details::set_font_style(os, style);

    const auto prefix_pair = get_prefix_text();
    const auto prefix_text = prefix_pair.first;
    const auto prefix_length = prefix_pair.second;
    os << prefix_text;

    os << get_value<details::ProgressBarOption::start>();

    details::BlockProgressScaleWriter writer{os,
                                             get_value<details::ProgressBarOption::bar_width>()};
    writer.write(progress_ / max_progress * 100);

    os << get_value<details::ProgressBarOption::end>();

    const auto postfix_pair = get_postfix_text();
    const auto postfix_text = postfix_pair.first;
    const auto postfix_length = postfix_pair.second;
    os << postfix_text;

    // Get length of prefix text and postfix text
    const auto start_length = get_value<details::ProgressBarOption::start>().size();
    const auto bar_width = get_value<details::ProgressBarOption::bar_width>();
    const auto end_length = get_value<details::ProgressBarOption::end>().size();
    const auto terminal_width = terminal_size().second;
    // prefix + bar_width + postfix should be <= terminal_width
    const int remaining =
        terminal_width - (prefix_length + start_length + bar_width + end_length + postfix_length);
    if (remaining > 0) {
      os << std::string(remaining, ' ') << "\r";
    } else if (remaining < 0) {
      // Do nothing. Maybe in the future truncate postfix with ...
    }
    os.flush();

    if (progress_ == max_progress) {
      get_value<details::ProgressBarOption::completed>() = true;
    }
    if (get_value<details::ProgressBarOption::completed>() &&
        !from_multi_progress) // Don't std::endl if calling from MultiProgress
      os << termcolor::reset << std::endl;
  }
};

} // namespace indicators

#pragma once

// #include <indicators/details/stream_helper.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
// #include <indicators/color.hpp>
// #include <indicators/setting.hpp>
// #include <indicators/terminal_size.hpp>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace indicators {

class IndeterminateProgressBar {
  using Settings =
      std::tuple<option::BarWidth, option::PrefixText, option::PostfixText, option::Start,
                 option::End, option::Fill, option::Lead, option::MaxPostfixTextLen,
                 option::Completed, option::ForegroundColor, option::FontStyles, option::Stream>;

  enum class Direction { forward, backward };

  Direction direction_{Direction::forward};

public:
  template <typename... Args,
            typename std::enable_if<details::are_settings_from_tuple<
                                        Settings, typename std::decay<Args>::type...>::value,
                                    void *>::type = nullptr>
  explicit IndeterminateProgressBar(Args &&... args)
      : settings_(details::get<details::ProgressBarOption::bar_width>(option::BarWidth{100},
                                                                      std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::prefix_text>(
                      option::PrefixText{}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::postfix_text>(
                      option::PostfixText{}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::start>(option::Start{"["},
                                                                  std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::end>(option::End{"]"},
                                                                std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::fill>(option::Fill{"."},
                                                                 std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::lead>(option::Lead{"<==>"},
                                                                 std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::max_postfix_text_len>(
                      option::MaxPostfixTextLen{0}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::completed>(option::Completed{false},
                                                                      std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::foreground_color>(
                      option::ForegroundColor{Color::unspecified}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::font_styles>(
                      option::FontStyles{std::vector<FontStyle>{}}, std::forward<Args>(args)...),
                  details::get<details::ProgressBarOption::stream>(option::Stream{std::cout},
                                                                   std::forward<Args>(args)...)) {
    // starts with [<==>...........]
    // progress_ = 0

    // ends with   [...........<==>]
    //             ^^^^^^^^^^^^^^^^^ bar_width
    //             ^^^^^^^^^^^^ (bar_width - len(lead))
    // progress_ = bar_width - len(lead)
    progress_ = 0;
    max_progress_ = get_value<details::ProgressBarOption::bar_width>() -
                    get_value<details::ProgressBarOption::lead>().size() +
                    get_value<details::ProgressBarOption::start>().size() +
                    get_value<details::ProgressBarOption::end>().size();
  }

  template <typename T, details::ProgressBarOption id>
  void set_option(details::Setting<T, id> &&setting) {
    static_assert(!std::is_same<T, typename std::decay<decltype(details::get_value<id>(
                                       std::declval<Settings>()))>::type>::value,
                  "Setting has wrong type!");
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<id>() = std::move(setting).value;
  }

  template <typename T, details::ProgressBarOption id>
  void set_option(const details::Setting<T, id> &setting) {
    static_assert(!std::is_same<T, typename std::decay<decltype(details::get_value<id>(
                                       std::declval<Settings>()))>::type>::value,
                  "Setting has wrong type!");
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<id>() = setting.value;
  }

  void set_option(
      const details::Setting<std::string, details::ProgressBarOption::postfix_text> &setting) {
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<details::ProgressBarOption::postfix_text>() = setting.value;
    if (setting.value.length() > get_value<details::ProgressBarOption::max_postfix_text_len>()) {
      get_value<details::ProgressBarOption::max_postfix_text_len>() = setting.value.length();
    }
  }

  void
  set_option(details::Setting<std::string, details::ProgressBarOption::postfix_text> &&setting) {
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<details::ProgressBarOption::postfix_text>() = std::move(setting).value;
    auto &new_value = get_value<details::ProgressBarOption::postfix_text>();
    if (new_value.length() > get_value<details::ProgressBarOption::max_postfix_text_len>()) {
      get_value<details::ProgressBarOption::max_postfix_text_len>() = new_value.length();
    }
  }

  void tick() {
    {
      std::lock_guard<std::mutex> lock{mutex_};
      if (get_value<details::ProgressBarOption::completed>())
        return;

      progress_ += (direction_ == Direction::forward) ? 1 : -1;
      if (direction_ == Direction::forward && progress_ == max_progress_) {
        // time to go back
        direction_ = Direction::backward;
      } else if (direction_ == Direction::backward && progress_ == 0) {
        direction_ = Direction::forward;
      }
    }
    print_progress();
  }

  bool is_completed() { return get_value<details::ProgressBarOption::completed>(); }

  void mark_as_completed() {
    get_value<details::ProgressBarOption::completed>() = true;
    print_progress();
  }

private:
  template <details::ProgressBarOption id>
  auto get_value() -> decltype((details::get_value<id>(std::declval<Settings &>()).value)) {
    return details::get_value<id>(settings_).value;
  }

  template <details::ProgressBarOption id>
  auto get_value() const
      -> decltype((details::get_value<id>(std::declval<const Settings &>()).value)) {
    return details::get_value<id>(settings_).value;
  }

  size_t progress_{0};
  size_t max_progress_;
  Settings settings_;
  std::chrono::nanoseconds elapsed_;
  std::mutex mutex_;

  template <typename Indicator, size_t count> friend class MultiProgress;
  template <typename Indicator> friend class DynamicProgress;
  std::atomic<bool> multi_progress_mode_{false};

  std::pair<std::string, size_t> get_prefix_text() {
    std::stringstream os;
    os << get_value<details::ProgressBarOption::prefix_text>();
    const auto result = os.str();
    const auto result_size = unicode::display_width(result);
    return {result, result_size};
  }

  std::pair<std::string, size_t> get_postfix_text() {
    std::stringstream os;
    os << " " << get_value<details::ProgressBarOption::postfix_text>();

    const auto result = os.str();
    const auto result_size = unicode::display_width(result);
    return {result, result_size};
  }

public:
  void print_progress(bool from_multi_progress = false) {
    std::lock_guard<std::mutex> lock{mutex_};

    auto &os = get_value<details::ProgressBarOption::stream>();

    if (multi_progress_mode_ && !from_multi_progress) {
      return;
    }
    if (get_value<details::ProgressBarOption::foreground_color>() != Color::unspecified)
      details::set_stream_color(os, get_value<details::ProgressBarOption::foreground_color>());

    for (auto &style : get_value<details::ProgressBarOption::font_styles>())
      details::set_font_style(os, style);

    const auto prefix_pair = get_prefix_text();
    const auto prefix_text = prefix_pair.first;
    const auto prefix_length = prefix_pair.second;
    os << prefix_text;

    os << get_value<details::ProgressBarOption::start>();

    details::IndeterminateProgressScaleWriter writer{
        os, get_value<details::ProgressBarOption::bar_width>(),
        get_value<details::ProgressBarOption::fill>(),
        get_value<details::ProgressBarOption::lead>()};
    writer.write(progress_);

    os << get_value<details::ProgressBarOption::end>();

    const auto postfix_pair = get_postfix_text();
    const auto postfix_text = postfix_pair.first;
    const auto postfix_length = postfix_pair.second;
    os << postfix_text;

    // Get length of prefix text and postfix text
    const auto start_length = get_value<details::ProgressBarOption::start>().size();
    const auto bar_width = get_value<details::ProgressBarOption::bar_width>();
    const auto end_length = get_value<details::ProgressBarOption::end>().size();
    const auto terminal_width = terminal_size().second;
    // prefix + bar_width + postfix should be <= terminal_width
    const int remaining =
        terminal_width - (prefix_length + start_length + bar_width + end_length + postfix_length);
    if (remaining > 0) {
      os << std::string(remaining, ' ') << "\r";
    } else if (remaining < 0) {
      // Do nothing. Maybe in the future truncate postfix with ...
    }
    os.flush();

    if (get_value<details::ProgressBarOption::completed>() &&
        !from_multi_progress) // Don't std::endl if calling from MultiProgress
      os << termcolor::reset << std::endl;
  }
};

} // namespace indicators

#pragma once
#include <atomic>
#include <functional>
#include <iostream>
#include <mutex>
#include <vector>

// #include <indicators/color.hpp>
// #include <indicators/cursor_movement.hpp>
// #include <indicators/details/stream_helper.hpp>

namespace indicators {

template <typename Indicator, size_t count> class MultiProgress {
public:
  template <typename... Indicators,
            typename = typename std::enable_if<(sizeof...(Indicators) == count)>::type>
  explicit MultiProgress(Indicators &... bars) {
    bars_ = {bars...};
    for (auto &bar : bars_) {
      bar.get().multi_progress_mode_ = true;
    }
  }

  template <size_t index>
  typename std::enable_if<(index >= 0 && index < count), void>::type set_progress(size_t value) {
    if (!bars_[index].get().is_completed())
      bars_[index].get().set_progress(value);
    print_progress();
  }

  template <size_t index>
  typename std::enable_if<(index >= 0 && index < count), void>::type set_progress(float value) {
    if (!bars_[index].get().is_completed())
      bars_[index].get().set_progress(value);
    print_progress();
  }

  template <size_t index>
  typename std::enable_if<(index >= 0 && index < count), void>::type tick() {
    if (!bars_[index].get().is_completed())
      bars_[index].get().tick();
    print_progress();
  }

  template <size_t index>
  typename std::enable_if<(index >= 0 && index < count), bool>::type is_completed() const {
    return bars_[index].get().is_completed();
  }

private:
  std::atomic<bool> started_{false};
  std::mutex mutex_;
  std::vector<std::reference_wrapper<Indicator>> bars_;

  bool _all_completed() {
    bool result{true};
    for (size_t i = 0; i < count; ++i)
      result &= bars_[i].get().is_completed();
    return result;
  }

public:
  void print_progress() {
    std::lock_guard<std::mutex> lock{mutex_};
    if (started_)
      move_up(count);
    for (auto &bar : bars_) {
      bar.get().print_progress(true);
      std::cout << "\n";
    }
    std::cout << termcolor::reset;
    if (!started_)
      started_ = true;
  }
};

} // namespace indicators

#pragma once
#include <atomic>
#include <functional>
// #include <indicators/color.hpp>
// #include <indicators/setting.hpp>
// #include <indicators/details/stream_helper.hpp>
#include <iostream>
#include <mutex>
#include <vector>

namespace indicators {

template <typename Indicator> class DynamicProgress {
  using Settings = std::tuple<option::HideBarWhenComplete>;

public:
  template <typename... Indicators> explicit DynamicProgress(Indicators &... bars) {
    bars_ = {bars...};
    for (auto &bar : bars_) {
      bar.get().multi_progress_mode_ = true;
      ++total_count_;
      ++incomplete_count_;
    }
  }

  Indicator &operator[](size_t index) {
    print_progress();
    std::lock_guard<std::mutex> lock{mutex_};
    return bars_[index].get();
  }

  size_t push_back(Indicator &bar) {
    std::lock_guard<std::mutex> lock{mutex_};
    bar.multi_progress_mode_ = true;
    bars_.push_back(bar);
    return bars_.size() - 1;
  }

  template <typename T, details::ProgressBarOption id>
  void set_option(details::Setting<T, id> &&setting) {
    static_assert(!std::is_same<T, typename std::decay<decltype(details::get_value<id>(
                                       std::declval<Settings>()))>::type>::value,
                  "Setting has wrong type!");
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<id>() = std::move(setting).value;
  }

  template <typename T, details::ProgressBarOption id>
  void set_option(const details::Setting<T, id> &setting) {
    static_assert(!std::is_same<T, typename std::decay<decltype(details::get_value<id>(
                                       std::declval<Settings>()))>::type>::value,
                  "Setting has wrong type!");
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<id>() = setting.value;
  }

private:
  Settings settings_;
  std::atomic<bool> started_{false};
  std::mutex mutex_;
  std::vector<std::reference_wrapper<Indicator>> bars_;
  std::atomic<size_t> total_count_{0};
  std::atomic<size_t> incomplete_count_{0};

  template <details::ProgressBarOption id>
  auto get_value() -> decltype((details::get_value<id>(std::declval<Settings &>()).value)) {
    return details::get_value<id>(settings_).value;
  }

  template <details::ProgressBarOption id>
  auto get_value() const
      -> decltype((details::get_value<id>(std::declval<const Settings &>()).value)) {
    return details::get_value<id>(settings_).value;
  }

public:
  void print_progress() {
    std::lock_guard<std::mutex> lock{mutex_};
    auto &hide_bar_when_complete = get_value<details::ProgressBarOption::hide_bar_when_complete>();
    if (hide_bar_when_complete) {
      // Hide completed bars
      if (started_) {
        for (size_t i = 0; i < incomplete_count_; ++i)
          std::cout << "\033[A\r\033[K" << std::flush;
      }
      incomplete_count_ = 0;
      for (auto &bar : bars_) {
        if (!bar.get().is_completed()) {
          bar.get().print_progress(true);
          std::cout << "\n";
          ++incomplete_count_;
        }
      }
      if (!started_)
        started_ = true;
    } else {
      // Don't hide any bars
      if (started_) {
        for (size_t i = 0; i < total_count_; ++i)
          std::cout << "\x1b[A";
      }
      for (auto &bar : bars_) {
        bar.get().print_progress(true);
        std::cout << "\n";
      }
      if (!started_)
        started_ = true;
    }
    total_count_ = bars_.size();
    std::cout << termcolor::reset;
  }
};

} // namespace indicators

#pragma once

// #include <indicators/details/stream_helper.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
// #include <indicators/color.hpp>
// #include <indicators/setting.hpp>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace indicators {

class ProgressSpinner {
  using Settings =
      std::tuple<option::ForegroundColor, option::PrefixText, option::PostfixText,
                 option::ShowPercentage, option::ShowElapsedTime, option::ShowRemainingTime,
                 option::ShowSpinner, option::SavedStartTime, option::Completed,
                 option::MaxPostfixTextLen, option::SpinnerStates, option::FontStyles,
                 option::MaxProgress, option::Stream>;

public:
  template <typename... Args,
            typename std::enable_if<details::are_settings_from_tuple<
                                        Settings, typename std::decay<Args>::type...>::value,
                                    void *>::type = nullptr>
  explicit ProgressSpinner(Args &&... args)
      : settings_(
            details::get<details::ProgressBarOption::foreground_color>(
                option::ForegroundColor{Color::unspecified}, std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::prefix_text>(option::PrefixText{},
                                                                  std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::postfix_text>(option::PostfixText{},
                                                                   std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::show_percentage>(option::ShowPercentage{true},
                                                                      std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::show_elapsed_time>(
                option::ShowElapsedTime{false}, std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::show_remaining_time>(
                option::ShowRemainingTime{false}, std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::spinner_show>(option::ShowSpinner{true},
                                                                   std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::saved_start_time>(
                option::SavedStartTime{false}, std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::completed>(option::Completed{false},
                                                                std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::max_postfix_text_len>(
                option::MaxPostfixTextLen{0}, std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::spinner_states>(
                option::SpinnerStates{
                    std::vector<std::string>{"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"}},
                std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::font_styles>(
                option::FontStyles{std::vector<FontStyle>{}}, std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::max_progress>(option::MaxProgress{100},
                                                                   std::forward<Args>(args)...),
            details::get<details::ProgressBarOption::stream>(option::Stream{std::cout},
                                                             std::forward<Args>(args)...)) {}

  template <typename T, details::ProgressBarOption id>
  void set_option(details::Setting<T, id> &&setting) {
    static_assert(!std::is_same<T, typename std::decay<decltype(details::get_value<id>(
                                       std::declval<Settings>()))>::type>::value,
                  "Setting has wrong type!");
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<id>() = std::move(setting).value;
  }

  template <typename T, details::ProgressBarOption id>
  void set_option(const details::Setting<T, id> &setting) {
    static_assert(!std::is_same<T, typename std::decay<decltype(details::get_value<id>(
                                       std::declval<Settings>()))>::type>::value,
                  "Setting has wrong type!");
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<id>() = setting.value;
  }

  void set_option(
      const details::Setting<std::string, details::ProgressBarOption::postfix_text> &setting) {
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<details::ProgressBarOption::postfix_text>() = setting.value;
    if (setting.value.length() > get_value<details::ProgressBarOption::max_postfix_text_len>()) {
      get_value<details::ProgressBarOption::max_postfix_text_len>() = setting.value.length();
    }
  }

  void
  set_option(details::Setting<std::string, details::ProgressBarOption::postfix_text> &&setting) {
    std::lock_guard<std::mutex> lock(mutex_);
    get_value<details::ProgressBarOption::postfix_text>() = std::move(setting).value;
    auto &new_value = get_value<details::ProgressBarOption::postfix_text>();
    if (new_value.length() > get_value<details::ProgressBarOption::max_postfix_text_len>()) {
      get_value<details::ProgressBarOption::max_postfix_text_len>() = new_value.length();
    }
  }

  void set_progress(size_t value) {
    {
      std::lock_guard<std::mutex> lock{mutex_};
      progress_ = value;
    }
    save_start_time();
    print_progress();
  }

  void tick() {
    {
      std::lock_guard<std::mutex> lock{mutex_};
      progress_ += 1;
    }
    save_start_time();
    print_progress();
  }

  size_t current() {
    std::lock_guard<std::mutex> lock{mutex_};
    return std::min(progress_, size_t(get_value<details::ProgressBarOption::max_progress>()));
  }

  bool is_completed() const { return get_value<details::ProgressBarOption::completed>(); }

  void mark_as_completed() {
    get_value<details::ProgressBarOption::completed>() = true;
    print_progress();
  }

private:
  Settings settings_;
  size_t progress_{0};
  size_t index_{0};
  std::chrono::time_point<std::chrono::high_resolution_clock> start_time_point_;
  std::mutex mutex_;

  template <details::ProgressBarOption id>
  auto get_value() -> decltype((details::get_value<id>(std::declval<Settings &>()).value)) {
    return details::get_value<id>(settings_).value;
  }

  template <details::ProgressBarOption id>
  auto get_value() const
      -> decltype((details::get_value<id>(std::declval<const Settings &>()).value)) {
    return details::get_value<id>(settings_).value;
  }

  void save_start_time() {
    auto &show_elapsed_time = get_value<details::ProgressBarOption::show_elapsed_time>();
    auto &show_remaining_time = get_value<details::ProgressBarOption::show_remaining_time>();
    auto &saved_start_time = get_value<details::ProgressBarOption::saved_start_time>();
    if ((show_elapsed_time || show_remaining_time) && !saved_start_time) {
      start_time_point_ = std::chrono::high_resolution_clock::now();
      saved_start_time = true;
    }
  }

public:
  void print_progress() {
    std::lock_guard<std::mutex> lock{mutex_};

    auto &os = get_value<details::ProgressBarOption::stream>();

    const auto max_progress = get_value<details::ProgressBarOption::max_progress>();
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start_time_point_);

    if (get_value<details::ProgressBarOption::foreground_color>() != Color::unspecified)
      details::set_stream_color(os, get_value<details::ProgressBarOption::foreground_color>());

    for (auto &style : get_value<details::ProgressBarOption::font_styles>())
      details::set_font_style(os, style);

    os << get_value<details::ProgressBarOption::prefix_text>();
    if (get_value<details::ProgressBarOption::spinner_show>())
      os << get_value<details::ProgressBarOption::spinner_states>()
              [index_ % get_value<details::ProgressBarOption::spinner_states>().size()];
    if (get_value<details::ProgressBarOption::show_percentage>()) {
      os << " " << std::size_t(progress_ / double(max_progress) * 100) << "%";
    }

    if (get_value<details::ProgressBarOption::show_elapsed_time>()) {
      os << " [";
      details::write_duration(os, elapsed);
    }

    if (get_value<details::ProgressBarOption::show_remaining_time>()) {
      if (get_value<details::ProgressBarOption::show_elapsed_time>())
        os << "<";
      else
        os << " [";
      auto eta = std::chrono::nanoseconds(
          progress_ > 0 ? static_cast<long long>(elapsed.count() * max_progress / progress_) : 0);
      auto remaining = eta > elapsed ? (eta - elapsed) : (elapsed - eta);
      details::write_duration(os, remaining);
      os << "]";
    } else {
      if (get_value<details::ProgressBarOption::show_elapsed_time>())
        os << "]";
    }

    if (get_value<details::ProgressBarOption::max_postfix_text_len>() == 0)
      get_value<details::ProgressBarOption::max_postfix_text_len>() = 10;
    os << " " << get_value<details::ProgressBarOption::postfix_text>()
       << std::string(get_value<details::ProgressBarOption::max_postfix_text_len>(), ' ') << "\r";
    os.flush();
    index_ += 1;
    if (progress_ > max_progress) {
      get_value<details::ProgressBarOption::completed>() = true;
    }
    if (get_value<details::ProgressBarOption::completed>())
      os << termcolor::reset << std::endl;
  }
};

} // namespace indicators


#pragma once
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace criterion {

struct benchmark_config {
  static inline std::tuple<> empty_tuple{};
  std::string name;
  using Fn = std::function<void(
      std::chrono::steady_clock::time_point &,                // start time stamp
      std::optional<std::chrono::steady_clock::time_point> &, // teardown time stamp
      void *parameters)>;                                     // benchmark parameters
  Fn fn;
  std::string parameterized_instance_name = "";
  void *parameters = (void *)(&empty_tuple);

  enum class benchmark_reporting_type { console };
  benchmark_reporting_type reporting_type = benchmark_reporting_type::console;
};

} // namespace criterion

#pragma once
#include <array>
// #include <criterion/details/benchmark_result.hpp>
#include <sstream>
#include <string>

namespace criterion {

struct benchmark_result {
  std::string name;
  std::size_t num_warmup_runs;
  std::size_t num_runs;
  std::size_t num_iterations;

  long double lowest_rsd;       // Lowest relative standard deviation (RSD)
  long double lowest_rsd_mean;  // mean @ lowest RSD
  std::size_t lowest_rsd_index; // which run had the lowest RSD best estimate

  long double warmup_execution_time;  // execution time during warmup
  long double mean_execution_time;    // global mean execution time
  long double fastest_execution_time; // global best execution time
  long double slowest_execution_time; // global worst execution time

  long double average_iteration_performance; // iterations per second in the average case
  long double fastest_iteration_performance; // iterations per second in the fastest case
  long double slowest_iteration_performance; // iterations per second in the slowest case

  std::string to_csv() const {
    std::stringstream os;

    os << '"' << name << "\",";
    os << std::fixed << std::setprecision(2) << num_warmup_runs << ',' << num_runs * num_iterations
       << ',' << mean_execution_time << ',' << fastest_execution_time << ','
       << slowest_execution_time << ',' << lowest_rsd_mean << ',' << lowest_rsd << ','
       << lowest_rsd_index << ',' << average_iteration_performance << ','
       << fastest_iteration_performance << ',' << slowest_iteration_performance;

    return os.str();
  }

  std::string to_json() const {
    std::stringstream os;
    os << std::fixed << std::setprecision(2) << "    {\n"
       << "      \"name\": \"" << name << "\",\n"
       << "      \"warmup_runs\": " << num_warmup_runs << ",\n"
       << "      \"iterations\": " << num_runs * num_iterations << ",\n"
       << "      \"mean_execution_time\": " << mean_execution_time << ",\n"
       << "      \"fastest_execution_time\": " << fastest_execution_time << ",\n"
       << "      \"slowest_execution_time\": " << slowest_execution_time << ",\n"
       << "      \"lowest_rsd_execution_time\": " << lowest_rsd_mean << ",\n"
       << "      \"lowest_rsd_percentage\": " << lowest_rsd << ",\n"
       << "      \"lowest_rsd_index\": " << lowest_rsd_index << ",\n"
       << "      \"average_iteration_performance\": " << average_iteration_performance << ",\n"
       << "      \"fastest_iteration_performance\": " << fastest_iteration_performance << ",\n"
       << "      \"slowest_iteration_performance\": " << slowest_iteration_performance << "\n"
       << "    }";

    return os.str();
  }

  std::string to_md() const {
    std::stringstream os;
    os << std::fixed << std::setprecision(2) << "|" << name << "|" << num_warmup_runs << "|"
       << num_runs * num_iterations << "|" << mean_execution_time << "|" << fastest_execution_time
       << "|" << slowest_execution_time << "|" << lowest_rsd_mean << "|" << lowest_rsd << "|"
       << lowest_rsd_index << "|" << average_iteration_performance << "|"
       << fastest_iteration_performance << "|" << slowest_iteration_performance << "\n";
    return os.str();
  }
};

} // namespace criterion
// #include <criterion/details/indicators.hpp>
#include <iomanip>
#include <sstream>
#include <string>

namespace criterion {

class console_writer {
  static std::string duration_to_string(const long double &ns) {
    const auto duration = std::abs(ns);
    std::stringstream os;
    if (duration < 1E3) {
      os << std::fixed << std::setprecision(2) << duration << " ns";
    } else if (duration < 1E6) {
      os << std::fixed << std::setprecision(2) << (duration / 1E3) << " us";
    } else if (duration < 1E9) {
      os << std::fixed << std::setprecision(2) << (duration / 1E6) << " ms";
    } else {
      os << std::fixed << std::setprecision(2) << (duration / 1E9) << " s";
    }

    std::string result{""};
    if (ns < 0) {
      result += "-";
    }
    result += os.str();
    return result;
  }

  static std::string ordinal(std::size_t n) {
    static const std::array<std::string, 10> ends{"th", "st", "nd", "rd", "th",
                                                  "th", "th", "th", "th", "th"};
    if (((n % 100) >= 11) && ((n % 100) <= 13)) {
      return std::to_string(n) + "th";
    } else {
      return std::to_string(n) + ends[n % 10];
    }
  }

public:
  static void write_result(const benchmark_result &result) {
    std::cout << termcolor::bold << termcolor::green << " ✓ " << result.name << termcolor::reset
              << "\n";

    std::cout << "    " << termcolor::bold << termcolor::underline << "Configuration"
              << termcolor::reset << "\n";

    std::cout << "      " << result.num_warmup_runs << " warmup runs, " << result.num_runs
              << (result.num_runs > 1 ? " benchmark runs, " : " benchmark run, ")
              << result.num_iterations << " iterations per run\n";

    std::cout << "    " << termcolor::bold << termcolor::underline << "Execution Time"
              << termcolor::reset << "\n";

    std::cout << termcolor::yellow << termcolor::bold << termcolor::italic << "      Average      "
              << std::right << std::setw(10) << duration_to_string(result.mean_execution_time)
              << termcolor::reset << "\n";

    const auto best_mean_difference = result.fastest_execution_time - result.mean_execution_time;
    const auto best_mean_percentage_difference =
        (best_mean_difference / static_cast<long double>(result.mean_execution_time) * 100.0);

    std::cout << "      Fastest      " << std::right << std::setw(10)
              << duration_to_string(result.fastest_execution_time);
    std::cout << " (" << termcolor::green << duration_to_string(best_mean_difference) << ", "
              << std::setprecision(2) << std::fixed << best_mean_percentage_difference << " %"
              << termcolor::reset << ")"
              << "\n";

    const auto worst_mean_difference = result.slowest_execution_time - result.mean_execution_time;
    const auto worst_mean_percentage_difference =
        (worst_mean_difference / static_cast<long double>(result.mean_execution_time) * 100.0);

    std::cout << "      Slowest      " << std::right << std::setw(10)
              << duration_to_string(result.slowest_execution_time);
    std::cout << " (" << termcolor::red << duration_to_string(worst_mean_difference) << ", "
              << std::setprecision(2) << std::fixed << worst_mean_percentage_difference << " %"
              << termcolor::reset << ")"
              << "\n";

    std::cout << termcolor::bold << termcolor::white << "      Lowest RSD   " << std::right
              << std::setw(10) << duration_to_string(result.lowest_rsd_mean) << " ± "
              << std::setprecision(2) << result.lowest_rsd << "%"
              << " (" << ordinal(result.lowest_rsd_index) << " run)" << termcolor::reset << "\n";

    std::cout << "    " << termcolor::bold << termcolor::underline << "Performance"
              << termcolor::reset << "\n";

    std::cout << "      Average    " << std::setprecision(2) << std::fixed << std::right
              << std::setw(10) << result.average_iteration_performance << " iterations/s"
              << termcolor::reset << "\n";

    const auto best_mean_iterations_difference =
        result.average_iteration_performance - result.fastest_iteration_performance;
    const auto best_mean_iterations_percentage_difference =
        (best_mean_iterations_difference /
         static_cast<long double>(result.average_iteration_performance) * 100.0);

    std::cout << "      Fastest    " << std::setprecision(2) << std::fixed << std::right
              << std::setw(10) << result.fastest_iteration_performance << " iterations/s"
              << termcolor::reset;
    std::cout << " (" << termcolor::green << std::setprecision(2) << std::fixed
              << best_mean_iterations_difference << " iterations/s, "
              << best_mean_iterations_percentage_difference << " %" << termcolor::reset << ")"
              << "\n";

    const auto worst_mean_iterations_difference =
        result.average_iteration_performance - result.slowest_iteration_performance;
    const auto worst_mean_iterations_percentage_difference =
        (worst_mean_iterations_difference /
         static_cast<long double>(result.average_iteration_performance) * 100.0);

    std::cout << "      Slowest    " << std::setprecision(2) << std::fixed << std::right
              << std::setw(10) << result.slowest_iteration_performance << " iterations/s"
              << termcolor::reset;
    std::cout << " (" << termcolor::red << std::setprecision(2) << std::fixed
              << worst_mean_iterations_difference << " iterations/s, "
              << worst_mean_iterations_percentage_difference << " %" << termcolor::reset << ")"
              << "\n";

    std::cout << "\n";
  }
};

} // namespace criterion

#pragma once
#include <sstream>
#include <string>

namespace criterion {

#if 0
struct benchmark_result {
  std::string name;
  std::size_t num_warmup_runs;
  std::size_t num_runs;
  std::size_t num_iterations;

  long double lowest_rsd;       // Lowest relative standard deviation (RSD)
  long double lowest_rsd_mean;  // mean @ lowest RSD
  std::size_t lowest_rsd_index; // which run had the lowest RSD best estimate

  long double warmup_execution_time;  // execution time during warmup
  long double mean_execution_time;    // global mean execution time
  long double fastest_execution_time; // global best execution time
  long double slowest_execution_time; // global worst execution time

  long double average_iteration_performance; // iterations per second in the average case
  long double fastest_iteration_performance; // iterations per second in the fastest case
  long double slowest_iteration_performance; // iterations per second in the slowest case

  std::string to_csv() const {
    std::stringstream os;

    os << '"' << name << "\",";
    os << std::fixed << std::setprecision(2) << num_warmup_runs << ',' << num_runs * num_iterations
       << ',' << mean_execution_time << ',' << fastest_execution_time << ','
       << slowest_execution_time << ',' << lowest_rsd_mean << ',' << lowest_rsd << ','
       << lowest_rsd_index << ',' << average_iteration_performance << ','
       << fastest_iteration_performance << ',' << slowest_iteration_performance;

    return os.str();
  }

  std::string to_json() const {
    std::stringstream os;
    os << std::fixed << std::setprecision(2) << "    {\n"
       << "      \"name\": \"" << name << "\",\n"
       << "      \"warmup_runs\": " << num_warmup_runs << ",\n"
       << "      \"iterations\": " << num_runs * num_iterations << ",\n"
       << "      \"mean_execution_time\": " << mean_execution_time << ",\n"
       << "      \"fastest_execution_time\": " << fastest_execution_time << ",\n"
       << "      \"slowest_execution_time\": " << slowest_execution_time << ",\n"
       << "      \"lowest_rsd_execution_time\": " << lowest_rsd_mean << ",\n"
       << "      \"lowest_rsd_percentage\": " << lowest_rsd << ",\n"
       << "      \"lowest_rsd_index\": " << lowest_rsd_index << ",\n"
       << "      \"average_iteration_performance\": " << average_iteration_performance << ",\n"
       << "      \"fastest_iteration_performance\": " << fastest_iteration_performance << ",\n"
       << "      \"slowest_iteration_performance\": " << slowest_iteration_performance << "\n"
       << "    }";

    return os.str();
  }

  std::string to_md() const {
    std::stringstream os;
    os << std::fixed << std::setprecision(2) << "|" << name << "|" << num_warmup_runs << "|"
       << num_runs * num_iterations << "|" << mean_execution_time << "|" << fastest_execution_time
       << "|" << slowest_execution_time << "|" << lowest_rsd_mean << "|" << lowest_rsd << "|"
       << lowest_rsd_index << "|" << average_iteration_performance << "|"
       << fastest_iteration_performance << "|" << slowest_iteration_performance << "\n";
    return os.str();
  }
};
#endif

} // namespace criterion

#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// #include <criterion/details/benchmark_config.hpp>
// #include <criterion/details/benchmark_result.hpp>
// #include <criterion/details/console_writer.hpp>
// #include <criterion/details/indicators.hpp>

namespace criterion {

class benchmark {
  benchmark_config config_;
  using Fn = benchmark_config::Fn;

  static inline constexpr std::size_t num_iterations_{20};
  std::size_t max_num_runs_{0};
  const long double ten_seconds_{1e+10};
  long double min_benchmark_time_{ten_seconds_};
  long double benchmark_time_;
  long double early_estimate_execution_time_;

  long double estimate_minimum_measurement_cost() {
    using namespace std::chrono;
    std::vector<long double> durations;

    for (std::size_t i = 0; i < 10; i++) {
      const auto start = steady_clock::now();
      // do nothing
      const auto end = steady_clock::now();
      const auto execution_time =
          static_cast<long double>(duration_cast<std::chrono::nanoseconds>(end - start).count());
      durations.push_back(execution_time);
    }
    return *std::min_element(durations.begin(), durations.end());
  }

  long double estimate_execution_time() {
    using namespace std::chrono;

    long double result = 0;
    bool first_run{true};
    for (std::size_t i = 0; i < warmup_runs; i++) {
      std::chrono::steady_clock::time_point start_timestamp;
      std::optional<std::chrono::steady_clock::time_point> teardown_timestamp;
      const auto start = steady_clock::now();
      config_.fn(start_timestamp, teardown_timestamp, config_.parameters);
      const auto end = steady_clock::now();
      const auto execution_time =
          static_cast<long double>(duration_cast<std::chrono::nanoseconds>(end - start).count());
      if (first_run) {
        result = execution_time;
        first_run = false;
      } else {
        result = std::min(execution_time, result);
      }
    }
    return result;
  }

  void update_iterations() {
    early_estimate_execution_time_ = estimate_execution_time();

    if (early_estimate_execution_time_ < 1)
      early_estimate_execution_time_ = 1;

    auto min_runs = 2;

    if (early_estimate_execution_time_ <= 100) {              // 100ns
      benchmark_time_ = 5e+8;                                 // 500 ms
    } else if (early_estimate_execution_time_ <= 1000) {      // 1us
      benchmark_time_ = 1e+9;                                 // 1s
    } else if (early_estimate_execution_time_ <= 100000) {    // 100us
      benchmark_time_ = 2.5e+9;                               // 2.5s
    } else if (early_estimate_execution_time_ <= 1000000) {   // 1ms
      benchmark_time_ = 5e+9;                                 // 5s
    } else if (early_estimate_execution_time_ <= 100000000) { // 100ms
      benchmark_time_ = 7.5e+9;                               // 7.5s
    } else {
      benchmark_time_ = min_benchmark_time_;
    }

    benchmark_time_ =
        std::max(early_estimate_execution_time_ * min_runs * num_iterations_, benchmark_time_);
    const auto total_iterations = size_t(benchmark_time_) / early_estimate_execution_time_;
    max_num_runs_ = std::max(size_t(total_iterations / num_iterations_), size_t(min_runs));
  }

public:
  benchmark(const benchmark_config &config) : config_(config) {}

  static inline std::unordered_map<std::string, benchmark_result> results;
  static inline std::vector<std::string> benchmark_execution_order;
  static inline bool show_console_output = true;
  static inline std::size_t warmup_runs = 3;

  void run() {
    std::chrono::steady_clock::time_point benchmark_start_timestamp;

    using namespace std::chrono;

    // run empty function to estimate minimum delay in scheduling and executing user function
    const auto estimated_minimum_measurement_cost = estimate_minimum_measurement_cost();

    const std::string benchmark_instance_name = config_.name + config_.parameterized_instance_name;
    benchmark_execution_order.push_back(benchmark_instance_name);

    // Get an early estimate for execution time
    // Update number of iterations to run for this benchmark based on estimate
    update_iterations();

    const auto total_number_of_iterations = max_num_runs_ * num_iterations_;

    long double lowest_rsd = 100;
    long double lowest_rsd_mean = 0;
    std::size_t lowest_rsd_index = 0;
    bool first_run{true};

    long double fastest_execution_time = 0;
    long double slowest_execution_time = 0;
    std::vector<long double> mean_in_each_run{};
    mean_in_each_run.reserve(max_num_runs_);

    std::size_t num_runs = 0;
    std::array<long double, num_iterations_> durations;

    using namespace indicators;

    // Hide cursor
    if (show_console_output) {
      show_console_cursor(false);
    }

    BlockProgressBar bar{option::BarWidth{50},
                         option::Start{"["},
                         option::End{"]"},
                         option::ShowElapsedTime{true},
                         option::ShowRemainingTime{true},
                         option::ShowPercentage{true},
                         option::ForegroundColor{Color::white},
                         option::FontStyles{std::vector<FontStyle>{FontStyle::bold}},
                         option::MaxProgress{total_number_of_iterations},
                         option::PrefixText{" > " + benchmark_instance_name + " "}};

    while (true) {
      // Benchmark runs
      if (first_run) {
        benchmark_start_timestamp = std::chrono::steady_clock::now();
      }
      for (std::size_t i = 0; i < num_iterations_; i++) {
        std::optional<std::chrono::steady_clock::time_point> teardown_timestamp;
        auto start = steady_clock::now();
        config_.fn(start, teardown_timestamp, config_.parameters);
        auto end = steady_clock::now();
        if (teardown_timestamp)
          end = teardown_timestamp.value();
        const auto execution_time = duration_cast<std::chrono::nanoseconds>(end - start).count();
        durations[i] = std::abs(execution_time - estimated_minimum_measurement_cost);
        if (show_console_output) {
          bar.tick();
        }
      }
      auto size = num_iterations_;
      const long double mean = std::accumulate(durations.begin(), durations.end(), 0.0) / size;

      long double E = 0;
      for (std::size_t i = 0; i < size; i++) {
        E += std::pow(durations[i] - mean, 2);
      }
      const long double variance = E / size;
      const long double standard_deviation = std::sqrt(variance);
      const long double relative_standard_deviation = standard_deviation * 100 / mean;

      const auto mean_in_this_run =
          std::accumulate(durations.begin(), durations.end(), 0.0) / num_iterations_;
      mean_in_each_run.push_back(mean_in_this_run);

      if (first_run) {
        lowest_rsd = relative_standard_deviation;
        lowest_rsd_mean = mean;
        lowest_rsd_index = num_runs + 1;
        fastest_execution_time = *std::min_element(durations.begin(), durations.end());
        slowest_execution_time = *std::max_element(durations.begin(), durations.end());
        first_run = false;
      } else {
        // Save record of lowest RSD
        const auto current_lowest_rsd = lowest_rsd;
        const auto current_lowest_rsd_index = lowest_rsd_index;
        lowest_rsd = std::min(relative_standard_deviation, lowest_rsd);
        if (lowest_rsd < current_lowest_rsd) {
          // There's a new LOWEST relative standard deviation

          if (mean < lowest_rsd_mean) {
            lowest_rsd_mean = mean; // new mean is lower
            lowest_rsd_index = num_runs + 1;
          } else {
            lowest_rsd = current_lowest_rsd; // go back to old estimate
            lowest_rsd_index = current_lowest_rsd_index;
          }
        } else {
          lowest_rsd = current_lowest_rsd; // go back to old estimate
          lowest_rsd_index = current_lowest_rsd_index;
        }

        // Save best and worst duration
        const auto current_best_execution_time =
            *std::min_element(durations.begin(), durations.end());
        if (current_best_execution_time > 0)
          fastest_execution_time = std::min(fastest_execution_time, current_best_execution_time);

        const auto current_worst_execution_time =
            *std::max_element(durations.begin(), durations.end());
        slowest_execution_time = std::max(slowest_execution_time, current_worst_execution_time);
      }

      num_runs += 1;

      if (num_runs >= max_num_runs_) {
        break;
      }

      const auto now = std::chrono::steady_clock::now();
      const auto elapsed_time =
          std::chrono::duration_cast<std::chrono::nanoseconds>(now - benchmark_start_timestamp)
              .count();
      if (elapsed_time > benchmark_time_) {
        break;
      } else {
        if (show_console_output) {
          const auto percentage_completed = elapsed_time / benchmark_time_;
          const auto new_bar_progress = percentage_completed * total_number_of_iterations;
          bar.set_progress(new_bar_progress);
        }
      }
    }

    const auto mean_execution_time =
        (std::accumulate(mean_in_each_run.begin(), mean_in_each_run.end(), 0.0) / num_runs);

    const auto benchmark_result = criterion::benchmark_result{
        .name = benchmark_instance_name,
        .num_warmup_runs = warmup_runs,
        .num_runs = max_num_runs_,
        .num_iterations = num_iterations_,
        .lowest_rsd = lowest_rsd,
        .lowest_rsd_mean = lowest_rsd_mean,
        .lowest_rsd_index = lowest_rsd_index,
        .warmup_execution_time = early_estimate_execution_time_,
        .mean_execution_time = mean_execution_time,
        .fastest_execution_time = fastest_execution_time,
        .slowest_execution_time = slowest_execution_time,
        .average_iteration_performance = (1E9 / mean_execution_time),
        .fastest_iteration_performance = (1E9 / fastest_execution_time),
        .slowest_iteration_performance = (1E9 / slowest_execution_time)};

    results.insert(std::make_pair(benchmark_instance_name, benchmark_result));

    if (show_console_output) {
      bar.set_progress(total_number_of_iterations);
      bar.mark_as_completed();

      // Show console cursor
      show_console_cursor(true);

      console_writer::write_result(benchmark_result);
    }
  }
};

} // namespace criterion

#pragma once
// #include <criterion/details/benchmark.hpp>
// #include <criterion/details/benchmark_result.hpp>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>

namespace criterion {

class csv_writer {
public:
  static bool write_results(const std::string &filename,
                            const std::unordered_map<std::string, benchmark_result> &results) {
    bool result{false};
    std::ofstream os(filename);
    if (os.is_open()) {
      os << "name,warmup_runs,iterations,mean_execution_time,fastest_execution_time,slowest_"
            "execution_time,lowest_rsd_execution_time,lowest_rsd_percentage,lowest_rsd_index,"
            "average_iteration_performance,fastest_iteration_performance,slowest_iteration_"
            "performance\n";
      for (const auto &name : benchmark::benchmark_execution_order) {
        const auto &this_result = results.at(name);
        os << this_result.to_csv() << "\n";
      }
      result = true;
    }
    os.close();
    return result;
  }
};

} // namespace criterion

#pragma once
// #include <criterion/details/benchmark.hpp>
// #include <criterion/details/benchmark_result.hpp>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>

namespace criterion {

class json_writer {
public:
  static bool write_results(const std::string &filename,
                            const std::unordered_map<std::string, benchmark_result> &results) {
    bool result{false};
    std::ofstream os(filename);
    if (os.is_open()) {
      os << "{\n";
      os << "  \"benchmarks\": [\n";

      bool first{true};
      for (const auto &name : benchmark::benchmark_execution_order) {
        const auto &this_result = results.at(name);
        if (first) {
          first = false;
        } else {
          os << ",\n";
        }
        os << this_result.to_json();
      }

      os << "\n";
      os << "  ]\n";
      os << "}";
      result = true;
    }
    os.close();
    return result;
  }
};

} // namespace criterion

#pragma once
// #include <criterion/details/benchmark.hpp>
// #include <criterion/details/benchmark_result.hpp>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>

namespace criterion {

class md_writer {
public:
  static bool write_results(const std::string &filename,
                            const std::unordered_map<std::string, benchmark_result> &results) {
    bool result{false};
    std::ofstream os(filename);
    if (os.is_open()) {

      os << "|Name            |Warmup Runs|Iterations|Mean Execution Time (ns)|Fastest Execution "
            "Time (ns)|Slowest Execution Time (ns)|Lowest RSD Execution Time (ns)|Lowest RSD "
            "(%)|Lowest RSD Index|Average Iteration Performance (iterations/s)|Fastest Iteration "
            "Performance (iterations/s)|Slowest Iteration Performance (iterations/s)|\n";
      os << "|:---------------|----------:|---------:|-----------------------:|--------------------"
            "------:|--------------------------:|-----------------------------:|-------------:|----"
            "-----------:|-------------------------------------------:|----------------------------"
            "---------------:|-------------------------------------------:|\n";

      for (const auto &name : benchmark::benchmark_execution_order) {
        const auto &this_result = results.at(name);
        os << this_result.to_md();
      }
      result = true;
    }
    os.close();
    return result;
  }
};

} // namespace criterion

#pragma once
// #include <criterion/details/benchmark.hpp>
// #include <criterion/details/benchmark_result.hpp>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>

namespace criterion {

class asciidoc_writer {
public:
  static bool write_results(const std::string &filename,
                            const std::unordered_map<std::string, benchmark_result> &results) {
    bool result{false};
    std::ofstream os(filename);
    if (os.is_open()) {

      std::time_t t = std::time(nullptr);
      os << ".Criterion Benchmark Results (" << std::put_time(std::gmtime(&t), "%c %Z") << ")\n";
      os << "[cols=\"<,>,>,>,>,>,>,>,>,>,>,>\", options=\"header\"]\n";
      os << "|===\n";
      os << "|Name            |Warmup Runs|Iterations|Mean Execution Time (ns)|Fastest Execution "
            "Time (ns)|Slowest Execution Time (ns)|Lowest RSD Execution Time (ns)|Lowest RSD "
            "(%)|Lowest RSD Index|Average Iteration Performance (iterations/s)|Fastest Iteration "
            "Performance (iterations/s)|Slowest Iteration Performance (iterations/s)\n";

      for (const auto &name : benchmark::benchmark_execution_order) {
        const auto &this_result = results.at(name);
        os << this_result.to_md();
      }
      os << "|===\n";
      result = true;
    }
    os.close();
    return result;
  }
};

} // namespace criterion

#pragma once
#include <chrono>
// #include <criterion/details/benchmark.hpp>
// #include <criterion/details/benchmark_config.hpp>
// #include <criterion/details/csv_writer.hpp>
#include <functional>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace criterion {

struct benchmark_registration_helper_struct {
  static std::vector<benchmark_config> &registered_benchmarks() {
    static std::vector<benchmark_config> v;
    return v;
  }

  static void register_benchmark(const benchmark_config &config) {
    registered_benchmarks().push_back(config);
  }

  static void execute_registered_benchmarks() {
    for (const auto &config : registered_benchmarks()) {
      benchmark{config}.run();
    }
  }

  static void list_registered_benchmarks() {
    for (const auto &config : registered_benchmarks()) {
      std::cout << config.name << config.parameterized_instance_name << "\n";
    }
  }

  static void list_filtered_registered_benchmarks(const std::string &regex_string) {
    std::regex regexp(regex_string);
    std::smatch matches;
    for (const auto &config : registered_benchmarks()) {
      const auto benchmark_instance_name = config.name + config.parameterized_instance_name;
      std::regex_search(benchmark_instance_name, matches, regexp);
      if (!matches.empty()) {
        std::cout << benchmark_instance_name << "\n";
      }
    }
  }

  static void execute_filtered_registered_benchmarks(const std::string &regex_string) {
    std::regex regexp(regex_string);
    std::smatch matches;
    for (const auto &config : registered_benchmarks()) {
      const auto benchmark_instance_name = config.name + config.parameterized_instance_name;
      std::regex_search(benchmark_instance_name, matches, regexp);
      if (!matches.empty()) {
        benchmark{config}.run();
      }
    }
  }
};

} // namespace criterion

#define SETUP_BENCHMARK(...)                                                                       \
  __VA_ARGS__                                                                                      \
  __benchmark_start_timestamp =                                                                    \
      std::chrono::steady_clock::now(); // updated benchmark start timestamp

#define TEARDOWN_BENCHMARK(...)                                                                    \
  __benchmark_teardown_timestamp = std::chrono::steady_clock::now();                               \
  __VA_ARGS__

namespace criterion {

struct benchmark_template_registration_helper_struct {
  static std::unordered_multimap<std::string, benchmark_config> &registered_benchmark_templates() {
    static std::unordered_multimap<std::string, benchmark_config> m;
    return m;
  }

  static void register_benchmark_template(const benchmark_config &config) {
    registered_benchmark_templates().insert({config.name, config});
  }

  template <class ArgTuple>
  static void execute_registered_benchmark_template(const std::string &template_name,
                                                    const std::string &instance_name,
                                                    ArgTuple &arg_tuple) {
    for (auto &[k, v] : registered_benchmark_templates()) {
      if (k == template_name) {
        benchmark_registration_helper_struct::register_benchmark(
            benchmark_config{.name = template_name,
                             .fn = v.fn,
                             .parameterized_instance_name = instance_name,
                             .parameters = (void *)(&arg_tuple)});
      }
    }
  }
};

} // namespace criterion

#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)

#define BENCHMARK_WITHOUT_PARAMETERS(Name)                                                         \
  typedef std::tuple<> CONCAT(Name, BenchmarkParameters);                                          \
  namespace detail {                                                                               \
  /* forward declare the benchmark function that we define later */                                \
  template <class T = CONCAT(Name, BenchmarkParameters)>                                           \
  struct CONCAT(Name, CONCAT(__benchmark_function_wrapper__, __LINE__)) {                          \
    static inline void CONCAT(Name, CONCAT(_registered_fun_, __LINE__))(                           \
        std::chrono::steady_clock::time_point &,                                                   \
        std::optional<std::chrono::steady_clock::time_point> &, void *);                           \
  };                                                                                               \
                                                                                                   \
  namespace /* ensure internal linkage for struct */                                               \
  {                                                                                                \
  /* helper struct for static registration in ctor */                                              \
  struct CONCAT(Name, CONCAT(_register_struct_, __LINE__)) {                                       \
    CONCAT(Name, CONCAT(_register_struct_, __LINE__))() { /* called once before main */            \
      criterion::benchmark_template_registration_helper_struct::register_benchmark_template(       \
          criterion::benchmark_config{                                                             \
              .name = #Name,                                                                       \
              .fn = CONCAT(Name, CONCAT(__benchmark_function_wrapper__, __LINE__)) <               \
                    CONCAT(Name, BenchmarkParameters) >                                            \
                    ::CONCAT(Name, CONCAT(_registered_fun_, __LINE__))});                          \
    }                                                                                              \
  } CONCAT(Name, CONCAT(_register_struct_instance_, __LINE__));                                    \
  }                                                                                                \
                                                                                                   \
  namespace /* ensure internal linkage for struct */                                               \
  {                                                                                                \
  static CONCAT(Name, BenchmarkParameters)                                                         \
      CONCAT(CONCAT(Name, _benchmark_template_parameters), __LINE__) = {};                         \
  /* helper struct for static registration in ctor */                                              \
  struct CONCAT(Name, CONCAT(_instantiation_struct_, __LINE__)) {                                  \
    CONCAT(Name, CONCAT(_instantiation_struct_, __LINE__))() { /* called once before main */       \
      criterion::benchmark_template_registration_helper_struct::                                   \
          execute_registered_benchmark_template<CONCAT(Name, BenchmarkParameters)>(                \
              #Name, "", CONCAT(CONCAT(Name, _benchmark_template_parameters), __LINE__));          \
    }                                                                                              \
  } CONCAT(Name, CONCAT(_instantiation_struct_instance_, __LINE__));                               \
  }                                                                                                \
  }                                                                                                \
                                                                                                   \
  /* now actually defined to allow BENCHMARK("name") { ... } syntax */                             \
  template <class T>                                                                               \
  void detail::CONCAT(Name, CONCAT(__benchmark_function_wrapper__, __LINE__))<T>::CONCAT(          \
      Name, CONCAT(_registered_fun_, __LINE__))(                                                   \
      [[maybe_unused]] std::chrono::steady_clock::time_point & __benchmark_start_timestamp,        \
      [[maybe_unused]] std::optional<std::chrono::steady_clock::time_point> &                      \
          __benchmark_teardown_timestamp,                                                          \
      [[maybe_unused]] void *__benchmark_parameters)

#define BENCHMARK_WITH_PARAMETERS(Name, ...)                                                       \
  typedef std::tuple<__VA_ARGS__> CONCAT(Name, BenchmarkParameters);                               \
  namespace detail {                                                                               \
  /* forward declare the benchmark function that we define later */                                \
  template <class T = CONCAT(Name, BenchmarkParameters)>                                           \
  struct CONCAT(Name, CONCAT(__benchmark_function_wrapper__, __LINE__)) {                          \
    static inline void CONCAT(Name, CONCAT(_registered_fun_, __LINE__))(                           \
        std::chrono::steady_clock::time_point &,                                                   \
        std::optional<std::chrono::steady_clock::time_point> &, void *);                           \
  };                                                                                               \
                                                                                                   \
  namespace /* ensure internal linkage for struct */                                               \
  {                                                                                                \
  /* helper struct for static registration in ctor */                                              \
  struct CONCAT(Name, CONCAT(_register_struct_, __LINE__)) {                                       \
    CONCAT(Name, CONCAT(_register_struct_, __LINE__))() { /* called once before main */            \
      criterion::benchmark_template_registration_helper_struct::register_benchmark_template(       \
          criterion::benchmark_config{                                                             \
              .name = #Name,                                                                       \
              .fn = CONCAT(Name, CONCAT(__benchmark_function_wrapper__, __LINE__)) <               \
                    CONCAT(Name, BenchmarkParameters) >                                            \
                    ::CONCAT(Name, CONCAT(_registered_fun_, __LINE__))});                          \
    }                                                                                              \
  } CONCAT(Name, CONCAT(_register_struct_instance_, __LINE__));                                    \
  }                                                                                                \
  }                                                                                                \
                                                                                                   \
  /* now actually defined to allow BENCHMARK("name") { ... } syntax */                             \
  template <class T>                                                                               \
  void detail::CONCAT(Name, CONCAT(__benchmark_function_wrapper__, __LINE__))<T>::CONCAT(          \
      Name, CONCAT(_registered_fun_, __LINE__))(                                                   \
      [[maybe_unused]] std::chrono::steady_clock::time_point & __benchmark_start_timestamp,        \
      [[maybe_unused]] std::optional<std::chrono::steady_clock::time_point> &                      \
          __benchmark_teardown_timestamp,                                                          \
      [[maybe_unused]] void *__benchmark_parameters)

#define GET_ARGUMENT_TUPLE *((T *)__benchmark_parameters)

#define GET_ARGUMENT(index) std::get<index>(*((T *)__benchmark_parameters));

#define BENCHMARK_1(Name) BENCHMARK_WITHOUT_PARAMETERS(Name)

#define BENCHMARK_2(Name, A) BENCHMARK_WITH_PARAMETERS(Name, A)
#define BENCHMARK_3(Name, A, B) BENCHMARK_WITH_PARAMETERS(Name, A, B)
#define BENCHMARK_4(Name, A, B, C) BENCHMARK_WITH_PARAMETERS(Name, A, B, C)
#define BENCHMARK_5(Name, A, B, C, D) BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D)
#define BENCHMARK_6(Name, A, B, C, D, E) BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F)
#define BENCHMARK_7(Name, A, B, C, D, E, F) BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F)
#define BENCHMARK_8(Name, A, B, C, D, E, F, G) BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G)
#define BENCHMARK_9(Name, A, B, C, D, E, F, G, H)                                                  \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H)
#define BENCHMARK_10(Name, A, B, C, D, E, F, G, H, I)                                              \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I)
#define BENCHMARK_11(Name, A, B, C, D, E, F, G, H, I, J)                                           \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I, J)
#define BENCHMARK_12(Name, A, B, C, D, E, F, G, H, I, J, K)                                        \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I, J, K)
#define BENCHMARK_13(Name, A, B, C, D, E, F, G, H, I, J, K, L)                                     \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I, J, K, L)
#define BENCHMARK_14(Name, A, B, C, D, E, F, G, H, I, J, K, L, M)                                  \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I, J, K, L, M)
#define BENCHMARK_15(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N)                               \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N)
#define BENCHMARK_16(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O)                            \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O)
#define BENCHMARK_17(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P)                         \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P)
#define BENCHMARK_18(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q)                      \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q)
#define BENCHMARK_19(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R)                   \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R)
#define BENCHMARK_20(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S)                \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S)
#define BENCHMARK_21(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T)             \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T)
#define BENCHMARK_22(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U)          \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U)
#define BENCHMARK_23(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V)       \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V)
#define BENCHMARK_24(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W)    \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U,   \
                            V, W)
#define BENCHMARK_25(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X) \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U,   \
                            V, W, X)
#define BENCHMARK_26(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, \
                     Y)                                                                            \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U,   \
                            V, W, X, Y)
#define BENCHMARK_27(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, \
                     Y, Z)                                                                         \
  BENCHMARK_WITH_PARAMETERS(Name, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U,   \
                            V, W, X, Y, Z)

// The interim macro that simply strips the excess and ends up with the required macro
#define BENCHMARK_X(Name, x, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W,  \
                    X, Y, Z, FUNC, ...)                                                            \
  FUNC

// BENCHMARK macro supports no more than 26 parameters
// The macro that the programmer uses
#define BENCHMARK(...)                                                                             \
  BENCHMARK_X(, ##__VA_ARGS__, BENCHMARK_27(__VA_ARGS__), BENCHMARK_26(__VA_ARGS__),               \
              BENCHMARK_25(__VA_ARGS__), BENCHMARK_24(__VA_ARGS__), BENCHMARK_23(__VA_ARGS__),     \
              BENCHMARK_22(__VA_ARGS__), BENCHMARK_21(__VA_ARGS__), BENCHMARK_20(__VA_ARGS__),     \
              BENCHMARK_19(__VA_ARGS__), BENCHMARK_18(__VA_ARGS__), BENCHMARK_17(__VA_ARGS__),     \
              BENCHMARK_16(__VA_ARGS__), BENCHMARK_15(__VA_ARGS__), BENCHMARK_14(__VA_ARGS__),     \
              BENCHMARK_13(__VA_ARGS__), BENCHMARK_12(__VA_ARGS__), BENCHMARK_11(__VA_ARGS__),     \
              BENCHMARK_10(__VA_ARGS__), BENCHMARK_9(__VA_ARGS__), BENCHMARK_8(__VA_ARGS__),       \
              BENCHMARK_7(__VA_ARGS__), BENCHMARK_6(__VA_ARGS__), BENCHMARK_5(__VA_ARGS__),        \
              BENCHMARK_4(__VA_ARGS__), BENCHMARK_3(__VA_ARGS__), BENCHMARK_2(__VA_ARGS__),        \
              BENCHMARK_1(__VA_ARGS__))

#define INVOKE_BENCHMARK(TemplateName, InstanceName, ...)                                          \
                                                                                                   \
  namespace /* ensure internal linkage for struct */                                               \
  {                                                                                                \
  static CONCAT(TemplateName, BenchmarkParameters)                                                 \
      CONCAT(CONCAT(TemplateName, _benchmark_template_parameters), __LINE__) = {__VA_ARGS__};      \
  /* helper struct for static registration in ctor */                                              \
  struct CONCAT(TemplateName, CONCAT(_instantiation_struct_, __LINE__)) {                          \
    CONCAT(TemplateName, CONCAT(_instantiation_struct_, __LINE__))                                 \
    () { /* called once before main */                                                             \
      criterion::benchmark_template_registration_helper_struct::                                   \
          execute_registered_benchmark_template<CONCAT(TemplateName, BenchmarkParameters)>(        \
              #TemplateName, InstanceName,                                                         \
              CONCAT(CONCAT(TemplateName, _benchmark_template_parameters), __LINE__));             \
    }                                                                                              \
  } CONCAT(TemplateName, CONCAT(_instantiation_struct_instance_, __LINE__));                       \
  }

#define GET_FIRST(first, ...) first
#define GET_REST(first, ...) __VA_ARGS__

#define INVOKE_BENCHMARK_N(TemplateName, Index, PackedArgument)                                    \
                                                                                                   \
  namespace /* ensure internal linkage for struct */                                               \
  {                                                                                                \
  static CONCAT(TemplateName, BenchmarkParameters)                                                 \
      CONCAT(CONCAT(CONCAT(TemplateName, _benchmark_template_parameters), __LINE__),               \
             Index) = {GET_REST(PackedArgument)};                                                  \
  /* helper struct for static registration in ctor */                                              \
  struct CONCAT(TemplateName, CONCAT(CONCAT(_instantiation_struct_, __LINE__), Index)) {           \
    CONCAT(TemplateName, CONCAT(CONCAT(_instantiation_struct_, __LINE__),                          \
                                Index)()) { /* called once before main */                          \
      criterion::benchmark_template_registration_helper_struct::                                   \
          execute_registered_benchmark_template<CONCAT(TemplateName, BenchmarkParameters)>(        \
              #TemplateName, GET_FIRST(PackedArgument),                                            \
              CONCAT(CONCAT(CONCAT(TemplateName, _benchmark_template_parameters), __LINE__),       \
                     Index));                                                                      \
    }                                                                                              \
  } CONCAT(TemplateName, CONCAT(CONCAT(_instantiation_struct_instance_, __LINE__), Index));        \
  }

#define _Args(...) __VA_ARGS__
#define STRIP_PARENS(X) X
#define PASS_PARAMETERS(X) STRIP_PARENS(_Args X)

#define STRINGIZE(arg) STRINGIZE1(arg)
#define STRINGIZE1(arg) STRINGIZE2(arg)
#define STRINGIZE2(arg) #arg

#define CONCATENATE(arg1, arg2) CONCATENATE1(arg1, arg2)
#define CONCATENATE1(arg1, arg2) CONCATENATE2(arg1, arg2)
#define CONCATENATE2(arg1, arg2) arg1##arg2

#define FOR_EACH_1(what, first, x, ...) what(1, first, x);

#define FOR_EACH_2(what, first, x, ...)                                                            \
  what(2, first, x);                                                                               \
  FOR_EACH_1(what, first, __VA_ARGS__);

#define FOR_EACH_3(what, first, x, ...)                                                            \
  what(3, first, x);                                                                               \
  FOR_EACH_2(what, first, __VA_ARGS__);

#define FOR_EACH_4(what, first, x, ...)                                                            \
  what(4, first, x);                                                                               \
  FOR_EACH_3(what, first, __VA_ARGS__);

#define FOR_EACH_5(what, first, x, ...)                                                            \
  what(5, first, x);                                                                               \
  FOR_EACH_4(what, first, __VA_ARGS__);

#define FOR_EACH_6(what, first, x, ...)                                                            \
  what(6, first, x);                                                                               \
  FOR_EACH_5(what, first, __VA_ARGS__);

#define FOR_EACH_7(what, first, x, ...)                                                            \
  what(7, first, x);                                                                               \
  FOR_EACH_6(what, first, __VA_ARGS__);

#define FOR_EACH_8(what, first, x, ...)                                                            \
  what(8, first, x);                                                                               \
  FOR_EACH_7(what, first, __VA_ARGS__);

#define FOR_EACH_9(what, first, x, ...)                                                            \
  what(9, first, x);                                                                               \
  FOR_EACH_8(what, first, __VA_ARGS__);

#define FOR_EACH_10(what, first, x, ...)                                                           \
  what(10, first, x);                                                                              \
  FOR_EACH_9(what, first, __VA_ARGS__);

#define FOR_EACH_11(what, first, x, ...)                                                           \
  what(11, first, x);                                                                              \
  FOR_EACH_10(what, first, __VA_ARGS__);

#define FOR_EACH_12(what, first, x, ...)                                                           \
  what(12, first, x);                                                                              \
  FOR_EACH_11(what, first, __VA_ARGS__);

#define FOR_EACH_13(what, first, x, ...)                                                           \
  what(13, first, x);                                                                              \
  FOR_EACH_12(what, first, __VA_ARGS__);

#define FOR_EACH_14(what, first, x, ...)                                                           \
  what(14, first, x);                                                                              \
  FOR_EACH_13(what, first, __VA_ARGS__);

#define FOR_EACH_15(what, first, x, ...)                                                           \
  what(15, first, x);                                                                              \
  FOR_EACH_14(what, first, __VA_ARGS__);

#define FOR_EACH_16(what, first, x, ...)                                                           \
  what(16, first, x);                                                                              \
  FOR_EACH_15(what, first, __VA_ARGS__);

#define FOR_EACH_17(what, first, x, ...)                                                           \
  what(17, first, x);                                                                              \
  FOR_EACH_16(what, first, __VA_ARGS__);

#define FOR_EACH_18(what, first, x, ...)                                                           \
  what(18, first, x);                                                                              \
  FOR_EACH_17(what, first, __VA_ARGS__);

#define FOR_EACH_19(what, first, x, ...)                                                           \
  what(19, first, x);                                                                              \
  FOR_EACH_18(what, first, __VA_ARGS__);

#define FOR_EACH_20(what, first, x, ...)                                                           \
  what(20, first, x);                                                                              \
  FOR_EACH_19(what, first, __VA_ARGS__);

#define FOR_EACH_21(what, first, x, ...)                                                           \
  what(21, first, x);                                                                              \
  FOR_EACH_20(what, first, __VA_ARGS__);

#define FOR_EACH_22(what, first, x, ...)                                                           \
  what(22, first, x);                                                                              \
  FOR_EACH_21(what, first, __VA_ARGS__);

#define FOR_EACH_23(what, first, x, ...)                                                           \
  what(23, first, x);                                                                              \
  FOR_EACH_22(what, first, __VA_ARGS__);

#define FOR_EACH_24(what, first, x, ...)                                                           \
  what(24, first, x);                                                                              \
  FOR_EACH_23(what, first, __VA_ARGS__);

#define FOR_EACH_25(what, first, x, ...)                                                           \
  what(25, first, x);                                                                              \
  FOR_EACH_24(what, first, __VA_ARGS__);

#define FOR_EACH_26(what, first, x, ...)                                                           \
  what(26, first, x);                                                                              \
  FOR_EACH_25(what, first, __VA_ARGS__);

#define FOR_EACH_27(what, first, x, ...)                                                           \
  what(27, first, x);                                                                              \
  FOR_EACH_26(what, first, __VA_ARGS__);

#define FOR_EACH_28(what, first, x, ...)                                                           \
  what(28, first, x);                                                                              \
  FOR_EACH_27(what, first, __VA_ARGS__);

#define FOR_EACH_29(what, first, x, ...)                                                           \
  what(29, first, x);                                                                              \
  FOR_EACH_28(what, first, __VA_ARGS__);

#define FOR_EACH_30(what, first, x, ...)                                                           \
  what(30, first, x);                                                                              \
  FOR_EACH_29(what, first, __VA_ARGS__);

#define FOR_EACH_31(what, first, x, ...)                                                           \
  what(31, first, x);                                                                              \
  FOR_EACH_30(what, first, __VA_ARGS__);

#define FOR_EACH_32(what, first, x, ...)                                                           \
  what(32, first, x);                                                                              \
  FOR_EACH_31(what, first, __VA_ARGS__);

#define FOR_EACH_33(what, first, x, ...)                                                           \
  what(33, first, x);                                                                              \
  FOR_EACH_32(what, first, __VA_ARGS__);

#define FOR_EACH_34(what, first, x, ...)                                                           \
  what(34, first, x);                                                                              \
  FOR_EACH_33(what, first, __VA_ARGS__);

#define FOR_EACH_35(what, first, x, ...)                                                           \
  what(35, first, x);                                                                              \
  FOR_EACH_34(what, first, __VA_ARGS__);

#define FOR_EACH_36(what, first, x, ...)                                                           \
  what(36, first, x);                                                                              \
  FOR_EACH_35(what, first, __VA_ARGS__);

#define FOR_EACH_37(what, first, x, ...)                                                           \
  what(37, first, x);                                                                              \
  FOR_EACH_36(what, first, __VA_ARGS__);

#define FOR_EACH_38(what, first, x, ...)                                                           \
  what(38, first, x);                                                                              \
  FOR_EACH_37(what, first, __VA_ARGS__);

#define FOR_EACH_39(what, first, x, ...)                                                           \
  what(39, first, x);                                                                              \
  FOR_EACH_38(what, first, __VA_ARGS__);

#define FOR_EACH_40(what, first, x, ...)                                                           \
  what(40, first, x);                                                                              \
  FOR_EACH_39(what, first, __VA_ARGS__);

#define FOR_EACH_41(what, first, x, ...)                                                           \
  what(41, first, x);                                                                              \
  FOR_EACH_40(what, first, __VA_ARGS__);

#define FOR_EACH_42(what, first, x, ...)                                                           \
  what(42, first, x);                                                                              \
  FOR_EACH_41(what, first, __VA_ARGS__);

#define FOR_EACH_43(what, first, x, ...)                                                           \
  what(43, first, x);                                                                              \
  FOR_EACH_42(what, first, __VA_ARGS__);

#define FOR_EACH_44(what, first, x, ...)                                                           \
  what(44, first, x);                                                                              \
  FOR_EACH_43(what, first, __VA_ARGS__);

#define FOR_EACH_45(what, first, x, ...)                                                           \
  what(45, first, x);                                                                              \
  FOR_EACH_44(what, first, __VA_ARGS__);

#define FOR_EACH_46(what, first, x, ...)                                                           \
  what(46, first, x);                                                                              \
  FOR_EACH_45(what, first, __VA_ARGS__);

#define FOR_EACH_47(what, first, x, ...)                                                           \
  what(47, first, x);                                                                              \
  FOR_EACH_46(what, first, __VA_ARGS__);

#define FOR_EACH_48(what, first, x, ...)                                                           \
  what(48, first, x);                                                                              \
  FOR_EACH_47(what, first, __VA_ARGS__);

#define FOR_EACH_49(what, first, x, ...)                                                           \
  what(49, first, x);                                                                              \
  FOR_EACH_48(what, first, __VA_ARGS__);

#define FOR_EACH_50(what, first, x, ...)                                                           \
  what(50, first, x);                                                                              \
  FOR_EACH_49(what, first, __VA_ARGS__);

#define FOR_EACH_51(what, first, x, ...)                                                           \
  what(51, first, x);                                                                              \
  FOR_EACH_50(what, first, __VA_ARGS__);

#define FOR_EACH_52(what, first, x, ...)                                                           \
  what(52, first, x);                                                                              \
  FOR_EACH_51(what, first, __VA_ARGS__);

#define FOR_EACH_53(what, first, x, ...)                                                           \
  what(53, first, x);                                                                              \
  FOR_EACH_52(what, first, __VA_ARGS__);

#define FOR_EACH_54(what, first, x, ...)                                                           \
  what(54, first, x);                                                                              \
  FOR_EACH_53(what, first, __VA_ARGS__);

#define FOR_EACH_55(what, first, x, ...)                                                           \
  what(55, first, x);                                                                              \
  FOR_EACH_54(what, first, __VA_ARGS__);

#define FOR_EACH_56(what, first, x, ...)                                                           \
  what(56, first, x);                                                                              \
  FOR_EACH_55(what, first, __VA_ARGS__);

#define FOR_EACH_57(what, first, x, ...)                                                           \
  what(57, first, x);                                                                              \
  FOR_EACH_56(what, first, __VA_ARGS__);

#define FOR_EACH_58(what, first, x, ...)                                                           \
  what(58, first, x);                                                                              \
  FOR_EACH_57(what, first, __VA_ARGS__);

#define FOR_EACH_59(what, first, x, ...)                                                           \
  what(59, first, x);                                                                              \
  FOR_EACH_58(what, first, __VA_ARGS__);

#define FOR_EACH_60(what, first, x, ...)                                                           \
  what(60, first, x);                                                                              \
  FOR_EACH_59(what, first, __VA_ARGS__);

#define FOR_EACH_61(what, first, x, ...)                                                           \
  what(61, first, x);                                                                              \
  FOR_EACH_60(what, first, __VA_ARGS__);

#define FOR_EACH_62(what, first, x, ...)                                                           \
  what(62, first, x);                                                                              \
  FOR_EACH_61(what, first, __VA_ARGS__);

#define FOR_EACH_63(what, first, x, ...)                                                           \
  what(63, first, x);                                                                              \
  FOR_EACH_62(what, first, __VA_ARGS__);

#define FOR_EACH_NARG(...) FOR_EACH_NARG_(__VA_ARGS__, FOR_EACH_RSEQ_N())
#define FOR_EACH_NARG_(...) FOR_EACH_ARG_N(__VA_ARGS__)
#define FOR_EACH_ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, \
                       _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32,  \
                       _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47,  \
                       _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, _61, _62,  \
                       _63, N, ...)                                                                \
  N
#define FOR_EACH_RSEQ_N()                                                                          \
  63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40,  \
      39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17,  \
      16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

#define FOR_EACH_(N, what, first, x, ...) CONCATENATE(FOR_EACH_, N)(what, first, x, __VA_ARGS__)
#define FOR_EACH(what, first, ...) FOR_EACH_(FOR_EACH_NARG(__VA_ARGS__), what, first, __VA_ARGS__)

#define INVOKE_BENCHMARK_FOR_EACH_HELPER(Index, TemplateName, ...)                                 \
  INVOKE_BENCHMARK_N(TemplateName, Index, PASS_PARAMETERS(__VA_ARGS__))

#define INVOKE_BENCHMARK_FOR_EACH(TemplateName, ...)                                               \
  FOR_EACH(INVOKE_BENCHMARK_FOR_EACH_HELPER, TemplateName, __VA_ARGS__)

  


//  (C) Copyright 2015 - 2018 Christopher Beck

//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef VISIT_STRUCT_HPP_INCLUDED
#define VISIT_STRUCT_HPP_INCLUDED

/***
 * Provides a facility to declare a structure as "visitable" and apply a visitor
 * to it. The list of members is a compile-time data structure, and there is no
 * run-time overhead.
 */

#include <type_traits>
#include <utility>

// Library version

#define VISIT_STRUCT_VERSION_MAJOR 1
#define VISIT_STRUCT_VERSION_MINOR 0
#define VISIT_STRUCT_VERSION_PATCH 0

#define VISIT_STRUCT_STRING_HELPER(X) #X
#define VISIT_STRUCT_STRING(X) VISIT_STRUCT_STRING_HELPER(X)

#define VISIT_STRUCT_VERSION_STRING                                                                \
  VISIT_STRUCT_STRING(VISIT_STRUCT_VERSION_MAJOR)                                                  \
  "." VISIT_STRUCT_STRING(VISIT_STRUCT_VERSION_MINOR) "." VISIT_STRUCT_STRING(                     \
      VISIT_STRUCT_VERSION_PATCH)

// For MSVC 2013 support, we put constexpr behind a define.

#ifndef VISIT_STRUCT_CONSTEXPR
#if (defined _MSC_VER) && (_MSC_VER <= 1800)
#define VISIT_STRUCT_CONSTEXPR
#else
#define VISIT_STRUCT_CONSTEXPR constexpr
#endif
#endif

// After C++14 the apply_visitor function can be constexpr.
// We target C++11, but such functions are tagged VISIT_STRUCT_CXX14_CONSTEXPR.

#ifndef VISIT_STRUCT_CXX14_CONSTEXPR
#if ((defined _MSC_VER) && (_MSC_VER <= 1900)) || (!defined __cplusplus) || (__cplusplus == 201103L)
#define VISIT_STRUCT_CXX14_CONSTEXPR
#else
#define VISIT_STRUCT_CXX14_CONSTEXPR constexpr
#endif
#endif

namespace visit_struct {

namespace traits {

// Primary template which is specialized to register a type
template <typename T, typename ENABLE = void> struct visitable;

// Helper template which checks if a type is registered
template <typename T, typename ENABLE = void> struct is_visitable : std::false_type {};

template <typename T>
struct is_visitable<T, typename std::enable_if<traits::visitable<T>::value>::type>
    : std::true_type {};

// Helper template which removes cv and reference from a type (saves some typing)
template <typename T> struct clean {
  typedef typename std::remove_cv<typename std::remove_reference<T>::type>::type type;
};

template <typename T> using clean_t = typename clean<T>::type;

// Mini-version of std::common_type (we only require C++11)
template <typename T, typename U> struct common_type {
  typedef decltype(true ? std::declval<T>() : std::declval<U>()) type;
};

} // end namespace traits

// Tag for tag dispatch
template <typename T> struct type_c { using type = T; };

// Accessor type: function object encapsulating a pointer-to-member
template <typename MemPtr, MemPtr ptr> struct accessor {
  template <typename T>
  VISIT_STRUCT_CONSTEXPR auto operator()(T &&t) const -> decltype(std::forward<T>(t).*ptr) {
    return std::forward<T>(t).*ptr;
  }
};

//
// User-interface
//

// Return number of fields in a visitable struct
template <typename S> VISIT_STRUCT_CONSTEXPR std::size_t field_count() {
  return traits::visitable<traits::clean_t<S>>::field_count;
}

template <typename S> VISIT_STRUCT_CONSTEXPR std::size_t field_count(S &&) {
  return field_count<S>();
}

// apply_visitor (one struct instance)
template <typename S, typename V>
VISIT_STRUCT_CXX14_CONSTEXPR auto apply_visitor(V &&v, S &&s) ->
    typename std::enable_if<traits::is_visitable<traits::clean_t<S>>::value>::type {
  traits::visitable<traits::clean_t<S>>::apply(std::forward<V>(v), std::forward<S>(s));
}

// apply_visitor (two struct instances)
template <typename S1, typename S2, typename V>
VISIT_STRUCT_CXX14_CONSTEXPR auto apply_visitor(V &&v, S1 &&s1, S2 &&s2) ->
    typename std::enable_if<traits::is_visitable<
        traits::clean_t<typename traits::common_type<S1, S2>::type>>::value>::type {
  using common_S = typename traits::common_type<S1, S2>::type;
  traits::visitable<traits::clean_t<common_S>>::apply(std::forward<V>(v), std::forward<S1>(s1),
                                                      std::forward<S2>(s2));
}

// for_each (Alternate syntax for apply_visitor, reverses order of arguments)
template <typename V, typename S>
VISIT_STRUCT_CXX14_CONSTEXPR auto for_each(S &&s, V &&v) ->
    typename std::enable_if<traits::is_visitable<traits::clean_t<S>>::value>::type {
  traits::visitable<traits::clean_t<S>>::apply(std::forward<V>(v), std::forward<S>(s));
}

// for_each with two structure instances
template <typename S1, typename S2, typename V>
VISIT_STRUCT_CXX14_CONSTEXPR auto for_each(S1 &&s1, S2 &&s2, V &&v) ->
    typename std::enable_if<traits::is_visitable<
        traits::clean_t<typename traits::common_type<S1, S2>::type>>::value>::type {
  using common_S = typename traits::common_type<S1, S2>::type;
  traits::visitable<traits::clean_t<common_S>>::apply(std::forward<V>(v), std::forward<S1>(s1),
                                                      std::forward<S2>(s2));
}

// Visit the types (visit_struct::type_c<...>) of the registered members
template <typename S, typename V>
VISIT_STRUCT_CXX14_CONSTEXPR auto visit_types(V &&v) ->
    typename std::enable_if<traits::is_visitable<traits::clean_t<S>>::value>::type {
  traits::visitable<traits::clean_t<S>>::visit_types(std::forward<V>(v));
}

// Visit the member pointers (&S::a) of the registered members
template <typename S, typename V>
VISIT_STRUCT_CXX14_CONSTEXPR auto visit_pointers(V &&v) ->
    typename std::enable_if<traits::is_visitable<traits::clean_t<S>>::value>::type {
  traits::visitable<traits::clean_t<S>>::visit_pointers(std::forward<V>(v));
}

// Visit the accessors (function objects) of the registered members
template <typename S, typename V>
VISIT_STRUCT_CXX14_CONSTEXPR auto visit_accessors(V &&v) ->
    typename std::enable_if<traits::is_visitable<traits::clean_t<S>>::value>::type {
  traits::visitable<traits::clean_t<S>>::visit_accessors(std::forward<V>(v));
}

// Apply visitor (with no instances)
// This calls visit_pointers, for backwards compat reasons
template <typename S, typename V>
VISIT_STRUCT_CXX14_CONSTEXPR auto apply_visitor(V &&v) ->
    typename std::enable_if<traits::is_visitable<traits::clean_t<S>>::value>::type {
  visit_struct::visit_pointers<S>(std::forward<V>(v));
}

// Get value by index (like std::get for tuples)
template <int idx, typename S>
VISIT_STRUCT_CONSTEXPR auto get(S &&s) ->
    typename std::enable_if<traits::is_visitable<traits::clean_t<S>>::value,
                            decltype(traits::visitable<traits::clean_t<S>>::get_value(
                                std::integral_constant<int, idx>{}, std::forward<S>(s)))>::type {
  return traits::visitable<traits::clean_t<S>>::get_value(std::integral_constant<int, idx>{},
                                                          std::forward<S>(s));
}

// Get name of field, by index
template <int idx, typename S>
VISIT_STRUCT_CONSTEXPR auto get_name() ->
    typename std::enable_if<traits::is_visitable<traits::clean_t<S>>::value,
                            decltype(traits::visitable<traits::clean_t<S>>::get_name(
                                std::integral_constant<int, idx>{}))>::type {
  return traits::visitable<traits::clean_t<S>>::get_name(std::integral_constant<int, idx>{});
}

template <int idx, typename S>
VISIT_STRUCT_CONSTEXPR auto get_name(S &&) -> decltype(get_name<idx, S>()) {
  return get_name<idx, S>();
}

// Get member pointer, by index
template <int idx, typename S>
VISIT_STRUCT_CONSTEXPR auto get_pointer() ->
    typename std::enable_if<traits::is_visitable<traits::clean_t<S>>::value,
                            decltype(traits::visitable<traits::clean_t<S>>::get_pointer(
                                std::integral_constant<int, idx>{}))>::type {
  return traits::visitable<traits::clean_t<S>>::get_pointer(std::integral_constant<int, idx>{});
}

template <int idx, typename S>
VISIT_STRUCT_CONSTEXPR auto get_pointer(S &&) -> decltype(get_pointer<idx, S>()) {
  return get_pointer<idx, S>();
}

// Get member accessor, by index
template <int idx, typename S>
VISIT_STRUCT_CONSTEXPR auto get_accessor() ->
    typename std::enable_if<traits::is_visitable<traits::clean_t<S>>::value,
                            decltype(traits::visitable<traits::clean_t<S>>::get_accessor(
                                std::integral_constant<int, idx>{}))>::type {
  return traits::visitable<traits::clean_t<S>>::get_accessor(std::integral_constant<int, idx>{});
}

template <int idx, typename S>
VISIT_STRUCT_CONSTEXPR auto get_accessor(S &&) -> decltype(get_accessor<idx, S>()) {
  return get_accessor<idx, S>();
}

// Get type, by index
template <int idx, typename S> struct type_at_s {
  using type_c =
      decltype(traits::visitable<traits::clean_t<S>>::type_at(std::integral_constant<int, idx>{}));
  using type = typename type_c::type;
};

template <int idx, typename S> using type_at = typename type_at_s<idx, S>::type;

// Get name of structure
template <typename S>
VISIT_STRUCT_CONSTEXPR auto get_name() ->
    typename std::enable_if<traits::is_visitable<traits::clean_t<S>>::value,
                            decltype(traits::visitable<traits::clean_t<S>>::get_name())>::type {
  return traits::visitable<traits::clean_t<S>>::get_name();
}

template <typename S> VISIT_STRUCT_CONSTEXPR auto get_name(S &&) -> decltype(get_name<S>()) {
  return get_name<S>();
}

/***
 * To implement the VISITABLE_STRUCT macro, we need a map-macro, which can take
 * the name of a macro and some other arguments, and apply that macro to each other
 * argument.
 *
 * There are some techniques you can use within C preprocessor to accomplish this
 * succinctly, by settng up "recursive" macros.
 *
 * But this can also cause it to give worse error messages when something goes wrong.
 *
 * We are now doing it in a more "dumb", bulletproof way which has the advantage that it
 * is more portable and gives better error messages. For discussion see
 * IMPLEMENTATION_NOTES.md
 *
 * The code below is based on a patch from Jarod42, and is now generated by a python
 * script. The purpose of the generated code is to define VISIT_STRUCT_PP_MAP as
 * described.
 */

/*** Generated code ***/

static VISIT_STRUCT_CONSTEXPR const int max_visitable_members = 69;

#define VISIT_STRUCT_EXPAND(x) x
#define VISIT_STRUCT_PP_ARG_N(                                                                     \
    _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20,     \
    _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, \
    _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, \
    _59, _60, _61, _62, _63, _64, _65, _66, _67, _68, _69, N, ...)                                 \
  N
#define VISIT_STRUCT_PP_NARG(...)                                                                  \
  VISIT_STRUCT_EXPAND(VISIT_STRUCT_PP_ARG_N(                                                       \
      __VA_ARGS__, 69, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, \
      49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27,  \
      26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2,  \
      1, 0))

/* need extra level to force extra eval */
#define VISIT_STRUCT_CONCAT_(a, b) a##b
#define VISIT_STRUCT_CONCAT(a, b) VISIT_STRUCT_CONCAT_(a, b)

#define VISIT_STRUCT_APPLYF0(f)
#define VISIT_STRUCT_APPLYF1(f, _1) f(_1)
#define VISIT_STRUCT_APPLYF2(f, _1, _2) f(_1) f(_2)
#define VISIT_STRUCT_APPLYF3(f, _1, _2, _3) f(_1) f(_2) f(_3)
#define VISIT_STRUCT_APPLYF4(f, _1, _2, _3, _4) f(_1) f(_2) f(_3) f(_4)
#define VISIT_STRUCT_APPLYF5(f, _1, _2, _3, _4, _5) f(_1) f(_2) f(_3) f(_4) f(_5)
#define VISIT_STRUCT_APPLYF6(f, _1, _2, _3, _4, _5, _6) f(_1) f(_2) f(_3) f(_4) f(_5) f(_6)
#define VISIT_STRUCT_APPLYF7(f, _1, _2, _3, _4, _5, _6, _7)                                        \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7)
#define VISIT_STRUCT_APPLYF8(f, _1, _2, _3, _4, _5, _6, _7, _8)                                    \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8)
#define VISIT_STRUCT_APPLYF9(f, _1, _2, _3, _4, _5, _6, _7, _8, _9)                                \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9)
#define VISIT_STRUCT_APPLYF10(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10)                          \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10)
#define VISIT_STRUCT_APPLYF11(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11)                     \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11)
#define VISIT_STRUCT_APPLYF12(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12)                \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12)
#define VISIT_STRUCT_APPLYF13(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13)           \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13)
#define VISIT_STRUCT_APPLYF14(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14)      \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14)
#define VISIT_STRUCT_APPLYF15(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15) \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)
#define VISIT_STRUCT_APPLYF16(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16)                                                                 \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16)
#define VISIT_STRUCT_APPLYF17(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17)                                                            \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17)
#define VISIT_STRUCT_APPLYF18(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18)                                                       \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18)
#define VISIT_STRUCT_APPLYF19(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19)                                                  \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19)
#define VISIT_STRUCT_APPLYF20(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20)                                             \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20)
#define VISIT_STRUCT_APPLYF21(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21)                                        \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21)
#define VISIT_STRUCT_APPLYF22(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22)                                   \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22)
#define VISIT_STRUCT_APPLYF23(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23)                              \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23)
#define VISIT_STRUCT_APPLYF24(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24)                         \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24)
#define VISIT_STRUCT_APPLYF25(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25)                    \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25)
#define VISIT_STRUCT_APPLYF26(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26)               \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26)
#define VISIT_STRUCT_APPLYF27(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27)          \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27)
#define VISIT_STRUCT_APPLYF28(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28)     \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)
#define VISIT_STRUCT_APPLYF29(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29)                                                                 \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29)
#define VISIT_STRUCT_APPLYF30(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30)                                                            \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30)
#define VISIT_STRUCT_APPLYF31(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31)                                                       \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31)
#define VISIT_STRUCT_APPLYF32(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32)                                                  \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32)
#define VISIT_STRUCT_APPLYF33(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33)                                             \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33)
#define VISIT_STRUCT_APPLYF34(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34)                                        \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34)
#define VISIT_STRUCT_APPLYF35(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35)                                   \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35)
#define VISIT_STRUCT_APPLYF36(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36)                              \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36)
#define VISIT_STRUCT_APPLYF37(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37)                         \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37)
#define VISIT_STRUCT_APPLYF38(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38)                    \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38)
#define VISIT_STRUCT_APPLYF39(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39)               \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39)
#define VISIT_STRUCT_APPLYF40(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40)          \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)
#define VISIT_STRUCT_APPLYF41(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41)     \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41)
#define VISIT_STRUCT_APPLYF42(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42)                                                                 \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42)
#define VISIT_STRUCT_APPLYF43(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43)                                                            \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43)
#define VISIT_STRUCT_APPLYF44(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44)                                                       \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44)
#define VISIT_STRUCT_APPLYF45(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45)                                                  \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45)
#define VISIT_STRUCT_APPLYF46(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46)                                             \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46)
#define VISIT_STRUCT_APPLYF47(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46, _47)                                        \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47)
#define VISIT_STRUCT_APPLYF48(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46, _47, _48)                                   \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48)
#define VISIT_STRUCT_APPLYF49(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46, _47, _48, _49)                              \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49)
#define VISIT_STRUCT_APPLYF50(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46, _47, _48, _49, _50)                         \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50)
#define VISIT_STRUCT_APPLYF51(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46, _47, _48, _49, _50, _51)                    \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51)
#define VISIT_STRUCT_APPLYF52(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52)               \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)
#define VISIT_STRUCT_APPLYF53(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53)          \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)  \
                  f(_53)
#define VISIT_STRUCT_APPLYF54(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54)     \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)  \
                  f(_53) f(_54)
#define VISIT_STRUCT_APPLYF55(                                                                     \
    f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20,  \
    _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, \
    _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55)                \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)  \
                  f(_53) f(_54) f(_55)
#define VISIT_STRUCT_APPLYF56(                                                                     \
    f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20,  \
    _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, \
    _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56)           \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)  \
                  f(_53) f(_54) f(_55) f(_56)
#define VISIT_STRUCT_APPLYF57(                                                                     \
    f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20,  \
    _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, \
    _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57)      \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)  \
                  f(_53) f(_54) f(_55) f(_56) f(_57)
#define VISIT_STRUCT_APPLYF58(                                                                     \
    f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20,  \
    _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, \
    _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58) \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)  \
                  f(_53) f(_54) f(_55) f(_56) f(_57) f(_58)
#define VISIT_STRUCT_APPLYF59(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54,     \
                              _55, _56, _57, _58, _59)                                             \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)  \
                  f(_53) f(_54) f(_55) f(_56) f(_57) f(_58) f(_59)
#define VISIT_STRUCT_APPLYF60(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54,     \
                              _55, _56, _57, _58, _59, _60)                                        \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)  \
                  f(_53) f(_54) f(_55) f(_56) f(_57) f(_58) f(_59) f(_60)
#define VISIT_STRUCT_APPLYF61(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54,     \
                              _55, _56, _57, _58, _59, _60, _61)                                   \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)  \
                  f(_53) f(_54) f(_55) f(_56) f(_57) f(_58) f(_59) f(_60) f(_61)
#define VISIT_STRUCT_APPLYF62(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54,     \
                              _55, _56, _57, _58, _59, _60, _61, _62)                              \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)  \
                  f(_53) f(_54) f(_55) f(_56) f(_57) f(_58) f(_59) f(_60) f(_61) f(_62)
#define VISIT_STRUCT_APPLYF63(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54,     \
                              _55, _56, _57, _58, _59, _60, _61, _62, _63)                         \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)  \
                  f(_53) f(_54) f(_55) f(_56) f(_57) f(_58) f(_59) f(_60) f(_61) f(_62) f(_63)
#define VISIT_STRUCT_APPLYF64(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54,     \
                              _55, _56, _57, _58, _59, _60, _61, _62, _63, _64)                    \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)  \
                  f(_53) f(_54) f(_55) f(_56) f(_57) f(_58) f(_59) f(_60) f(_61) f(_62) f(_63)     \
                      f(_64)
#define VISIT_STRUCT_APPLYF65(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54,     \
                              _55, _56, _57, _58, _59, _60, _61, _62, _63, _64, _65)               \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)  \
                  f(_53) f(_54) f(_55) f(_56) f(_57) f(_58) f(_59) f(_60) f(_61) f(_62) f(_63)     \
                      f(_64) f(_65)
#define VISIT_STRUCT_APPLYF66(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54,     \
                              _55, _56, _57, _58, _59, _60, _61, _62, _63, _64, _65, _66)          \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)  \
                  f(_53) f(_54) f(_55) f(_56) f(_57) f(_58) f(_59) f(_60) f(_61) f(_62) f(_63)     \
                      f(_64) f(_65) f(_66)
#define VISIT_STRUCT_APPLYF67(f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                              _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28,     \
                              _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41,     \
                              _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54,     \
                              _55, _56, _57, _58, _59, _60, _61, _62, _63, _64, _65, _66, _67)     \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)  \
                  f(_53) f(_54) f(_55) f(_56) f(_57) f(_58) f(_59) f(_60) f(_61) f(_62) f(_63)     \
                      f(_64) f(_65) f(_66) f(_67)
#define VISIT_STRUCT_APPLYF68(                                                                     \
    f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20,  \
    _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, \
    _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, \
    _59, _60, _61, _62, _63, _64, _65, _66, _67, _68)                                              \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)  \
                  f(_53) f(_54) f(_55) f(_56) f(_57) f(_58) f(_59) f(_60) f(_61) f(_62) f(_63)     \
                      f(_64) f(_65) f(_66) f(_67) f(_68)
#define VISIT_STRUCT_APPLYF69(                                                                     \
    f, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20,  \
    _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, \
    _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, \
    _59, _60, _61, _62, _63, _64, _65, _66, _67, _68, _69)                                         \
  f(_1) f(_2) f(_3) f(_4) f(_5) f(_6) f(_7) f(_8) f(_9) f(_10) f(_11) f(_12) f(_13) f(_14) f(_15)  \
      f(_16) f(_17) f(_18) f(_19) f(_20) f(_21) f(_22) f(_23) f(_24) f(_25) f(_26) f(_27) f(_28)   \
          f(_29) f(_30) f(_31) f(_32) f(_33) f(_34) f(_35) f(_36) f(_37) f(_38) f(_39) f(_40)      \
              f(_41) f(_42) f(_43) f(_44) f(_45) f(_46) f(_47) f(_48) f(_49) f(_50) f(_51) f(_52)  \
                  f(_53) f(_54) f(_55) f(_56) f(_57) f(_58) f(_59) f(_60) f(_61) f(_62) f(_63)     \
                      f(_64) f(_65) f(_66) f(_67) f(_68) f(_69)

#define VISIT_STRUCT_APPLY_F_(M, ...) VISIT_STRUCT_EXPAND(M(__VA_ARGS__))
#define VISIT_STRUCT_PP_MAP(f, ...)                                                                \
  VISIT_STRUCT_EXPAND(VISIT_STRUCT_APPLY_F_(                                                       \
      VISIT_STRUCT_CONCAT(VISIT_STRUCT_APPLYF, VISIT_STRUCT_PP_NARG(__VA_ARGS__)), f,              \
      __VA_ARGS__))

/*** End generated code ***/

/***
 * These macros are used with VISIT_STRUCT_PP_MAP
 */

#define VISIT_STRUCT_FIELD_COUNT(MEMBER_NAME) +1

#define VISIT_STRUCT_MEMBER_HELPER(MEMBER_NAME)                                                    \
  std::forward<V>(visitor)(#MEMBER_NAME, std::forward<S>(struct_instance).MEMBER_NAME);

#define VISIT_STRUCT_MEMBER_HELPER_PTR(MEMBER_NAME)                                                \
  std::forward<V>(visitor)(#MEMBER_NAME, &this_type::MEMBER_NAME);

#define VISIT_STRUCT_MEMBER_HELPER_TYPE(MEMBER_NAME)                                               \
  std::forward<V>(visitor)(#MEMBER_NAME, visit_struct::type_c<decltype(this_type::MEMBER_NAME)>{});

#define VISIT_STRUCT_MEMBER_HELPER_ACC(MEMBER_NAME)                                                \
  std::forward<V>(visitor)(                                                                        \
      #MEMBER_NAME,                                                                                \
      visit_struct::accessor<decltype(&this_type::MEMBER_NAME), &this_type::MEMBER_NAME>{});

#define VISIT_STRUCT_MEMBER_HELPER_PAIR(MEMBER_NAME)                                               \
  std::forward<V>(visitor)(#MEMBER_NAME, std::forward<S1>(s1).MEMBER_NAME,                         \
                           std::forward<S2>(s2).MEMBER_NAME);

#define VISIT_STRUCT_MAKE_GETTERS(MEMBER_NAME)                                                     \
  template <typename S>                                                                            \
  static VISIT_STRUCT_CONSTEXPR auto get_value(                                                    \
      std::integral_constant<int, fields_enum::MEMBER_NAME>, S &&s)                                \
      ->decltype((std::forward<S>(s).MEMBER_NAME)) {                                               \
    return std::forward<S>(s).MEMBER_NAME;                                                         \
  }                                                                                                \
                                                                                                   \
  static VISIT_STRUCT_CONSTEXPR auto get_name(                                                     \
      std::integral_constant<int, fields_enum::MEMBER_NAME>)                                       \
      ->decltype(#MEMBER_NAME) {                                                                   \
    return #MEMBER_NAME;                                                                           \
  }                                                                                                \
                                                                                                   \
  static VISIT_STRUCT_CONSTEXPR auto get_pointer(                                                  \
      std::integral_constant<int, fields_enum::MEMBER_NAME>)                                       \
      ->decltype(&this_type::MEMBER_NAME) {                                                        \
    return &this_type::MEMBER_NAME;                                                                \
  }                                                                                                \
                                                                                                   \
  static VISIT_STRUCT_CONSTEXPR auto get_accessor(                                                 \
      std::integral_constant<int, fields_enum::MEMBER_NAME>)                                       \
      ->visit_struct::accessor<decltype(&this_type::MEMBER_NAME), &this_type::MEMBER_NAME> {       \
    return {};                                                                                     \
  }                                                                                                \
                                                                                                   \
  static auto type_at(std::integral_constant<int, fields_enum::MEMBER_NAME>)                       \
      ->visit_struct::type_c<decltype(this_type::MEMBER_NAME)>;

// This macro specializes the trait, provides "apply" method which does the work.
// Below, template parameter S should always be the same as STRUCT_NAME modulo const and
// reference. The interface defined above ensures that STRUCT_NAME is clean_t<S>
// basically.
//
// Note: The code to make the indexed getters work is more convoluted than I'd like.
//       PP_MAP doesn't give you the index of each member. And rather than hack it so that
//       it will do that, what we do instead is: 1: Declare an enum `field_enum` in the
//       scope of visitable, which maps names to indices.
//          This gives an easy way for the macro to get the index from the name token.
//       2: Intuitively we'd like to use template partial specialization to make indices
//       map to
//          values, and have a new specialization for each member. But, specializations
//          can only be made at namespace scope. So to keep things tidy and contained
//          within this trait, we use tag dispatch with std::integral_constant<int>
//          instead.

#define VISITABLE_STRUCT(STRUCT_NAME, ...)                                                         \
  namespace visit_struct {                                                                         \
  namespace traits {                                                                               \
                                                                                                   \
  template <> struct visitable<STRUCT_NAME, void> {                                                \
                                                                                                   \
    using this_type = STRUCT_NAME;                                                                 \
                                                                                                   \
    static VISIT_STRUCT_CONSTEXPR auto get_name() -> decltype(#STRUCT_NAME) {                      \
      return #STRUCT_NAME;                                                                         \
    }                                                                                              \
                                                                                                   \
    static VISIT_STRUCT_CONSTEXPR const std::size_t field_count =                                  \
        0 VISIT_STRUCT_PP_MAP(VISIT_STRUCT_FIELD_COUNT, __VA_ARGS__);                              \
                                                                                                   \
    template <typename V, typename S>                                                              \
    VISIT_STRUCT_CXX14_CONSTEXPR static void apply(V &&visitor, S &&struct_instance) {             \
      VISIT_STRUCT_PP_MAP(VISIT_STRUCT_MEMBER_HELPER, __VA_ARGS__)                                 \
    }                                                                                              \
                                                                                                   \
    template <typename V, typename S1, typename S2>                                                \
    VISIT_STRUCT_CXX14_CONSTEXPR static void apply(V &&visitor, S1 &&s1, S2 &&s2) {                \
      VISIT_STRUCT_PP_MAP(VISIT_STRUCT_MEMBER_HELPER_PAIR, __VA_ARGS__)                            \
    }                                                                                              \
                                                                                                   \
    template <typename V> VISIT_STRUCT_CXX14_CONSTEXPR static void visit_pointers(V &&visitor) {   \
      VISIT_STRUCT_PP_MAP(VISIT_STRUCT_MEMBER_HELPER_PTR, __VA_ARGS__)                             \
    }                                                                                              \
                                                                                                   \
    template <typename V> VISIT_STRUCT_CXX14_CONSTEXPR static void visit_types(V &&visitor) {      \
      VISIT_STRUCT_PP_MAP(VISIT_STRUCT_MEMBER_HELPER_TYPE, __VA_ARGS__)                            \
    }                                                                                              \
                                                                                                   \
    template <typename V> VISIT_STRUCT_CXX14_CONSTEXPR static void visit_accessors(V &&visitor) {  \
      VISIT_STRUCT_PP_MAP(VISIT_STRUCT_MEMBER_HELPER_ACC, __VA_ARGS__)                             \
    }                                                                                              \
                                                                                                   \
    struct fields_enum {                                                                           \
      enum index { __VA_ARGS__ };                                                                  \
    };                                                                                             \
                                                                                                   \
    VISIT_STRUCT_PP_MAP(VISIT_STRUCT_MAKE_GETTERS, __VA_ARGS__)                                    \
                                                                                                   \
    static VISIT_STRUCT_CONSTEXPR const bool value = true;                                         \
  };                                                                                               \
  }                                                                                                \
  }                                                                                                \
  static_assert(true, "")

} // end namespace visit_struct

#endif // VISIT_STRUCT_HPP_INCLUDED

//  __  __             _        ______                          _____
// |  \/  |           (_)      |  ____|                        / ____|_     _
// | \  / | __ _  __ _ _  ___  | |__   _ __  _   _ _ __ ___   | |   _| |_ _| |_
// | |\/| |/ _` |/ _` | |/ __| |  __| | '_ \| | | | '_ ` _ \  | |  |_   _|_   _|
// | |  | | (_| | (_| | | (__  | |____| | | | |_| | | | | | | | |____|_|   |_|
// |_|  |_|\__,_|\__, |_|\___| |______|_| |_|\__,_|_| |_| |_|  \_____|
//                __/ | https://github.com/Neargye/magic_enum
//               |___/  version 0.6.6
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2019 - 2020 Daniil Goncharov <neargye@gmail.com>.
//
// Permission is hereby  granted, free of charge, to any  person obtaining a copy
// of this software and associated  documentation files (the "Software"), to deal
// in the Software  without restriction, including without  limitation the rights
// to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
// copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
// IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
// FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
// AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
// LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef NEARGYE_MAGIC_ENUM_HPP
#define NEARGYE_MAGIC_ENUM_HPP

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored                                                                   \
    "-Wsign-conversion" // Implicit conversion changes signedness: 'int' to 'size_t'.
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored                                                                     \
    "-Wsign-conversion" // Implicit conversion changes signedness: 'int' to 'size_t'.
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 26495) // Variable 'static_string<N>::chars' is uninitialized.
#endif

// Checks magic_enum compiler compatibility.
#if defined(__clang__) && __clang_major__ >= 5 || defined(__GNUC__) && __GNUC__ >= 9 ||            \
    defined(_MSC_VER) && _MSC_VER >= 1910
#undef MAGIC_ENUM_SUPPORTED
#define MAGIC_ENUM_SUPPORTED 1
#endif

// Enum value must be greater or equals than MAGIC_ENUM_RANGE_MIN. By default
// MAGIC_ENUM_RANGE_MIN = -128. If need another min range for all enum types by default,
// redefine the macro MAGIC_ENUM_RANGE_MIN.
#if !defined(MAGIC_ENUM_RANGE_MIN)
#define MAGIC_ENUM_RANGE_MIN -128
#endif

// Enum value must be less or equals than MAGIC_ENUM_RANGE_MAX. By default
// MAGIC_ENUM_RANGE_MAX = 128. If need another max range for all enum types by default,
// redefine the macro MAGIC_ENUM_RANGE_MAX.
#if !defined(MAGIC_ENUM_RANGE_MAX)
#define MAGIC_ENUM_RANGE_MAX 128
#endif

namespace magic_enum {

// Enum value must be in range [MAGIC_ENUM_RANGE_MIN, MAGIC_ENUM_RANGE_MAX]. By default
// MAGIC_ENUM_RANGE_MIN = -128, MAGIC_ENUM_RANGE_MAX = 128. If need another range for all
// enum types by default, redefine the macro MAGIC_ENUM_RANGE_MIN and
// MAGIC_ENUM_RANGE_MAX. If need another range for specific enum type, add specialization
// enum_range for necessary enum type.
template <typename E> struct enum_range {
  static_assert(std::is_enum_v<E>, "magic_enum::enum_range requires enum type.");
  inline static constexpr int min = MAGIC_ENUM_RANGE_MIN;
  inline static constexpr int max = MAGIC_ENUM_RANGE_MAX;
  static_assert(max > min, "magic_enum::enum_range requires max > min.");
};

static_assert(MAGIC_ENUM_RANGE_MIN <= 0, "MAGIC_ENUM_RANGE_MIN must be less or equals than 0.");
static_assert(MAGIC_ENUM_RANGE_MIN > (std::numeric_limits<std::int16_t>::min)(),
              "MAGIC_ENUM_RANGE_MIN must be greater than INT16_MIN.");

static_assert(MAGIC_ENUM_RANGE_MAX > 0, "MAGIC_ENUM_RANGE_MAX must be greater than 0.");
static_assert(MAGIC_ENUM_RANGE_MAX < (std::numeric_limits<std::int16_t>::max)(),
              "MAGIC_ENUM_RANGE_MAX must be less than INT16_MAX.");

static_assert(MAGIC_ENUM_RANGE_MAX > MAGIC_ENUM_RANGE_MIN,
              "MAGIC_ENUM_RANGE_MAX must be greater than MAGIC_ENUM_RANGE_MIN.");

namespace detail {

template <typename T>
struct supported
#if defined(MAGIC_ENUM_SUPPORTED) && MAGIC_ENUM_SUPPORTED || defined(MAGIC_ENUM_NO_CHECK_SUPPORT)
    : std::true_type {
};
#else
    : std::false_type {
};
#endif

template <typename T>
inline constexpr bool is_enum_v = std::is_enum_v<T> &&std::is_same_v<T, std::decay_t<T>>;

template <std::size_t N> struct static_string {
  constexpr explicit static_string(std::string_view str) noexcept
      : static_string{str, std::make_index_sequence<N>{}} {
    assert(str.size() == N);
  }

  constexpr const char *data() const noexcept { return chars.data(); }

  constexpr std::size_t size() const noexcept { return N; }

  constexpr operator std::string_view() const noexcept { return {data(), size()}; }

private:
  template <std::size_t... I>
  constexpr static_string(std::string_view str, std::index_sequence<I...>) noexcept
      : chars{{str[I]..., '\0'}} {}

  const std::array<char, N + 1> chars;
};

template <> struct static_string<0> {
  constexpr explicit static_string(std::string_view) noexcept {}

  constexpr const char *data() const noexcept { return nullptr; }

  constexpr std::size_t size() const noexcept { return 0; }

  constexpr operator std::string_view() const noexcept { return {}; }
};

constexpr std::string_view pretty_name(std::string_view name) noexcept {
  for (std::size_t i = name.size(); i > 0; --i) {
    if (!((name[i - 1] >= '0' && name[i - 1] <= '9') ||
          (name[i - 1] >= 'a' && name[i - 1] <= 'z') ||
          (name[i - 1] >= 'A' && name[i - 1] <= 'Z') || (name[i - 1] == '_'))) {
      name.remove_prefix(i);
      break;
    }
  }

  if (name.size() > 0 && ((name.front() >= 'a' && name.front() <= 'z') ||
                          (name.front() >= 'A' && name.front() <= 'Z') || (name.front() == '_'))) {
    return name;
  }

  return {}; // Invalid name.
}

template <typename BinaryPredicate>
constexpr bool cmp_equal(std::string_view lhs, std::string_view rhs, BinaryPredicate &&p) noexcept(
    std::is_nothrow_invocable_r_v<bool, BinaryPredicate, char, char>) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  const auto size = lhs.size();
  for (std::size_t i = 0; i < size; ++i) {
    if (!p(lhs[i], rhs[i])) {
      return false;
    }
  }

  return true;
}

template <typename L, typename R> constexpr bool cmp_less(L lhs, R rhs) noexcept {
  static_assert(std::is_integral_v<L> && std::is_integral_v<R>,
                "magic_enum::detail::cmp_less requires integral type.");

  if constexpr (std::is_signed_v<L> == std::is_signed_v<R>) {
    // If same signedness (both signed or both unsigned).
    return lhs < rhs;
  } else if constexpr (std::is_signed_v<R>) {
    // If 'right' is negative, then result is 'false', otherwise cast & compare.
    return rhs > 0 && lhs < static_cast<std::make_unsigned_t<R>>(rhs);
  } else {
    // If 'left' is negative, then result is 'true', otherwise cast & compare.
    return lhs < 0 || static_cast<std::make_unsigned_t<L>>(lhs) < rhs;
  }
}

template <typename E> constexpr auto n() noexcept {
  static_assert(is_enum_v<E>, "magic_enum::detail::n requires enum type.");
#if defined(MAGIC_ENUM_SUPPORTED) && MAGIC_ENUM_SUPPORTED
#if defined(__clang__)
  constexpr std::string_view name{__PRETTY_FUNCTION__ + 34, sizeof(__PRETTY_FUNCTION__) - 36};
#elif defined(__GNUC__)
  constexpr std::string_view name{__PRETTY_FUNCTION__ + 49, sizeof(__PRETTY_FUNCTION__) - 51};
#elif defined(_MSC_VER)
  constexpr std::string_view name{__FUNCSIG__ + 40, sizeof(__FUNCSIG__) - 57};
#endif
  return static_string<name.size()>{name};
#else
  return std::string_view{}; // Unsupported compiler.
#endif
}

template <typename E> inline constexpr auto type_name_v = n<E>();

template <typename E, E V> constexpr auto n() noexcept {
  static_assert(is_enum_v<E>, "magic_enum::detail::n requires enum type.");
#if defined(MAGIC_ENUM_SUPPORTED) && MAGIC_ENUM_SUPPORTED
#if defined(__clang__) || defined(__GNUC__)
  constexpr auto name = pretty_name({__PRETTY_FUNCTION__, sizeof(__PRETTY_FUNCTION__) - 2});
#elif defined(_MSC_VER)
  constexpr auto name = pretty_name({__FUNCSIG__, sizeof(__FUNCSIG__) - 17});
#endif
  return static_string<name.size()>{name};
#else
  return std::string_view{}; // Unsupported compiler.
#endif
}

template <typename E, E V> inline constexpr auto name_v = n<E, V>();

template <typename E, auto V> constexpr bool is_valid() noexcept {
  static_assert(is_enum_v<E>, "magic_enum::detail::is_valid requires enum type.");

  return n<E, static_cast<E>(V)>().size() != 0;
}

template <typename E, int Min, int Max> constexpr std::size_t range_size() noexcept {
  static_assert(is_enum_v<E>, "magic_enum::detail::range_size requires enum type.");
  constexpr auto size = Max - Min + 1;
  static_assert(size > 0, "magic_enum::enum_range requires valid size.");
  static_assert(size < (std::numeric_limits<std::uint16_t>::max)(),
                "magic_enum::enum_range requires valid size.");

  return static_cast<std::size_t>(size);
}

template <typename E> constexpr int reflected_min() noexcept {
  static_assert(is_enum_v<E>, "magic_enum::detail::reflected_min requires enum type.");
  constexpr auto lhs = enum_range<E>::min;
  static_assert(lhs > (std::numeric_limits<std::int16_t>::min)(),
                "magic_enum::enum_range requires min must be greater than INT16_MIN.");
  constexpr auto rhs = (std::numeric_limits<std::underlying_type_t<E>>::min)();

  return cmp_less(lhs, rhs) ? rhs : lhs;
}

template <typename E> constexpr int reflected_max() noexcept {
  static_assert(is_enum_v<E>, "magic_enum::detail::reflected_max requires enum type.");
  constexpr auto lhs = enum_range<E>::max;
  static_assert(lhs < (std::numeric_limits<std::int16_t>::max)(),
                "magic_enum::enum_range requires max must be less than INT16_MAX.");
  constexpr auto rhs = (std::numeric_limits<std::underlying_type_t<E>>::max)();

  return cmp_less(lhs, rhs) ? lhs : rhs;
}

template <typename E> inline constexpr int reflected_min_v = reflected_min<E>();

template <typename E> inline constexpr int reflected_max_v = reflected_max<E>();

template <std::size_t N>
constexpr std::size_t values_count(const std::array<bool, N>& valid) noexcept {
  auto count = std::size_t{0};
  for (std::size_t i = 0; i < valid.size(); ++i) {
    if (valid[i]) {
      ++count;
    }
  }

  return count;
}

// https://github.com/Neargye/magic_enum/commit/9ed98e3463568124588a0e76d980c141f222cb40

template <typename E, int... I> constexpr auto values(std::integer_sequence<int, I...>) noexcept {
  static_assert(is_enum_v<E>, "magic_enum::detail::values requires enum type.");
  constexpr std::array<bool, sizeof...(I)> valid{{is_valid<E, I + reflected_min_v<E>>()...}};
  //constexpr int count = ((valid[I] ? 1 : 0) + ...);
  constexpr int count = values_count(valid);

  std::array<E, count> values{};
  for (int i = 0, v = 0; v < count; ++i) {
    if (valid[i]) {
      values[v++] = static_cast<E>(i + reflected_min_v<E>);
    }
  }

  return values;
}

template <typename E>
inline constexpr auto values_v = values<E>(
    std::make_integer_sequence<int, range_size<E, reflected_min_v<E>, reflected_max_v<E>>()>{});

template <typename E> inline constexpr std::size_t count_v = values_v<E>.size();

template <typename E> inline constexpr int min_v = static_cast<int>(values_v<E>.front());

template <typename E> inline constexpr int max_v = static_cast<int>(values_v<E>.back());

template <typename E>
inline constexpr std::size_t range_size_v = range_size<E, min_v<E>, max_v<E>>();

template <typename E>
using index_t = std::conditional_t < range_size_v<E>
                <(std::numeric_limits<std::uint8_t>::max)(), std::uint8_t, std::uint16_t>;

template <typename E>
inline constexpr auto invalid_index_v = (std::numeric_limits<index_t<E>>::max)();

template <typename E, int... I> constexpr auto indexes(std::integer_sequence<int, I...>) noexcept {
  static_assert(is_enum_v<E>, "magic_enum::detail::indexes requires enum type.");
  [[maybe_unused]] index_t<E> i = 0;

  return std::array<index_t<E>, sizeof...(I)>{
      {(is_valid<E, I + min_v<E>>() ? i++ : invalid_index_v<E>)...}};
}

template <typename E>
inline constexpr auto indexes_v = indexes<E>(std::make_integer_sequence<int, range_size_v<E>>{});

template <typename E, std::size_t... I> constexpr auto names(std::index_sequence<I...>) noexcept {
  static_assert(is_enum_v<E>, "magic_enum::detail::names requires enum type.");

  return std::array<std::string_view, sizeof...(I)>{{name_v<E, values_v<E>[I]>...}};
}

template <typename E>
inline constexpr auto names_v = names<E>(std::make_index_sequence<count_v<E>>{});

template <typename E, std::size_t... I> constexpr auto entries(std::index_sequence<I...>) noexcept {
  static_assert(is_enum_v<E>, "magic_enum::detail::entries requires enum type.");

  return std::array<std::pair<E, std::string_view>, sizeof...(I)>{
      {{values_v<E>[I], name_v<E, values_v<E>[I]>}...}};
}

template <typename E>
inline constexpr auto entries_v = entries<E>(std::make_index_sequence<count_v<E>>{});

template <typename E> inline constexpr bool is_sparse_v = range_size_v<E> != count_v<E>;

template <typename E, typename U = std::underlying_type_t<E>>
constexpr int undex(U value) noexcept {
  static_assert(is_enum_v<E>, "magic_enum::detail::undex requires enum type.");

  if (const auto i = static_cast<int>(value) - min_v<E>;
      value >= static_cast<U>(min_v<E>) && value <= static_cast<U>(max_v<E>)) {
    if constexpr (is_sparse_v<E>) {
      if (const auto idx = indexes_v<E>[i]; idx != invalid_index_v<E>) {
        return idx;
      }
    } else {
      return i;
    }
  }

  return -1; // Value out of range.
}

template <typename E> constexpr int endex(E value) noexcept {
  static_assert(is_enum_v<E>, "magic_enum::detail::endex requires enum type.");

  return undex<E>(static_cast<std::underlying_type_t<E>>(value));
}

template <typename T, typename R>
using enable_if_enum_t = std::enable_if_t<std::is_enum_v<std::decay_t<T>>, R>;

template <typename T, typename Enable = std::enable_if_t<std::is_enum_v<std::decay_t<T>>>>
using enum_concept = T;

template <typename T, bool = std::is_enum_v<T>> struct is_scoped_enum : std::false_type {};

template <typename T>
struct is_scoped_enum<T, true>
    : std::bool_constant<!std::is_convertible_v<T, std::underlying_type_t<T>>> {};

template <typename T, bool = std::is_enum_v<T>> struct is_unscoped_enum : std::false_type {};

template <typename T>
struct is_unscoped_enum<T, true>
    : std::bool_constant<std::is_convertible_v<T, std::underlying_type_t<T>>> {};

template <typename T, bool = std::is_enum_v<std::decay_t<T>>> struct underlying_type {};

template <typename T> struct underlying_type<T, true> : std::underlying_type<std::decay_t<T>> {};

} // namespace detail

// Checks is magic_enum supported compiler.
inline constexpr bool is_magic_enum_supported = detail::supported<void>::value;

template <typename T> using Enum = detail::enum_concept<T>;

// Checks whether T is an Unscoped enumeration type.
// Provides the member constant value which is equal to true, if T is an [Unscoped
// enumeration](https://en.cppreference.com/w/cpp/language/enum#Unscoped_enumeration)
// type. Otherwise, value is equal to false.
template <typename T> struct is_unscoped_enum : detail::is_unscoped_enum<T> {};

template <typename T> inline constexpr bool is_unscoped_enum_v = is_unscoped_enum<T>::value;

// Checks whether T is an Scoped enumeration type.
// Provides the member constant value which is equal to true, if T is an [Scoped
// enumeration](https://en.cppreference.com/w/cpp/language/enum#Scoped_enumerations) type.
// Otherwise, value is equal to false.
template <typename T> struct is_scoped_enum : detail::is_scoped_enum<T> {};

template <typename T> inline constexpr bool is_scoped_enum_v = is_scoped_enum<T>::value;

// If T is a complete enumeration type, provides a member typedef type that names the
// underlying type of T. Otherwise, if T is not an enumeration type, there is no member
// type. Otherwise (T is an incomplete enumeration type), the program is ill-formed.
template <typename T> struct underlying_type : detail::underlying_type<T> {};

template <typename T> using underlying_type_t = typename underlying_type<T>::type;

// Returns string name of enum type.
template <typename E>
[[nodiscard]] constexpr auto enum_type_name() noexcept
    -> detail::enable_if_enum_t<E, std::string_view> {
  using D = std::decay_t<E>;
  constexpr std::string_view name = detail::type_name_v<D>;
  static_assert(name.size() > 0, "Enum type does not have a name.");

  return name;
}

// Returns number of enum values.
template <typename E>
[[nodiscard]] constexpr auto enum_count() noexcept -> detail::enable_if_enum_t<E, std::size_t> {
  using D = std::decay_t<E>;
  static_assert(detail::supported<D>::value,
                "magic_enum unsupported compiler "
                "(https://github.com/Neargye/magic_enum#compiler-compatibility).");
  static_assert(detail::count_v<D>> 0,
                "magic_enum requires enum implementation and valid max and min.");

  return detail::count_v<D>;
}

// Returns enum value at specified index.
// No bounds checking is performed: the behavior is undefined if index >= number of enum
// values.
template <typename E>
[[nodiscard]] constexpr auto enum_value(std::size_t index) noexcept
    -> detail::enable_if_enum_t<E, std::decay_t<E>> {
  using D = std::decay_t<E>;
  static_assert(detail::supported<D>::value,
                "magic_enum unsupported compiler "
                "(https://github.com/Neargye/magic_enum#compiler-compatibility).");
  static_assert(detail::count_v<D>> 0,
                "magic_enum requires enum implementation and valid max and min.");

  if constexpr (detail::is_sparse_v<D>) {
    return assert(index < detail::count_v<D>), detail::values_v<D>[index];
  } else {
    return assert(index < detail::count_v<D>), static_cast<D>(index + detail::min_v<D>);
  }
}

// Obtains value enum sequence.
// Returns std::array with enum values, sorted by enum value.
template <typename E>
[[nodiscard]] constexpr auto enum_values() noexcept
    -> detail::enable_if_enum_t<E, decltype(detail::values_v<std::decay_t<E>>) &> {
  using D = std::decay_t<E>;
  static_assert(detail::supported<D>::value,
                "magic_enum unsupported compiler "
                "(https://github.com/Neargye/magic_enum#compiler-compatibility).");
  static_assert(detail::count_v<D>> 0,
                "magic_enum requires enum implementation and valid max and min.");

  return detail::values_v<D>;
}

// Returns string enum name from static storage enum variable.
// This version is much lighter on the compile times and is not restricted to the
// enum_range limitation.
template <auto V>
[[nodiscard]] constexpr auto enum_name() noexcept
    -> detail::enable_if_enum_t<decltype(V), std::string_view> {
  using D = std::decay_t<decltype(V)>;
  static_assert(detail::supported<D>::value,
                "magic_enum unsupported compiler "
                "(https://github.com/Neargye/magic_enum#compiler-compatibility).");
  constexpr std::string_view name = detail::name_v<std::decay_t<D>, V>;
  static_assert(name.size() > 0, "Enum value does not have a name.");

  return name;
}

// Returns string enum name from enum value.
// If enum value does not have name or value out of range, returns empty string.
template <typename E>
[[nodiscard]] constexpr auto enum_name(E value) noexcept
    -> detail::enable_if_enum_t<E, std::string_view> {
  using D = std::decay_t<E>;
  static_assert(detail::supported<D>::value,
                "magic_enum unsupported compiler "
                "(https://github.com/Neargye/magic_enum#compiler-compatibility).");
  static_assert(detail::count_v<D>> 0,
                "magic_enum requires enum implementation and valid max and min.");

  if (const auto i = detail::endex<D>(value); i != -1) {
    return detail::names_v<D>[i];
  }

  return {}; // Value out of range.
}

// Obtains string enum name sequence.
// Returns std::array with string enum names, sorted by enum value.
template <typename E>
[[nodiscard]] constexpr auto enum_names() noexcept
    -> detail::enable_if_enum_t<E, decltype(detail::names_v<std::decay_t<E>>) &> {
  using D = std::decay_t<E>;
  static_assert(detail::supported<D>::value,
                "magic_enum unsupported compiler "
                "(https://github.com/Neargye/magic_enum#compiler-compatibility).");
  static_assert(detail::count_v<D>> 0,
                "magic_enum requires enum implementation and valid max and min.");

  return detail::names_v<D>;
}

// Obtains pair (value enum, string enum name) sequence.
// Returns std::array with std::pair (value enum, string enum name), sorted by enum value.
template <typename E>
[[nodiscard]] constexpr auto enum_entries() noexcept
    -> detail::enable_if_enum_t<E, decltype(detail::entries_v<std::decay_t<E>>) &> {
  using D = std::decay_t<E>;
  static_assert(detail::supported<D>::value,
                "magic_enum unsupported compiler "
                "(https://github.com/Neargye/magic_enum#compiler-compatibility).");
  static_assert(detail::count_v<D>> 0,
                "magic_enum requires enum implementation and valid max and min.");

  return detail::entries_v<D>;
}

// Obtains enum value from enum string name.
// Returns std::optional with enum value.
template <typename E, typename BinaryPredicate>
[[nodiscard]] constexpr auto enum_cast(std::string_view value, BinaryPredicate p) noexcept(
    std::is_nothrow_invocable_r_v<bool, BinaryPredicate, char, char>)
    -> detail::enable_if_enum_t<E, std::optional<std::decay_t<E>>> {
  using D = std::decay_t<E>;
  static_assert(detail::supported<D>::value,
                "magic_enum unsupported compiler "
                "(https://github.com/Neargye/magic_enum#compiler-compatibility).");
  static_assert(detail::count_v<D>> 0,
                "magic_enum requires enum implementation and valid max and min.");
  static_assert(std::is_invocable_r_v<bool, BinaryPredicate, char, char>,
                "magic_enum::enum_cast requires bool(char, char) invocable predicate.");

  for (std::size_t i = 0; i < detail::count_v<D>; ++i) {
    if (detail::cmp_equal(value, detail::names_v<D>[i], p)) {
      return enum_value<D>(i);
    }
  }

  return std::nullopt; // Invalid value or out of range.
}

template <typename E>
[[nodiscard]] constexpr auto enum_cast(std::string_view value) noexcept
    -> detail::enable_if_enum_t<E, std::optional<std::decay_t<E>>> {
  using D = std::decay_t<E>;
  static_assert(detail::supported<D>::value,
                "magic_enum unsupported compiler "
                "(https://github.com/Neargye/magic_enum#compiler-compatibility).");
  static_assert(detail::count_v<D>> 0,
                "magic_enum requires enum implementation and valid max and min.");

  for (std::size_t i = 0; i < detail::count_v<D>; ++i) {
    if (value == detail::names_v<D>[i]) {
      return enum_value<D>(i);
    }
  }

  return std::nullopt; // Invalid value or out of range.
}

// Obtains enum value from integer value.
// Returns std::optional with enum value.
template <typename E>
[[nodiscard]] constexpr auto enum_cast(underlying_type_t<E> value) noexcept
    -> detail::enable_if_enum_t<E, std::optional<std::decay_t<E>>> {
  using D = std::decay_t<E>;
  static_assert(detail::supported<D>::value,
                "magic_enum unsupported compiler "
                "(https://github.com/Neargye/magic_enum#compiler-compatibility).");
  static_assert(detail::count_v<D>> 0,
                "magic_enum requires enum implementation and valid max and min.");

  if (detail::undex<D>(value) != -1) {
    return static_cast<D>(value);
  }

  return std::nullopt; // Invalid value or out of range.
}

// Returns integer value from enum value.
template <typename E>
[[nodiscard]] constexpr auto enum_integer(E value) noexcept
    -> detail::enable_if_enum_t<E, underlying_type_t<E>> {
  return static_cast<underlying_type_t<E>>(value);
}

// Obtains index in enum value sequence from enum value.
// Returns std::optional with index.
template <typename E>
[[nodiscard]] constexpr auto enum_index(E value) noexcept
    -> detail::enable_if_enum_t<E, std::optional<std::size_t>> {
  using D = std::decay_t<E>;
  static_assert(detail::supported<D>::value,
                "magic_enum unsupported compiler "
                "(https://github.com/Neargye/magic_enum#compiler-compatibility).");
  static_assert(detail::count_v<D>> 0,
                "magic_enum requires enum implementation and valid max and min.");

  if (const auto i = detail::endex<D>(value); i != -1) {
    return i;
  }

  return std::nullopt; // Value out of range.
}

// Checks whether enum contains enumerator with such value.
template <typename E>
[[nodiscard]] constexpr auto enum_contains(E value) noexcept -> detail::enable_if_enum_t<E, bool> {
  using D = std::decay_t<E>;
  static_assert(detail::supported<D>::value,
                "magic_enum unsupported compiler "
                "(https://github.com/Neargye/magic_enum#compiler-compatibility).");
  static_assert(detail::count_v<D>> 0,
                "magic_enum requires enum implementation and valid max and min.");

  return detail::endex<D>(value) != -1;
}

// Checks whether enum contains enumerator with such integer value.
template <typename E>
[[nodiscard]] constexpr auto enum_contains(underlying_type_t<E> value) noexcept
    -> detail::enable_if_enum_t<E, bool> {
  using D = std::decay_t<E>;
  static_assert(detail::supported<D>::value,
                "magic_enum unsupported compiler "
                "(https://github.com/Neargye/magic_enum#compiler-compatibility).");
  static_assert(detail::count_v<D>> 0,
                "magic_enum requires enum implementation and valid max and min.");

  return detail::undex<D>(value) != -1;
}

// Checks whether enum contains enumerator with such string enum name.
template <typename E>
[[nodiscard]] constexpr auto enum_contains(std::string_view value) noexcept
    -> detail::enable_if_enum_t<E, bool> {
  using D = std::decay_t<E>;
  static_assert(detail::supported<D>::value,
                "magic_enum unsupported compiler "
                "(https://github.com/Neargye/magic_enum#compiler-compatibility).");
  static_assert(detail::count_v<D>> 0,
                "magic_enum requires enum implementation and valid max and min.");

  return enum_cast<D>(value).has_value();
}

namespace ostream_operators {

template <typename Char, typename Traits, typename E>
auto operator<<(std::basic_ostream<Char, Traits> &os, E value)
    -> detail::enable_if_enum_t<E, std::basic_ostream<Char, Traits> &> {
  if (const auto name = enum_name(value); !name.empty()) {
    for (const auto c : name) {
      os.put(c);
    }
  } else {
    os << enum_integer(value);
  }

  return os;
}

template <typename Char, typename Traits, typename E>
auto operator<<(std::basic_ostream<Char, Traits> &os, std::optional<E> value)
    -> detail::enable_if_enum_t<E, std::basic_ostream<Char, Traits> &> {
  if (value.has_value()) {
    os << value.value();
  }

  return os;
}

} // namespace ostream_operators

namespace bitwise_operators {

template <typename E> constexpr auto operator~(E rhs) noexcept -> detail::enable_if_enum_t<E, E> {
  return static_cast<E>(~static_cast<underlying_type_t<E>>(rhs));
}

template <typename E>
constexpr auto operator|(E lhs, E rhs) noexcept -> detail::enable_if_enum_t<E, E> {
  return static_cast<E>(static_cast<underlying_type_t<E>>(lhs) |
                        static_cast<underlying_type_t<E>>(rhs));
}

template <typename E>
constexpr auto operator&(E lhs, E rhs) noexcept -> detail::enable_if_enum_t<E, E> {
  return static_cast<E>(static_cast<underlying_type_t<E>>(lhs) &
                        static_cast<underlying_type_t<E>>(rhs));
}

template <typename E>
constexpr auto operator^(E lhs, E rhs) noexcept -> detail::enable_if_enum_t<E, E> {
  return static_cast<E>(static_cast<underlying_type_t<E>>(lhs) ^
                        static_cast<underlying_type_t<E>>(rhs));
}

template <typename E>
constexpr auto operator|=(E &lhs, E rhs) noexcept -> detail::enable_if_enum_t<E, E &> {
  return lhs = lhs | rhs;
}

template <typename E>
constexpr auto operator&=(E &lhs, E rhs) noexcept -> detail::enable_if_enum_t<E, E &> {
  return lhs = lhs & rhs;
}

template <typename E>
constexpr auto operator^=(E &lhs, E rhs) noexcept -> detail::enable_if_enum_t<E, E &> {
  return lhs = lhs ^ rhs;
}

} // namespace bitwise_operators

} // namespace magic_enum

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif // NEARGYE_MAGIC_ENUM_HPP

#pragma once
#include <array>
using std::array;

namespace structopt {

template <typename> struct array_size;
template <typename T, size_t N> struct array_size<array<T, N>> { static size_t const size = N; };

} // namespace structopt
#pragma once

namespace structopt {

template <typename Test, template <typename...> class Ref>
struct is_specialization : std::false_type {};

template <template <typename...> class Ref, typename... Args>
struct is_specialization<Ref<Args...>, Ref> : std::true_type {};

} // namespace structopt
#pragma once
#include <deque>
#include <forward_list>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace structopt {

// specialize a type for all of the STL containers.
namespace is_stl_container_impl {
template <typename T> struct is_stl_container : std::false_type {};
template <typename T, std::size_t N> struct is_stl_container<std::array<T, N>> : std::true_type {};
template <typename... Args> struct is_stl_container<std::vector<Args...>> : std::true_type {};
template <typename... Args> struct is_stl_container<std::deque<Args...>> : std::true_type {};
template <typename... Args> struct is_stl_container<std::list<Args...>> : std::true_type {};
template <typename... Args> struct is_stl_container<std::forward_list<Args...>> : std::true_type {};
template <typename... Args> struct is_stl_container<std::set<Args...>> : std::true_type {};
template <typename... Args> struct is_stl_container<std::multiset<Args...>> : std::true_type {};
template <typename... Args> struct is_stl_container<std::map<Args...>> : std::true_type {};
template <typename... Args> struct is_stl_container<std::multimap<Args...>> : std::true_type {};
template <typename... Args>
struct is_stl_container<std::unordered_set<Args...>> : std::true_type {};
template <typename... Args>
struct is_stl_container<std::unordered_multiset<Args...>> : std::true_type {};
template <typename... Args>
struct is_stl_container<std::unordered_map<Args...>> : std::true_type {};
template <typename... Args>
struct is_stl_container<std::unordered_multimap<Args...>> : std::true_type {};
template <typename... Args> struct is_stl_container<std::stack<Args...>> : std::true_type {};
template <typename... Args> struct is_stl_container<std::queue<Args...>> : std::true_type {};
template <typename... Args>
struct is_stl_container<std::priority_queue<Args...>> : std::true_type {};
} // namespace is_stl_container_impl

template <class T> struct is_array : std::is_array<T> {};
template <class T, std::size_t N> struct is_array<std::array<T, N>> : std::true_type {};
// optional:
template <class T> struct is_array<T const> : is_array<T> {};
template <class T> struct is_array<T volatile> : is_array<T> {};
template <class T> struct is_array<T volatile const> : is_array<T> {};

// type trait to utilize the implementation type traits as well as decay the type
template <typename T> struct is_stl_container {
  static constexpr bool const value =
      is_stl_container_impl::is_stl_container<std::decay_t<T>>::value;
};

} // namespace structopt

#pragma once
#include <string>

namespace structopt {

namespace details {

static inline bool string_replace(std::string &str, const std::string &from,
                                  const std::string &to) {
  size_t start_pos = str.find(from);
  if (start_pos == std::string::npos)
    return false;
  str.replace(start_pos, from.length(), to);
  return true;
}

} // namespace details

} // namespace structopt
#pragma once
#include <string>

namespace structopt {

namespace details {

static inline bool is_binary_notation(std::string const &input) {
  return input.compare(0, 2, "0b") == 0 && input.size() > 2 &&
         input.find_first_not_of("01", 2) == std::string::npos;
}

static inline bool is_hex_notation(std::string const &input) {
  return input.compare(0, 2, "0x") == 0 && input.size() > 2 &&
         input.find_first_not_of("0123456789abcdefABCDEF", 2) == std::string::npos;
}

static inline bool is_octal_notation(std::string const &input) {
  return input.compare(0, 1, "0") == 0 && input.size() > 1 &&
         input.find_first_not_of("01234567", 1) == std::string::npos;
}

static inline bool is_valid_number(const std::string &input) {
  if (is_binary_notation(input) || is_hex_notation(input) || is_octal_notation(input)) {
    return true;
  }

  if (input.empty()) {
    return false;
  }

  std::size_t i = 0, j = input.length() - 1;

  // Handling whitespaces
  while (i < input.length() && input[i] == ' ')
    i++;
  while (input[j] == ' ')
    j--;

  if (i > j)
    return false;

  // if string is of length 1 and the only
  // character is not a digit
  if (i == j && !(input[i] >= '0' && input[i] <= '9'))
    return false;

  // If the 1st char is not '+', '-', '.' or digit
  if (input[i] != '.' && input[i] != '+' && input[i] != '-' &&
      !(input[i] >= '0' && input[i] <= '9'))
    return false;

  // To check if a '.' or 'e' is found in given
  // string. We use this flag to make sure that
  // either of them appear only once.
  bool dot_or_exp = false;

  for (; i <= j; i++) {
    // If any of the char does not belong to
    // {digit, +, -, ., e}
    if (input[i] != 'e' && input[i] != '.' && input[i] != '+' && input[i] != '-' &&
        !(input[i] >= '0' && input[i] <= '9'))
      return false;

    if (input[i] == '.') {
      // checks if the char 'e' has already
      // occurred before '.' If yes, return false;.
      if (dot_or_exp == true)
        return false;

      // If '.' is the last character.
      if (i + 1 > input.length())
        return false;

      // if '.' is not followed by a digit.
      if (!(input[i + 1] >= '0' && input[i + 1] <= '9'))
        return false;
    }

    else if (input[i] == 'e') {
      // set dot_or_exp = 1 when e is encountered.
      dot_or_exp = true;

      // if there is no digit before 'e'.
      if (!(input[i - 1] >= '0' && input[i - 1] <= '9'))
        return false;

      // If 'e' is the last Character
      if (i + 1 > input.length())
        return false;

      // if e is not followed either by
      // '+', '-' or a digit
      if (input[i + 1] != '+' && input[i + 1] != '-' && (input[i + 1] >= '0' && input[i] <= '9'))
        return false;
    }
  }

  /* If the string skips all above cases, then
  it is numeric*/
  return true;
}

} // namespace details

} // namespace structopt

#pragma once
#include <algorithm>
#include <iostream>
#include <optional>
#include <queue>
#include <string>
// #include <structopt/is_specialization.hpp>
// #include <structopt/string.hpp>
// #include <structopt/third_party/visit_struct/visit_struct.hpp>
#include <type_traits>
#include <vector>

namespace structopt {

class app;

namespace details {

struct visitor {
  std::string name;
  std::string version;
  std::vector<std::string> field_names;
  std::deque<std::string> positional_field_names; // mutated by parser
  std::deque<std::string> positional_field_names_for_help;
  std::deque<std::string> vector_like_positional_field_names;
  std::deque<std::string> flag_field_names;
  std::deque<std::string> optional_field_names;
  std::deque<std::string> nested_struct_field_names;

  visitor() = default;

  explicit visitor(const std::string &name, const std::string &version)
      : name(name), version(version) {}

  // Visitor function for std::optional - could be an option or a flag
  template <typename T>
  inline typename std::enable_if<structopt::is_specialization<T, std::optional>::value, void>::type
  operator()(const char *name, T &) {
    field_names.push_back(name);
    if constexpr (std::is_same<typename T::value_type, bool>::value) {
      flag_field_names.push_back(name);
    } else {
      optional_field_names.push_back(name);
    }
  }

  // Visitor function for any positional field (not std::optional)
  template <typename T>
  inline typename std::enable_if<!structopt::is_specialization<T, std::optional>::value &&
                                     !visit_struct::traits::is_visitable<T>::value,
                                 void>::type
  operator()(const char *name, T &) {
    field_names.push_back(name);
    positional_field_names.push_back(name);
    positional_field_names_for_help.push_back(name);
    if constexpr (structopt::is_specialization<T, std::deque>::value ||
                  structopt::is_specialization<T, std::list>::value ||
                  structopt::is_specialization<T, std::vector>::value ||
                  structopt::is_specialization<T, std::set>::value ||
                  structopt::is_specialization<T, std::multiset>::value ||
                  structopt::is_specialization<T, std::unordered_set>::value ||
                  structopt::is_specialization<T, std::unordered_multiset>::value ||
                  structopt::is_specialization<T, std::queue>::value ||
                  structopt::is_specialization<T, std::stack>::value ||
                  structopt::is_specialization<T, std::priority_queue>::value) {
      // keep track of vector-like fields as these (even though positional)
      // can be happy without any arguments
      vector_like_positional_field_names.push_back(name);
    }
  }

  // Visitor function for nested structs
  template <typename T>
  inline typename std::enable_if<visit_struct::traits::is_visitable<T>::value, void>::type
  operator()(const char *name, T &) {
    field_names.push_back(name);
    nested_struct_field_names.push_back(name);
  }

  bool is_field_name(const std::string &field_name) {
    return std::find(field_names.begin(), field_names.end(), field_name) != field_names.end();
  }

  void print_help(std::ostream &os) const {
    os << "\nUSAGE: " << name << " ";

    if (flag_field_names.empty() == false) {
      os << "[FLAGS] ";
    }

    if (optional_field_names.empty() == false) {
      os << "[OPTIONS] ";
    }

    if (nested_struct_field_names.empty() == false) {
      os << "[SUBCOMMANDS] ";
    }

    for (auto &field : positional_field_names_for_help) {
      os << field << " ";
    }

    if (flag_field_names.empty() == false) {
      os << "\n\nFLAGS:\n";
      for (auto &flag : flag_field_names) {
        os << "    -" << flag[0] << ", --" << flag << "\n";
      }
    } else {
      os << "\n";
    }

    if (optional_field_names.empty() == false) {
      os << "\nOPTIONS:\n";
      for (auto &option : optional_field_names) {

        // Generate kebab case and present as option
        auto kebab_case = option;
        details::string_replace(kebab_case, "_", "-");
        std::string long_form = "";
        if (kebab_case != option) {
          long_form = kebab_case;
        } else {
          long_form = option;
        }

        os << "    -" << option[0] << ", --" << long_form << " <" << option << ">"
           << "\n";
      }
    }

    if (nested_struct_field_names.empty() == false) {
      os << "\nSUBCOMMANDS:\n";
      for (auto &sc : nested_struct_field_names) {
        os << "    " << sc << "\n";
      }
    }

    if (positional_field_names_for_help.empty() == false) {
      os << "\nARGS:\n";
      for (auto &arg : positional_field_names_for_help) {
        os << "    " << arg << "\n";
      }
    }
  }
};

} // namespace details

} // namespace structopt

#pragma once
#include <exception>
#include <sstream>
#include <string>
// #include <structopt/visitor.hpp>

namespace structopt {

class exception : public std::exception {
  std::string what_{""};
  std::string help_{""};
  details::visitor visitor_;

public:
  exception(const std::string &what, const details::visitor &visitor)
      : what_(what), help_(""), visitor_(visitor) {
    std::stringstream os;
    visitor_.print_help(os);
    help_ = os.str();
  }

  const char *what() const throw() { return what_.c_str(); }

  const char *help() const throw() { return help_.c_str(); }
};

} // namespace structopt
#pragma once
#include <optional>
// #include <structopt/visitor.hpp>

namespace structopt {

namespace details {
struct parser;
}

class sub_command {
  std::optional<bool> invoked_;
  details::visitor visitor_;

  friend struct structopt::details::parser;

public:
  bool has_value() const { return invoked_.has_value(); }
};

} // namespace structopt
#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
// #include <structopt/array_size.hpp>
// #include <structopt/exception.hpp>
// #include <structopt/is_number.hpp>
// #include <structopt/is_specialization.hpp>
// #include <structopt/sub_command.hpp>
// #include <structopt/third_party/magic_enum/magic_enum.hpp>
// #include <structopt/third_party/visit_struct/visit_struct.hpp>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace structopt {

namespace details {

struct parser {
  structopt::details::visitor visitor;
  std::vector<std::string> arguments;
  std::size_t current_index{1};
  std::size_t next_index{1};
  bool double_dash_encountered{false}; // "--" option-argument delimiter
  bool sub_command_invoked{false};
  std::string already_invoked_subcommand_name{""};

  bool is_optional(const std::string &name) {
    if (double_dash_encountered) {
      return false;
    } else if (name == "--") {
      double_dash_encountered = true;
      return false;
    } else if (is_valid_number(name)) {
      return false;
    }

    bool result = false;
    if (name.size() >= 2) {
      // e.g., -b, -v
      if (name[0] == '-') {
        result = true;
        // e.g., --verbose
        if (name[1] == '-') {
          result = true;
        }
      }
    }
    return result;
  }

  bool is_kebab_case(const std::string &next, const std::string &field_name) {
    bool result = false;
    auto maybe_kebab_case = next;
    if (maybe_kebab_case.size() > 1 && maybe_kebab_case[0] == '-') {
      // remove first dash
      maybe_kebab_case.erase(0, 1);
      if (maybe_kebab_case[0] == '-') {
        // there is a second leading dash
        // remove it
        maybe_kebab_case.erase(0, 1);
      }
      std::replace(maybe_kebab_case.begin(), maybe_kebab_case.end(), '-', '_');
      if (maybe_kebab_case == field_name) {
        result = true;
      }
    }
    return result;
  }

  bool is_optional_field(const std::string &next, const std::string &field_name) {
    bool result = false;
    if (next == "-" + field_name || next == "--" + field_name ||
        next == "-" + std::string(1, field_name[0]) || is_kebab_case(next, field_name)) {
      // okay `next` matches _a_ field name (which is an optional field)
      result = true;
    }
    return result;
  }

  bool is_optional_field(const std::string &next) {
    if (!is_optional(next)) {
      return false;
    }

    bool result = false;
    for (auto &field_name : visitor.field_names) {
      result = is_optional_field(next, field_name);
      if (result) {
        break;
      }
    }
    return result;
  }

  // checks if the next argument is a delimited optional field
  // e.g., -std=c++17, where std matches a field name
  // and it is delimited by one of the two allowed delimiters: `=` and `:`
  //
  // if true, the return value includes the delimiter that was used
  std::pair<bool, char> is_delimited_optional_argument(const std::string &next) {
    bool success = false;
    char delimiter = '\0';

    auto equal_pos = next.find('=');
    auto colon_pos = next.find(':');

    if (equal_pos == std::string::npos && colon_pos == std::string::npos) {
      // not delimited
      return {success, delimiter};
    } else {
      // assume `=` comes first
      char c = '=';

      if (colon_pos < equal_pos) {
        // confirmed: `:` comes first
        c = ':';
      }

      // split `next` into key and value
      // check if key is an optional field
      std::string key;
      bool delimiter_found = false;
      for (size_t i = 0; i < next.size(); i++) {
        if (next[i] == c && !delimiter_found) {
          delimiter = c;
          delimiter_found = true;
        } else {
          if (!delimiter_found) {
            key += next[i];
          }
        }
      }

      // check if `key` is a valid optional field
      if (delimiter_found && is_optional_field(key)) {
        success = true;
      }
    }
    return {success, delimiter};
  }

  std::pair<std::string, std::string> split_delimited_argument(char delimiter,
                                                               const std::string &next) {
    std::string key, value;
    bool delimiter_found = false;
    for (size_t i = 0; i < next.size(); i++) {
      if (next[i] == delimiter && !delimiter_found) {
        delimiter_found = true;
      } else {
        if (!delimiter_found) {
          key += next[i];
        } else {
          value += next[i];
        }
      }
    }
    return {key, value};
  }

  // Strip the initial dashes on the left of an optional argument
  // e.g., --verbose => verbose
  // e.g., -log-level => log-level
  std::string lstrip_dashes(const std::string &next) {
    std::string result;
    bool prefix_dashes_ended = false;
    for (auto &c : next) {
      if (prefix_dashes_ended == false && c != '-') {
        prefix_dashes_ended = true;
      }
      if (prefix_dashes_ended) {
        result += c;
      }
    }
    return result;
  }

  // Get the optional field name if any from
  // e.g., `-v` => `verbose`
  // e.g., `-log-level` => `log_level`
  std::optional<std::string> get_full_optional_field_name(const std::string &next) {
    std::optional<std::string> result;

    if (next.size() == 2 && next[0] == '-') {
      // short form of optional argument
      for (auto &oarg : visitor.optional_field_names) {
        if (oarg[0] == next[1]) {
          // second character of next matches first character of some optional field_name
          result = oarg;
          break;
        }
      }
    } else {
      // long form of optional argument

      // strip dashes on the left
      std::string potential_field_name = lstrip_dashes(next);

      // replace `-` in the middle with `_`
      std::replace(potential_field_name.begin(), potential_field_name.end(), '-', '_');

      // check if `potential_field_name` is in the optional field names list
      for (auto &oarg : visitor.optional_field_names) {
        if (oarg == potential_field_name) {
          result = oarg;
          break;
        }
      }
    }

    return result;
  }

  template <typename T> std::pair<T, bool> parse_argument(const char *name) {
    if (next_index >= arguments.size()) {
      return {T(), false};
    }
    T result;
    bool success = true;
    if constexpr (visit_struct::traits::is_visitable<T>::value) {
      result = parse_nested_struct<T>(name);
    } else if constexpr (std::is_enum<T>::value) {
      result = parse_enum_argument<T>(name);
      next_index += 1;
    } else if constexpr (structopt::is_specialization<T, std::pair>::value) {
      result = parse_pair_argument<typename T::first_type, typename T::second_type>(name);
    } else if constexpr (structopt::is_specialization<T, std::tuple>::value) {
      result = parse_tuple_argument<T>(name);
    } else if constexpr (!is_stl_container<T>::value) {
      result = parse_single_argument<T>(name);
      next_index += 1;
    } else if constexpr (structopt::is_array<T>::value) {
      constexpr std::size_t N = structopt::array_size<T>::size;
      result = parse_array_argument<typename T::value_type, N>(name);
    } else if constexpr (structopt::is_specialization<T, std::deque>::value ||
                         structopt::is_specialization<T, std::list>::value ||
                         structopt::is_specialization<T, std::vector>::value) {
      result = parse_vector_like_argument<T>(name);
    } else if constexpr (structopt::is_specialization<T, std::set>::value ||
                         structopt::is_specialization<T, std::multiset>::value ||
                         structopt::is_specialization<T, std::unordered_set>::value ||
                         structopt::is_specialization<T, std::unordered_multiset>::value) {
      result = parse_set_argument<T>(name);
    } else if constexpr (structopt::is_specialization<T, std::queue>::value ||
                         structopt::is_specialization<T, std::stack>::value ||
                         structopt::is_specialization<T, std::priority_queue>::value) {
      result = parse_container_adapter_argument<T>(name);
    } else {
      success = false;
    }
    return {result, success};
  }

  template <typename T> std::optional<T> parse_optional_argument(const char *name) {
    next_index += 1;
    std::optional<T> result;
    if (next_index < arguments.size()) {
      auto [value, success] = parse_argument<T>(name);
      if (success) {
        result = value;
      } else {
        throw structopt::exception("Error: failed to correctly parse optional argument `" +
                                       std::string{name} + "`.",
                                   visitor);
      }
    } else {
      throw structopt::exception(
          "Error: expected value for optional argument `" + std::string{name} + "`.", visitor);
    }
    return result;
  }

  // Any field that can be constructed using std::stringstream
  // Not container type
  // Not a visitable type, i.e., a nested struct
  template <typename T>
  inline typename std::enable_if<!visit_struct::traits::is_visitable<T>::value, T>::type
  parse_single_argument(const char *) {
    std::string argument = arguments[next_index];
    std::istringstream ss(argument);
    T result;

    if constexpr (std::is_integral<T>::value) {
      if (is_hex_notation(argument)) {
        ss >> std::hex >> result;
      } else if (is_octal_notation(argument)) {
        ss >> std::oct >> result;
      } else if (is_binary_notation(argument)) {
        argument.erase(0, 2); // remove "0b"
        result = static_cast<T>(std::stoi(argument, nullptr, 2));
      } else {
        ss >> std::dec >> result;
      }
    } else {
      ss >> result;
    }

    return result;
  }

  // Nested visitable struct
  template <typename T>
  inline typename std::enable_if<visit_struct::traits::is_visitable<T>::value, T>::type
  parse_nested_struct(const char *name) {

    T argument_struct;

    if constexpr (std::is_base_of<structopt::sub_command, T>::value) {
      argument_struct.invoked_ = true;
    }

    // Save struct field names
    argument_struct.visitor_.name = name; // sub-command name; not the program
    argument_struct.visitor_.version = visitor.version;
    visit_struct::for_each(argument_struct, argument_struct.visitor_);

    // add `help` and `version` optional arguments
    argument_struct.visitor_.optional_field_names.push_back("help");
    argument_struct.visitor_.optional_field_names.push_back("version");

    if (!sub_command_invoked) {
      sub_command_invoked = true;
      already_invoked_subcommand_name = name;
    } else {
      // a sub-command has already been invoked
      throw structopt::exception("Error: failed to invoke sub-command `" + std::string{name} +
                                     "` because a different sub-command, `" +
                                     already_invoked_subcommand_name +
                                     "`, has already been invoked.",
                                 argument_struct.visitor_);
    }

    structopt::details::parser parser;
    parser.next_index = 0;
    parser.current_index = 0;
    parser.double_dash_encountered = double_dash_encountered;
    parser.visitor = argument_struct.visitor_;

    std::copy(arguments.begin() + next_index, arguments.end(),
              std::back_inserter(parser.arguments));

    for (std::size_t i = 0; i < parser.arguments.size(); i++) {
      parser.current_index = i;
      visit_struct::for_each(argument_struct, parser);
    }

    // directly call the parser to check for `help` and `version` flags
    std::optional<bool> help = false, version = false;
    for (std::size_t i = 0; i < parser.arguments.size(); i++) {
      parser.operator()("help", help);
      parser.operator()("version", version);

      if (help == true) {
        // if help is requested, print help and exit
        // argument_struct.visitor_.print_help(std::cout);
        // exit(EXIT_SUCCESS);
        throw structopt::exception("", parser.visitor);
      } else if (version == true) {
        // if version is requested, print version and exit
        // std::cout << argument_struct.visitor_.version << "\n";
        // exit(EXIT_SUCCESS);
        throw structopt::exception("", parser.visitor);
      }
    }

    // if all positional arguments were provided
    // this list would be empty
    if (!parser.visitor.positional_field_names.empty()) {
      for (auto &field_name : parser.visitor.positional_field_names) {
        if (std::find(parser.visitor.vector_like_positional_field_names.begin(),
                      parser.visitor.vector_like_positional_field_names.end(),
                      field_name) == parser.visitor.vector_like_positional_field_names.end()) {
          // this positional argument is not a vector-like argument
          // it expects value(s)
          throw structopt::exception("Error: expected value for positional argument `" +
                                         field_name + "`.",
                                     argument_struct.visitor_);
        }
      }
    }

    // update current and next
    current_index += parser.next_index;
    next_index += parser.next_index;

    return argument_struct;
  }

  // Pair argument
  template <typename T1, typename T2> std::pair<T1, T2> parse_pair_argument(const char *name) {
    std::pair<T1, T2> result;
    {
      // Pair first
      auto [value, success] = parse_argument<T1>(name);
      if (success) {
        result.first = value;
      } else {
        if (next_index == arguments.size()) {
          // end of arguments list
          // first argument not provided
          throw structopt::exception("Error: failed to correctly parse the pair `" +
                                         std::string{name} + "`. Expected 2 arguments, 0 provided.",
                                     visitor);
        } else {
          throw structopt::exception("Error: failed to correctly parse first element of pair `" +
                                         std::string{name} + "`",
                                     visitor);
        }
      }
    }
    {
      // Pair second
      auto [value, success] = parse_argument<T2>(name);
      if (success) {
        result.second = value;
      } else {
        if (next_index == arguments.size()) {
          // end of arguments list
          // second argument not provided
          throw structopt::exception("Error: failed to correctly parse the pair `" +
                                         std::string{name} +
                                         "`. Expected 2 arguments, only 1 provided.",
                                     visitor);
        } else {
          throw structopt::exception("Error: failed to correctly parse second element of pair `" +
                                         std::string{name} + "`",
                                     visitor);
        }
      }
    }
    return result;
  }

  // Array argument
  template <typename T, std::size_t N> std::array<T, N> parse_array_argument(const char *name) {
    std::array<T, N> result{};

    const auto arguments_left = arguments.size() - next_index;
    if (arguments_left == 0 || arguments_left < N) {
      throw structopt::exception(
          "Error: expected " + std::to_string(N) + " values for std::array argument `" + name +
              "` - instead got only " + std::to_string(arguments_left) + " arguments.",
          visitor);
    }

    for (std::size_t i = 0; i < N; i++) {
      auto [value, success] = parse_argument<T>(name);
      if (success) {
        result[i] = value;
      }
    }
    return result;
  }

  template <class Tuple, class F, std::size_t... I>
  constexpr F for_each_impl(Tuple &&t, F &&f, std::index_sequence<I...>) {
    return (void)std::initializer_list<int>{
               (std::forward<F>(f)(std::get<I>(std::forward<Tuple>(t))), 0)...},
           f;
  }

  template <class Tuple, class F> constexpr F for_each(Tuple &&t, F &&f) {
    return for_each_impl(
        std::forward<Tuple>(t), std::forward<F>(f),
        std::make_index_sequence<std::tuple_size<std::remove_reference_t<Tuple>>::value>{});
  }

  // Parse single tuple element
  template <typename T>
  void parse_tuple_element(const char *name, std::size_t index, std::size_t size, T &&result) {
    auto [value, success] = parse_argument<typename std::remove_reference<T>::type>(name);
    if (success) {
      result = value;
    } else {
      if (next_index == arguments.size()) {
        // end of arguments list
        // failed to parse tuple <>. expected `size` arguments, `index` provided
        throw structopt::exception("Error: failed to correctly parse tuple `" + std::string{name} +
                                       "`. Expected " + std::to_string(size) + " arguments, " +
                                       std::to_string(index) + " provided.",
                                   visitor);
      } else {
        throw structopt::exception("Error: failed to correctly parse tuple `" + std::string{name} +
                                       "` {size = " + std::to_string(size) + "} at index " +
                                       std::to_string(index) + ".",
                                   visitor);
      }
    }
  }

  // Tuple argument
  template <typename Tuple> Tuple parse_tuple_argument(const char *name) {
    Tuple result;
    std::size_t i = 0;
    constexpr auto tuple_size = std::tuple_size<Tuple>::value;
    for_each(result, [&](auto &&arg) {
      parse_tuple_element(name, i, tuple_size, arg);
      i += 1;
    });
    return result;
  }

  // Vector, deque, list
  template <typename T> T parse_vector_like_argument(const char *name) {
    T result;

    // Parse from current till end
    while (next_index < arguments.size()) {
      const auto next = arguments[next_index];
      if (is_optional_field(next) || std::string{next} == "--" ||
          is_delimited_optional_argument(next).first) {
        if (std::string{next} == "--") {
          double_dash_encountered = true;
          next_index += 1;
        }
        // this marks the end of the container (break here)
        break;
      }
      auto [value, success] = parse_argument<typename T::value_type>(name);
      if (success) {
        result.push_back(value);
      }
    }
    return result;
  }

  // stack, queue, priority_queue
  template <typename T> T parse_container_adapter_argument(const char *name) {
    T result;
    // Parse from current till end
    while (next_index < arguments.size()) {
      const auto next = arguments[next_index];
      if (is_optional_field(next) || std::string{next} == "--" ||
          is_delimited_optional_argument(next).first) {
        if (std::string{next} == "--") {
          double_dash_encountered = true;
          next_index += 1;
        }
        // this marks the end of the container (break here)
        break;
      }
      auto [value, success] = parse_argument<typename T::value_type>(name);
      if (success) {
        result.push(value);
      }
    }
    return result;
  }

  // Set, multiset, unordered_set, unordered_multiset
  template <typename T> T parse_set_argument(const char *name) {
    T result;
    // Parse from current till end
    while (next_index < arguments.size()) {
      const auto next = arguments[next_index];
      if (is_optional_field(next) || std::string{next} == "--" ||
          is_delimited_optional_argument(next).first) {
        if (std::string{next} == "--") {
          double_dash_encountered = true;
          next_index += 1;
        }
        // this marks the end of the container (break here)
        break;
      }
      auto [value, success] = parse_argument<typename T::value_type>(name);
      if (success) {
        result.insert(value);
      }
    }
    return result;
  }

  // Enum class
  template <typename T> T parse_enum_argument(const char *name) {
    T result;
    auto maybe_enum_value = magic_enum::enum_cast<T>(arguments[next_index]);
    if (maybe_enum_value.has_value()) {
      result = maybe_enum_value.value();
    } else {
      constexpr auto allowed_names = magic_enum::enum_names<T>();

      std::string allowed_names_string = "";
      if (allowed_names.size()) {
        for (size_t i = 0; i < allowed_names.size() - 1; i++) {
          allowed_names_string += std::string{allowed_names[i]} + ", ";
        }
        allowed_names_string += allowed_names[allowed_names.size() - 1];
      }

      throw structopt::exception("Error: unexpected input `" + std::string{arguments[next_index]} +
                                     "` provided for enum argument `" + std::string{name} +
                                     "`. Allowed values are {" + allowed_names_string + "}",
                                 visitor);
      // TODO: Throw error invalid enum option
    }
    return result;
  }

  // Visitor function for nested struct
  template <typename T>
  inline typename std::enable_if<visit_struct::traits::is_visitable<T>::value, void>::type
  operator()(const char *name, T &value) {
    if (next_index > current_index) {
      current_index = next_index;
    }

    if (current_index < arguments.size()) {
      const auto next = arguments[current_index];
      const auto field_name = std::string{name};

      // Check if `next` is the start of a subcommand
      if (visitor.is_field_name(next) && next == field_name) {
        next_index += 1;
        value = parse_nested_struct<T>(name);
      }
    }
  }

  // Visitor function for any positional field (not std::optional)
  template <typename T>
  inline typename std::enable_if<!structopt::is_specialization<T, std::optional>::value &&
                                     !visit_struct::traits::is_visitable<T>::value,
                                 void>::type
  operator()(const char *name, T &result) {
    if (next_index > current_index) {
      current_index = next_index;
    }

    if (current_index < arguments.size()) {
      const auto next = arguments[current_index];

      if (is_optional(next)) {
        return;
      }

      if (visitor.positional_field_names.empty()) {
        // We're not looking to save any more positional fields
        // all of them already have a value
        // TODO: Report error, unexpected argument
        return;
      }

      const auto field_name = visitor.positional_field_names.front();

      // // This will be parsed as a subcommand (nested struct)
      // if (visitor.is_field_name(next) && next == field_name) {
      //   return;
      // }

      if (field_name != std::string{name}) {
        // current field is not the one we want to parse
        return;
      }

      // Remove from the positional field list as it is about to be parsed
      visitor.positional_field_names.pop_front();

      auto [value, success] = parse_argument<T>(field_name.c_str());
      if (success) {
        result = value;
      } else {
        // positional field does not yet have a value
        visitor.positional_field_names.push_front(field_name);
      }
    }
  }

  // Visitor function for std::optional field
  template <typename T>
  inline typename std::enable_if<structopt::is_specialization<T, std::optional>::value, void>::type
  operator()(const char *name, T &value) {
    if (next_index > current_index) {
      current_index = next_index;
    }

    if (current_index < arguments.size()) {
      const auto next = arguments[current_index];
      const auto field_name = std::string{name};

      if (next == "--" && double_dash_encountered == false) {
        double_dash_encountered = true;
        next_index += 1;
        return;
      }

      // if `next` looks like an optional argument
      // i.e., starts with `-` or `--`
      // see if you can find an optional field in the struct with a matching name

      // check if the current argument looks like it could be this optional field
      if (double_dash_encountered == false && is_optional_field(next, field_name)) {

        // this is an optional argument matching the current struct field
        if constexpr (std::is_same<typename T::value_type, bool>::value) {
          // It is a boolean optional argument
          // Does it have a default value?
          // If yes, this is a FLAG argument, e.g,, "--verbose" will set it to true if the
          // default value is false No need to write "--verbose true"
          if (value.has_value()) {
            // The field already has a default value!
            value = !value.value(); // simply toggle it
            next_index += 1;
          } else {
            // boolean optional argument doesn't have a default value
            // expect one
            value = parse_optional_argument<typename T::value_type>(name);
          }
        } else {
          // Not std::optional<bool>
          // Parse the argument type <T>
          value = parse_optional_argument<typename T::value_type>(name);
        }
      } else {
        if (double_dash_encountered == false) {

          // maybe this is an optional argument that is delimited with '=' or ':'
          // e.g., --foo=bar or --foo:BAR
          if (next.size() > 1 && next[0] == '-') {
            const auto [success, delimiter] = is_delimited_optional_argument(next);
            if (success) {
              const auto [lhs, rhs] = split_delimited_argument(delimiter, next);
              // update next_index and return
              // the parser will take care of the rest

              // if `lhs` is an optional argument (i.e., maps to an optional field in the original
              // struct), then insert into arguments list
              auto potential_field_name = get_full_optional_field_name(lhs);
              if (potential_field_name.has_value()) {
                for (auto &arg : {rhs, lhs}) {
                  const auto begin = arguments.begin();
                  arguments.insert(begin + next_index + 1, arg);
                }
              }
              // get past the current argument, e.g., `--foo=bar`
              next_index += 1;
              return;
            }
          }

          // A direct match of optional argument with field_name has not happened
          // This _could_ be a combined argument
          // e.g., -abc => -a, -b, and -c where each of these is a flag argument

          std::vector<std::string> potential_combined_argument;

          if (is_optional_field(next) == false && next[0] == '-' &&
              (next.size() > 1 && next[1] != '-')) {
            for (std::size_t i = 1; i < next.size(); i++) {
              potential_combined_argument.push_back("-" + std::string(1, next[i]));
            }
          }

          if (!potential_combined_argument.empty()) {
            bool is_combined_argument = true;
            for (auto &arg : potential_combined_argument) {
              if (!is_optional_field(arg)) {
                is_combined_argument = false;
                // TODO: report error unrecognized option in combined argument
              }
            }

            if (is_combined_argument) {

              // check and make sure the current field_name is
              // in `potential_combined_argument`
              //
              // Let's say the argument `next` is `-abc`
              // the current field name is `b`
              // 1. Split `-abc` into `-a`, `-b`, and `-c`
              // 2. Check if `-b` is in the above list
              //    1. If yes, consider this as a combined argument
              //       push the list of arguments (split up) into `arguments`
              //    2. If no, nothing to do here
              bool field_name_matched = false;
              for (auto &arg : potential_combined_argument) {
                if (arg == "-" + std::string(1, field_name[0])) {
                  field_name_matched = true;
                }
              }

              if (field_name_matched) {
                // confirmed: this is a combined argument

                // insert the individual options that make up the combined argument
                // right after the combined argument
                // e.g., ""./main -abc" becomes "./main -abc -a -b -c"
                // Once this is done, increment `next_index` so that the parser loop will
                // service
                // `-a`, `-b` and `-c` like any other optional arguments (flags and
                // otherwise)
                for (std::vector<std::string>::reverse_iterator it =
                         potential_combined_argument.rbegin();
                     it != potential_combined_argument.rend(); ++it) {
                  auto &arg = *it;
                  if (next_index < arguments.size()) {
                    auto begin = arguments.begin();
                    arguments.insert(begin + next_index + 1, arg);
                  } else {
                    arguments.push_back(arg);
                  }
                }

                // get past the current combined argument
                next_index += 1;
              }
            }
          }
        }
      }
    }
  }
};

// Specialization for std::string
template <> inline std::string parser::parse_single_argument<std::string>(const char *) {
  return arguments[next_index];
}

// Specialization for bool
// yes, YES, on, 1, true, TRUE, etc. = true
// no, NO, off, 0, false, FALSE, etc. = false
// Converts argument to lower case before check
template <> inline bool parser::parse_single_argument<bool>(const char *name) {
  if (next_index > current_index) {
    current_index = next_index;
  }

  if (current_index < arguments.size()) {
    const std::vector<std::string> true_strings{"on", "yes", "1", "true"};
    const std::vector<std::string> false_strings{"off", "no", "0", "false"};
    std::string current_argument = arguments[current_index];

    // Convert argument to lower case
    std::transform(current_argument.begin(), current_argument.end(), current_argument.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Detect if argument is true or false
    if (std::find(true_strings.begin(), true_strings.end(), current_argument) !=
        true_strings.end()) {
      return true;
    } else if (std::find(false_strings.begin(), false_strings.end(), current_argument) !=
               false_strings.end()) {
      return false;
    } else {
      throw structopt::exception("Error: failed to parse boolean argument `" + std::string{name} +
                                     "`." + " `" + current_argument + "`" + " is invalid.",
                                 visitor);
      return false;
    }
  } else {
    return false;
  }
}

} // namespace details

} // namespace structopt

#pragma once
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
// #include <structopt/is_stl_container.hpp>
// #include <structopt/parser.hpp>
// #include <structopt/third_party/visit_struct/visit_struct.hpp>
#include <type_traits>
#include <vector>

#define STRUCTOPT VISITABLE_STRUCT

namespace structopt {

class app {
  details::visitor visitor;

public:
  explicit app(const std::string &name, const std::string &version = "") : visitor(name, version) {}

  template <typename T> T parse(const std::vector<std::string> &arguments) {
    T argument_struct = T();

    // Visit the struct and save flag, optional and positional field names
    visit_struct::for_each(argument_struct, visitor);

    // add `help` and `version` optional arguments
    visitor.optional_field_names.push_back("help");
    visitor.optional_field_names.push_back("version");

    // Construct the argument parser
    structopt::details::parser parser;
    parser.visitor = visitor;
    parser.arguments = arguments;

    for (std::size_t i = 1; i < parser.arguments.size(); i++) {
      parser.current_index = i;
      visit_struct::for_each(argument_struct, parser);
    }

    // directly call the parser to check for `help` and `version` flags
    std::optional<bool> help = false, version = false;
    for (std::size_t i = 1; i < parser.arguments.size(); i++) {
      parser.operator()("help", help);
      parser.operator()("version", version);

      if (help == true) {
        // if help is requested, print help and exit
        visitor.print_help(std::cout);
        exit(EXIT_SUCCESS);
      } else if (version == true) {
        // if version is requested, print version and exit
        std::cout << visitor.version << "\n";
        exit(EXIT_SUCCESS);
      }
    }

    // if all positional arguments were provided
    // this list would be empty
    if (!parser.visitor.positional_field_names.empty()) {
      for (auto &field_name : parser.visitor.positional_field_names) {
        if (std::find(parser.visitor.vector_like_positional_field_names.begin(),
                      parser.visitor.vector_like_positional_field_names.end(),
                      field_name) == parser.visitor.vector_like_positional_field_names.end()) {
          // this positional argument is not a vector-like argument
          // it expects value(s)
          throw structopt::exception("Error: expected value for positional argument `" +
                                         field_name + "`.",
                                     parser.visitor);
        }
      }
    }

    return argument_struct;
  }

  template <typename T> T parse(int argc, char *argv[]) {
    std::vector<std::string> arguments;
    std::copy(argv, argv + argc, std::back_inserter(arguments));
    return parse<T>(arguments);
  }

  std::string help() const {
    std::stringstream os;
    visitor.print_help(os);
    return os.str();
  }
};

} // namespace structopt


#pragma once
// #include <criterion/details/indicators.hpp>
#include <sstream>
#include <string>

static inline void print_criterion_help(const std::string &program_name) {
  std::cout << "\n";
  std::cout << termcolor::bold << "NAME\n" << termcolor::reset;
  std::cout << "     " << termcolor::bold << program_name << termcolor::reset
            << " -- Run Criterion benchmarks\n";
  std::cout << "\n";
  std::cout << termcolor::bold << "SYNOPSIS\n" << termcolor::reset;
  std::cout << termcolor::bold << "     " << program_name << "\n           " << termcolor::reset
            << "[" << termcolor::bold << "-w,--warmup" << termcolor::reset << " <number>]\n"
            << "           [" << termcolor::bold << "-l,--list" << termcolor::reset << "] "
            << "[" << termcolor::bold << "--list_filtered" << termcolor::reset << " <regex>] "
            << "[" << termcolor::bold << "-r,--run_filtered" << termcolor::reset << " <regex>] "
            << "\n           [" << termcolor::bold << "-e,--export_results" << termcolor::reset
            << " {csv,json,md,asciidoc} <filename>]\n"
            << "           [" << termcolor::bold << "-q,--quiet" << termcolor::reset << "] "
            << "[" << termcolor::bold << "-h,--help" << termcolor::reset << "] ";
  std::cout << "\n";
  std::cout << termcolor::bold << "DESCRIPTION\n" << termcolor::reset;
  std::cout
      << "     This microbenchmarking utility repeatedly executes a list of benchmarks,\n   "
         "  statistically analyzing and reporting on the temporal behavior of the executed code.\n";
  std::cout << "\n";
  std::cout << "     The options are as follows:\n";
  std::cout << "\n";
  std::cout << termcolor::bold << "     -w,--warmup " << termcolor::reset << termcolor::underline
            << "number" << termcolor::reset << "\n";
  std::cout << "          Number of warmup runs (at least 1) to execute before the benchmark "
               "(default=3)\n";
  std::cout << "\n";
  std::cout << termcolor::bold << "     -l,--list " << termcolor::reset << "\n";
  std::cout << "          Print the list of available benchmarks\n";
  std::cout << "\n";
  std::cout << termcolor::bold << "     --list_filtered " << termcolor::reset
            << termcolor::underline << "regex" << termcolor::reset << "\n";
  std::cout
      << "          Print a filtered list of available benchmarks (based on user-provided regex)\n";
  std::cout << "\n";
  std::cout << termcolor::bold << "     -r,--run_filtered " << termcolor::reset << termcolor::underline
            << "regex" << termcolor::reset << "\n";
  std::cout
      << "          Run a filtered list of available benchmarks (based on user-provided regex)\n";
  std::cout << "\n";
  std::cout << termcolor::bold << "     -e,--export_results " << termcolor::reset
            << termcolor::underline << "format" << termcolor::reset << " " << termcolor::underline
            << "filename" << termcolor::reset << "\n";
  std::cout
      << "          Export benchmark results to file. The following are the supported formats.\n";
  std::cout << "\n";
  std::cout << "          " << termcolor::bold << "csv" << termcolor::reset
            << "       Comma separated values (CSV) delimited text file\n";
  std::cout << "          " << termcolor::bold << "json" << termcolor::reset
            << "      JavaScript Object Notation (JSON) text file\n";
  std::cout << "          " << termcolor::bold << "md" << termcolor::reset
            << "        Markdown (md) text file\n";
  std::cout << "          " << termcolor::bold << "asciidoc" << termcolor::reset
            << "  AsciiDoc (asciidoc) text file\n";
  std::cout << "\n";
  std::cout << termcolor::bold << "     -q,--quiet " << termcolor::reset << "\n";
  std::cout << "          Run benchmarks quietly, suppressing activity indicators\n";
  std::cout << "\n";
  std::cout << termcolor::bold << "     -h,--help " << termcolor::reset << "\n";
  std::cout << "          Print this help message\n";
}

#pragma once
// #include <criterion/details/asciidoc_writer.hpp>
// #include <criterion/details/csv_writer.hpp>
// #include <criterion/details/help.hpp>
// #include <criterion/details/indicators.hpp>
// #include <criterion/details/json_writer.hpp>
// #include <criterion/details/macros.hpp>
// #include <criterion/details/md_writer.hpp>
// #include <criterion/details/structopt.hpp>
#include <cstring>

static inline void signal_handler(int signal) {
  indicators::show_console_cursor(true);
  std::cout << termcolor::reset;
  exit(signal);
}

namespace criterion {

struct options {

  struct export_options : structopt::sub_command {
    enum class format_type { csv, json, md, asciidoc };

    // Export format
    format_type format;

    // Export filename
    std::string filename;
  };

  // Number of warmup runs to perform on benchmark
  std::optional<std::size_t> warmup;

  // List available benchmarks
  std::optional<bool> list = false;

  // List available benchmarks, filtered by user-provided regex string
  std::optional<std::string> list_filtered;

  // Run filtered benchmarks, filtered by user-provided regex string
  std::optional<std::string> run_filtered;

  // --export_results csv result.csv
  // --export_results json foo.json
  std::optional<export_options> export_results;

  // Run benchmarks quietly
  std::optional<bool> quiet = false;

  // Prints help
  std::optional<bool> help = false;

  // Remaining arguments
  std::vector<std::string> remaining;
};

} // namespace criterion

STRUCTOPT(criterion::options::export_options, format, filename);
STRUCTOPT(criterion::options, warmup, list, list_filtered, run_filtered, export_results, quiet,
          help, remaining);

static inline int criterion_main(int argc, char *argv[]) {
  const auto program_name = argv[0];

  std::signal(SIGTERM, signal_handler);
  std::signal(SIGSEGV, signal_handler);
  std::signal(SIGINT, signal_handler);
  std::signal(SIGILL, signal_handler);
  std::signal(SIGABRT, signal_handler);
  std::signal(SIGFPE, signal_handler);

  try {
    auto options = structopt::app(program_name).parse<criterion::options>(argc, argv);

    if (options.help.value() == true) {
      print_criterion_help(program_name);
      exit(0);
    } else if (options.list.value() == true) {
      criterion::benchmark_registration_helper_struct::list_registered_benchmarks();
      exit(0);
    } else if (options.list_filtered.has_value()) {
      criterion::benchmark_registration_helper_struct::list_filtered_registered_benchmarks(
          options.list_filtered.value());
      exit(0);
    } else if (!options.remaining.empty()) {
      std::cout << termcolor::bold << termcolor::red;
      std::cout << "Error: Unrecognized argument \"";
      std::cout << options.remaining[0];
      std::cout << "\"" << termcolor::reset << "\n";
      exit(1);
    }

    if (options.warmup.has_value()) {
      const auto warmup = options.warmup.value();
      if (warmup > 0) {
        criterion::benchmark::warmup_runs = options.warmup.value();
      } else {
        criterion::benchmark::warmup_runs = 1;
      }
    }

    if (options.quiet.value() == true) {
      criterion::benchmark::show_console_output = false;
    }

    // Run benchmarks
    if (options.run_filtered.has_value()) { // Run filtered
      criterion::benchmark_registration_helper_struct::execute_filtered_registered_benchmarks(
          options.run_filtered.value());
    } else {
      criterion::benchmark_registration_helper_struct::execute_registered_benchmarks();
    }

    if (options.export_results.has_value()) {
      auto export_options = options.export_results.value();
      if (export_options.format == criterion::options::export_options::format_type::csv) {
        // CSV export
        criterion::csv_writer::write_results(export_options.filename,
                                             criterion::benchmark::results);
      } else if (export_options.format == criterion::options::export_options::format_type::json) {
        // JSON export
        criterion::json_writer::write_results(export_options.filename,
                                              criterion::benchmark::results);
      } else if (export_options.format == criterion::options::export_options::format_type::md) {
        // Markdown export
        criterion::md_writer::write_results(export_options.filename, criterion::benchmark::results);
      } else if (export_options.format ==
                 criterion::options::export_options::format_type::asciidoc) {
        // Markdown export
        criterion::asciidoc_writer::write_results(export_options.filename,
                                                  criterion::benchmark::results);
      }
    }

  } catch (structopt::exception &e) {
    const auto message = e.what();
    if (message && std::strlen(message) > 0)
      std::cout << termcolor::bold << termcolor::red << message << termcolor::reset << "\n";
    print_criterion_help(program_name);
    exit(1);
  }
  return 0;
}

#define CRITERION_BENCHMARK_MAIN(...)                                                              \
  int main(int argc, char *argv[]) { criterion_main(argc, argv); }

  

