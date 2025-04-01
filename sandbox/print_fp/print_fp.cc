#include <vector>
#include <iostream>
#include <sstream>
#include <chrono>
#include <cstring>

#include <random>

#include "dragonbox_to_chars.h"
#include "dtoa_milo.h"

std::vector<float> gen_floats(size_t n) {

  std::vector<float> dst;
  dst.resize(n);

  std::random_device rd;

  std::mt19937 engine(rd());
  std::uniform_real_distribution<> dist(-0.001, 0.01);

  for (size_t i = 0; i < n; i++) {
    double f = dist(engine);
    dst[i] = float(f);
  }

  return dst;
}


//
// based on fmtlib
// 

// TOOD: Use builtin_clz insturction 
// T = uint32 or uint64
template<typename T>
inline int count_digits(T n) {
  int count = 1;
  for (;;) {
    // Integer division is slow so do it for a group of four digits instead
    // of for every digit. The idea comes from the talk by Alexandrescu
    // "Three Optimization Tips for C++". See speed-test for a comparison.
    if (n < 10) return count;
    if (n < 100) return count + 1;
    if (n < 1000) return count + 2;
    if (n < 10000) return count + 3;
    n /= 10000u;
    count += 4;
  }
}

  // Writes a two-digit value to out.
  inline void write2digits(char* out, size_t value) {
    //if (!is_constant_evaluated() && std::is_same<Char, char>::value &&
    //    !FMT_OPTIMIZE_SIZE) {
    //  memcpy(out, digits2(value), 2);
    //  return;
    //}
    *out++ = static_cast<char>('0' + value / 10);
    *out = static_cast<char>('0' + value % 10);
  }

inline char *format_decimal(char* out, uint64_t value, uint32_t size) {
    //FMT_ASSERT(size >= count_digits(value), "invalid digit count");
    unsigned n = size;
    while (value >= 100) {
      // Integer division is slow so do it for a group of two digits instead
      // of for every digit. The idea comes from the talk by Alexandrescu
      // "Three Optimization Tips for C++". See speed-test for a comparison.
      n -= 2;
      write2digits(out + n, static_cast<unsigned>(value % 100));
      value /= 100;
    }
    if (value >= 10) {
      n -= 2;
      write2digits(out + n, static_cast<unsigned>(value));
    } else {
      out[--n] = static_cast<char>('0' + value);
    }
    return out + n;
  }

inline char* write_significand(char* out, uint64_t significand, int significand_size,
                                int integral_size, char decimal_point) {
    if (!decimal_point) return format_decimal(out, significand, significand_size);
    out += significand_size + 1;
    char* end = out;
    int floating_size = significand_size - integral_size;
    for (int i = floating_size / 2; i > 0; --i) {
      out -= 2;
      write2digits(out, static_cast<std::size_t>(significand % 100));
      significand /= 100;
    }
    if (floating_size % 2 != 0) {
      *--out = static_cast<char>('0' + significand % 10);
      significand /= 10;
    }
    *--out = decimal_point;
    format_decimal(out - integral_size, significand, integral_size);
    return end;
  }

char *write_float(const float f, char *buf)
{

  bool is_negative = std::signbit(f);

  auto ret = jkj::dragonbox::to_decimal(f);

  // print human-readable float for the value in range [1e-4, 1e+16]
  const int exp_lower = -4;
  const int exp_upper = 16; // (15 + 1) for double, (6+1) for float
  char exp_char = 'e';

  auto significand = ret.significand;
  int significand_size = count_digits(significand);

  size_t size = size_t(significand_size) + (is_negative ? 1u : 0u);

  int output_exp = ret.exponent + significand_size - 1;
  bool use_exp_format = (output_exp < exp_lower) || (output_exp >= exp_upper); 
  
  char decimal_point = '.';
  if (use_exp_format) {
    int num_zeros = 0;
    if (significand_size == 1) {
      decimal_point = '\0';
    }
      auto abs_output_exp = output_exp >= 0 ? output_exp : -output_exp;
      int exp_digits = 2;
      if (abs_output_exp >= 100) exp_digits = abs_output_exp >= 1000 ? 4 : 3;

    size += (decimal_point ? 1u : 0u) + 2u + size_t(exp_digits);

    if (is_negative) {
      *buf++ = '-'; 
      buf = write_significand(buf, significand, significand_size, 1, decimal_point);
    }
  } else {
  }
  
  // TODO
  return nullptr;
}

#if 0
std::string print_floats(const std::vector<float> &v) {
  
  char buffer[40];

  size_t n = v.size();
  std::vector<char> dst;
  dst.reserve(n * 10); // 10 : heuristics.

  size_t curr = 0;
  for (size_t i = 0; i < v.size(); i++) {

    if (i > 0) {
      dst[curr] =  ',';
      dst[curr+1] =  ' ';
      curr += 2;
    }

    char *e = jkj::dragonbox::to_chars(v[i], buffer);
    size_t len = e - buffer; // includes '\0'
    std::cout << len << "\n";

    // +2 for ', '
    if ((curr + len + 2) >= dst.size()) {
      dst.resize((curr + len) + 2);
    }

    memcpy(dst.data() + curr, buffer, len);

    curr += len;
  }
  dst[curr] = '\n';
  std::string s(dst.data(), curr);
  return s;
}
#endif

std::string print_floats(const std::vector<float> &v) {
  
  char buffer[25];

  size_t n = v.size();
  std::vector<char> dst;
  dst.reserve(n * 10); // 10 : heuristics.

  size_t curr = 0;
  for (size_t i = 0; i < v.size(); i++) {

    if (i > 0) {
      dst[curr] =  ',';
      dst[curr+1] =  ' ';
      curr += 2;
    }

    char *e = dtoa_milo(v[i], buffer);
    size_t len = e - buffer; // includes position of '\0'

    // +2 for ', '
    if ((curr + len + 2) >= dst.size()) {
      dst.resize((curr + len) + 2);
    }

    memcpy(dst.data() + curr, buffer, len);

    curr += len;
  }
  dst[curr] = '\n';
  std::string s(dst.data(), curr);
  return s;
}

int main(int argc, char **argv)
{
  bool delim_at_end = true;
  size_t n = 1024*1024*16;
  if (argc > 1) {
    n = std::stoi(argv[1]);
  }
  if (argc > 2) {
    delim_at_end = std::stoi(argv[2]) > 0;
  }

  double d = 1.0;
  for (size_t i = 0; i < 32; i++) {
    char buf[25];
    char *p = dtoa_milo(d, buf);
    *p = '\0';
    std::cout << buf << "\n";

    {
      auto ret = jkj::dragonbox::to_decimal(d);
      std::cout << "to_decimal " << ret.significand << "\n";
      std::cout << "to_decimal " << ret.exponent << "\n";
    }

    {
      char db_buf[40];
      auto ret = jkj::dragonbox::to_chars(d, db_buf);
      std::cout << "to_chars " << db_buf << "\n";
    }

    {
      char buffer[25];
      int length, K;
      Grisu2(d, buffer, &length, &K);
      std::cout << "grisu len " << length << "\n";
      std::cout << "grisu K " << K << "\n";
      std::cout << "grisu " << buffer << "\n";
    }

    d = d * 10.0;

  }

  std::vector<float> arr =  gen_floats(n);
  //std::cout << input << "\n";

  auto start = std::chrono::steady_clock::now();

  std::string s = print_floats(arr);
  auto end = std::chrono::steady_clock::now();

  std::cout << "n elems " << arr.size() << "\n";

  std::cout << "print : " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " [ms]\n";

  //std::cout << s << "\n";

  return 0;
}
