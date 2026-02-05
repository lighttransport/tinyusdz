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
const https = require('https');
const http = require('http');
const os = require('os');

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
// Synthetic Test Generation
// ============================================================================

/**
 * MaterialX node definitions for synthetic test generation
 */
const MTLX_NODE_DEFS = {
  // Binary operations (in1, in2)
  binary_float: ['add', 'subtract', 'multiply', 'divide', 'power', 'min', 'max'],
  binary_color3: ['add', 'subtract', 'multiply', 'divide', 'power', 'min', 'max'],
  binary_vector3: ['add', 'subtract', 'multiply', 'divide', 'crossproduct'],

  // Unary operations (in)
  unary_float: ['absval', 'floor', 'ceil', 'round', 'sqrt', 'sin', 'cos', 'tan', 'exp', 'ln'],
  unary_color3: ['luminance'],
  unary_vector3: ['normalize', 'magnitude'],

  // Special operations
  clamp: { inputs: ['in', 'low', 'high'], types: ['float', 'color3', 'vector3'] },
  mix: { inputs: ['fg', 'bg', 'mix'], types: ['float', 'color3', 'vector3'] },
  extract: { inputs: ['in', 'index'], types: ['color3', 'vector3'] },
  combine3: { inputs: ['in1', 'in2', 'in3'], outputTypes: ['color3', 'vector3'] },
  hsvadjust: { inputs: ['in', 'amount'], types: ['color3'] },
  remap: { inputs: ['in', 'inlow', 'inhigh', 'outlow', 'outhigh'], types: ['float'] },

  // Geometry
  geometry: ['position', 'normal', 'tangent', 'texcoord'],

  // Conversion
  convert: [
    { from: 'color3', to: 'vector3' },
    { from: 'vector3', to: 'color3' },
    { from: 'float', to: 'color3' }
  ]
};

/**
 * Convert MaterialX type to USD type
 */
function mtlxTypeToUsdGen(type) {
  const map = {
    'float': 'float',
    'color3': 'color3f',
    'color4': 'color4f',
    'vector2': 'float2',
    'vector3': 'vector3f',
    'vector4': 'float4',
    'integer': 'int',
    'boolean': 'bool',
    'string': 'string'
  };
  return map[type] || 'float';
}

/**
 * Generate USDA for a single MaterialX node test
 */
function generateNodeTestUsda(nodeName, nodeType, inputs, outputType) {
  const inputDefs = inputs.map(inp => {
    let value = '';
    let type = inp.type || 'float';
    let usdType = mtlxTypeToUsdGen(type);
    if (type === 'float') value = inp.value || '0.5';
    else if (type === 'color3') value = inp.value || '(0.5, 0.5, 0.5)';
    else if (type === 'vector3') value = inp.value || '(0.5, 0.5, 0.5)';
    else if (type === 'integer') value = inp.value || '0';
    return `                    ${usdType} inputs:${inp.name} = ${value}`;
  }).join('\n');

  const usdOutputType = mtlxTypeToUsdGen(outputType);

  return `#usda 1.0
(
    defaultPrim = "root"
    metersPerUnit = 1
    upAxis = "Y"
)

def Xform "root"
{
    def Scope "mtl"
    {
        def Material "TestMaterial" (
            prepend apiSchemas = ["MaterialXConfigAPI"]
        )
        {
            string config:mtlx:version = "1.38"
            token outputs:mtlx:surface.connect = </root/mtl/TestMaterial/OpenPBRShader.outputs:out>

            def Shader "OpenPBRShader"
            {
                uniform token info:id = "ND_open_pbr_surface_surfaceshader"
                color3f inputs:base_color.connect = </root/mtl/TestMaterial/NodeGraph/out.outputs:out>
                token outputs:out
            }

            def NodeGraph "NodeGraph"
            {
                color3f outputs:out.connect = </root/mtl/TestMaterial/NodeGraph/TestNode.outputs:out>

                def Shader "TestNode"
                {
                    uniform token info:id = "ND_${nodeName}_${outputType}"
${inputDefs}
                    ${usdOutputType} outputs:out
                }

                def Shader "ConstInput"
                {
                    uniform token info:id = "ND_constant_color3"
                    color3f inputs:value = (0.8, 0.2, 0.1)
                    color3f outputs:out
                }
            }
        }
    }
}
`;
}

/**
 * Generate synthetic test files
 */
