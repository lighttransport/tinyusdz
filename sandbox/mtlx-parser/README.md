# MaterialX Parser

A secure, dependency-free, C++14 XML parser specifically designed for MaterialX documents. This parser replaces pugixml in TinyUSDZ's usdMtlx implementation.

## Features

- **Security-focused**: Built-in bounds checking, memory limits, and no buffer overflows
- **No dependencies**: Pure C++14 implementation without external libraries
- **MaterialX-specific**: Optimized for parsing MaterialX documents
- **Drop-in replacement**: Provides pugixml-compatible adapter for easy migration
- **Fast and lightweight**: Minimal memory footprint

## Architecture

The parser consists of several layers:

1. **XML Tokenizer** (`mtlx-xml-tokenizer.hh/cc`): Low-level tokenization with security limits
2. **Simple Parser** (`mtlx-simple-parser.hh/cc`): Builds a lightweight DOM tree
3. **MaterialX DOM** (`mtlx-dom.hh/cc`): MaterialX-specific document object model
4. **USD Adapter** (`mtlx-usd-adapter.hh`): pugixml-compatible interface for usdMtlx

## Usage

### Basic Parsing

```cpp
#include "mtlx-simple-parser.hh"

tinyusdz::mtlx::SimpleXMLParser parser;
if (parser.Parse(xml_string)) {
    auto root = parser.GetRoot();
    // Process the document...
}
```

### Using the pugixml-compatible Adapter

```cpp
#include "mtlx-usd-adapter.hh"

// Use like pugixml
tinyusdz::mtlx::pugi::xml_document doc;
tinyusdz::mtlx::pugi::xml_parse_result result = doc.load_string(xml);

if (result) {
    tinyusdz::mtlx::pugi::xml_node root = doc.child("materialx");
    // Process nodes...
}
```

### MaterialX DOM

```cpp
#include "mtlx-dom.hh"

tinyusdz::mtlx::MtlxDocument doc;
if (doc.ParseFromFile("material.mtlx")) {
    for (const auto& shader : doc.GetNodes()) {
        std::cout << "Shader: " << shader->GetName() << std::endl;
    }
}
```

## Security Features

- Maximum name length: 256 characters
- Maximum string length: 64KB
- Maximum text content: 1MB
- Maximum nesting depth: 1000 levels
- No dynamic memory allocation beyond limits
- Safe entity handling (HTML entities)
- No external file access

## Building

```bash
mkdir build && cd build
cmake ..
make
```

Run tests:
```bash
./test_parser
./test_adapter
```

Parse a MaterialX file:
```bash
./parse_mtlx material.mtlx
```

## Integration with usdMtlx

To replace pugixml in usdMtlx:

1. Include `mtlx-usd-adapter.hh` instead of `pugixml.hpp`
2. Use the namespace aliases provided
3. The API is compatible with basic pugixml usage

Example migration:
```cpp
// Before (with pugixml)
#include "pugixml.hpp"
pugi::xml_document doc;
pugi::xml_parse_result result = doc.load_string(xml);

// After (with mtlx-parser)
#include "mtlx-usd-adapter.hh"
tinyusdz::mtlx::pugi::xml_document doc;
tinyusdz::mtlx::pugi::xml_parse_result result = doc.load_string(xml);
```

## Limitations

- Supports MaterialX 1.36, 1.37, and 1.38
- XML namespaces are not fully supported (MaterialX doesn't use them)
- No XPath support (not needed for MaterialX)
- Read-only parsing (no DOM manipulation)

## License

Apache 2.0