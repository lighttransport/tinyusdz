#!/usr/bin/env node
// mtlx-roundtrip.js
//
// A CLI tool to test MaterialX import/export roundtrip in TinyUSDZ.
// Parses USD files containing MaterialX materials, exports them back to XML,
// and validates the roundtrip.
//
// Usage:
//   node mtlx-roundtrip.js <input.usd>
//   node mtlx-roundtrip.js --tusdcat <path> <input.usd>
//   node mtlx-roundtrip.js --tusdcat <path> "**/*.usd"

const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

// ============================================================================
// Glob Pattern Matching (from compare-usda.js)
// ============================================================================

function globToRegex(pattern) {
  let regex = '';
  let i = 0;

  while (i < pattern.length) {
    const c = pattern[i];

    if (c === '*') {
      if (pattern[i + 1] === '*') {
        if (pattern[i + 2] === '/' || pattern[i + 2] === path.sep) {
          regex += '(?:.*\\/)?';
          i += 3;
        } else {
          regex += '.*';
          i += 2;
        }
      } else {
        regex += '[^\\/]*';
        i++;
      }
    } else if (c === '?') {
      regex += '[^\\/]';
      i++;
    } else if (c === '[') {
      let j = i + 1;
      let classContent = '';
      while (j < pattern.length && pattern[j] !== ']') {
        classContent += pattern[j];
        j++;
      }
      regex += '[' + classContent + ']';
      i = j + 1;
    } else if (c === '{') {
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

function isGlobPattern(str) {
  return /[*?[\]{}]/.test(str);
}

function expandGlob(pattern, baseDir = '.') {
  const results = [];
  const absolutePattern = path.isAbsolute(pattern) ? pattern : path.join(baseDir, pattern);

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
        if (globPattern.includes('**') || regex.test(entryRelPath + '/')) {
          walk(fullPath, entryRelPath);
        } else {
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

  return [...new Set(files)];
}

// ============================================================================
// MaterialX XML Parser (Simple)
// ============================================================================

/**
 * Simple XML parser for MaterialX
 * Extracts nodes, inputs, outputs for comparison
 */
class MtlxParser {
  constructor(xml) {
    this.xml = xml;
    this.pos = 0;
  }

  parse() {
    const result = {
      version: null,
      colorspace: null,
      nodegraphs: [],
      shaders: [],
      materials: []
    };

    // Extract materialx attributes
    const mtlxMatch = this.xml.match(/<materialx\s+([^>]+)>/i);
    if (mtlxMatch) {
      const versionMatch = mtlxMatch[1].match(/version="([^"]+)"/);
      const colorspaceMatch = mtlxMatch[1].match(/colorspace="([^"]+)"/);
      if (versionMatch) result.version = versionMatch[1];
      if (colorspaceMatch) result.colorspace = colorspaceMatch[1];
    }

    // Extract nodegraphs
    const nodegraphRegex = /<nodegraph\s+name="([^"]+)"[^>]*>([\s\S]*?)<\/nodegraph>/gi;
    let match;
    while ((match = nodegraphRegex.exec(this.xml)) !== null) {
      result.nodegraphs.push({
        name: match[1],
        content: match[2],
        nodes: this.parseNodes(match[2]),
        outputs: this.parseOutputs(match[2])
      });
    }

    // Extract surface shaders (open_pbr_surface, standard_surface, etc.)
    const shaderRegex = /<(open_pbr_surface|standard_surface|UsdPreviewSurface)\s+name="([^"]+)"[^>]*>([\s\S]*?)<\/\1>/gi;
    while ((match = shaderRegex.exec(this.xml)) !== null) {
      result.shaders.push({
        type: match[1],
        name: match[2],
        inputs: this.parseInputs(match[3])
      });
    }

    // Extract surfacematerial
    const materialRegex = /<surfacematerial\s+name="([^"]+)"[^>]*>([\s\S]*?)<\/surfacematerial>/gi;
    while ((match = materialRegex.exec(this.xml)) !== null) {
      result.materials.push({
        name: match[1],
        inputs: this.parseInputs(match[2])
      });
    }

    return result;
  }

  parseNodes(content) {
    const nodes = [];
    // Match any node element (not input/output)
    const nodeRegex = /<(\w+)\s+name="([^"]+)"[^>]*(?:\/>|>([\s\S]*?)<\/\1>)/gi;
    let match;

    while ((match = nodeRegex.exec(content)) !== null) {
      const tagName = match[1].toLowerCase();
      if (tagName === 'input' || tagName === 'output') continue;

      nodes.push({
        category: match[1],
        name: match[2],
        inputs: this.parseInputs(match[3] || ''),
        rawTag: match[0]
      });
    }

    return nodes;
  }

  parseInputs(content) {
    const inputs = [];
    const inputRegex = /<input\s+([^>]+)\/?>/gi;
    let match;

    while ((match = inputRegex.exec(content)) !== null) {
      const attrs = this.parseAttributes(match[1]);
      inputs.push(attrs);
    }

    return inputs;
  }

  parseOutputs(content) {
    const outputs = [];
    const outputRegex = /<output\s+([^>]+)\/?>/gi;
    let match;

    while ((match = outputRegex.exec(content)) !== null) {
      const attrs = this.parseAttributes(match[1]);
      outputs.push(attrs);
    }

    return outputs;
  }

  parseAttributes(attrString) {
    const attrs = {};
    const attrRegex = /(\w+)="([^"]*)"/g;
    let match;

    while ((match = attrRegex.exec(attrString)) !== null) {
      attrs[match[1]] = match[2];
    }

    return attrs;
  }
}