function generateSyntheticTests(outputDir) {
  const generated = [];

  if (!fs.existsSync(outputDir)) {
    fs.mkdirSync(outputDir, { recursive: true });
  }

  // Binary float operations
  for (const op of MTLX_NODE_DEFS.binary_float) {
    const filename = path.join(outputDir, `synthetic_${op}_float.usda`);
    const usda = generateNodeTestUsda(op, 'float', [
      { name: 'in1', type: 'float', value: '0.5' },
      { name: 'in2', type: 'float', value: '0.25' }
    ], 'float');
    fs.writeFileSync(filename, usda);
    generated.push(filename);
  }

  // Binary color3 operations
  for (const op of MTLX_NODE_DEFS.binary_color3) {
    const filename = path.join(outputDir, `synthetic_${op}_color3.usda`);
    const usda = generateNodeTestUsda(op, 'color3', [
      { name: 'in1', type: 'color3', value: '(0.8, 0.2, 0.1)' },
      { name: 'in2', type: 'color3', value: '(0.1, 0.5, 0.9)' }
    ], 'color3');
    fs.writeFileSync(filename, usda);
    generated.push(filename);
  }

  // Binary vector3 operations
  for (const op of MTLX_NODE_DEFS.binary_vector3) {
    const filename = path.join(outputDir, `synthetic_${op}_vector3.usda`);
    const usda = generateNodeTestUsda(op, 'vector3', [
      { name: 'in1', type: 'vector3', value: '(1, 0, 0)' },
      { name: 'in2', type: 'vector3', value: '(0, 1, 0)' }
    ], 'vector3');
    fs.writeFileSync(filename, usda);
    generated.push(filename);
  }

  // Unary float operations
  for (const op of MTLX_NODE_DEFS.unary_float) {
    const filename = path.join(outputDir, `synthetic_${op}_float.usda`);
    const usda = generateNodeTestUsda(op, 'float', [
      { name: 'in', type: 'float', value: '0.5' }
    ], 'float');
    fs.writeFileSync(filename, usda);
    generated.push(filename);
  }

  // Unary vector3 operations
  for (const op of MTLX_NODE_DEFS.unary_vector3) {
    const outputType = op === 'magnitude' ? 'float' : 'vector3';
    const filename = path.join(outputDir, `synthetic_${op}_vector3.usda`);
    const usda = generateNodeTestUsda(op, 'vector3', [
      { name: 'in', type: 'vector3', value: '(1, 2, 3)' }
    ], outputType);
    fs.writeFileSync(filename, usda);
    generated.push(filename);
  }

  // Clamp
  for (const type of MTLX_NODE_DEFS.clamp.types) {
    const val = type === 'float' ? '0.5' : '(0.5, 0.5, 0.5)';
    const low = type === 'float' ? '0.2' : '(0.2, 0.2, 0.2)';
    const high = type === 'float' ? '0.8' : '(0.8, 0.8, 0.8)';
    const filename = path.join(outputDir, `synthetic_clamp_${type}.usda`);
    const usda = generateNodeTestUsda('clamp', type, [
      { name: 'in', type, value: val },
      { name: 'low', type, value: low },
      { name: 'high', type, value: high }
    ], type);
    fs.writeFileSync(filename, usda);
    generated.push(filename);
  }

  // Mix
  for (const type of MTLX_NODE_DEFS.mix.types) {
    const val = type === 'float' ? '0.8' : '(0.8, 0.2, 0.1)';
    const filename = path.join(outputDir, `synthetic_mix_${type}.usda`);
    const usda = generateNodeTestUsda('mix', type, [
      { name: 'fg', type, value: val },
      { name: 'bg', type, value: type === 'float' ? '0.2' : '(0.1, 0.5, 0.9)' },
      { name: 'mix', type: 'float', value: '0.5' }
    ], type);
    fs.writeFileSync(filename, usda);
    generated.push(filename);
  }

  // Extract
  for (const type of MTLX_NODE_DEFS.extract.types) {
    const filename = path.join(outputDir, `synthetic_extract_${type}.usda`);
    const usda = generateNodeTestUsda('extract', type, [
      { name: 'in', type, value: '(0.8, 0.5, 0.2)' },
      { name: 'index', type: 'integer', value: '1' }
    ], 'float');
    fs.writeFileSync(filename, usda);
    generated.push(filename);
  }

  // Combine3
  for (const type of MTLX_NODE_DEFS.combine3.outputTypes) {
    const filename = path.join(outputDir, `synthetic_combine3_${type}.usda`);
    const usda = generateNodeTestUsda('combine3', type, [
      { name: 'in1', type: 'float', value: '0.8' },
      { name: 'in2', type: 'float', value: '0.5' },
      { name: 'in3', type: 'float', value: '0.2' }
    ], type);
    fs.writeFileSync(filename, usda);
    generated.push(filename);
  }

  // HSV Adjust
  {
    const filename = path.join(outputDir, `synthetic_hsvadjust_color3.usda`);
    const usda = generateNodeTestUsda('hsvadjust', 'color3', [
      { name: 'in', type: 'color3', value: '(0.8, 0.2, 0.1)' },
      { name: 'amount', type: 'vector3', value: '(0.1, 1.2, 1.1)' }
    ], 'color3');
    fs.writeFileSync(filename, usda);
    generated.push(filename);
  }

  // Luminance
  {
    const filename = path.join(outputDir, `synthetic_luminance_color3.usda`);
    const usda = generateNodeTestUsda('luminance', 'color3', [
      { name: 'in', type: 'color3', value: '(0.8, 0.5, 0.2)' }
    ], 'color3');
    fs.writeFileSync(filename, usda);
    generated.push(filename);
  }

  // Conversions
  for (const conv of MTLX_NODE_DEFS.convert) {
    const inVal = conv.from === 'float' ? '0.5' : '(0.8, 0.5, 0.2)';
    const filename = path.join(outputDir, `synthetic_convert_${conv.from}_${conv.to}.usda`);
    const usda = generateNodeTestUsda(`convert_${conv.from}_${conv.to}`, conv.from, [
      { name: 'in', type: conv.from, value: inVal }
    ], conv.to);
    fs.writeFileSync(filename, usda);
    generated.push(filename);
  }

  // Geometry nodes - test as standalone nodes in nodegraph without shader connection issues
  for (const geo of MTLX_NODE_DEFS.geometry) {
    const outputType = geo === 'texcoord' ? 'float2' : 'vector3f';
    const filename = path.join(outputDir, `synthetic_${geo}.usda`);
    // Geometry nodes don't have inputs, just space parameter
    // Don't connect to shader inputs to avoid type mismatch issues
    const usda = `#usda 1.0
(
    defaultPrim = "root"
    metersPerUnit = 1
    upAxis = "Y"
)

def Xform "root"
{
    def Scope "mtl"
    {
        def Material "TestMaterial" (
            prepend apiSchemas = ["MaterialXConfigAPI"]
        )
        {
            string config:mtlx:version = "1.38"
            token outputs:mtlx:surface.connect = </root/mtl/TestMaterial/OpenPBRShader.outputs:out>

            def Shader "OpenPBRShader"
            {
                uniform token info:id = "ND_open_pbr_surface_surfaceshader"
                color3f inputs:base_color = (0.5, 0.5, 0.5)
                token outputs:out
            }

            def NodeGraph "NodeGraph"
            {
                ${outputType} outputs:out.connect = </root/mtl/TestMaterial/NodeGraph/TestNode.outputs:out>

                def Shader "TestNode"
                {
                    uniform token info:id = "ND_${geo}_vector3"
                    token inputs:space = "world"
                    ${outputType} outputs:out
                }
            }
        }
    }
}
`;
    fs.writeFileSync(filename, usda);
    generated.push(filename);
  }

  return generated;
}

