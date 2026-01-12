#!/usr/bin/env node
/**
 * compare-usda.js
 *
 * A CLI tool to compare USDA (USD ASCII) outputs from tusdcat and usdcat.
 * Compares at the Prim and Attribute level, ignoring ordering differences.
 *
 * Usage:
 *   node compare-usda.js <file1.usda> <file2.usda>
 *   node compare-usda.js --tusdcat <tusdcat_path> --usdcat <usdcat_path> <input.usd>
 */

const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

/**
 * Simple glob pattern matcher
 * Supports: *, **, ?, [abc], [a-z]
 */
function globToRegex(pattern) {
  let regex = '';
  let i = 0;

  while (i < pattern.length) {
    const c = pattern[i];

    if (c === '*') {
      if (pattern[i + 1] === '*') {
        // ** matches any path including /
        if (pattern[i + 2] === '/' || pattern[i + 2] === path.sep) {
          regex += '(?:.*\\/)?';
          i += 3;
        } else {
          regex += '.*';
          i += 2;
        }
      } else {
        // * matches anything except /
        regex += '[^\\/]*';
        i++;
      }
    } else if (c === '?') {
      regex += '[^\\/]';
      i++;
    } else if (c === '[') {
      // Character class
      let j = i + 1;
      let classContent = '';
      while (j < pattern.length && pattern[j] !== ']') {
        classContent += pattern[j];
        j++;
      }
      regex += '[' + classContent + ']';
      i = j + 1;
    } else if (c === '{') {
      // Brace expansion {a,b,c}
      let j = i + 1;
      let options = [];
      let current = '';
      while (j < pattern.length && pattern[j] !== '}') {
        if (pattern[j] === ',') {
          options.push(current);
          current = '';
        } else {
          current += pattern[j];
        }
        j++;
      }
      options.push(current);
      regex += '(?:' + options.map(o => escapeRegex(o)).join('|') + ')';
      i = j + 1;
    } else if ('/\\^$.|+()'.includes(c)) {
      regex += '\\' + c;
      i++;
    } else {
      regex += c;
      i++;
    }
  }

  return new RegExp('^' + regex + '$');
}