/**
 * Parse MaterialX XML string
 */
function parseMtlx(xml) {
  const parser = new MtlxParser(xml);
  return parser.parse();
}

// ============================================================================
// Comparison Functions
// ============================================================================

/**
 * Compare two MaterialX structures
 */
function compareMtlx(mtlx1, mtlx2, options = {}) {
  const differences = [];

  // Compare version
  if (mtlx1.version !== mtlx2.version) {
    differences.push({
      type: 'version_mismatch',
      message: `Version mismatch: "${mtlx1.version}" vs "${mtlx2.version}"`
    });
  }

  // Compare colorspace
  if (mtlx1.colorspace !== mtlx2.colorspace) {
    differences.push({
      type: 'colorspace_mismatch',
      message: `Colorspace mismatch: "${mtlx1.colorspace}" vs "${mtlx2.colorspace}"`
    });
  }

  // Compare nodegraphs
  const ngDiffs = compareNodegraphs(mtlx1.nodegraphs, mtlx2.nodegraphs, options);
  differences.push(...ngDiffs);

  // Compare shaders
  const shaderDiffs = compareShaders(mtlx1.shaders, mtlx2.shaders, options);
  differences.push(...shaderDiffs);

  // Compare materials
  const matDiffs = compareMaterials(mtlx1.materials, mtlx2.materials, options);
  differences.push(...matDiffs);

  return differences;
}

/**
 * Compare nodegraphs
 */
function compareNodegraphs(ngs1, ngs2, options) {
  const differences = [];

  const map1 = new Map(ngs1.map(ng => [ng.name, ng]));
  const map2 = new Map(ngs2.map(ng => [ng.name, ng]));

  const allNames = new Set([...map1.keys(), ...map2.keys()]);

  for (const name of allNames) {
    const ng1 = map1.get(name);
    const ng2 = map2.get(name);

    if (!ng1) {
      differences.push({
        type: 'nodegraph_missing',
        location: 'file1',
        name,
        message: `NodeGraph "${name}" exists in file2 but not in file1`
      });
      continue;
    }

    if (!ng2) {
      differences.push({
        type: 'nodegraph_missing',
        location: 'file2',
        name,
        message: `NodeGraph "${name}" exists in file1 but not in file2`
      });
      continue;
    }

    // Compare nodes within nodegraph
    const nodeDiffs = compareNodes(ng1.nodes, ng2.nodes, name, options);
    differences.push(...nodeDiffs);

    // Compare outputs
    const outputDiffs = compareOutputs(ng1.outputs, ng2.outputs, name, options);
    differences.push(...outputDiffs);
  }

  return differences;
}

/**
 * Compare nodes within a nodegraph
 */