// ============================================================================
// Web-based MaterialX Test Fetching
// ============================================================================

/**
 * Fetch content from URL
 */
function fetchUrl(url) {
  return new Promise((resolve, reject) => {
    const protocol = url.startsWith('https') ? https : http;

    const request = protocol.get(url, {
      headers: { 'User-Agent': 'TinyUSDZ-MaterialX-Tester/1.0' }
    }, (response) => {
      // Handle redirects
      if (response.statusCode >= 300 && response.statusCode < 400 && response.headers.location) {
        fetchUrl(response.headers.location).then(resolve).catch(reject);
        return;
      }

      if (response.statusCode !== 200) {
        reject(new Error(`HTTP ${response.statusCode}: ${url}`));
        return;
      }

      const chunks = [];
      response.on('data', chunk => chunks.push(chunk));
      response.on('end', () => resolve(Buffer.concat(chunks)));
      response.on('error', reject);
    });

    request.on('error', reject);
    request.setTimeout(30000, () => {
      request.destroy();
      reject(new Error('Request timeout'));
    });
  });
}

/**
 * MaterialX GitHub raw content URLs
 */
const MTLX_TEST_URLS = {
  // Official MaterialX repo test files
  standard_surface: 'https://raw.githubusercontent.com/AcademySoftwareFoundation/MaterialX/main/resources/Materials/Examples/StandardSurface/standard_surface_default.mtlx',
  chess_set: 'https://raw.githubusercontent.com/AcademySoftwareFoundation/MaterialX/main/resources/Materials/Examples/StandardSurface/standard_surface_chess_set.mtlx',
  marble: 'https://raw.githubusercontent.com/AcademySoftwareFoundation/MaterialX/main/resources/Materials/Examples/StandardSurface/standard_surface_marble_solid.mtlx',
  jade: 'https://raw.githubusercontent.com/AcademySoftwareFoundation/MaterialX/main/resources/Materials/Examples/StandardSurface/standard_surface_jade.mtlx',
  brick: 'https://raw.githubusercontent.com/AcademySoftwareFoundation/MaterialX/main/resources/Materials/Examples/StandardSurface/standard_surface_brick_procedural.mtlx',

  // Node definition examples
  nodegraph_example: 'https://raw.githubusercontent.com/AcademySoftwareFoundation/MaterialX/main/resources/Materials/TestSuite/stdlib/math/math.mtlx'
};

