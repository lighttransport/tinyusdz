#include <iostream>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <cassert>
#include <stack>
#include <sstream>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <ryu/ryu.h>
#include <ryu/ryu_parse.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <stream-reader.hh>

namespace tinyusdz {

struct ErrorDiagnositc
{
  std::string err;
  int line_row = -1;
  int line_col = -1;
};

class USDAParser
{
 public:
  struct ParseState {
    int64_t loc{-1}; // byte location in StreamReder
  };

  USDAParser(tinyusdz::StreamReader *sr) : _sr(sr) {
  }

  bool SkipUntilNewline() {
    while (!_sr->eof()) {
      char c;
      if (!_sr->read1(&c)) {
        // this should not happen.
        return false;
      }

      if ((c == '\n') || (c == '\r')) {
        // continue
      } else {
        break;
      }
    }

    _line_row++;
    return true;
  }

  bool SkipWhitespace() {

    while (!_sr->eof()) {
      char c;
      if (!_sr->read1(&c)) {
        // this should not happen.
        return false;
      }

      if ((c == ' ') || (c == '\t') || (c == '\f')) {
        // continue
      } else {
        break;
      }
    }

    // unwind 1 char
    if (!_sr->seek_from_current(-1)) {
      return false;
    }

    return true;
  }

  // #usda FLOAT
  // `#` style comment
  bool ParseHeader() {
    SkipWhitespace();
    if (_sr->eof()) {
      return false;
    }

    {
      char magic[5];
      if (!_sr->read(5, 5, reinterpret_cast<uint8_t*>(magic))) {
        // eol
        return false;
      }

      if ((magic[0] == '#') && (magic[1] == 'u') &&
          (magic[2] == 's') && (magic[3] == 'd') &&
          (magic[4] == 'a')) {
        // ok
      } else {
        ErrorDiagnositc diag;
        diag.line_row = _line_row;
        diag.line_col = _line_col;
        diag.err = "Magic header must be `#usda` but got `" + std::string(magic, 5) + "`\n";
        err_stack.push(diag);

        return false;
      }
    }

    if (!SkipWhitespace()) {
      // eof
      return false;
    }

    // current we only accept "1.0"
    {
      char ver[3];
      if (!_sr->read(3, 3, reinterpret_cast<uint8_t*>(ver))) {
        return false;
      }

      if ((ver[0] == '1') && (ver[1] == '.') &&
          (ver[2] == '0')) {
        // ok
        _version = 1.0f;
      } else {
        ErrorDiagnositc diag;
        diag.line_row = _line_row;
        diag.line_col = _line_col;
        diag.err = "Version must be `1.0` but got `" + std::string(ver, 3) + "`\n";
        err_stack.push(diag);

        return false;
      }
    }

    SkipUntilNewline();

    return true;
  }

  // `#` style comment
  bool ParseSharpComment() {
    char c;
    if (!_sr->read1(&c)) {
      // eol
      return false;
    }

    if (c != '#') {
      return false;
    }

    return true;
  }

  bool Push() {

    // Stack size must be less than the number of input bytes.
    assert (parse_stack.size() < _sr->size());

    uint64_t loc = _sr->tell();

    ParseState state;
    state.loc = int64_t(loc);
    parse_stack.push(state);

    return true;
  }

  bool Pop(ParseState *state) {
    if (parse_stack.empty()) {
      return false;
    }

    (*state) = parse_stack.top();

    parse_stack.pop();

    return true; 
  }

  bool Parse() {
    bool ok{false};

    ok = ParseHeader();

    return ok;
  }

  std::string GetError() {
    if (err_stack.empty()) {
      return std::string();
    }

    ErrorDiagnositc diag = err_stack.top();

    std::stringstream ss;
    ss << "line " << diag.line_row << ", col " << diag.line_col << ": ";
    ss << diag.err << "\n";
    return ss.str();
  }

 private:
  const tinyusdz::StreamReader *_sr = nullptr;

  std::stack<ErrorDiagnositc> err_stack;
  std::stack<ParseState> parse_stack;

  int _line_row{0};
  int _line_col{0};

  float _version{1.0f};

};

}

int main(int argc, char **argv)
{
  if (argc < 2) {
    std::cout << "Need input.usda\n";
    exit(-1);
  }

  std::string filename = argv[1];

  std::vector<uint8_t> data;
  {
    // TODO(syoyo): Support UTF-8 filename
    std::ifstream ifs(filename.c_str(), std::ifstream::binary);
    if (!ifs) {
      std::cerr << "Failed to open file: " << filename << "\n";
      return -1;
    }

    // TODO(syoyo): Use mmap
    ifs.seekg(0, ifs.end);
    size_t sz = static_cast<size_t>(ifs.tellg());
    if (int64_t(sz) < 0) {
      // Looks reading directory, not a file.
      std::cerr << "Looks like filename is a directory : \"" << filename << "\"\n";
      return -1;
    }

    data.resize(sz);

    ifs.seekg(0, ifs.beg);
    ifs.read(reinterpret_cast<char *>(&data.at(0)),
             static_cast<std::streamsize>(sz));
  }

  tinyusdz::StreamReader sr(data.data(), data.size(), /* swap endian */false);
  tinyusdz::USDAParser parser(&sr);

  {
    bool ret = parser.Parse();

    if (!ret) {
      std::cerr << "Failed to parse .usda: \n";
      std::cerr << parser.GetError() << "\n";
    }

    std::cout << "ok\n";
  }  

  return 0;
}
