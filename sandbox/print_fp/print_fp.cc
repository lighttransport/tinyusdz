#include <vector>
#include <iostream>
#include <sstream>
#include <chrono>
#include <cstring>

#include <random>

#include "dragonbox_to_chars.h"

std::vector<float> gen_floats(size_t n) {

  std::vector<float> dst;
  dst.resize(n);

  std::random_device rd;

  std::mt19937 engine(rd());
  std::uniform_real_distribution<> dist(-10000.0, 10000.0);

  for (size_t i = 0; i < n; i++) {
    double f = dist(engine);
    dst[i] = float(f);
  }

  return dst;
}

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

int main(int argc, char **argv)
{
  bool delim_at_end = true;
  size_t n = 1024*1024*32;
  if (argc > 1) {
    n = std::stoi(argv[1]);
  }
  if (argc > 2) {
    delim_at_end = std::stoi(argv[2]) > 0;
  }

  std::vector<float> arr =  gen_floats(n);
  //std::cout << input << "\n";

  auto start = std::chrono::steady_clock::now();

  std::string s = print_floats(arr);
  auto end = std::chrono::steady_clock::now();

  std::cout << "n elems " << arr.size() << "\n";

  std::cout << "print : " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " [ms]\n";

  std::cout << s << "\n";

  return 0;
}
