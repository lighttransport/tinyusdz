// Debug test for XML parser
#include <iostream>
#include "mtlx-simple-parser.hh"
#include "mtlx-xml-tokenizer.hh"

const char* simple_xml = R"(<?xml version="1.0"?>
<root>
  <child attr="value">text</child>
</root>
)";

int main() {
  // Test tokenizer first
  std::cout << "=== Tokenizer Test ===\n";
  tinyusdz::mtlx::XMLTokenizer tokenizer;
  tokenizer.Initialize(simple_xml, std::strlen(simple_xml));

  tinyusdz::mtlx::Token token;
  int count = 0;
  while (tokenizer.NextToken(token) && count++ < 20) {
    std::cout << count << ". Type=" << static_cast<int>(token.type)
              << " Name='" << token.name << "' Value='" << token.value << "'\n";

    if (token.type == tinyusdz::mtlx::TokenType::EndOfDocument) {
      break;
    }
  }

  // Test parser
  std::cout << "\n=== Parser Test ===\n";
  tinyusdz::mtlx::SimpleXMLParser parser;
  if (parser.Parse(simple_xml)) {
    std::cout << "Parse successful!\n";
    auto root = parser.GetRoot();
    if (root) {
      std::cout << "Root name: " << root->name << "\n";
      std::cout << "Root children: " << root->children.size() << "\n";
    }
  } else {
    std::cout << "Parse failed: " << parser.GetError() << "\n";
  }

  return 0;
}