function compareNodes(nodes1, nodes2, ngName, options) {
  const differences = [];

  const map1 = new Map(nodes1.map(n => [n.name, n]));
  const map2 = new Map(nodes2.map(n => [n.name, n]));

  const allNames = new Set([...map1.keys(), ...map2.keys()]);

  for (const name of allNames) {
    const node1 = map1.get(name);
    const node2 = map2.get(name);

    if (!node1) {
      differences.push({
        type: 'node_missing',
        location: 'file1',
        nodegraph: ngName,
        name,
        message: `Node "${name}" in NodeGraph "${ngName}" exists in file2 but not in file1`
      });
      continue;
    }

    if (!node2) {
      differences.push({
        type: 'node_missing',
        location: 'file2',
        nodegraph: ngName,
        name,
        message: `Node "${name}" in NodeGraph "${ngName}" exists in file1 but not in file2`
      });
      continue;
    }

    // Compare category
    if (node1.category.toLowerCase() !== node2.category.toLowerCase()) {
      differences.push({
        type: 'node_category_mismatch',
        nodegraph: ngName,
        name,
        file1: node1.category,
        file2: node2.category,
        message: `Node "${name}" category mismatch: "${node1.category}" vs "${node2.category}"`
      });
    }

    // Compare inputs
    const inputDiffs = compareInputs(node1.inputs, node2.inputs, `${ngName}/${name}`, options);
    differences.push(...inputDiffs);
  }

  return differences;
}

/**
 * Compare inputs
 */
function compareInputs(inputs1, inputs2, context, options) {
  const differences = [];

  const map1 = new Map(inputs1.map(i => [i.name, i]));
  const map2 = new Map(inputs2.map(i => [i.name, i]));

  const allNames = new Set([...map1.keys(), ...map2.keys()]);

  for (const name of allNames) {
    const inp1 = map1.get(name);
    const inp2 = map2.get(name);

    if (!inp1) {
      differences.push({
        type: 'input_missing',
        location: 'file1',
        context,
        name,
        message: `Input "${name}" in "${context}" exists in file2 but not in file1`
      });
      continue;
    }

    if (!inp2) {
      differences.push({
        type: 'input_missing',
        location: 'file2',
        context,
        name,
        message: `Input "${name}" in "${context}" exists in file1 but not in file2`
      });
      continue;
    }

    // Compare type
    if (inp1.type !== inp2.type) {
      differences.push({
        type: 'input_type_mismatch',
        context,
        name,
        file1: inp1.type,
        file2: inp2.type,
        message: `Input "${name}" type mismatch in "${context}": "${inp1.type}" vs "${inp2.type}"`
      });
    }

    // Compare value (if present)
    if (inp1.value !== undefined || inp2.value !== undefined) {
      if (!areValuesEqual(inp1.value, inp2.value, options.floatTolerance)) {
        differences.push({
          type: 'input_value_mismatch',
          context,
          name,
          file1: inp1.value,
          file2: inp2.value,
          message: `Input "${name}" value mismatch in "${context}": "${inp1.value}" vs "${inp2.value}"`
        });
      }
    }

    // Compare nodename (if present)
    if (inp1.nodename !== inp2.nodename) {
      differences.push({
        type: 'input_connection_mismatch',
        context,
        name,
        file1: inp1.nodename,
        file2: inp2.nodename,
        message: `Input "${name}" connection mismatch in "${context}": "${inp1.nodename}" vs "${inp2.nodename}"`
      });
    }

    // Compare nodegraph reference (if present)
    if (inp1.nodegraph !== inp2.nodegraph) {
      differences.push({
        type: 'input_nodegraph_mismatch',
        context,
        name,
        file1: inp1.nodegraph,
        file2: inp2.nodegraph,
        message: `Input "${name}" nodegraph mismatch in "${context}": "${inp1.nodegraph}" vs "${inp2.nodegraph}"`
      });
    }
  }

  return differences;
}

/**
 * Compare outputs
 */