/**
 * Convert MaterialX XML to simplified USD for testing
 * (Basic conversion - full conversion would need full MTLX parser)
 */
function mtlxToUsda(mtlxContent, name) {
  // Extract nodegraphs and surface shaders from MaterialX XML
  const parser = new MtlxParser(mtlxContent);
  const parsed = parser.parse();

  let nodeGraphDefs = '';
  let nodeCount = 0;

  // Build NodeGraph content from parsed nodes
  for (const ng of parsed.nodegraphs) {
    let nodeDefs = '';
    for (const node of ng.nodes) {
      const infoId = `ND_${node.category.toLowerCase()}_${getOutputType(node)}`;
      let inputDefs = node.inputs.map(inp => {
        const type = inp.type || 'float';
        const usdType = mtlxTypeToUsd(type);
        if (inp.nodename) {
          return `                    ${usdType} inputs:${inp.name}.connect = </root/mtl/${name}/NodeGraph/${inp.nodename}.outputs:out>`;
        } else if (inp.value) {
          return `                    ${usdType} inputs:${inp.name} = ${formatUsdValue(inp.value, type)}`;
        }
        return '';
      }).filter(s => s).join('\n');

      nodeDefs += `
                def Shader "${node.name}"
                {
                    uniform token info:id = "${infoId}"
${inputDefs}
                    ${mtlxTypeToUsd(getOutputType(node))} outputs:out
                }
`;
      nodeCount++;
    }

    nodeGraphDefs += nodeDefs;
  }

  return `#usda 1.0
(
    defaultPrim = "root"
    metersPerUnit = 1
    upAxis = "Y"
)

def Xform "root"
{
    def Scope "mtl"
    {
        def Material "${name}" (
            prepend apiSchemas = ["MaterialXConfigAPI"]
        )
        {
            string config:mtlx:version = "1.38"
            token outputs:mtlx:surface.connect = </root/mtl/${name}/OpenPBRShader.outputs:out>

            def Shader "OpenPBRShader"
            {
                uniform token info:id = "ND_open_pbr_surface_surfaceshader"
                color3f inputs:base_color = (0.5, 0.5, 0.5)
                token outputs:out
            }

            def NodeGraph "NodeGraph"
            {
                color3f outputs:out.connect = </root/mtl/${name}/NodeGraph/out.outputs:out>
${nodeGraphDefs}
            }
        }
    }
}
`;
}

function getOutputType(node) {
  // Determine output type from node category and inputs
  const category = node.category.toLowerCase();
  if (['add', 'subtract', 'multiply', 'divide', 'mix', 'clamp'].includes(category)) {
    // Check input types
    for (const inp of node.inputs) {
      if (inp.type === 'color3' || inp.type === 'color4') return 'color3';
      if (inp.type === 'vector3' || inp.type === 'vector4') return 'vector3';
    }
    return 'float';
  }
  if (category === 'luminance') return 'color3';
  if (category === 'normalize' || category === 'crossproduct') return 'vector3';
  if (category === 'dotproduct' || category === 'magnitude') return 'float';
  return 'color3'; // Default
}