function escapeRegex(str) {
  return str.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

/**
 * Check if a string contains glob pattern characters
 */
function isGlobPattern(str) {
  return /[*?[\]{}]/.test(str);
}

/**
 * Expand a glob pattern to matching file paths
 */
function expandGlob(pattern, baseDir = '.') {
  const results = [];
  const absolutePattern = path.isAbsolute(pattern) ? pattern : path.join(baseDir, pattern);

  // Find the first directory without glob characters
  const parts = absolutePattern.split(path.sep);
  let staticParts = [];
  let globParts = [];
  let inGlob = false;

  for (const part of parts) {
    if (inGlob || isGlobPattern(part)) {
      inGlob = true;
      globParts.push(part);
    } else {
      staticParts.push(part);
    }
  }

  const staticPath = staticParts.join(path.sep) || '/';
  const globPattern = globParts.join('/');

  if (!globPattern) {
    // No glob pattern, just check if file exists
    if (fs.existsSync(absolutePattern)) {
      return [absolutePattern];
    }
    return [];
  }

  const regex = globToRegex(globPattern);

  function walk(dir, relativePath = '') {
    if (!fs.existsSync(dir)) return;

    let entries;
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch (e) {
      return;
    }

    for (const entry of entries) {
      const entryRelPath = relativePath ? relativePath + '/' + entry.name : entry.name;
      const fullPath = path.join(dir, entry.name);

      if (entry.isDirectory()) {
        // Check if pattern could match in subdirectory
        if (globPattern.includes('**') || regex.test(entryRelPath + '/')) {
          walk(fullPath, entryRelPath);
        } else {
          // Check if directory name matches prefix of pattern
          const dirPattern = globPattern.split('/')[0];
          if (globToRegex(dirPattern).test(entry.name)) {
            walk(fullPath, entryRelPath);
          }
        }
      } else if (entry.isFile()) {
        if (regex.test(entryRelPath)) {
          results.push(fullPath);
        }
      }
    }
  }

  walk(staticPath);
  return results.sort();
}

/**
 * Expand glob patterns in file list
 */
function expandFilePatterns(patterns, baseDir = '.') {
  const files = [];

  for (const pattern of patterns) {
    if (isGlobPattern(pattern)) {
      const matched = expandGlob(pattern, baseDir);
      if (matched.length === 0) {
        console.warn(`Warning: No files matched pattern "${pattern}"`);
      }
      files.push(...matched);
    } else {
      files.push(pattern);
    }
  }

  // Remove duplicates
  return [...new Set(files)];
}

// USDA Token types
const TokenType = {
  IDENTIFIER: 'IDENTIFIER',
  STRING: 'STRING',
  NUMBER: 'NUMBER',
  LBRACE: 'LBRACE',
  RBRACE: 'RBRACE',
  LPAREN: 'LPAREN',
  RPAREN: 'RPAREN',
  LBRACKET: 'LBRACKET',
  RBRACKET: 'RBRACKET',
  EQUALS: 'EQUALS',
  COLON: 'COLON',
  DOT: 'DOT',
  COMMA: 'COMMA',
  SEMICOLON: 'SEMICOLON',
  AT: 'AT',
  COMMENT: 'COMMENT',
  NEWLINE: 'NEWLINE',
  EOF: 'EOF',
};

/**
 * Simple USDA Lexer
 */
class UsdaLexer {
  constructor(input) {
    this.input = input;
    this.pos = 0;
    this.line = 1;
    this.col = 1;
  }

  peek(offset = 0) {
    return this.input[this.pos + offset];
  }

  advance() {
    const ch = this.input[this.pos++];
    if (ch === '\n') {
      this.line++;
      this.col = 1;
    } else {
      this.col++;
    }
    return ch;
  }

  skipWhitespace() {
    while (this.pos < this.input.length) {
      const ch = this.peek();
      if (ch === ' ' || ch === '\t' || ch === '\r') {
        this.advance();
      } else if (ch === '\n') {
        this.advance();
      } else if (ch === '#') {
        // Skip comments
        while (this.pos < this.input.length && this.peek() !== '\n') {
          this.advance();
        }
      } else {
        break;
      }
    }
  }

  readString() {
    const quote = this.advance(); // consume opening quote
    let value = '';
    let isTriple = false;

    // Check for triple-quoted string
    if (this.peek() === quote && this.peek(1) === quote) {
      this.advance();
      this.advance();
      isTriple = true;
    }

    while (this.pos < this.input.length) {
      const ch = this.peek();

      if (isTriple) {
        if (ch === quote && this.peek(1) === quote && this.peek(2) === quote) {
          this.advance();
          this.advance();
          this.advance();
          break;
        }
      } else {
        if (ch === quote) {
          this.advance();
          break;
        }
        if (ch === '\n') {
          break; // Unterminated string
        }
      }

      if (ch === '\\') {
        this.advance();
        const next = this.advance();
        switch (next) {
          case 'n': value += '\n'; break;
          case 't': value += '\t'; break;
          case 'r': value += '\r'; break;
          case '\\': value += '\\'; break;
          case '"': value += '"'; break;
          case "'": value += "'"; break;
          default: value += next;
        }
      } else {
        value += this.advance();
      }
    }

    return { type: TokenType.STRING, value };
  }

  readAssetPath() {
    this.advance(); // consume opening @
    let value = '';

    // Check if it's a triple-@ delimited path @@@...@@@
    let isTripleDelim = false;
    if (this.peek() === '@' && this.peek(1) === '@') {
      // It's @@@...@@@
      this.advance(); // consume second @
      this.advance(); // consume third @
      isTripleDelim = true;
    }

    // Check if it's a quoted asset path @"..."@ or @'...'@
    if (!isTripleDelim && (this.peek() === '"' || this.peek() === "'")) {
      const quote = this.advance();
      while (this.pos < this.input.length) {
        const ch = this.peek();
        if (ch === quote) {
          this.advance();
          break;
        }
        if (ch === '\\') {
          this.advance();
          value += this.advance();
        } else {
          value += this.advance();
        }
      }
    } else {
      // Unquoted asset path @path@ or @@@path@@@
      while (this.pos < this.input.length) {
        const ch = this.peek();

        // Handle escaped characters (including \@@@)
        if (ch === '\\') {
          this.advance();
          value += this.advance();
          continue;
        }

        if (isTripleDelim) {
          // For @@@...@@@, look for closing @@@
          if (ch === '@' && this.peek(1) === '@' && this.peek(2) === '@') {
            this.advance(); // consume first @
            this.advance(); // consume second @
            this.advance(); // consume third @
            break;
          }
        } else {
          // For @...@, look for closing @
          if (ch === '@') {
            this.advance();
            break;
          }
        }

        value += this.advance();
      }
    }

    return { type: TokenType.STRING, value, isAsset: true };
  }

  readNumber() {
    let value = '';
    let hasDecimal = false;
    let hasExponent = false;

    // Handle sign
    if (this.peek() === '-' || this.peek() === '+') {
      value += this.advance();
    }

    // Handle special values: inf, nan
    if (this.peek() === 'i' || this.peek() === 'n') {
      const rest = this.input.slice(this.pos, this.pos + 3);
      if (rest === 'inf' || rest === 'nan') {
        value += this.advance() + this.advance() + this.advance();
        return { type: TokenType.NUMBER, value };
      }
    }

    while (this.pos < this.input.length) {
      const ch = this.peek();
      if (/[0-9]/.test(ch)) {
        value += this.advance();
      } else if (ch === '.' && !hasDecimal && !hasExponent) {
        hasDecimal = true;
        value += this.advance();
      } else if ((ch === 'e' || ch === 'E') && !hasExponent) {
        hasExponent = true;
        value += this.advance();
        if (this.peek() === '+' || this.peek() === '-') {
          value += this.advance();
        }
      } else {
        break;
      }
    }

    return { type: TokenType.NUMBER, value };
  }

  readIdentifier() {
    let value = '';
    while (this.pos < this.input.length) {
      const ch = this.peek();
      if (/[a-zA-Z0-9_]/.test(ch)) {
        value += this.advance();
      } else {
        break;
      }
    }
    return { type: TokenType.IDENTIFIER, value };
  }

  nextToken() {
    this.skipWhitespace();

    if (this.pos >= this.input.length) {
      return { type: TokenType.EOF, value: '', line: this.line, col: this.col };
    }

    const startLine = this.line;
    const startCol = this.col;
    const ch = this.peek();

    // Single character tokens
    switch (ch) {
      case '{': this.advance(); return { type: TokenType.LBRACE, value: '{', line: startLine, col: startCol };
      case '}': this.advance(); return { type: TokenType.RBRACE, value: '}', line: startLine, col: startCol };
      case '(': this.advance(); return { type: TokenType.LPAREN, value: '(', line: startLine, col: startCol };
      case ')': this.advance(); return { type: TokenType.RPAREN, value: ')', line: startLine, col: startCol };
      case '[': this.advance(); return { type: TokenType.LBRACKET, value: '[', line: startLine, col: startCol };
      case ']': this.advance(); return { type: TokenType.RBRACKET, value: ']', line: startLine, col: startCol };
      case '=': this.advance(); return { type: TokenType.EQUALS, value: '=', line: startLine, col: startCol };
      case ':': this.advance(); return { type: TokenType.COLON, value: ':', line: startLine, col: startCol };
      case '.': this.advance(); return { type: TokenType.DOT, value: '.', line: startLine, col: startCol };
      case ',': this.advance(); return { type: TokenType.COMMA, value: ',', line: startLine, col: startCol };
      case ';': this.advance(); return { type: TokenType.SEMICOLON, value: ';', line: startLine, col: startCol };
      case '@': {
        const token = this.readAssetPath();
        return { ...token, line: startLine, col: startCol };
      }
    }

    // Strings
    if (ch === '"' || ch === "'") {
      const token = this.readString();
      return { ...token, line: startLine, col: startCol };
    }

    // Numbers (including negative)
    if (/[0-9]/.test(ch) || ((ch === '-' || ch === '+') && /[0-9.]/.test(this.peek(1)))) {
      const token = this.readNumber();
      return { ...token, line: startLine, col: startCol };
    }

    // Identifiers and keywords
    if (/[a-zA-Z_]/.test(ch)) {
      const token = this.readIdentifier();
      return { ...token, line: startLine, col: startCol };
    }

    // Skip unknown characters
    this.advance();
    return this.nextToken();
  }

  tokenize() {
    const tokens = [];
    let token;
    while ((token = this.nextToken()).type !== TokenType.EOF) {
      tokens.push(token);
    }
    return tokens;
  }
}

/**
 * USDA Parser - parses into a structured representation
 */
class UsdaParser {
  constructor(tokens) {
    this.tokens = tokens;
    this.pos = 0;
    this.depth = 0;
    this.maxDepth = 10000;  // Very deep nesting for complex USD files (skeletal animation, etc.)
    this.iterations = 0;
    this.maxIterations = 100000;
  }

  checkDepth() {
    if (++this.depth > this.maxDepth) {
      throw new Error(`Parser depth limit exceeded (${this.maxDepth})`);
    }
    if (++this.iterations > this.maxIterations) {
      throw new Error(`Parser iteration limit exceeded (${this.maxIterations})`);
    }
  }

  resetDepth() {
    this.depth = 0;
  }

  peek(offset = 0) {
    const idx = this.pos + offset;
    if (idx >= this.tokens.length) {
      return { type: TokenType.EOF, value: '', line: -1, col: -1 };
    }
    const token = this.tokens[idx];
    // Ensure token always has required properties
    if (!token) {
      return { type: TokenType.EOF, value: '', line: -1, col: -1 };
    }
    return token;
  }

  advance() {
    const token = this.tokens[this.pos++];
    if (!token) {
      return { type: TokenType.EOF, value: '', line: -1, col: -1 };
    }
    return token;
  }

  expect(type) {
    const token = this.advance();
    if (token.type !== type) {
      const tokenStr = token ? `${token.type}(${token.value})` : 'EOF()';
      const context = this.getTokenContext();
      throw new Error(`Expected ${type} but got ${tokenStr} at position ${this.pos}\nContext:\n${context}`);
    }
    return token;
  }

  getTokenContext(offset = 0) {
    const startIdx = Math.max(0, this.pos - 2);
    const endIdx = Math.min(this.tokens.length, this.pos + 3);
    const tokens = this.tokens.slice(startIdx, endIdx);
    return tokens.map((t, i) => {
      const marker = startIdx + i === this.pos ? '>>> ' : '    ';
      return `${marker}[${startIdx + i}] ${t ? t.type : 'UNDEFINED'}('${t ? t.value : ''}')`;
    }).join('\n');
  }

  match(type, value = null) {
    const token = this.peek();
    if (token.type === type && (value === null || token.value === value)) {
      return this.advance();
    }
    return null;
  }

  parse() {
    const result = {
      header: null,
      metadata: {},
      prims: []
    };

    try {
      // Parse header (e.g., #usda 1.0)
      const headerToken = this.peek();
      if (headerToken && headerToken.type === TokenType.IDENTIFIER && headerToken.value === 'usda') {
        this.advance();
        const versionToken = this.peek();
        if (versionToken && versionToken.type === TokenType.NUMBER) {
          result.header = { version: this.advance().value };
        }
      }

      // Parse top-level metadata
      const metaToken = this.peek();
      if (metaToken && metaToken.type === TokenType.LPAREN) {
        result.metadata = this.parseMetadata();
      }

      // Parse prims
      while (this.pos < this.tokens.length) {
        const token = this.peek();
        if (!token || token.type === TokenType.EOF) break;

        if (token.type === TokenType.IDENTIFIER &&
            (token.value === 'def' || token.value === 'over' || token.value === 'class')) {
          try {
            result.prims.push(this.parsePrim(''));
          } catch (e) {
            // If prim parsing fails, skip to next prim
            console.error('Error parsing prim:', e.message);
            const lines = e.message.split('\n');
            for (const line of lines) {
              if (line.startsWith('Expected') || line.includes('>>>')) {
                console.error('  ' + line);
              }
            }
            while (this.pos < this.tokens.length &&
                   !(this.peek() && this.peek().type === TokenType.IDENTIFIER &&
                     (this.peek().value === 'def' || this.peek().value === 'over' || this.peek().value === 'class'))) {
              this.advance();
            }
          }
        } else {
          this.advance(); // Skip unexpected tokens
        }
      }
    } catch (e) {
      console.error('Error in top-level parse:', e.message);
      // Return partial result
    }

    return result;
  }

  parseMetadata() {
    const metadata = {};
    this.expect(TokenType.LPAREN);

    while (this.peek().type !== TokenType.RPAREN && this.peek().type !== TokenType.EOF) {
      // Skip any unexpected tokens (e.g., leftover numbers from broken token sequences)
      if (this.peek().type === TokenType.NUMBER || this.peek().type === TokenType.DOT) {
        this.advance();
        continue;
      }

      const key = this.parseAttributeName();
      if (!key) {
        // If we can't parse an attribute name, skip this token and continue
        if (this.peek().type !== TokenType.RPAREN && this.peek().type !== TokenType.EOF) {
          this.advance();
        }
        continue;
      }

      if (this.match(TokenType.EQUALS)) {
        metadata[key] = this.parseValue();
      }
    }

    this.expect(TokenType.RPAREN);
    return metadata;
  }

  parseAttributeName() {
    let name = '';

    // Handle type prefix (e.g., "uniform", "custom")
    while (this.peek() && this.peek().type === TokenType.IDENTIFIER) {
      const token = this.peek();
      if (token && token.value && ['uniform', 'custom', 'varying', 'config', 'prepend', 'append', 'delete', 'add', 'reorder'].includes(token.value)) {
        name += this.advance().value + ' ';
      } else {
        break;
      }
    }

    // Handle type annotation (e.g., "float3", "token[]")
    const typeToken = this.peek();
    if (typeToken && typeToken.type === TokenType.IDENTIFIER) {
      name += this.advance().value;

      // Handle array type
      const bracketToken = this.peek();
      if (bracketToken && bracketToken.type === TokenType.LBRACKET) {
        this.advance();
        this.expect(TokenType.RBRACKET);
        name += '[]';
      }
    }

    // Handle attribute name with namespaces (e.g., "xformOp:translate")
    const nameToken = this.peek();
    if (nameToken && nameToken.type === TokenType.IDENTIFIER) {
      name += ' ' + this.advance().value;

      while (this.peek() && this.peek().type === TokenType.COLON) {
        this.advance();
        const colonToken = this.peek();
        if (colonToken && colonToken.type === TokenType.IDENTIFIER) {
          name += ':' + this.advance().value;
        } else {
          break;
        }
      }
    }

    return name.trim();
  }

  parseValue() {
    this.checkDepth();
    const token = this.peek();

    if (!token) {
      throw new Error(`Unexpected end of tokens while parsing value at position ${this.pos}`);
    }

    // Dictionary (e.g., assetInfo = { string name = "baked_mesh" })
    if (token.type === TokenType.LBRACE) {
      return this.parseDictionary();
    }

    // Array
    if (token.type === TokenType.LBRACKET) {
      return this.parseArray();
    }

    // Tuple/Vector
    if (token.type === TokenType.LPAREN) {
      return this.parseTuple();
    }

    // String or Asset reference
    if (token.type === TokenType.STRING) {
      const tok = this.advance();
      if (tok && tok.isAsset) {
        return { type: 'asset', value: tok.value };
      }
      return { type: 'string', value: tok && tok.value ? tok.value : '' };
    }

    // Number
    if (token.type === TokenType.NUMBER) {
      const numToken = this.advance();
      return { type: 'number', value: numToken && numToken.value ? numToken.value : '0' };
    }

    // Identifier (token, enum, etc.)
    if (token.type === TokenType.IDENTIFIER) {
      let value = this.advance().value;

      // Handle path references (e.g., </Root/Child>)
      if (value === 'None' || value === 'True' || value === 'False') {
        return { type: 'keyword', value };
      }

      return { type: 'token', value };
    }

    // Path reference
    if (token.type === TokenType.STRING || (token.value && token.value === '<')) {
      return this.parsePath();
    }

    return { type: 'unknown', value: token && token.value ? token.value : '?' };
  }

  parseDictionary() {
    this.expect(TokenType.LBRACE);
    const entries = {};

    while (this.peek().type !== TokenType.RBRACE && this.peek().type !== TokenType.EOF) {
      // Dictionary entry format: type key = value
      // e.g., "string name = "baked_mesh""
      const key = this.parseDictionaryKey();
      if (this.match(TokenType.EQUALS)) {
        entries[key] = this.parseValue();
      }
      this.match(TokenType.COMMA);
    }

    this.expect(TokenType.RBRACE);
    return { type: 'dictionary', value: entries };
  }

  parseDictionaryKey() {
    let key = '';

    // Handle type prefix (e.g., "string", "int", "asset", "dictionary")
    const typeToken = this.peek();
    if (typeToken && typeToken.type === TokenType.IDENTIFIER) {
      const typeName = typeToken.value;
      key = typeName;

      this.advance();

      // Handle array type (e.g., "string[]")
      const bracketToken = this.peek();
      if (bracketToken && bracketToken.type === TokenType.LBRACKET) {
        this.advance();
        this.expect(TokenType.RBRACKET);
        key += '[]';
      }

      // Handle key name - can be identifier or string (path)
      const nameToken = this.peek();
      if (nameToken && nameToken.type === TokenType.IDENTIFIER) {
        key += ' ' + this.advance().value;
      } else if (nameToken && nameToken.type === TokenType.STRING) {
        const str = this.advance();
        key += ' ' + (str && str.isAsset ? '@' + str.value + '@' : '"' + (str && str.value ? str.value : '') + '"');
      }
    }

    return key.trim();
  }

  parseArray() {
    this.expect(TokenType.LBRACKET);
    const elements = [];

    while (this.peek().type !== TokenType.RBRACKET && this.peek().type !== TokenType.EOF) {
      elements.push(this.parseValue());
      this.match(TokenType.COMMA);
    }

    this.expect(TokenType.RBRACKET);
    return { type: 'array', value: elements };
  }

  parseTuple() {
    this.expect(TokenType.LPAREN);
    const elements = [];

    while (this.peek().type !== TokenType.RPAREN && this.peek().type !== TokenType.EOF) {
      elements.push(this.parseValue());
      this.match(TokenType.COMMA);
    }

    this.expect(TokenType.RPAREN);
    return { type: 'tuple', value: elements };
  }

  parsePath() {
    // Simple path parsing for now
    let path = '';
    while (this.pos < this.tokens.length) {
      const token = this.peek();
      if (!token || token.type === TokenType.EOF ||
          token.type === TokenType.COMMA ||
          token.type === TokenType.RBRACKET ||
          token.type === TokenType.RPAREN ||
          token.type === TokenType.RBRACE ||
          token.type === TokenType.EQUALS) {
        break;
      }
      const val = token && token.value ? token.value : '';
      path += val;
      this.advance();
    }
    return { type: 'path', value: path };
  }

  parsePrim(parentPath) {
    const startToken = this.peek();
    const startLine = startToken && startToken.line ? startToken.line : -1;
    const specToken = this.advance();
    const specifier = specToken && specToken.value ? specToken.value : 'def'; // def, over, class

    let typeName = '';
    const typeToken = this.peek();
    if (typeToken && typeToken.type === TokenType.IDENTIFIER && typeToken.value !== 'None') {
      // Check if it's a type name or the prim name
      const nextToken = this.peek(1);
      if (nextToken && nextToken.type === TokenType.STRING) {
        typeName = this.advance().value;
      }
    }

    const nameToken = this.peek();
    const name = nameToken && nameToken.type === TokenType.STRING ? this.advance().value : '';
    const primPath = parentPath ? `${parentPath}/${name}` : `/${name}`;

    const prim = {
      specifier,
      typeName,
      name,
      path: primPath,
      startLine,
      metadata: {},
      attributes: {},
      relationships: {},
      children: []
    };

    // Parse prim metadata
    if (this.peek().type === TokenType.LPAREN) {
      prim.metadata = this.parseMetadata();
    }

    // Parse prim body
    if (this.peek().type === TokenType.LBRACE) {
      this.expect(TokenType.LBRACE);

      while (this.peek().type !== TokenType.RBRACE && this.peek().type !== TokenType.EOF) {
        const token = this.peek();

        // Child prim
        if (token.type === TokenType.IDENTIFIER &&
            (token.value === 'def' || token.value === 'over' || token.value === 'class')) {
          prim.children.push(this.parsePrim(primPath));
          continue;
        }

        // Relationship
        if (token.type === TokenType.IDENTIFIER && token.value === 'rel') {
          this.advance();
          const relName = this.parseRelationshipName();
          if (this.match(TokenType.EQUALS)) {
            prim.relationships[relName] = this.parseValue();
          } else {
            prim.relationships[relName] = null;
          }
          continue;
        }

        // Attribute
        if (token.type === TokenType.IDENTIFIER) {
          const attrStartLine = token.line;
          const attrName = this.parseAttributeName();
          if (!attrName) {
            // Failed to parse attribute name, skip this token
            this.advance();
            continue;
          }

          if (this.match(TokenType.EQUALS)) {
            try {
              const value = this.parseValue();
              prim.attributes[attrName] = {
                value: value,
                line: attrStartLine
              };
            } catch (e) {
              // Skip on parse error
            }
          } else if (this.peek().type === TokenType.DOT) {
            // Handle time samples or connections
            this.advance();
            if (this.peek().type === TokenType.IDENTIFIER || this.peek().type === TokenType.STRING) {
              const qualifier = this.advance().value;
              if (this.match(TokenType.EQUALS)) {
                try {
                  const key = `${attrName}.${qualifier}`;
                  let value;
                  if (qualifier === 'timeSamples' && this.peek().type === TokenType.LBRACE) {
                    value = this.parseTimeSamples();
                  } else {
                    value = this.parseValue();
                  }
                  prim.attributes[key] = {
                    value: value,
                    line: attrStartLine
                  };
                } catch (e) {
                  // Skip on parse error
                }
              }
            }
          }
          continue;
        }

        // Skip unknown tokens
        this.advance();
      }

      this.expect(TokenType.RBRACE);
    }

    return prim;
  }

  parseRelationshipName() {
    let name = '';
    const firstToken = this.peek();
    if (firstToken && firstToken.type === TokenType.IDENTIFIER) {
      name = this.advance().value;
      while (this.peek() && this.peek().type === TokenType.COLON) {
        this.advance();
        const nextToken = this.peek();
        if (nextToken && nextToken.type === TokenType.IDENTIFIER) {
          name += ':' + this.advance().value;
        } else {
          break;
        }
      }
    }
    return name;
  }

  parseTimeSamples() {
    // Parse timeSamples dictionary: { timeCode: value, timeCode: value, ... }
    this.expect(TokenType.LBRACE);
    const entries = {};

    while (this.peek().type !== TokenType.RBRACE && this.peek().type !== TokenType.EOF) {
      // Parse time code (number or identifier)
      let timeCode = '';
      const timeToken = this.peek();

      if (timeToken.type === TokenType.NUMBER) {
        timeCode = this.advance().value;
      } else if (timeToken.type === TokenType.IDENTIFIER) {
        timeCode = this.advance().value;
      } else {
        // Skip malformed entry
        this.advance();
        continue;
      }

      // Expect colon
      if (!this.match(TokenType.COLON)) {
        continue;
      }

      // Parse value at this time code
      try {
        entries[timeCode] = this.parseValue();
      } catch (e) {
        // Skip on parse error
      }

      this.match(TokenType.COMMA);
    }

    this.expect(TokenType.RBRACE);
    return { type: 'timeSamples', value: entries };
  }

  skipComplexValue() {
    // Skip over complex values like timeSamples dictionaries
    let braceDepth = 0;
    let bracketDepth = 0;
    let parenDepth = 0;

    while (this.pos < this.tokens.length) {
      const token = this.peek();

      if (token.type === TokenType.LBRACE) braceDepth++;
      else if (token.type === TokenType.RBRACE) {
        braceDepth--;
        if (braceDepth === -1) break;
      } else if (token.type === TokenType.LBRACKET) bracketDepth++;
      else if (token.type === TokenType.RBRACKET) bracketDepth--;
      else if (token.type === TokenType.LPAREN) parenDepth++;
      else if (token.type === TokenType.RPAREN) parenDepth--;

      if (braceDepth === 0 && bracketDepth === 0 && parenDepth === 0 &&
          (token.type === TokenType.COMMA || token.type === TokenType.RBRACE ||
           token.type === TokenType.RBRACKET || token.type === TokenType.RPAREN)) {
        break;
      }

      this.advance();
    }
  }
}

/**
 * Normalize a value for comparison
 */
function normalizeValue(val) {
  // Extract actual value if it's wrapped with location info
  if (val && typeof val === 'object' && val.value !== undefined && val.line !== undefined) {
    val = val.value;
  }

  if (val === null || val === undefined) {
    return 'null';
  }

  if (typeof val !== 'object') {
    return String(val);
  }

  if (val.type === 'number') {
    // Normalize number representation - treat 1, 1.0, 1e0 as same
    const num = parseFloat(val.value);
    if (isNaN(num)) return val.value;

    // For integers, return as integer to match "1" with "1.0"
    if (Number.isInteger(num)) {
      return String(num);
    }

    // For decimals, use normalized fixed precision
    // Round to 6 decimal places and remove trailing zeros
    return num.toFixed(6).replace(/\.?0+$/, '');
  }

  if (val.type === 'string' || val.type === 'token' || val.type === 'keyword') {
    return val.value;
  }

  if (val.type === 'array') {
    // Normalize each element and sort for consistent comparison (for unordered arrays)
    const normalized = val.value.map(normalizeValue);
    return '[' + normalized.join(', ') + ']';
  }

  if (val.type === 'tuple') {
    // Keep tuple order but normalize each element
    const normalized = val.value.map(normalizeValue);
    return '(' + normalized.join(', ') + ')';
  }

  if (val.type === 'dictionary') {
    // Sort keys for consistent comparison
    const keys = Object.keys(val.value).sort();
    const pairs = keys.map(k => `${k} = ${normalizeValue(val.value[k])}`);
    return '{ ' + pairs.join(', ') + ' }';
  }

  if (val.type === 'asset') {
    return '@' + val.value + '@';
  }

  if (val.type === 'path') {
    return val.value;
  }

  if (val.type === 'timeSamples') {
    // Sort time codes numerically for consistent comparison
    const timeCodes = Object.keys(val.value).sort((a, b) => parseFloat(a) - parseFloat(b));
    const pairs = timeCodes.map(tc => `${tc}: ${normalizeValue(val.value[tc])}`);
    return '{ ' + pairs.join(', ') + ' }';
  }

  return JSON.stringify(val);
}

/**
 * Check if a normalized value is numeric
 */
function isNumericValue(normalizedValue) {
  if (typeof normalizedValue !== 'string') return false;
  const num = parseFloat(normalizedValue);
  return !isNaN(num) && isFinite(num);
}

/**
 * Compare two numeric values with epsilon tolerance
 */
function areNumbersEqual(val1, val2, epsilon = 1e-6) {
  const num1 = parseFloat(val1);
  const num2 = parseFloat(val2);

  // Handle special cases
  if (isNaN(num1) || isNaN(num2)) return false;
  if (!isFinite(num1) || !isFinite(num2)) return num1 === num2;

  // Use relative epsilon for large numbers, absolute for small numbers
  const absNum1 = Math.abs(num1);
  const absNum2 = Math.abs(num2);
  const maxAbs = Math.max(absNum1, absNum2);

  if (maxAbs < 1.0) {
    // For small numbers, use absolute epsilon
    return Math.abs(num1 - num2) <= epsilon;
  } else {
    // For larger numbers, use relative epsilon
    return Math.abs(num1 - num2) <= epsilon * maxAbs;
  }
}

/**
 * Compare two timeSamples values with epsilon tolerance for numeric values
 */
function areTimeSamplesEqual(val1, val2, epsilon = 1e-6) {
  // Check if both are timeSamples objects
  if (!val1 || !val2 || val1.type !== 'timeSamples' || val2.type !== 'timeSamples') {
    return false;
  }

  const times1 = Object.keys(val1.value);
  const times2 = Object.keys(val2.value);

  // Check if they have the same number of time samples
  if (times1.length !== times2.length) {
    return false;
  }

  // Sort time codes for consistent comparison
  times1.sort((a, b) => parseFloat(a) - parseFloat(b));
  times2.sort((a, b) => parseFloat(a) - parseFloat(b));

  // Compare each time code and its value
  for (let i = 0; i < times1.length; i++) {
    const t1 = times1[i];
    const t2 = times2[i];

    // Compare time codes
    if (!areNumbersEqual(t1, t2, epsilon)) {
      return false;
    }

    // Compare values at this time
    const v1 = normalizeValue(val1.value[t1]);
    const v2 = normalizeValue(val2.value[t2]);

    // Use epsilon comparison for numeric values
    if (isNumericValue(v1) && isNumericValue(v2)) {
      if (!areNumbersEqual(v1, v2, epsilon)) {
        return false;
      }
    } else {
      // Non-numeric values must match exactly
      if (v1 !== v2) {
        return false;
      }
    }
  }

  return true;
}

/**
 * Compare two USDA structures
 */
function compareUsda(usda1, usda2, options = {}) {
  const differences = [];
  const { verbose = false } = options;

  // Compare metadata
  const metaDiff = compareObjects(usda1.metadata, usda2.metadata, 'metadata');
  differences.push(...metaDiff);

  // Build prim maps by path
  const primMap1 = buildPrimMap(usda1.prims);
  const primMap2 = buildPrimMap(usda2.prims);

  const allPaths = new Set([...Object.keys(primMap1), ...Object.keys(primMap2)]);

  for (const path of allPaths) {
    const prim1 = primMap1[path];
    const prim2 = primMap2[path];

    if (!prim1) {
      differences.push({
        type: 'prim_missing',
        location: 'file1',
        path,
        line2: prim2?.startLine,
        message: `Prim "${path}" exists in file2 but not in file1`
      });
      continue;
    }

    if (!prim2) {
      differences.push({
        type: 'prim_missing',
        location: 'file2',
        path,
        line1: prim1?.startLine,
        message: `Prim "${path}" exists in file1 but not in file2`
      });
      continue;
    }

    // Compare prim specifier
    if (prim1.specifier !== prim2.specifier) {
      differences.push({
        type: 'specifier_mismatch',
        path,
        line1: prim1.startLine,
        line2: prim2.startLine,
        file1: prim1.specifier,
        file2: prim2.specifier,
        message: `Specifier mismatch at "${path}": "${prim1.specifier}" vs "${prim2.specifier}"`
      });
    }

    // Compare prim type
    if (prim1.typeName !== prim2.typeName) {
      differences.push({
        type: 'type_mismatch',
        path,
        line1: prim1.startLine,
        line2: prim2.startLine,
        file1: prim1.typeName,
        file2: prim2.typeName,
        message: `Type mismatch at "${path}": "${prim1.typeName}" vs "${prim2.typeName}"`
      });
    }

    // Compare attributes
    const attrDiff = compareAttributes(prim1.attributes, prim2.attributes, path);
    differences.push(...attrDiff);

    // Compare relationships
    const relDiff = compareObjects(prim1.relationships, prim2.relationships, `${path} relationships`);
    differences.push(...relDiff);

    // Compare prim metadata
    const primMetaDiff = compareObjects(prim1.metadata, prim2.metadata, `${path} metadata`);
    differences.push(...primMetaDiff);
  }

  return differences;
}

/**
 * Build a flat map of prims by path
 */
function buildPrimMap(prims, map = {}) {
  for (const prim of prims) {
    map[prim.path] = prim;
    buildPrimMap(prim.children, map);
  }
  return map;
}

/**
 * Extract just the attribute name without type annotation
 * E.g., "texCoord2f inputs:st.connect" -> "inputs:st.connect"
 */
function extractAttributeName(fullKey) {
  // Split by spaces and take the last part(s) as the attribute name
  const parts = fullKey.trim().split(/\s+/);

  if (parts.length === 0) return '';

  // Skip known prefixes: uniform, custom, varying, config, prepend, append, delete, add, reorder
  const prefixes = ['uniform', 'custom', 'varying', 'config', 'prepend', 'append', 'delete', 'add', 'reorder'];
  let i = 0;

  while (i < parts.length && prefixes.includes(parts[i])) {
    i++;
  }

  // Skip the type annotation (everything until we find the attribute name with namespace or dot)
  // Type annotations are typically: float, double, int, bool, token, string, asset, etc.
  // They may have [] for arrays or be complex types like texCoord2f, normal3f, etc.
  if (i < parts.length) {
    // Check if next part looks like a type (contains only letters, numbers, [] or is a role type)
    const possibleType = parts[i];
    // Type if it:
    // 1. Contains only alphanumeric, brackets, but no colons or dots (not an attribute name)
    // 2. Doesn't already contain a colon (which indicates namespace like "inputs:file")
    if (possibleType.match(/^[a-zA-Z0-9\[\]]+$/) && !possibleType.includes(':') && !possibleType.includes('.')) {
      i++;
    }
  }

  // Return the remaining parts joined as the attribute name
  const result = parts.slice(i).join(' ').trim();
  return result;
}

/**
 * Compare attributes between two prims
 */
function compareAttributes(attrs1, attrs2, primPath) {
  const differences = [];

  // Create a map of normalized attribute names to their original keys
  // This makes comparison order-independent
  const attrMap1 = {};
  const attrMap2 = {};

  for (const key of Object.keys(attrs1)) {
    const normName = extractAttributeName(key);
    // Skip empty attribute names (parser edge cases)
    if (normName && normName.length > 0) {
      attrMap1[normName] = key;
    }
  }

  for (const key of Object.keys(attrs2)) {
    const normName = extractAttributeName(key);
    // Skip empty attribute names (parser edge cases)
    if (normName && normName.length > 0) {
      attrMap2[normName] = key;
    }
  }

  // Compare all attributes (order-independent due to using Set of normalized names)
  const allNormNames = new Set([...Object.keys(attrMap1), ...Object.keys(attrMap2)]);

  for (const normName of allNormNames) {
    const key1 = attrMap1[normName];
    const key2 = attrMap2[normName];

    const val1 = key1 ? attrs1[key1] : undefined;
    const val2 = key2 ? attrs2[key2] : undefined;

    // Extract location info if present
    const line1 = val1 && typeof val1 === 'object' && val1.line ? val1.line : undefined;
    const line2 = val2 && typeof val2 === 'object' && val2.line ? val2.line : undefined;

    if (val1 === undefined) {
      differences.push({
        type: 'attribute_missing',
        location: 'file1',
        path: primPath,
        attribute: normName,
        line2: line2,
        message: `Attribute "${normName}" at "${primPath}" exists in file2 but not in file1`
      });
      continue;
    }

    if (val2 === undefined) {
      differences.push({
        type: 'attribute_missing',
        location: 'file2',
        path: primPath,
        attribute: normName,
        line1: line1,
        message: `Attribute "${normName}" at "${primPath}" exists in file1 but not in file2`
      });
      continue;
    }

    // Extract actual values if wrapped with location info
    let actualVal1 = val1 && typeof val1 === 'object' && val1.value !== undefined && val1.line !== undefined ? val1.value : val1;
    let actualVal2 = val2 && typeof val2 === 'object' && val2.value !== undefined && val2.line !== undefined ? val2.value : val2;

    // Check for timeSamples before normalization
    let valuesEqual = false;

    // Special handling for timeSamples (compare before normalization)
    if (actualVal1 && typeof actualVal1 === 'object' && actualVal1.type === 'timeSamples' &&
        actualVal2 && typeof actualVal2 === 'object' && actualVal2.type === 'timeSamples') {
      valuesEqual = areTimeSamplesEqual(actualVal1, actualVal2);
    } else {
      // Normal comparison path
      const norm1 = normalizeValue(val1);
      const norm2 = normalizeValue(val2);

      // Use epsilon comparison for numeric values
      if (isNumericValue(norm1) && isNumericValue(norm2)) {
        valuesEqual = areNumbersEqual(norm1, norm2);
      } else {
        valuesEqual = (norm1 === norm2);
      }
    }

    if (!valuesEqual) {
      // Normalize for error message display
      const norm1 = normalizeValue(val1);
      const norm2 = normalizeValue(val2);
      differences.push({
        type: 'attribute_value_mismatch',
        path: primPath,
        attribute: normName,
        line1: line1,
        line2: line2,
        file1: norm1,
        file2: norm2,
        message: `Attribute "${normName}" at "${primPath}" differs: "${truncate(norm1, 50)}" vs "${truncate(norm2, 50)}"`
      });
    }
  }

  return differences;
}

/**
 * Compare two objects
 */
function compareObjects(obj1, obj2, context) {
  const differences = [];
  const allKeys = new Set([...Object.keys(obj1), ...Object.keys(obj2)]);

  for (const key of allKeys) {
    const val1 = obj1[key];
    const val2 = obj2[key];

    if (val1 === undefined) {
      differences.push({
        type: 'key_missing',
        location: 'file1',
        context,
        key,
        message: `Key "${key}" in ${context} exists in file2 but not in file1`
      });
      continue;
    }

    if (val2 === undefined) {
      differences.push({
        type: 'key_missing',
        location: 'file2',
        context,
        key,
        message: `Key "${key}" in ${context} exists in file1 but not in file2`
      });
      continue;
    }

    // Extract actual values if wrapped with location info
    let actualVal1 = val1 && typeof val1 === 'object' && val1.value !== undefined && val1.line !== undefined ? val1.value : val1;
    let actualVal2 = val2 && typeof val2 === 'object' && val2.value !== undefined && val2.line !== undefined ? val2.value : val2;

    // Check for timeSamples before normalization
    let valuesEqual = false;

    // Special handling for timeSamples (compare before normalization)
    if (actualVal1 && typeof actualVal1 === 'object' && actualVal1.type === 'timeSamples' &&
        actualVal2 && typeof actualVal2 === 'object' && actualVal2.type === 'timeSamples') {
      valuesEqual = areTimeSamplesEqual(actualVal1, actualVal2);
    } else {
      // Normal comparison path
      const norm1 = normalizeValue(val1);
      const norm2 = normalizeValue(val2);

      // Use epsilon comparison for numeric values
      if (isNumericValue(norm1) && isNumericValue(norm2)) {
        valuesEqual = areNumbersEqual(norm1, norm2);
      } else {
        valuesEqual = (norm1 === norm2);
      }
    }

    if (!valuesEqual) {
      // Normalize for error message display
      const norm1 = normalizeValue(val1);
      const norm2 = normalizeValue(val2);
      differences.push({
        type: 'value_mismatch',
        context,
        key,
        file1: norm1,
        file2: norm2,
        message: `Value for "${key}" in ${context} differs: "${truncate(norm1, 50)}" vs "${truncate(norm2, 50)}"`
      });
    }
  }

  return differences;
}

/**
 * Truncate string for display
 */
function truncate(str, maxLen) {
  if (str.length <= maxLen) return str;
  return str.slice(0, maxLen - 3) + '...';
}

/**
 * Parse USDA content
 */
function parseUsda(content) {
  const lexer = new UsdaLexer(content);
  const tokens = lexer.tokenize();
  const parser = new UsdaParser(tokens);
  return parser.parse();
}

/**
 * Get a specific line from content
 */
function getLine(content, lineNum) {
  const lines = content.split('\n');
  return lines[lineNum - 1] || '';
}

/**
 * Get context lines around a specific line
 */
function getLineContext(content, lineNum, contextLines = 1) {
  const lines = content.split('\n');
  const start = Math.max(0, lineNum - contextLines - 1);
  const end = Math.min(lines.length, lineNum + contextLines);
  const context = [];

  for (let i = start; i < end; i++) {
    const isTargetLine = (i + 1) === lineNum;
    const marker = isTargetLine ? '>>> ' : '    ';
    context.push(`${marker}${String(i + 1).padStart(5)}: ${lines[i]}`);
  }

  return context.join('\n');
}

/**
 * Format a detailed diff for a single difference
 */
function formatDetailedDiff(diff, content1, content2) {
  let output = `\n  ✗ ${diff.message}\n`;

  if (diff.type === 'attribute_value_mismatch' || diff.type === 'attribute_missing') {
    if (diff.line1) {
      output += `\n    File 1 (line ${diff.line1}):\n`;
      output += getLineContext(content1, diff.line1, 1).split('\n').map(l => '    ' + l).join('\n');
      output += '\n';
    }
    if (diff.line2) {
      output += `\n    File 2 (line ${diff.line2}):\n`;
      output += getLineContext(content2, diff.line2, 1).split('\n').map(l => '    ' + l).join('\n');
      output += '\n';
    }
  } else if (diff.type === 'prim_missing') {
    if (diff.line1) {
      output += `\n    Prim defined in File 1 at line ${diff.line1}:\n`;
      output += getLineContext(content1, diff.line1, 1).split('\n').map(l => '    ' + l).join('\n');
      output += '\n';
    }
    if (diff.line2) {
      output += `\n    Prim defined in File 2 at line ${diff.line2}:\n`;
      output += getLineContext(content2, diff.line2, 1).split('\n').map(l => '    ' + l).join('\n');
      output += '\n';
    }
  } else if (diff.type === 'specifier_mismatch' || diff.type === 'type_mismatch') {
    if (diff.line1) {
      output += `\n    File 1 (line ${diff.line1}): ${diff.file1}\n`;
      output += getLineContext(content1, diff.line1, 0).split('\n').map(l => '    ' + l).join('\n');
      output += '\n';
    }
    if (diff.line2) {
      output += `\n    File 2 (line ${diff.line2}): ${diff.file2}\n`;
      output += getLineContext(content2, diff.line2, 0).split('\n').map(l => '    ' + l).join('\n');
      output += '\n';
    }
  }

  return output;
}

/**
 * Print usage information
 */
function printUsage() {
  console.log(`
Usage: compare-usda.js [options] <file1.usda> <file2.usda>
       compare-usda.js --tusdcat <path> --usdcat <path> <input.usd>
       compare-usda.js --tusdcat <path> --usdcat <path> "**/*.usd"

Compare USDA outputs at the Prim and Attribute level, ignoring ordering differences.

Supports glob patterns for batch comparison:
  *        - matches any characters except /
  **       - matches any characters including /
  ?        - matches single character
  [abc]    - matches any character in brackets
  {a,b,c}  - matches any of the alternatives

Examples:
  compare-usda.js file1.usda file2.usda
  compare-usda.js --tusdcat ./tusdcat --usdcat usdcat model.usd
  compare-usda.js --tusdcat ./tusdcat --usdcat usdcat "models/*.usda"
  compare-usda.js --tusdcat ./tusdcat --usdcat usdcat "**/*.usd{a,c,z}"
  compare-usda.js --tusdcat ./tusdcat --usdcat usdcat --base-dir /path/to/models "**/*.usd"

Options:
  -h, --help              Show this help message
  -v, --verbose           Show detailed output
  -q, --quiet             Only show if files differ (exit code)
  --tusdcat <path>        Path to tusdcat executable
  --usdcat <path>         Path to usdcat executable
  --base-dir <path>       Base directory for glob patterns (default: current dir)
  --summary               Show summary statistics only
  --detailed-diff         Show detailed diffs with line numbers and context
  --ignore-metadata       Ignore top-level metadata differences
  --ignore-types          Ignore attribute type differences
  --float-tolerance <n>   Tolerance for floating point comparison (default: 1e-6)
  --continue-on-error     Continue processing other files if one fails
  --json                  Output results as JSON
  --timeout <ms>          Timeout per file in milliseconds (default: 60000)

Exit codes:
  0 - All files are equivalent
  1 - At least one file differs
  2 - Error occurred
`);
}

/**
 * Compare a single file with tusdcat vs usdcat
 */
function compareSingleFile(inputFile, options) {
  const result = {
    file: inputFile,
    status: 'unknown',
    differences: [],
    error: null,
    content1: null,
    content2: null
  };

  try {
    let content1, content2;

    // Run tusdcat with timeout
    try {
      content1 = execSync(`"${options.tusdcat}" "${inputFile}"`, {
        encoding: 'utf-8',
        maxBuffer: 100 * 1024 * 1024,
        stdio: ['pipe', 'pipe', 'pipe'],
        timeout: options.timeout
      });
    } catch (e) {
      result.status = 'error';
      const timeoutMsg = e.code === 'ETIMEDOUT' ? `timeout (${options.timeout / 1000}s)` : e.message;
      result.error = `tusdcat failed: ${timeoutMsg}`;
      return result;
    }

    // Run usdcat with timeout
    try {
      content2 = execSync(`"${options.usdcat}" "${inputFile}"`, {
        encoding: 'utf-8',
        maxBuffer: 100 * 1024 * 1024,
        stdio: ['pipe', 'pipe', 'pipe'],
        timeout: options.timeout
      });
    } catch (e) {
      result.status = 'error';
      const timeoutMsg = e.code === 'ETIMEDOUT' ? `timeout (${options.timeout / 1000}s)` : e.message;
      result.error = `usdcat failed: ${timeoutMsg}`;
      return result;
    }

    // Store contents for detailed diff reporting
    result.content1 = content1;
    result.content2 = content2;

    // Parse both USDA contents
    let usda1, usda2;
    try {
      usda1 = parseUsda(content1);
    } catch (e) {
      result.status = 'error';
      result.error = `Failed to parse tusdcat output: ${e.message}`;
      // Show context in verbose mode
      if (options.verbose) {
        const lines = content1.split('\n');
        const errorLine = parseInt(e.message.match(/position (\d+)/) ? RegExp.$1 : 0);
        if (errorLine > 0) {
          const charPos = errorLine;
          let lineNum = 0;
          let pos = 0;
          for (let i = 0; i < lines.length; i++) {
            pos += lines[i].length + 1;
            lineNum = i;
            if (pos >= charPos) break;
          }
          result.error += `\n    at line ${lineNum + 1}: ${lines[lineNum]}`;
        }
      }
      return result;
    }
    try {
      usda2 = parseUsda(content2);
    } catch (e) {
      result.status = 'error';
      result.error = `Failed to parse usdcat output: ${e.message}`;
      // Show context in verbose mode
      if (options.verbose) {
        const lines = content2.split('\n');
        const errorLine = parseInt(e.message.match(/position (\d+)/) ? RegExp.$1 : 0);
        if (errorLine > 0) {
          const charPos = errorLine;
          let lineNum = 0;
          let pos = 0;
          for (let i = 0; i < lines.length; i++) {
            pos += lines[i].length + 1;
            lineNum = i;
            if (pos >= charPos) break;
          }
          result.error += `\n    at line ${lineNum + 1}: ${lines[lineNum]}`;
        }
      }
      return result;
    }

    // Compare
    let differences = compareUsda(usda1, usda2, options);

    // Filter differences based on options
    if (options.ignoreMetadata) {
      differences = differences.filter(d => !d.context?.includes('metadata'));
    }

    if (options.ignoreTypes) {
      differences = differences.filter(d => d.type !== 'type_mismatch');
    }

    result.differences = differences;
    result.status = differences.length === 0 ? 'equivalent' : 'different';

  } catch (error) {
    result.status = 'error';
    result.error = error.message;
  }

  return result;
}

/**
 * Main entry point
 */
function main() {
  const args = process.argv.slice(2);

  if (args.length === 0 || args.includes('-h') || args.includes('--help')) {
    printUsage();
    process.exit(0);
  }

  const options = {
    verbose: false,
    quiet: false,
    summary: false,
    detailedDiff: false,
    ignoreMetadata: false,
    ignoreTypes: false,
    floatTolerance: 1e-6,
    tusdcat: null,
    usdcat: null,
    baseDir: process.cwd(),
    continueOnError: false,
    json: false,
    timeout: 60000,
    printCommands: false,
    files: []
  };

  // Parse arguments
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    switch (arg) {
      case '-v':
      case '--verbose':
        options.verbose = true;
        break;
      case '-q':
      case '--quiet':
        options.quiet = true;
        break;
      case '--summary':
        options.summary = true;
        break;
      case '--detailed-diff':
        options.detailedDiff = true;
        break;
      case '--ignore-metadata':
        options.ignoreMetadata = true;
        break;
      case '--ignore-types':
        options.ignoreTypes = true;
        break;
      case '--float-tolerance':
        options.floatTolerance = parseFloat(args[++i]);
        break;
      case '--tusdcat':
        options.tusdcat = args[++i];
        break;
      case '--usdcat':
        options.usdcat = args[++i];
        break;
      case '--base-dir':
        options.baseDir = args[++i];
        break;
      case '--continue-on-error':
        options.continueOnError = true;
        break;
      case '--json':
        options.json = true;
        break;
      case '--timeout':
        options.timeout = parseInt(args[++i], 10);
        break;
      case '--print-commands':
        options.printCommands = true;
        break;
      default:
        if (!arg.startsWith('-')) {
          options.files.push(arg);
        }
        break;
    }
  }

  try {
    // Mode 1: Compare two USDA files directly
    if (options.files.length === 2 && !options.tusdcat && !options.usdcat) {
      const label1 = options.files[0];
      const label2 = options.files[1];
      const content1 = fs.readFileSync(options.files[0], 'utf-8');
      const content2 = fs.readFileSync(options.files[1], 'utf-8');

      // Parse both USDA contents
      if (!options.quiet && !options.json) {
        console.log('Parsing USDA files...');
      }

      const usda1 = parseUsda(content1);
      const usda2 = parseUsda(content2);

      // Compare
      if (!options.quiet && !options.json) {
        console.log('Comparing...\n');
      }

      let differences = compareUsda(usda1, usda2, options);

      // Filter differences based on options
      if (options.ignoreMetadata) {
        differences = differences.filter(d => !d.context?.includes('metadata'));
      }

      if (options.ignoreTypes) {
        differences = differences.filter(d => d.type !== 'type_mismatch');
      }

      if (options.json) {
        console.log(JSON.stringify({
          file1: label1,
          file2: label2,
          status: differences.length === 0 ? 'equivalent' : 'different',
          differenceCount: differences.length,
          differences: options.summary ? undefined : differences
        }, null, 2));
        process.exit(differences.length === 0 ? 0 : 1);
      }

      // Output results
      if (differences.length === 0) {
        if (!options.quiet) {
          console.log('✓ Files are equivalent (attributes and prims match)');
        }
        process.exit(0);
      } else {
        if (!options.quiet) {
          console.log(`✗ Found ${differences.length} difference(s):\n`);

          if (options.summary) {
            // Group by type
            const byType = {};
            for (const diff of differences) {
              byType[diff.type] = (byType[diff.type] || 0) + 1;
            }
            console.log('Summary:');
            for (const [type, count] of Object.entries(byType)) {
              console.log(`  ${type}: ${count}`);
            }
          } else if (options.detailedDiff) {
            // Show detailed diffs with line numbers and context
            for (const diff of differences) {
              console.log(formatDetailedDiff(diff, content1, content2));
            }
          } else {
            // Show all differences
            for (const diff of differences) {
              console.log(`  - ${diff.message}`);
              if (options.verbose && diff.file1 && diff.file2) {
                console.log(`      File 1: ${diff.file1}`);
                console.log(`      File 2: ${diff.file2}`);
              }
            }
          }

          console.log(`\nFiles compared: ${label1} vs ${label2}`);
        }
        process.exit(1);
      }
    }
    // Mode 2: Run tusdcat and usdcat on input file(s) - supports glob patterns
    else if (options.files.length >= 1 && options.tusdcat && options.usdcat) {
      // Expand glob patterns
      const expandedFiles = expandFilePatterns(options.files, options.baseDir)
        // Filter out empty strings to prevent invalid file paths
        .filter(f => f && f.trim().length > 0);

      if (expandedFiles.length === 0) {
        console.error('Error: No files found matching the pattern(s).');
        process.exit(2);
      }

      if (!options.quiet && !options.json) {
        console.log(`Found ${expandedFiles.length} file(s) to compare\n`);
      }

      const results = [];
      let equivalent = 0;
      let different = 0;
      let errors = 0;

      for (let i = 0; i < expandedFiles.length; i++) {
        const inputFile = expandedFiles[i];

        if (!options.quiet && !options.json) {
          console.log(`[${i + 1}/${expandedFiles.length}] Processing: ${inputFile}`);
          if (options.printCommands) {
            console.log(`  $ "${options.tusdcat}" "${inputFile}"`);
            console.log(`  $ "${options.usdcat}" "${inputFile}"`);
          }
        }

        const result = compareSingleFile(inputFile, options);
        results.push(result);

        if (result.status === 'equivalent') {
          equivalent++;
          if (!options.quiet && !options.json) {
            console.log(`  ✓ Equivalent\n`);
          }
        } else if (result.status === 'different') {
          different++;
          if (!options.quiet && !options.json) {
            console.log(`  ✗ ${result.differences.length} difference(s)`);
            if (options.detailedDiff && result.content1 && result.content2) {
              // Show detailed diffs with line numbers and context
              for (const diff of result.differences) {
                console.log(formatDetailedDiff(diff, result.content1, result.content2));
              }
            } else if (options.verbose && !options.summary) {
              for (const diff of result.differences) {
                console.log(`    - ${diff.message}`);
              }
            }
            console.log('');
          }
        } else {
          errors++;
          if (!options.quiet && !options.json) {
            console.log(`  ⚠ Error: ${result.error}\n`);
          }
          if (!options.continueOnError) {
            break;
          }
        }
      }

      // Output final summary
      if (options.json) {
        console.log(JSON.stringify({
          totalFiles: expandedFiles.length,
          equivalent,
          different,
          errors,
          results: options.summary ? undefined : results
        }, null, 2));
      } else if (!options.quiet) {
        console.log('═'.repeat(50));
        console.log(`Summary: ${expandedFiles.length} file(s) processed`);
        console.log(`  ✓ Equivalent: ${equivalent}`);
        console.log(`  ✗ Different:  ${different}`);
        if (errors > 0) {
          console.log(`  ⚠ Errors:     ${errors}`);
        }
      }

      // Exit code
      if (errors > 0 && !options.continueOnError) {
        process.exit(2);
      } else if (different > 0 || errors > 0) {
        process.exit(1);
      } else {
        process.exit(0);
      }
    }
    else {
      console.error('Error: Invalid arguments. Either provide two USDA files or use --tusdcat and --usdcat with input file(s)/pattern(s).');
      printUsage();
      process.exit(2);
    }

  } catch (error) {
    console.error(`Error: ${error.message}`);
    if (options.verbose) {
      console.error(error.stack);
    }
    process.exit(2);
  }
}

// Export for testing
module.exports = {
  UsdaLexer,
  UsdaParser,
  parseUsda,
  compareUsda,
  normalizeValue,
  expandGlob,
  expandFilePatterns,
  isGlobPattern,
  globToRegex
};

// Run if executed directly
if (require.main === module) {
  main();
}