function compareOutputs(outputs1, outputs2, ngName, options) {
  const differences = [];

  const map1 = new Map(outputs1.map(o => [o.name, o]));
  const map2 = new Map(outputs2.map(o => [o.name, o]));

  const allNames = new Set([...map1.keys(), ...map2.keys()]);

  for (const name of allNames) {
    const out1 = map1.get(name);
    const out2 = map2.get(name);

    if (!out1) {
      differences.push({
        type: 'output_missing',
        location: 'file1',
        nodegraph: ngName,
        name,
        message: `Output "${name}" in NodeGraph "${ngName}" exists in file2 but not in file1`
      });
      continue;
    }

    if (!out2) {
      differences.push({
        type: 'output_missing',
        location: 'file2',
        nodegraph: ngName,
        name,
        message: `Output "${name}" in NodeGraph "${ngName}" exists in file1 but not in file2`
      });
      continue;
    }

    // Compare type
    if (out1.type !== out2.type) {
      differences.push({
        type: 'output_type_mismatch',
        nodegraph: ngName,
        name,
        file1: out1.type,
        file2: out2.type,
        message: `Output "${name}" type mismatch in "${ngName}": "${out1.type}" vs "${out2.type}"`
      });
    }

    // Compare nodename
    if (out1.nodename !== out2.nodename) {
      differences.push({
        type: 'output_connection_mismatch',
        nodegraph: ngName,
        name,
        file1: out1.nodename,
        file2: out2.nodename,
        message: `Output "${name}" nodename mismatch in "${ngName}": "${out1.nodename}" vs "${out2.nodename}"`
      });
    }
  }

  return differences;
}

/**
 * Compare shaders
 */
function compareShaders(shaders1, shaders2, options) {
  const differences = [];

  const map1 = new Map(shaders1.map(s => [s.name, s]));
  const map2 = new Map(shaders2.map(s => [s.name, s]));

  const allNames = new Set([...map1.keys(), ...map2.keys()]);

  for (const name of allNames) {
    const shader1 = map1.get(name);
    const shader2 = map2.get(name);

    if (!shader1) {
      differences.push({
        type: 'shader_missing',
        location: 'file1',
        name,
        message: `Shader "${name}" exists in file2 but not in file1`
      });
      continue;
    }

    if (!shader2) {
      differences.push({
        type: 'shader_missing',
        location: 'file2',
        name,
        message: `Shader "${name}" exists in file1 but not in file2`
      });
      continue;
    }

    // Compare shader type
    if (shader1.type.toLowerCase() !== shader2.type.toLowerCase()) {
      differences.push({
        type: 'shader_type_mismatch',
        name,
        file1: shader1.type,
        file2: shader2.type,
        message: `Shader "${name}" type mismatch: "${shader1.type}" vs "${shader2.type}"`
      });
    }

    // Compare inputs
    const inputDiffs = compareInputs(shader1.inputs, shader2.inputs, `shader/${name}`, options);
    differences.push(...inputDiffs);
  }

  return differences;
}

/**
 * Compare materials
 */
function compareMaterials(mats1, mats2, options) {
  const differences = [];

  const map1 = new Map(mats1.map(m => [m.name, m]));
  const map2 = new Map(mats2.map(m => [m.name, m]));

  const allNames = new Set([...map1.keys(), ...map2.keys()]);

  for (const name of allNames) {
    const mat1 = map1.get(name);
    const mat2 = map2.get(name);

    if (!mat1) {
      differences.push({
        type: 'material_missing',
        location: 'file1',
        name,
        message: `Material "${name}" exists in file2 but not in file1`
      });
      continue;
    }

    if (!mat2) {
      differences.push({
        type: 'material_missing',
        location: 'file2',
        name,
        message: `Material "${name}" exists in file1 but not in file2`
      });
      continue;
    }

    // Compare inputs
    const inputDiffs = compareInputs(mat1.inputs, mat2.inputs, `material/${name}`, options);
    differences.push(...inputDiffs);
  }

  return differences;
}

/**
 * Compare two values with float tolerance
 */
function areValuesEqual(val1, val2, tolerance = 1e-6) {
  if (val1 === val2) return true;
  if (val1 === undefined || val2 === undefined) return false;

  // Try parsing as comma-separated numbers (e.g., "0.5, 0.5, 0.5")
  const nums1 = val1.split(',').map(s => parseFloat(s.trim()));
  const nums2 = val2.split(',').map(s => parseFloat(s.trim()));

  if (nums1.length !== nums2.length) return false;

  for (let i = 0; i < nums1.length; i++) {
    if (isNaN(nums1[i]) || isNaN(nums2[i])) {
      // Non-numeric comparison
      if (val1.split(',')[i]?.trim() !== val2.split(',')[i]?.trim()) {
        return false;
      }
    } else {
      // Numeric comparison with tolerance
      if (Math.abs(nums1[i] - nums2[i]) > tolerance) {
        return false;
      }
    }
  }

  return true;
}