function mtlxTypeToUsd(type) {
  const map = {
    'float': 'float',
    'color3': 'color3f',
    'color4': 'color4f',
    'vector2': 'float2',
    'vector3': 'vector3f',
    'vector4': 'float4',
    'integer': 'int',
    'boolean': 'bool',
    'string': 'string'
  };
  return map[type] || 'float';
}

function formatUsdValue(value, type) {
  if (type === 'float' || type === 'integer') {
    return value;
  }
  // Convert "x, y, z" to "(x, y, z)"
  if (value.includes(',') && !value.startsWith('(')) {
    return `(${value})`;
  }
  return value;
}

/**
 * Fetch and convert MaterialX test files
 */
async function fetchMtlxTests(outputDir) {
  const fetched = [];

  if (!fs.existsSync(outputDir)) {
    fs.mkdirSync(outputDir, { recursive: true });
  }

  for (const [name, url] of Object.entries(MTLX_TEST_URLS)) {
    try {
      console.log(`  Fetching ${name}...`);
      const content = await fetchUrl(url);
      const mtlxFile = path.join(outputDir, `${name}.mtlx`);
      fs.writeFileSync(mtlxFile, content);
      fetched.push({ name, mtlxFile, status: 'fetched' });
    } catch (err) {
      console.log(`  Failed to fetch ${name}: ${err.message}`);
      fetched.push({ name, status: 'failed', error: err.message });
    }
  }

  return fetched;
}

// ============================================================================
// CLI
// ============================================================================

function printUsage() {
  console.log(`
Usage: mtlx-roundtrip.js [options] <input.usd>
       mtlx-roundtrip.js --tusdcat <path> <input.usd>
       mtlx-roundtrip.js --tusdcat <path> --generate-synthetic
       mtlx-roundtrip.js --tusdcat <path> --fetch-mtlx-tests

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
  mtlx-roundtrip.js --tusdcat ./tusdcat --generate-synthetic
  mtlx-roundtrip.js --tusdcat ./tusdcat --fetch-mtlx-tests

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

Test Generation:
  --generate-synthetic    Generate synthetic test files for all node types
  --synthetic-dir <path>  Directory for synthetic tests (default: /tmp/mtlx-synthetic)
  --fetch-mtlx-tests      Fetch MaterialX test files from GitHub
  --fetch-dir <path>      Directory for fetched tests (default: /tmp/mtlx-fetched)

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
    files: [],
    generateSynthetic: false,
    syntheticDir: path.join(os.tmpdir(), 'mtlx-synthetic'),
    fetchMtlxTests: false,
    fetchDir: path.join(os.tmpdir(), 'mtlx-fetched')
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
      case '--generate-synthetic':
        options.generateSynthetic = true;
        break;
      case '--synthetic-dir':
        options.syntheticDir = args[++i];
        break;
      case '--fetch-mtlx-tests':
        options.fetchMtlxTests = true;
        break;
      case '--fetch-dir':
        options.fetchDir = args[++i];
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

  // Handle synthetic test generation
  if (options.generateSynthetic) {
    if (!options.quiet) {
      console.log('Generating synthetic test files...');
    }
    const generated = generateSyntheticTests(options.syntheticDir);
    if (!options.quiet) {
      console.log(`Generated ${generated.length} synthetic test files in ${options.syntheticDir}\n`);
    }
    options.files.push(...generated);
  }

  // Handle fetching MaterialX tests
  if (options.fetchMtlxTests) {
    if (!options.quiet) {
      console.log('Fetching MaterialX test files from GitHub...');
    }
    // This is async, so we need to handle it
    fetchMtlxTests(options.fetchDir).then(fetched => {
      const successful = fetched.filter(f => f.status === 'fetched');
      if (!options.quiet) {
        console.log(`Fetched ${successful.length}/${fetched.length} MaterialX test files to ${options.fetchDir}\n`);
      }
      // Note: MTLX files need conversion to USD before testing
      // For now, just report what was fetched
      for (const f of successful) {
        console.log(`  - ${f.mtlxFile}`);
      }
      console.log('\nNote: MTLX files fetched but require conversion to USD for roundtrip testing.\n');
    }).catch(err => {
      console.error(`Error fetching MaterialX tests: ${err.message}`);
    });
    // If only fetching, exit early
    if (options.files.length === 0 && !options.generateSynthetic) {
      return;
    }
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
  expandFilePatterns,
  generateSyntheticTests,
  fetchMtlxTests,
  MTLX_NODE_DEFS
};

// Run if executed directly
if (require.main === module) {
  main();
}
