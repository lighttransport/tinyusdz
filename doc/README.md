# PCP API Documentation

This directory contains the comprehensive PCP (Prim Composition Pipeline) API documentation for TinyUSDZ.

## Files

### `pcp.md`
- **Format**: Markdown
- **Size**: 53 KB
- **Contents**: Complete API reference for all 15 PCP modules including:
  - 7 Core Modules (Cache, PrimIndex, Node, MapFunction, LayerStack, Dependencies, ComposeSite)
  - 6 Advanced Modules (Changes, PathTranslation, Instancing, Diagnostics, DebugUtils, Performance)
  - 2 Specialized Modules (Threading, TimeSample)
  - Usage patterns and integration examples

### `pcp.html`
- **Format**: Single-page HTML with embedded CSS
- **Size**: 90 KB
- **Features**:
  - Fully self-contained (no external dependencies)
  - Responsive design with fixed sidebar navigation
  - Auto-generated table of contents with anchor links
  - Simple, clean CSS styling
  - C++ syntax highlighting for code examples
  - Mobile-friendly layout

## Viewing the Documentation

To view the HTML documentation:

1. **In a web browser**: Open `pcp.html` directly in any modern web browser
2. **From the command line**: 
   ```bash
   firefox pcp.html
   ```

## Generation Tools

### `md2html.py`
Custom markdown-to-HTML converter that:
- Parses markdown syntax (headers, lists, code blocks, tables, etc.)
- Generates a single-page HTML output with embedded CSS
- Creates a responsive navigation sidebar with auto-generated table of contents
- Supports C++ syntax highlighting
- Mobile-responsive design

Usage:
```bash
python3 ../scripts/md2html.py pcp.md pcp.html
```

## Documentation Structure

### Core Modules
- **pcp-cache**: Central caching system with BLAKE3-based instancing
- **pcp-prim-index**: Represents composition results as DAGs
- **pcp-node**: Individual composition nodes
- **pcp-map-function**: Path and value translation
- **pcp-layer-stack**: Local layer composition management
- **pcp-dependencies**: Composition dependency tracking
- **pcp-compose-site**: Composition sites and arcs

### Advanced Modules
- **pcp-changes**: Change notification and processing system
- **pcp-path-translation**: Advanced path mapping utilities
- **pcp-instancing**: Instance detection and optimization
- **pcp-diagnostics**: Debugging and validation tools
- **pcp-debug-utils**: Enhanced debugging and analysis
- **pcp-performance**: Performance monitoring and profiling

### Specialized Modules
- **pcp-threading**: Multi-threaded composition evaluation
- **pcp-timesample**: Time-based animation support

## Features Documented

- Class hierarchies and inheritance
- Constructor signatures and parameters
- Public method definitions with return types
- Data structures and type definitions
- Usage patterns and code examples
- API integration points
- Configuration options
- Performance characteristics

## Regenerating the Documentation

To regenerate the HTML from markdown:

```bash
cd <project-root>
python3 scripts/md2html.py doc/pcp.md doc/pcp.html
```

## License

This documentation is part of TinyUSDZ, available under the Apache 2.0 license.