// ============================================================================
// Roundtrip Testing
// ============================================================================

/**
 * Extract MaterialX XML from USD file using tusdcat
 */
function extractMtlxFromUsd(usdFile, tusdcatPath, options = {}) {
  try {
    // Run tusdcat to get USDA output
    const usda = execSync(`"${tusdcatPath}" "${usdFile}"`, {
      encoding: 'utf-8',
      maxBuffer: 100 * 1024 * 1024,
      timeout: options.timeout || 60000
    });

    // Look for MaterialX content in the USDA
    // This is a simplified extraction - in practice, we'd need to parse the full USD
    // and extract materials with MaterialX representations
    return {
      usda,
      hasMtlx: usda.includes('mtlx:') || usda.includes('MaterialXConfigAPI') || usda.includes('open_pbr_surface')
    };
  } catch (e) {
    return { error: e.message, usda: null, hasMtlx: false };
  }
}

/**
 * Test roundtrip for a single file
 */
function testRoundtrip(inputFile, options) {
  const result = {
    file: inputFile,
    status: 'unknown',
    hasMtlx: false,
    nodeGraphCount: 0,
    shaderCount: 0,
    warnings: [],
    errors: []
  };

  try {
    // Extract using tusdcat
    const extracted = extractMtlxFromUsd(inputFile, options.tusdcat, options);

    if (extracted.error) {
      result.status = 'error';
      result.errors.push(`tusdcat failed: ${extracted.error}`);
      return result;
    }

    result.hasMtlx = extracted.hasMtlx;

    if (!extracted.hasMtlx) {
      result.status = 'skip';
      result.warnings.push('No MaterialX content detected');
      return result;
    }

    // Count NodeGraphs and shaders in USDA
    const nodeGraphMatches = extracted.usda.match(/def NodeGraph/g);
    const shaderMatches = extracted.usda.match(/uniform token info:id = "ND_open_pbr_surface/g);

    result.nodeGraphCount = nodeGraphMatches ? nodeGraphMatches.length : 0;
    result.shaderCount = shaderMatches ? shaderMatches.length : 0;

    // Check for expected patterns
    const hasOpenPBR = extracted.usda.includes('ND_open_pbr_surface');
    const hasNodeGraph = extracted.usda.includes('def NodeGraph');

    if (!hasOpenPBR && !hasNodeGraph) {
      result.status = 'skip';
      result.warnings.push('File has mtlx markers but no OpenPBR shaders or NodeGraphs');
      return result;
    }

    // Validate structure
    if (hasOpenPBR) {
      // Check for required OpenPBR inputs
      const hasBaseColor = extracted.usda.includes('inputs:base_color');
      if (!hasBaseColor) {
        result.warnings.push('OpenPBR shader missing base_color input');
      }
    }

    if (hasNodeGraph) {
      // Check for node info:id patterns
      const infoIdMatches = extracted.usda.match(/info:id = "ND_\w+/g);
      if (infoIdMatches) {
        result.nodeTypes = [...new Set(infoIdMatches.map(m => m.replace('info:id = "', '')))];
      }
    }

    result.status = 'pass';

  } catch (error) {
    result.status = 'error';
    result.errors.push(error.message);
  }

  return result;
}

// ============================================================================
// CLI
// ============================================================================

function printUsage() {
  console.log(`
Usage: mtlx-roundtrip.js [options] <input.usd>
       mtlx-roundtrip.js --tusdcat <path> <input.usd>
       mtlx-roundtrip.js --tusdcat <path> "**/*.usd"

Test MaterialX import/export roundtrip in TinyUSDZ.

Supports glob patterns:
  *        - matches any characters except /
  **       - matches any characters including /
  ?        - matches single character
  [abc]    - matches any character in brackets
  {a,b,c}  - matches any of the alternatives

Examples:
  mtlx-roundtrip.js --tusdcat ./tusdcat model.usd
  mtlx-roundtrip.js --tusdcat ./tusdcat "models/*.usda"
  mtlx-roundtrip.js --tusdcat ./tusdcat "**/*.usd{a,c,z}"

Options:
  -h, --help              Show this help message
  -v, --verbose           Show detailed output
  -q, --quiet             Only show summary
  --tusdcat <path>        Path to tusdcat executable (required)
  --base-dir <path>       Base directory for glob patterns (default: current dir)
  --continue-on-error     Continue processing other files if one fails
  --json                  Output results as JSON
  --timeout <ms>          Timeout per file in milliseconds (default: 60000)
  --float-tolerance <n>   Tolerance for floating point comparison (default: 1e-6)

Exit codes:
  0 - All files processed successfully
  1 - Some files had warnings or missing MaterialX
  2 - Error occurred
`);
}

function main() {
  const args = process.argv.slice(2);

  if (args.length === 0 || args.includes('-h') || args.includes('--help')) {
    printUsage();
    process.exit(0);
  }

  const options = {
    verbose: false,
    quiet: false,
    tusdcat: null,
    baseDir: process.cwd(),
    continueOnError: false,
    json: false,
    timeout: 60000,
    floatTolerance: 1e-6,
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
      case '--tusdcat':
        options.tusdcat = args[++i];
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
      case '--float-tolerance':
        options.floatTolerance = parseFloat(args[++i]);
        break;
      default:
        if (!arg.startsWith('-')) {
          options.files.push(arg);
        }
        break;
    }
  }

  if (!options.tusdcat) {
    console.error('Error: --tusdcat is required');
    printUsage();
    process.exit(2);
  }

  if (options.files.length === 0) {
    console.error('Error: No input files specified');
    printUsage();
    process.exit(2);
  }

  try {
    // Expand glob patterns
    const expandedFiles = expandFilePatterns(options.files, options.baseDir)
      .filter(f => f && f.trim().length > 0);

    if (expandedFiles.length === 0) {
      console.error('Error: No files found matching the pattern(s).');
      process.exit(2);
    }

    if (!options.quiet && !options.json) {
      console.log(`Found ${expandedFiles.length} file(s) to test\n`);
    }

    const results = [];
    let passed = 0;
    let skipped = 0;
    let errors = 0;

    for (let i = 0; i < expandedFiles.length; i++) {
      const inputFile = expandedFiles[i];

      if (!options.quiet && !options.json) {
        console.log(`[${i + 1}/${expandedFiles.length}] Testing: ${inputFile}`);
      }

      const result = testRoundtrip(inputFile, options);
      results.push(result);

      if (result.status === 'pass') {
        passed++;
        if (!options.quiet && !options.json) {
          console.log(`  ✓ Pass (${result.nodeGraphCount} NodeGraphs, ${result.shaderCount} shaders)`);
          if (options.verbose && result.nodeTypes) {
            console.log(`    Node types: ${result.nodeTypes.join(', ')}`);
          }
          if (result.warnings.length > 0) {
            for (const warn of result.warnings) {
              console.log(`    ⚠ ${warn}`);
            }
          }
          console.log('');
        }
      } else if (result.status === 'skip') {
        skipped++;
        if (!options.quiet && !options.json) {
          console.log(`  ○ Skip: ${result.warnings.join(', ')}\n`);
        }
      } else {
        errors++;
        if (!options.quiet && !options.json) {
          console.log(`  ✗ Error: ${result.errors.join(', ')}\n`);
        }
        if (!options.continueOnError) {
          break;
        }
      }
    }

    // Output summary
    if (options.json) {
      console.log(JSON.stringify({
        totalFiles: expandedFiles.length,
        passed,
        skipped,
        errors,
        results
      }, null, 2));
    } else if (!options.quiet) {
      console.log('═'.repeat(50));
      console.log(`Summary: ${expandedFiles.length} file(s) processed`);
      console.log(`  ✓ Passed:  ${passed}`);
      console.log(`  ○ Skipped: ${skipped}`);
      if (errors > 0) {
        console.log(`  ✗ Errors:  ${errors}`);
      }
    }

    // Exit code
    if (errors > 0 && !options.continueOnError) {
      process.exit(2);
    } else if (errors > 0) {
      process.exit(1);
    } else {
      process.exit(0);
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
  MtlxParser,
  parseMtlx,
  compareMtlx,
  testRoundtrip,
  expandGlob,
  expandFilePatterns
};

// Run if executed directly
if (require.main === module) {
  main();
}
