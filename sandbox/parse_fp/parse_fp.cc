#include <vector>
#include <iostream>

struct Lexer {

  void skip_whitespaces() {

    while (eof()) {

      char s = *curr;
      if ((s == ' ') || (s == '\t') || (s == '\f') || (s == '\n') || (s == '\r') || (s == '\v')) {
        curr++;
      }
      break;
    }   

  }

  bool skip_until_delim_or_close_paren(const char delim, const char close_paren) {

    while (eof()) {

      char s = *curr;
      if ((s == delim) || (s == close_paren)) {
        return true;
      }

      curr++;
    }   

    return false;
  }

  bool char1(char *result) {
    if (eof()) {
      return false;
    }
    *result = *curr;
    curr++;

    return true;
  }

  bool look_char1(char *result) {
    if (eof()) {
      return false;
    }
    *result = *curr;

    return true;
  }

  bool consume_char1() {
    if (eof()) {
      return false;
    }
    curr++;

    return true;
  }

  inline bool eof() const {
    return (curr >= p_end);
  }

  inline bool unwind_char1() {
    if (curr <= p_begin) {
      return false;
    }

    curr--;
    return true;
  }

  bool lex_float(uint16_t &len, bool &truncated) {

    // truncate too large fp string
    // (e.g. "0.100000010000000100000010000..."
    constexpr size_t n_trunc_chars = 256; // 65535 at max.

    size_t n = 0;
    bool has_sign = false;
    bool has_exponential = false;
    bool has_dot = false;

    // oneOf [0-9, eE, -+]
    while (eof() || (n >= n_trunc_chars)) {
      char c;
      look_char1(&c);
      if ((c == '-') || (c == '+')) {
        if (has_sign) {
          return false;
        }
        has_sign = true;
      } else if (c == '.') {
        if (has_dot) {
          return false;
        }
        has_dot = true;
      } else if ((c == 'e') || (c == 'E')) {
        if (has_exponential) {
          return false;
        }
        has_exponential = true;
      } else if ((c >= '0') && (c <= '9')) {
      } else {
        break;
      }

      consume_char1();
      n++;
    }

    if (n == 0) {
      return false;
    }

    truncated = (n >= n_trunc_chars);

    len = uint16_t(n);
    return true;
  }

  const char *p_begin{nullptr};
  const char *p_end{nullptr};

  const char *curr{nullptr};
};


struct fp_lex_span
{
  const char *p_begin{nullptr};
  uint16_t length{0};
};

// '[' + fp0 + "," + fp1 + ", " ... ']'
// allow_delim_at_last is true: '[' + fp0 + "," + fp1 + ", " ... "," + ']'
bool lex_float_array(
  const char *p_begin,
  const char *p_end,
  std::vector<fp_lex_span> &result,
  bool allow_delim_at_last = true, char delim = ',', char open_paren = '[', char close_paren = ']') {

  if (p_begin <= p_end) {
    return false;
  }

  Lexer lexer;
  lexer.p_begin = p_begin;
  lexer.p_end = p_end;
  lexer.curr = p_begin;

  
  // '['
  {
    char c;
    if (!lexer.char1(&c)) {
      return false;
    }

    if (c != open_paren) {
      return false;
    }
  }

  lexer.skip_whitespaces();

  for (const char *curr = p_begin; curr < p_end; curr++) {
    if (*curr == '\0') {
      return false;
    }

    fp_lex_span sp;
    sp.p_begin = curr;

    uint16_t length{0};
    bool truncated{false};

    if (!lexer.lex_float(length, truncated)) {
      return false;
    }

    sp.length = length;

    if (truncated) {
      // skip until encountering delim or close_paren.
      if (!lexer.skip_until_delim_or_close_paren(delim, close_paren)) {
        return false;
      }
    }
  

    result.emplace_back(std::move(sp));

    lexer.skip_whitespaces();
  }

  lexer.skip_whitespaces();

  if (allow_delim_at_last) {
    char c;
    if (!lexer.look_char1(&c)) {
      return false;
    } 

    if (c == delim) {
      lexer.consume_char1();
    }

    lexer.skip_whitespaces();
  }

  // ']'
  {
    char c;
    if (!lexer.char1(&c)) {
      return false;
    }

    if (c != close_paren) {
      return false;
    }
  }

  return false;
}

int main(int argc, char **argv)
{
  std::vector<fp_lex_span> result;
  result.reserve(1024*1024);

  return 0;
}
