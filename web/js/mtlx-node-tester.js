/**
 * MaterialX Node Graph Compile & Run Tester
 *
 * Validates MtlxNodeGraphProcessor node evaluation functions and the
 * optimization pipeline with synthetic graphs.  Pure CPU — no GPU needed.
 */

import { MtlxNodeGraphProcessor, MtlxNodeEval } from 'tinyusdz/TinyUSDZOpenPBR_WebGL.js';
import {
    optimizeNodeGraph,
    NodeGraphOptimizationLevel,
    markActiveNodes,
    removeInactiveNodes
} from 'tinyusdz/TinyUSDZMaterialX.js';

// ============================================================================
// Graph Builder Helpers
// ============================================================================

/** Create a constant node.  Auto-detects float vs color3. */
function makeConst(name, value) {
    const isArr = Array.isArray(value);
    return {
        name,
        category: isArr ? `constant_color${value.length}` : 'constant_float',
        type: isArr ? `color${value.length}` : 'float',
        value,
        inputs: [{ name: 'value', value }]
    };
}

/** Create a unary op node.  `inp` is {nodename} or {value}. */
function makeUnary(name, op, inp) {
    return { name, category: op, inputs: [{ name: 'in', ...inp }] };
}

/** Create a binary op node. */
function makeBinary(name, op, in1, in2) {
    return { name, category: op, inputs: [{ name: 'in1', ...in1 }, { name: 'in2', ...in2 }] };
}

/** Create a mix node (bg, fg, mix). */
function makeMix(name, bg, fg, mix) {
    return {
        name, category: 'mix',
        inputs: [{ name: 'bg', ...bg }, { name: 'fg', ...fg }, { name: 'mix', ...mix }]
    };
}

/** Wrap nodes + outputNode name into a full graph structure. */
function makeGraph(nodes, outputNode, outputName = 'out') {
    return {
        nodegraph: {
            nodes,
            outputs: [{ name: outputName, nodename: outputNode }]
        }
    };
}

/** Shorthand: build a one-node constant graph and return its expected output. */
function constGraph(val) {
    return makeGraph([makeConst('c', val)], 'c');
}

/** Shorthand for connection ref */
function conn(nodename) { return { nodename }; }
/** Shorthand for literal value */
function val(v) { return { value: v }; }

// ============================================================================
// Comparison Utilities
// ============================================================================

function approxEqual(a, b, tol) {
    if (typeof a === 'number' && typeof b === 'number') {
        return Math.abs(a - b) <= tol;
    }
    if (Array.isArray(a) && Array.isArray(b)) {
        if (a.length !== b.length) return false;
        return a.every((v, i) => Math.abs(v - b[i]) <= tol);
    }
    // Strict equality for non-numeric (should not happen in normal tests)
    return a === b;
}

function maxError(a, b) {
    if (typeof a === 'number' && typeof b === 'number') return Math.abs(a - b);
    if (Array.isArray(a) && Array.isArray(b)) {
        return Math.max(...a.map((v, i) => Math.abs(v - (b[i] ?? 0))));
    }
    return Infinity;
}

function fmt(v) {
    if (v === undefined) return 'undefined';
    if (v === null) return 'null';
    if (typeof v === 'number') return v.toPrecision(8);
    if (Array.isArray(v)) return `[${v.map(x => typeof x === 'number' ? x.toPrecision(6) : String(x)).join(', ')}]`;
    if (typeof v === 'object') return JSON.stringify(v);
    return String(v);
}

// ============================================================================
// Test Generation Functions
// ============================================================================

function generateUnaryMathTests() {
    const tests = [];
    const ops = [
        ['absval',       -0.7,  0.7],
        ['absval',       0.3,   0.3],
        ['negate',       0.6,  -0.6],
        ['sqrt',         0.25,  0.5],
        ['floor',        2.7,   2],
        ['floor',       -1.3,  -2],
        ['ceil',         2.3,   3],
        ['ceil',        -1.7,  -1],
        ['round',        2.5,   3],
        ['round',        2.4,   2],
        ['fract',        3.75,  0.75],
        ['fract',       -0.25,  0.75],  // fract(-0.25) = -0.25 - floor(-0.25) = -0.25 - (-1) = 0.75
        ['sign',         5,     1],
        ['sign',        -3,    -1],
        ['sign',         0,     0],
        ['sin',          0,     0],
        ['cos',          0,     1],
        ['exp',          0,     1],
        ['exp',          1,     Math.E],
        ['ln',           1,     0],
        ['ln',           Math.E, 1],
        ['inversesqrt',  4,     0.5],
        ['saturate',     1.5,   1],
        ['saturate',    -0.5,   0],
        ['saturate',     0.3,   0.3],
    ];

    for (const [op, input, expected] of ops) {
        const suffix = typeof input === 'number' ? 'float' : 'color3';
        tests.push({
            name: `${op}_${suffix}_${fmt(input)}`,
            category: 'Unary Math',
            graph: makeGraph([
                makeConst('a', input),
                makeUnary('r', op, conn('a'))
            ], 'r'),
            expected,
            tolerance: 1e-6
        });
    }

    // Color3 variants for key unary ops
    const c3ops = [
        ['absval', [-0.3, 0.5, -0.8], [0.3, 0.5, 0.8]],
        ['negate', [1, 2, 3], [-1, -2, -3]],
        ['sqrt',   [0.04, 0.09, 0.16], [0.2, 0.3, 0.4]],
        ['floor',  [1.9, 2.1, -0.5], [1, 2, -1]],
    ];

    for (const [op, input, expected] of c3ops) {
        tests.push({
            name: `${op}_color3`,
            category: 'Unary Math',
            graph: makeGraph([
                makeConst('a', input),
                makeUnary('r', op, conn('a'))
            ], 'r'),
            expected,
            tolerance: 1e-6
        });
    }

    return tests;
}

function generateBinaryMathTests() {
    const tests = [];

    // Scalar ops
    const scalarOps = [
        ['add',      3,   4,   7],
        ['subtract', 10,  3,   7],
        ['multiply', 3,   4,   12],
        ['divide',   10,  4,   2.5],
        ['power',    2,   10,  1024],
        ['min',      3,   7,   3],
        ['max',      3,   7,   7],
    ];
    for (const [op, a, b, expected] of scalarOps) {
        tests.push({
            name: `${op}_float`,
            category: 'Binary Math',
            graph: makeGraph([
                makeConst('a', a), makeConst('b', b),
                makeBinary('r', op, conn('a'), conn('b'))
            ], 'r'),
            expected,
            tolerance: 1e-6
        });
    }

    // Color3 ops
    const c3ops = [
        ['add',      [1, 2, 3],   [4, 5, 6],   [5, 7, 9]],
        ['subtract', [5, 7, 9],   [1, 2, 3],   [4, 5, 6]],
        ['multiply', [2, 3, 4],   [3, 2, 1],   [6, 6, 4]],
        ['divide',   [10, 6, 8],  [2, 3, 4],   [5, 2, 2]],
        ['power',    [4, 9, 16],  [0.5, 0.5, 0.5], [2, 3, 4]],
        ['min',      [1, 5, 3],   [4, 2, 6],   [1, 2, 3]],
        ['max',      [1, 5, 3],   [4, 2, 6],   [4, 5, 6]],
    ];
    for (const [op, a, b, expected] of c3ops) {
        tests.push({
            name: `${op}_color3`,
            category: 'Binary Math',
            graph: makeGraph([
                makeConst('a', a), makeConst('b', b),
                makeBinary('r', op, conn('a'), conn('b'))
            ], 'r'),
            expected,
            tolerance: 1e-6
        });
    }

    // Broadcast: scalar * color3
    tests.push({
        name: 'multiply_broadcast',
        category: 'Binary Math',
        graph: makeGraph([
            makeConst('a', 2),
            makeConst('b', [3, 4, 5]),
            makeBinary('r', 'multiply', conn('a'), conn('b'))
        ], 'r'),
        expected: [6, 8, 10],
        tolerance: 1e-6
    });

    // Clamp
    tests.push({
        name: 'clamp_float',
        category: 'Binary Math',
        graph: makeGraph([
            makeConst('v', 1.5),
            { name: 'r', category: 'clamp', inputs: [
                { name: 'in', nodename: 'v' },
                { name: 'low', value: 0 },
                { name: 'high', value: 1 }
            ]}
        ], 'r'),
        expected: 1,
        tolerance: 1e-6
    });

    tests.push({
        name: 'clamp_low',
        category: 'Binary Math',
        graph: makeGraph([
            makeConst('v', -0.5),
            { name: 'r', category: 'clamp', inputs: [
                { name: 'in', nodename: 'v' },
                { name: 'low', value: 0 },
                { name: 'high', value: 1 }
            ]}
        ], 'r'),
        expected: 0,
        tolerance: 1e-6
    });

    return tests;
}

function generateColorOpTests() {
    const tests = [];

    // Mix
    tests.push({
        name: 'mix_float_t0',
        category: 'Color Ops',
        graph: makeGraph([
            makeConst('a', 10), makeConst('b', 20), makeConst('t', 0),
            makeMix('r', conn('a'), conn('b'), conn('t'))
        ], 'r'),
        expected: 10,
        tolerance: 1e-6
    });
    tests.push({
        name: 'mix_float_t1',
        category: 'Color Ops',
        graph: makeGraph([
            makeConst('a', 10), makeConst('b', 20), makeConst('t', 1),
            makeMix('r', conn('a'), conn('b'), conn('t'))
        ], 'r'),
        expected: 20,
        tolerance: 1e-6
    });
    tests.push({
        name: 'mix_float_half',
        category: 'Color Ops',
        graph: makeGraph([
            makeConst('a', 0), makeConst('b', 1), makeConst('t', 0.5),
            makeMix('r', conn('a'), conn('b'), conn('t'))
        ], 'r'),
        expected: 0.5,
        tolerance: 1e-6
    });
    tests.push({
        name: 'mix_color3',
        category: 'Color Ops',
        graph: makeGraph([
            makeConst('a', [1, 0, 0]), makeConst('b', [0, 0, 1]), makeConst('t', 0.5),
            makeMix('r', conn('a'), conn('b'), conn('t'))
        ], 'r'),
        expected: [0.5, 0, 0.5],
        tolerance: 1e-6
    });

    // Invert
    tests.push({
        name: 'invert_color3',
        category: 'Color Ops',
        graph: makeGraph([
            makeConst('c', [0.2, 0.5, 0.8]),
            { name: 'r', category: 'invert', inputs: [
                { name: 'in', nodename: 'c' }, { name: 'amount', value: 1 }
            ]}
        ], 'r'),
        expected: [0.8, 0.5, 0.2],
        tolerance: 1e-6
    });

    // Invert with fac=0 (no effect)
    tests.push({
        name: 'invert_fac0',
        category: 'Color Ops',
        graph: makeGraph([
            makeConst('c', [0.2, 0.5, 0.8]),
            { name: 'r', category: 'invert', inputs: [
                { name: 'in', nodename: 'c' }, { name: 'amount', value: 0 }
            ]}
        ], 'r'),
        expected: [0.2, 0.5, 0.8],
        tolerance: 1e-6
    });

    // Contrast
    tests.push({
        name: 'contrast_identity',
        category: 'Color Ops',
        graph: makeGraph([
            makeConst('c', 0.7),
            { name: 'r', category: 'contrast', inputs: [
                { name: 'in', nodename: 'c' },
                { name: 'amount', value: 1 },
                { name: 'pivot', value: 0.5 }
            ]}
        ], 'r'),
        expected: 0.7,  // (0.7 - 0.5) * 1 + 0.5 = 0.7
        tolerance: 1e-6
    });
    tests.push({
        name: 'contrast_amplify',
        category: 'Color Ops',
        graph: makeGraph([
            makeConst('c', 0.7),
            { name: 'r', category: 'contrast', inputs: [
                { name: 'in', nodename: 'c' },
                { name: 'amount', value: 2 },
                { name: 'pivot', value: 0.5 }
            ]}
        ], 'r'),
        expected: 0.9,  // (0.7 - 0.5) * 2 + 0.5 = 0.9
        tolerance: 1e-6
    });

    // Luminance
    tests.push({
        name: 'luminance_white',
        category: 'Color Ops',
        graph: makeGraph([
            makeConst('c', [1, 1, 1]),
            makeUnary('r', 'luminance', conn('c'))
        ], 'r'),
        expected: 1.0,  // 0.2126 + 0.7152 + 0.0722 = 1.0
        tolerance: 1e-6
    });
    tests.push({
        name: 'luminance_red',
        category: 'Color Ops',
        graph: makeGraph([
            makeConst('c', [1, 0, 0]),
            makeUnary('r', 'luminance', conn('c'))
        ], 'r'),
        expected: 0.2126,
        tolerance: 1e-6
    });

    // RGB ↔ HSV roundtrip
    tests.push({
        name: 'rgbtohsv_red',
        category: 'Color Ops',
        graph: makeGraph([
            makeConst('c', [1, 0, 0]),
            makeUnary('r', 'rgbtohsv', conn('c'))
        ], 'r'),
        expected: [0, 1, 1],  // red = hue 0
        tolerance: 1e-6
    });
    tests.push({
        name: 'hsv_roundtrip',
        category: 'Color Ops',
        graph: makeGraph([
            makeConst('c', [0.3, 0.6, 0.9]),
            makeUnary('h', 'rgbtohsv', conn('c')),
            makeUnary('r', 'hsvtorgb', conn('h'))
        ], 'r'),
        expected: [0.3, 0.6, 0.9],
        tolerance: 1e-4
    });

    // hsvadjust
    tests.push({
        name: 'hsvadjust_identity',
        category: 'Color Ops',
        graph: makeGraph([
            makeConst('c', [0.5, 0.3, 0.8]),
            makeConst('amt', [0, 1, 1]),
            { name: 'r', category: 'hsvadjust', inputs: [
                { name: 'in', nodename: 'c' }, { name: 'amount', nodename: 'amt' }
            ]}
        ], 'r'),
        expected: [0.5, 0.3, 0.8],
        tolerance: 1e-4
    });

    // hsv_adjust (Blender variant with separate inputs)
    tests.push({
        name: 'hsv_adjust_identity',
        category: 'Color Ops',
        graph: makeGraph([
            makeConst('c', [0.5, 0.3, 0.8]),
            { name: 'r', category: 'hsv_adjust', inputs: [
                { name: 'in', nodename: 'c' },
                { name: 'hue', value: 0 },
                { name: 'saturation', value: 1 },
                { name: 'value', value: 1 }
            ]}
        ], 'r'),
        expected: [0.5, 0.3, 0.8],
        tolerance: 1e-4
    });

    // brightness_contrast
    tests.push({
        name: 'brightness_contrast_identity',
        category: 'Color Ops',
        graph: makeGraph([
            makeConst('c', [0.5, 0.5, 0.5]),
            { name: 'r', category: 'brightness_contrast', inputs: [
                { name: 'in', nodename: 'c' },
                { name: 'brightness', value: 0 },
                { name: 'contrast', value: 1 }
            ]}
        ], 'r'),
        expected: [0.5, 0.5, 0.5],
        tolerance: 1e-6
    });
    tests.push({
        name: 'brightness_add',
        category: 'Color Ops',
        graph: makeGraph([
            makeConst('c', [0.3, 0.3, 0.3]),
            { name: 'r', category: 'brightness_contrast', inputs: [
                { name: 'in', nodename: 'c' },
                { name: 'brightness', value: 0.2 },
                { name: 'contrast', value: 1 }
            ]}
        ], 'r'),
        // brightness_contrast: contrast( add(in, brightness), contrast, 0.5 )
        // add(0.3, 0.2) = 0.5; contrast(0.5, 1, 0.5) = (0.5-0.5)*1+0.5 = 0.5
        expected: [0.5, 0.5, 0.5],
        tolerance: 1e-6
    });

    // Smoothstep
    tests.push({
        name: 'smoothstep_below',
        category: 'Color Ops',
        graph: makeGraph([
            { name: 'r', category: 'smoothstep', inputs: [
                { name: 'low', value: 0.2 },
                { name: 'high', value: 0.8 },
                { name: 'in', value: 0.0 }
            ]}
        ], 'r'),
        expected: 0,
        tolerance: 1e-6
    });
    tests.push({
        name: 'smoothstep_above',
        category: 'Color Ops',
        graph: makeGraph([
            { name: 'r', category: 'smoothstep', inputs: [
                { name: 'low', value: 0.2 },
                { name: 'high', value: 0.8 },
                { name: 'in', value: 1.0 }
            ]}
        ], 'r'),
        expected: 1,
        tolerance: 1e-6
    });
    tests.push({
        name: 'smoothstep_mid',
        category: 'Color Ops',
        graph: makeGraph([
            { name: 'r', category: 'smoothstep', inputs: [
                { name: 'low', value: 0 },
                { name: 'high', value: 1 },
                { name: 'in', value: 0.5 }
            ]}
        ], 'r'),
        expected: 0.5,  // t=0.5, 0.5*0.5*(3-2*0.5) = 0.25*2 = 0.5
        tolerance: 1e-6
    });

    // Step
    tests.push({
        name: 'step_below',
        category: 'Color Ops',
        graph: makeGraph([
            { name: 'r', category: 'step', inputs: [
                { name: 'edge', value: 0.5 },
                { name: 'in', value: 0.3 }
            ]}
        ], 'r'),
        expected: 0,
        tolerance: 1e-6
    });
    tests.push({
        name: 'step_above',
        category: 'Color Ops',
        graph: makeGraph([
            { name: 'r', category: 'step', inputs: [
                { name: 'edge', value: 0.5 },
                { name: 'in', value: 0.7 }
            ]}
        ], 'r'),
        expected: 1,
        tolerance: 1e-6
    });

    // Remap
    tests.push({
        name: 'remap_basic',
        category: 'Color Ops',
        graph: makeGraph([
            { name: 'r', category: 'remap', inputs: [
                { name: 'in', value: 0.5 },
                { name: 'inlow', value: 0 },
                { name: 'inhigh', value: 1 },
                { name: 'outlow', value: 10 },
                { name: 'outhigh', value: 20 }
            ]}
        ], 'r'),
        expected: 15,
        tolerance: 1e-6
    });

    return tests;
}

function generateCombineExtractTests() {
    const tests = [];

    tests.push({
        name: 'combine3_basic',
        category: 'Combine/Extract',
        graph: makeGraph([
            makeConst('x', 0.1), makeConst('y', 0.2), makeConst('z', 0.3),
            { name: 'r', category: 'combine3', inputs: [
                { name: 'in1', nodename: 'x' },
                { name: 'in2', nodename: 'y' },
                { name: 'in3', nodename: 'z' }
            ]}
        ], 'r'),
        expected: [0.1, 0.2, 0.3],
        tolerance: 1e-6
    });

    tests.push({
        name: 'combine2_basic',
        category: 'Combine/Extract',
        graph: makeGraph([
            makeConst('x', 0.5), makeConst('y', 0.7),
            { name: 'r', category: 'combine2', inputs: [
                { name: 'in1', nodename: 'x' },
                { name: 'in2', nodename: 'y' }
            ]}
        ], 'r'),
        expected: [0.5, 0.7],
        tolerance: 1e-6
    });

    tests.push({
        name: 'combine4_basic',
        category: 'Combine/Extract',
        graph: makeGraph([
            makeConst('x', 1), makeConst('y', 2), makeConst('z', 3), makeConst('w', 4),
            { name: 'r', category: 'combine4', inputs: [
                { name: 'in1', nodename: 'x' },
                { name: 'in2', nodename: 'y' },
                { name: 'in3', nodename: 'z' },
                { name: 'in4', nodename: 'w' }
            ]}
        ], 'r'),
        expected: [1, 2, 3, 4],
        tolerance: 1e-6
    });

    // Extract
    for (let i = 0; i < 3; i++) {
        tests.push({
            name: `extract_index${i}`,
            category: 'Combine/Extract',
            graph: makeGraph([
                makeConst('v', [10, 20, 30]),
                { name: 'r', category: 'extract', index: i, inputs: [
                    { name: 'in', nodename: 'v' }
                ]}
            ], 'r'),
            expected: [10, 20, 30][i],
            tolerance: 1e-6
        });
    }

    // Roundtrip: combine3 → extract
    tests.push({
        name: 'combine3_extract_roundtrip',
        category: 'Combine/Extract',
        graph: makeGraph([
            makeConst('v', [0.4, 0.5, 0.6]),
            { name: 'e0', category: 'extract', index: 0, inputs: [{ name: 'in', nodename: 'v' }] },
            { name: 'e1', category: 'extract', index: 1, inputs: [{ name: 'in', nodename: 'v' }] },
            { name: 'e2', category: 'extract', index: 2, inputs: [{ name: 'in', nodename: 'v' }] },
            { name: 'r', category: 'combine3', inputs: [
                { name: 'in1', nodename: 'e0' },
                { name: 'in2', nodename: 'e1' },
                { name: 'in3', nodename: 'e2' }
            ]}
        ], 'r'),
        expected: [0.4, 0.5, 0.6],
        tolerance: 1e-6
    });

    return tests;
}

function generateVectorOpTests() {
    const tests = [];
    const sqrt2 = Math.sqrt(2);
    const invSqrt3 = 1 / Math.sqrt(3);

    tests.push({
        name: 'normalize_basic',
        category: 'Vector Ops',
        graph: makeGraph([
            makeConst('v', [3, 0, 0]),
            makeUnary('r', 'normalize', conn('v'))
        ], 'r'),
        expected: [1, 0, 0],
        tolerance: 1e-6
    });
    tests.push({
        name: 'normalize_diagonal',
        category: 'Vector Ops',
        graph: makeGraph([
            makeConst('v', [1, 1, 1]),
            makeUnary('r', 'normalize', conn('v'))
        ], 'r'),
        expected: [invSqrt3, invSqrt3, invSqrt3],
        tolerance: 1e-6
    });

    tests.push({
        name: 'length_basic',
        category: 'Vector Ops',
        graph: makeGraph([
            makeConst('v', [3, 4, 0]),
            makeUnary('r', 'length', conn('v'))
        ], 'r'),
        expected: 5,
        tolerance: 1e-6
    });

    tests.push({
        name: 'dot_perpendicular',
        category: 'Vector Ops',
        graph: makeGraph([
            makeConst('a', [1, 0, 0]), makeConst('b', [0, 1, 0]),
            makeBinary('r', 'dot', conn('a'), conn('b'))
        ], 'r'),
        expected: 0,
        tolerance: 1e-6
    });
    tests.push({
        name: 'dot_parallel',
        category: 'Vector Ops',
        graph: makeGraph([
            makeConst('a', [2, 3, 4]), makeConst('b', [2, 3, 4]),
            makeBinary('r', 'dot', conn('a'), conn('b'))
        ], 'r'),
        expected: 29,  // 4+9+16
        tolerance: 1e-6
    });

    tests.push({
        name: 'cross_xy',
        category: 'Vector Ops',
        graph: makeGraph([
            makeConst('a', [1, 0, 0]), makeConst('b', [0, 1, 0]),
            makeBinary('r', 'cross', conn('a'), conn('b'))
        ], 'r'),
        expected: [0, 0, 1],
        tolerance: 1e-6
    });

    tests.push({
        name: 'distance_basic',
        category: 'Vector Ops',
        graph: makeGraph([
            makeConst('a', [1, 0, 0]), makeConst('b', [4, 0, 0]),
            makeBinary('r', 'distance', conn('a'), conn('b'))
        ], 'r'),
        expected: 3,
        tolerance: 1e-6
    });

    // Rotate3d: rotate [1,0,0] 90° about Y → [0,0,-1]
    tests.push({
        name: 'rotate3d_90y',
        category: 'Vector Ops',
        graph: makeGraph([
            makeConst('v', [1, 0, 0]),
            makeConst('ax', [0, 1, 0]),
            { name: 'r', category: 'rotate3d', inputs: [
                { name: 'in', nodename: 'v' },
                { name: 'amount', value: 90 },
                { name: 'axis', nodename: 'ax' }
            ]}
        ], 'r'),
        expected: [0, 0, -1],
        tolerance: 1e-4
    });
    // Rotate [0,0,1] 90° about X → [0,-1,0]
    tests.push({
        name: 'rotate3d_90x',
        category: 'Vector Ops',
        graph: makeGraph([
            makeConst('v', [0, 0, 1]),
            makeConst('ax', [1, 0, 0]),
            { name: 'r', category: 'rotate3d', inputs: [
                { name: 'in', nodename: 'v' },
                { name: 'amount', value: 90 },
                { name: 'axis', nodename: 'ax' }
            ]}
        ], 'r'),
        expected: [0, -1, 0],
        tolerance: 1e-4
    });
    // Rotate 360° = identity
    tests.push({
        name: 'rotate3d_360',
        category: 'Vector Ops',
        graph: makeGraph([
            makeConst('v', [0.5, 0.3, 0.7]),
            makeConst('ax', [0, 1, 0]),
            { name: 'r', category: 'rotate3d', inputs: [
                { name: 'in', nodename: 'v' },
                { name: 'amount', value: 360 },
                { name: 'axis', nodename: 'ax' }
            ]}
        ], 'r'),
        expected: [0.5, 0.3, 0.7],
        tolerance: 1e-4
    });

    return tests;
}

function generateConditionalTests() {
    const tests = [];

    tests.push({
        name: 'ifgreater_true',
        category: 'Conditionals',
        graph: makeGraph([
            { name: 'r', category: 'ifgreater', inputs: [
                { name: 'value1', value: 5 },
                { name: 'value2', value: 3 },
                { name: 'in1', value: 100 },
                { name: 'in2', value: 200 }
            ]}
        ], 'r'),
        expected: 100,
        tolerance: 1e-6
    });
    tests.push({
        name: 'ifgreater_false',
        category: 'Conditionals',
        graph: makeGraph([
            { name: 'r', category: 'ifgreater', inputs: [
                { name: 'value1', value: 2 },
                { name: 'value2', value: 3 },
                { name: 'in1', value: 100 },
                { name: 'in2', value: 200 }
            ]}
        ], 'r'),
        expected: 200,
        tolerance: 1e-6
    });
    tests.push({
        name: 'ifequal_true',
        category: 'Conditionals',
        graph: makeGraph([
            { name: 'r', category: 'ifequal', inputs: [
                { name: 'value1', value: 5 },
                { name: 'value2', value: 5 },
                { name: 'in1', value: 100 },
                { name: 'in2', value: 200 }
            ]}
        ], 'r'),
        expected: 100,
        tolerance: 1e-6
    });
    tests.push({
        name: 'ifequal_false',
        category: 'Conditionals',
        graph: makeGraph([
            { name: 'r', category: 'ifequal', inputs: [
                { name: 'value1', value: 5 },
                { name: 'value2', value: 6 },
                { name: 'in1', value: 100 },
                { name: 'in2', value: 200 }
            ]}
        ], 'r'),
        expected: 200,
        tolerance: 1e-6
    });

    return tests;
}

function generateChainedGraphTests() {
    const tests = [];

    // Chain: add → multiply → negate
    tests.push({
        name: 'chain_add_mul_negate',
        category: 'Chained Graphs',
        graph: makeGraph([
            makeConst('a', 2), makeConst('b', 3),
            makeBinary('sum', 'add', conn('a'), conn('b')),       // 5
            makeConst('c', 4),
            makeBinary('prod', 'multiply', conn('sum'), conn('c')), // 20
            makeUnary('r', 'negate', conn('prod'))                  // -20
        ], 'r'),
        expected: -20,
        tolerance: 1e-6
    });

    // Diamond: two paths merge
    //  a=5 → add(a,b)=8  → multiply(sum1, sum2) = 56
    //  b=3 → add(a,b)=8
    //         add(b,c)=7
    //  c=4 → add(b,c)=7
    tests.push({
        name: 'diamond_topology',
        category: 'Chained Graphs',
        graph: makeGraph([
            makeConst('a', 5), makeConst('b', 3), makeConst('c', 4),
            makeBinary('left', 'add', conn('a'), conn('b')),      // 8
            makeBinary('right', 'add', conn('b'), conn('c')),     // 7
            makeBinary('r', 'multiply', conn('left'), conn('right'))  // 56
        ], 'r'),
        expected: 56,
        tolerance: 1e-6
    });

    // 10-node chain: start at 1, add 1 ten times
    {
        const nodes = [makeConst('n0', 1)];
        for (let i = 1; i <= 10; i++) {
            nodes.push(makeConst(`one${i}`, 1));
            nodes.push(makeBinary(`n${i}`, 'add', conn(`n${i - 1}`), conn(`one${i}`)));
        }
        tests.push({
            name: 'chain_10_adds',
            category: 'Chained Graphs',
            graph: makeGraph(nodes, 'n10'),
            expected: 11,
            tolerance: 1e-6
        });
    }

    // Color pipeline: const → invert → mix with another color
    tests.push({
        name: 'chain_color_pipeline',
        category: 'Chained Graphs',
        graph: makeGraph([
            makeConst('base', [0.2, 0.4, 0.6]),
            { name: 'inv', category: 'invert', inputs: [
                { name: 'in', nodename: 'base' }, { name: 'amount', value: 1 }
            ]},
            makeConst('overlay', [1, 0, 0]),
            makeMix('r', conn('inv'), conn('overlay'), val(0.5))
        ], 'r'),
        // invert([0.2,0.4,0.6]) = [0.8,0.6,0.4]
        // mix([0.8,0.6,0.4], [1,0,0], 0.5) = [0.9, 0.3, 0.2]
        expected: [0.9, 0.3, 0.2],
        tolerance: 1e-6
    });

    // Nested unary: sin(cos(0)) = sin(1)
    tests.push({
        name: 'chain_sin_cos',
        category: 'Chained Graphs',
        graph: makeGraph([
            makeConst('a', 0),
            makeUnary('c', 'cos', conn('a')),  // cos(0) = 1
            makeUnary('r', 'sin', conn('c'))    // sin(1)
        ], 'r'),
        expected: Math.sin(1),
        tolerance: 1e-6
    });

    // Chain: sqrt(add(a, b)) for color3
    tests.push({
        name: 'chain_sqrt_add_color3',
        category: 'Chained Graphs',
        graph: makeGraph([
            makeConst('a', [0.01, 0.05, 0.12]),
            makeConst('b', [0.03, 0.04, 0.04]),
            makeBinary('s', 'add', conn('a'), conn('b')),  // [0.04, 0.09, 0.16]
            makeUnary('r', 'sqrt', conn('s'))               // [0.2, 0.3, 0.4]
        ], 'r'),
        expected: [0.2, 0.3, 0.4],
        tolerance: 1e-6
    });

    // Chain: combine3(extract(v,0), extract(v,1), extract(v,2)) with ops in between
    tests.push({
        name: 'chain_extract_negate_combine',
        category: 'Chained Graphs',
        graph: makeGraph([
            makeConst('v', [1, 2, 3]),
            { name: 'e0', category: 'extract', index: 0, inputs: [{ name: 'in', nodename: 'v' }] },
            { name: 'e1', category: 'extract', index: 1, inputs: [{ name: 'in', nodename: 'v' }] },
            { name: 'e2', category: 'extract', index: 2, inputs: [{ name: 'in', nodename: 'v' }] },
            makeUnary('n0', 'negate', conn('e0')),  // -1
            makeUnary('n1', 'negate', conn('e1')),  // -2
            makeUnary('n2', 'negate', conn('e2')),  // -3
            { name: 'r', category: 'combine3', inputs: [
                { name: 'in1', nodename: 'n0' },
                { name: 'in2', nodename: 'n1' },
                { name: 'in3', nodename: 'n2' }
            ]}
        ], 'r'),
        expected: [-1, -2, -3],
        tolerance: 1e-6
    });

    // Power chain: ((2^2)^2) = 16
    tests.push({
        name: 'chain_power_squared',
        category: 'Chained Graphs',
        graph: makeGraph([
            makeConst('base', 2), makeConst('exp', 2),
            makeBinary('p1', 'power', conn('base'), conn('exp')),   // 4
            makeBinary('r', 'power', conn('p1'), conn('exp'))       // 16
        ], 'r'),
        expected: 16,
        tolerance: 1e-6
    });

    return tests;
}

function generateIdentityPassthroughTests() {
    const tests = [];

    // Double negate
    tests.push({
        name: 'double_negate',
        category: 'Identity/Passthrough',
        graph: makeGraph([
            makeConst('a', 7),
            makeUnary('n1', 'negate', conn('a')),
            makeUnary('r', 'negate', conn('n1'))
        ], 'r'),
        expected: 7,
        tolerance: 1e-6
    });

    // Add 0
    tests.push({
        name: 'add_zero',
        category: 'Identity/Passthrough',
        graph: makeGraph([
            makeConst('a', 42), makeConst('z', 0),
            makeBinary('r', 'add', conn('a'), conn('z'))
        ], 'r'),
        expected: 42,
        tolerance: 1e-6
    });

    // Multiply by 1
    tests.push({
        name: 'multiply_one',
        category: 'Identity/Passthrough',
        graph: makeGraph([
            makeConst('a', [0.3, 0.6, 0.9]), makeConst('one', 1),
            makeBinary('r', 'multiply', conn('a'), conn('one'))
        ], 'r'),
        expected: [0.3, 0.6, 0.9],
        tolerance: 1e-6
    });

    // Multiply by 0
    tests.push({
        name: 'multiply_zero',
        category: 'Identity/Passthrough',
        graph: makeGraph([
            makeConst('a', [0.3, 0.6, 0.9]), makeConst('z', 0),
            makeBinary('r', 'multiply', conn('a'), conn('z'))
        ], 'r'),
        expected: [0, 0, 0],
        tolerance: 1e-6
    });

    // Power 1 = identity
    tests.push({
        name: 'power_one',
        category: 'Identity/Passthrough',
        graph: makeGraph([
            makeConst('a', 5), makeConst('one', 1),
            makeBinary('r', 'power', conn('a'), conn('one'))
        ], 'r'),
        expected: 5,
        tolerance: 1e-6
    });

    // Power 0 = 1
    tests.push({
        name: 'power_zero',
        category: 'Identity/Passthrough',
        graph: makeGraph([
            makeConst('a', 5), makeConst('z', 0),
            makeBinary('r', 'power', conn('a'), conn('z'))
        ], 'r'),
        expected: 1,
        tolerance: 1e-6
    });

    // Mix t=0 → bg
    tests.push({
        name: 'mix_t0_passthrough',
        category: 'Identity/Passthrough',
        graph: makeGraph([
            makeConst('a', [0.1, 0.2, 0.3]),
            makeConst('b', [0.9, 0.8, 0.7]),
            makeMix('r', conn('a'), conn('b'), val(0))
        ], 'r'),
        expected: [0.1, 0.2, 0.3],
        tolerance: 1e-6
    });

    // Mix t=1 → fg
    tests.push({
        name: 'mix_t1_passthrough',
        category: 'Identity/Passthrough',
        graph: makeGraph([
            makeConst('a', [0.1, 0.2, 0.3]),
            makeConst('b', [0.9, 0.8, 0.7]),
            makeMix('r', conn('a'), conn('b'), val(1))
        ], 'r'),
        expected: [0.9, 0.8, 0.7],
        tolerance: 1e-6
    });

    // Convert passthrough
    tests.push({
        name: 'convert_passthrough',
        category: 'Identity/Passthrough',
        graph: makeGraph([
            makeConst('a', [0.5, 0.6, 0.7]),
            { name: 'r', category: 'convert', inputs: [{ name: 'in', nodename: 'a' }] }
        ], 'r'),
        expected: [0.5, 0.6, 0.7],
        tolerance: 1e-6
    });

    // Subtract self = 0
    tests.push({
        name: 'subtract_self',
        category: 'Identity/Passthrough',
        graph: makeGraph([
            makeConst('a', 42),
            makeBinary('r', 'subtract', conn('a'), conn('a'))
        ], 'r'),
        expected: 0,
        tolerance: 1e-6
    });

    return tests;
}

function generateEdgeCaseTests() {
    const tests = [];

    // Division by zero → 0 (safe division)
    tests.push({
        name: 'divide_by_zero_float',
        category: 'Edge Cases',
        graph: makeGraph([
            makeConst('a', 5), makeConst('b', 0),
            makeBinary('r', 'divide', conn('a'), conn('b'))
        ], 'r'),
        expected: 0,
        tolerance: 1e-6
    });

    // Division by zero color3
    tests.push({
        name: 'divide_by_zero_color3',
        category: 'Edge Cases',
        graph: makeGraph([
            makeConst('a', [1, 2, 3]), makeConst('b', [0, 0, 0]),
            makeBinary('r', 'divide', conn('a'), conn('b'))
        ], 'r'),
        expected: [0, 0, 0],
        tolerance: 1e-6
    });

    // sqrt of negative → 0 (clamped)
    tests.push({
        name: 'sqrt_negative',
        category: 'Edge Cases',
        graph: makeGraph([
            makeConst('a', -4),
            makeUnary('r', 'sqrt', conn('a'))
        ], 'r'),
        expected: 0,  // sqrt(max(0, -4)) = 0
        tolerance: 1e-6
    });

    // ln of very small → ln(1e-10)
    tests.push({
        name: 'ln_near_zero',
        category: 'Edge Cases',
        graph: makeGraph([
            makeConst('a', 0),
            makeUnary('r', 'ln', conn('a'))
        ], 'r'),
        expected: Math.log(1e-10),  // clamped
        tolerance: 1e-6
    });

    // inversesqrt of very small
    tests.push({
        name: 'inversesqrt_near_zero',
        category: 'Edge Cases',
        graph: makeGraph([
            makeConst('a', 0),
            makeUnary('r', 'inversesqrt', conn('a'))
        ], 'r'),
        expected: 1.0 / Math.sqrt(1e-10),
        tolerance: 1e-2
    });

    // Normalize zero vector
    tests.push({
        name: 'normalize_zero',
        category: 'Edge Cases',
        graph: makeGraph([
            makeConst('v', [0, 0, 0]),
            makeUnary('r', 'normalize', conn('v'))
        ], 'r'),
        expected: [0, 0, 0],
        tolerance: 1e-6
    });

    // Large value through pipeline
    tests.push({
        name: 'large_value_chain',
        category: 'Edge Cases',
        graph: makeGraph([
            makeConst('a', 1e6), makeConst('b', 1e6),
            makeBinary('s', 'add', conn('a'), conn('b')),
            makeUnary('r', 'sqrt', conn('s'))
        ], 'r'),
        expected: Math.sqrt(2e6),
        tolerance: 1e-2
    });

    // Constant-only graph (no ops)
    tests.push({
        name: 'constant_only',
        category: 'Edge Cases',
        graph: constGraph(42),
        expected: 42,
        tolerance: 1e-6
    });

    // Constant color
    tests.push({
        name: 'constant_color3_only',
        category: 'Edge Cases',
        graph: constGraph([0.1, 0.2, 0.3]),
        expected: [0.1, 0.2, 0.3],
        tolerance: 1e-6
    });

    // Extract out of bounds → 0
    tests.push({
        name: 'extract_out_of_bounds',
        category: 'Edge Cases',
        graph: makeGraph([
            makeConst('v', [1, 2, 3]),
            { name: 'r', category: 'extract', index: 5, inputs: [{ name: 'in', nodename: 'v' }] }
        ], 'r'),
        expected: 0,
        tolerance: 1e-6
    });

    return tests;
}

function generateMultiOutputTests() {
    const tests = [];

    // Graph with two outputs
    tests.push({
        name: 'multi_output_two',
        category: 'Multi-Output',
        graph: {
            nodegraph: {
                nodes: [
                    makeConst('a', 10), makeConst('b', 3),
                    makeBinary('sum', 'add', conn('a'), conn('b')),
                    makeBinary('diff', 'subtract', conn('a'), conn('b'))
                ],
                outputs: [
                    { name: 'out_sum', nodename: 'sum' },
                    { name: 'out_diff', nodename: 'diff' }
                ]
            }
        },
        expectedMulti: { out_sum: 13, out_diff: 7 },
        tolerance: 1e-6
    });

    // Graph where two outputs share the same source node
    tests.push({
        name: 'multi_output_shared_source',
        category: 'Multi-Output',
        graph: {
            nodegraph: {
                nodes: [
                    makeConst('a', 5), makeConst('b', 3),
                    makeBinary('prod', 'multiply', conn('a'), conn('b'))
                ],
                outputs: [
                    { name: 'out1', nodename: 'prod' },
                    { name: 'out2', nodename: 'prod' }
                ]
            }
        },
        expectedMulti: { out1: 15, out2: 15 },
        tolerance: 1e-6
    });

    // Three outputs: color3, extracted float, and luminance
    tests.push({
        name: 'multi_output_mixed_types',
        category: 'Multi-Output',
        graph: {
            nodegraph: {
                nodes: [
                    makeConst('c', [0.5, 0.3, 0.8]),
                    makeUnary('lum', 'luminance', conn('c')),
                    { name: 'r_ch', category: 'extract', index: 0, inputs: [{ name: 'in', nodename: 'c' }] }
                ],
                outputs: [
                    { name: 'color', nodename: 'c' },
                    { name: 'luma', nodename: 'lum' },
                    { name: 'red', nodename: 'r_ch' }
                ]
            }
        },
        expectedMulti: {
            color: [0.5, 0.3, 0.8],
            luma: 0.5 * 0.2126 + 0.3 * 0.7152 + 0.8 * 0.0722,
            red: 0.5
        },
        tolerance: 1e-6
    });

    return tests;
}

function generateOptimizationTests() {
    const tests = [];

    // DCE: dead node should be removed
    tests.push({
        name: 'opt_dce_removes_dead',
        category: 'Optimization',
        run: () => {
            const graph = {
                nodegraph: {
                    nodes: [
                        makeConst('live', 5),
                        makeConst('dead', 99),
                        makeBinary('used', 'add', conn('live'), val(1)),
                        makeBinary('unused', 'multiply', conn('dead'), val(2))
                    ],
                    outputs: [{ name: 'out', nodename: 'used' }]
                },
                connections: [{ output: 'out', input: 'base_color' }]
            };
            const marked = markActiveNodes(graph);
            const cleaned = removeInactiveNodes(marked);
            const activeNames = cleaned.nodegraph.nodes.map(n => n.name);
            if (!activeNames.includes('live')) return { pass: false, error: 'live node removed' };
            if (!activeNames.includes('used')) return { pass: false, error: 'used node removed' };
            if (activeNames.includes('dead')) return { pass: false, error: 'dead node not removed' };
            if (activeNames.includes('unused')) return { pass: false, error: 'unused node not removed' };
            return { pass: true };
        }
    });

    // DCE: all nodes reachable → none removed
    tests.push({
        name: 'opt_dce_all_reachable',
        category: 'Optimization',
        run: () => {
            const graph = {
                nodegraph: {
                    nodes: [
                        makeConst('a', 1), makeConst('b', 2),
                        makeBinary('r', 'add', conn('a'), conn('b'))
                    ],
                    outputs: [{ name: 'out', nodename: 'r' }]
                },
                connections: [{ output: 'out', input: 'base_color' }]
            };
            const marked = markActiveNodes(graph);
            const cleaned = removeInactiveNodes(marked);
            if (cleaned._dceInfo.removedCount !== 0) {
                return { pass: false, error: `Expected 0 removed, got ${cleaned._dceInfo.removedCount}` };
            }
            return { pass: true };
        }
    });

    // Optimization preserves result (BASIC level)
    tests.push({
        name: 'opt_basic_preserves_result',
        category: 'Optimization',
        run: () => {
            const graph = {
                nodegraph: {
                    nodes: [
                        makeConst('a', [0.5, 0.3, 0.8]),
                        makeConst('b', [0.1, 0.2, 0.3]),
                        makeBinary('sum', 'add', conn('a'), conn('b')),
                        makeUnary('r', 'negate', conn('sum'))
                    ],
                    outputs: [{ name: 'out', nodename: 'r' }]
                }
            };
            const proc = new MtlxNodeGraphProcessor();
            const before = proc.processGraph(graph);
            const optimized = optimizeNodeGraph(JSON.parse(JSON.stringify(graph)), NodeGraphOptimizationLevel.BASIC);
            const after = proc.processGraph(optimized);
            if (!approxEqual(before.out, after.out, 1e-6)) {
                return { pass: false, error: `Before: ${fmt(before.out)}, After: ${fmt(after.out)}` };
            }
            return { pass: true };
        }
    });

    // Optimization preserves result (STANDARD level)
    tests.push({
        name: 'opt_standard_preserves_result',
        category: 'Optimization',
        run: () => {
            const graph = {
                nodegraph: {
                    nodes: [
                        makeConst('a', [0.3, 0.6, 0.9]),
                        { name: 'inv', category: 'invert', inputs: [
                            { name: 'in', nodename: 'a' }, { name: 'amount', value: 1 }
                        ]},
                        makeBinary('r', 'multiply', conn('inv'), val(2))
                    ],
                    outputs: [{ name: 'out', nodename: 'r' }]
                }
            };
            const proc = new MtlxNodeGraphProcessor();
            const before = proc.processGraph(graph);
            const optimized = optimizeNodeGraph(JSON.parse(JSON.stringify(graph)), NodeGraphOptimizationLevel.STANDARD);
            const after = proc.processGraph(optimized);
            if (!approxEqual(before.out, after.out, 1e-6)) {
                return { pass: false, error: `Before: ${fmt(before.out)}, After: ${fmt(after.out)}` };
            }
            return { pass: true };
        }
    });

    // Optimization preserves result (AGGRESSIVE level)
    tests.push({
        name: 'opt_aggressive_preserves_result',
        category: 'Optimization',
        run: () => {
            const graph = {
                nodegraph: {
                    nodes: [
                        makeConst('a', [0.2, 0.4, 0.6]),
                        makeConst('b', [0.1, 0.3, 0.5]),
                        makeBinary('sum', 'add', conn('a'), conn('b')),
                        makeUnary('neg', 'negate', conn('sum')),
                        makeBinary('r', 'multiply', conn('neg'), val(0.5))
                    ],
                    outputs: [{ name: 'out', nodename: 'r' }]
                }
            };
            const proc = new MtlxNodeGraphProcessor();
            const before = proc.processGraph(graph);
            const optimized = optimizeNodeGraph(JSON.parse(JSON.stringify(graph)), NodeGraphOptimizationLevel.AGGRESSIVE);
            const after = proc.processGraph(optimized);
            if (!approxEqual(before.out, after.out, 1e-6)) {
                return { pass: false, error: `Before: ${fmt(before.out)}, After: ${fmt(after.out)}` };
            }
            return { pass: true };
        }
    });

    // NONE level returns graph unchanged
    tests.push({
        name: 'opt_none_is_noop',
        category: 'Optimization',
        run: () => {
            const graph = {
                nodegraph: {
                    nodes: [makeConst('a', 5), makeUnary('r', 'negate', conn('a'))],
                    outputs: [{ name: 'out', nodename: 'r' }]
                }
            };
            const result = optimizeNodeGraph(graph, NodeGraphOptimizationLevel.NONE);
            // Should be same reference (no cloning)
            if (result !== graph) {
                return { pass: false, error: 'NONE level should return input unchanged' };
            }
            return { pass: true };
        }
    });

    // optimizeNodeGraph handles null/undefined gracefully
    tests.push({
        name: 'opt_null_input',
        category: 'Optimization',
        run: () => {
            try {
                optimizeNodeGraph(null);
                optimizeNodeGraph(undefined);
                optimizeNodeGraph({});
                return { pass: true };
            } catch (e) {
                return { pass: false, error: `Threw: ${e.message}` };
            }
        }
    });

    // Optimization + processGraph on complex pipeline
    tests.push({
        name: 'opt_complex_pipeline',
        category: 'Optimization',
        run: () => {
            const graph = {
                nodegraph: {
                    nodes: [
                        makeConst('base', [0.4, 0.5, 0.6]),
                        { name: 'inv', category: 'invert', inputs: [
                            { name: 'in', nodename: 'base' }, { name: 'amount', value: 1 }
                        ]},
                        { name: 'bc', category: 'brightness_contrast', inputs: [
                            { name: 'in', nodename: 'inv' },
                            { name: 'brightness', value: 0.1 },
                            { name: 'contrast', value: 1.5 }
                        ]},
                        makeConst('overlay', [1, 0, 0]),
                        makeMix('final', conn('bc'), conn('overlay'), val(0.3)),
                        // Dead node
                        makeConst('dead', [9, 9, 9]),
                        makeUnary('also_dead', 'negate', conn('dead'))
                    ],
                    outputs: [{ name: 'out', nodename: 'final' }]
                }
            };
            const proc = new MtlxNodeGraphProcessor();
            const before = proc.processGraph(graph);
            const optimized = optimizeNodeGraph(
                JSON.parse(JSON.stringify(graph)),
                NodeGraphOptimizationLevel.AGGRESSIVE
            );
            const after = proc.processGraph(optimized);
            if (!approxEqual(before.out, after.out, 1e-4)) {
                return { pass: false, error: `Before: ${fmt(before.out)}, After: ${fmt(after.out)}` };
            }
            return { pass: true };
        }
    });

    return tests;
}

// ============================================================================
// Test Runner
// ============================================================================

class MtlxNodeTester {
    constructor() {
        this.tests = [];
        this.results = [];
        this.running = false;
        this.stopRequested = false;
        this.processor = new MtlxNodeGraphProcessor();
    }

    loadAllTests() {
        this.tests = [
            ...generateUnaryMathTests(),
            ...generateBinaryMathTests(),
            ...generateColorOpTests(),
            ...generateCombineExtractTests(),
            ...generateVectorOpTests(),
            ...generateConditionalTests(),
            ...generateChainedGraphTests(),
            ...generateIdentityPassthroughTests(),
            ...generateEdgeCaseTests(),
            ...generateMultiOutputTests(),
            ...generateOptimizationTests(),
        ];
        return this.tests;
    }

    runSingleTest(tc) {
        const t0 = performance.now();
        try {
            // Custom run function (optimization tests)
            if (tc.run) {
                const res = tc.run();
                const dt = performance.now() - t0;
                return { ...res, time: dt };
            }

            // Standard graph evaluation test
            const result = this.processor.processGraph(tc.graph);

            // Multi-output test
            if (tc.expectedMulti) {
                for (const [key, exp] of Object.entries(tc.expectedMulti)) {
                    const actual = result[key];
                    if (!approxEqual(actual, exp, tc.tolerance)) {
                        const dt = performance.now() - t0;
                        return {
                            pass: false, time: dt,
                            error: `Output '${key}': expected ${fmt(exp)}, got ${fmt(actual)} (err=${maxError(actual, exp).toExponential(2)})`
                        };
                    }
                }
                const dt = performance.now() - t0;
                return { pass: true, time: dt, actual: result };
            }

            // Single output
            const actual = result.out;
            const pass = approxEqual(actual, tc.expected, tc.tolerance);
            const dt = performance.now() - t0;

            if (!pass) {
                return {
                    pass: false, time: dt,
                    actual,
                    error: `Expected ${fmt(tc.expected)}, got ${fmt(actual)} (err=${maxError(actual, tc.expected).toExponential(2)})`
                };
            }

            // Secondary check: optimize + re-evaluate (only for graph-based tests)
            if (tc.graph) {
                try {
                    const optGraph = optimizeNodeGraph(
                        JSON.parse(JSON.stringify(tc.graph)),
                        NodeGraphOptimizationLevel.STANDARD
                    );
                    const optResult = this.processor.processGraph(optGraph);
                    const optActual = tc.expectedMulti ? optResult : optResult.out;
                    const optExpected = tc.expectedMulti ? tc.expectedMulti : tc.expected;

                    if (tc.expectedMulti) {
                        for (const [key, exp] of Object.entries(optExpected)) {
                            if (!approxEqual(optResult[key], exp, tc.tolerance * 10)) {
                                return {
                                    pass: false, time: performance.now() - t0,
                                    actual,
                                    error: `Optimization broke output '${key}': expected ${fmt(exp)}, got ${fmt(optResult[key])}`
                                };
                            }
                        }
                    } else if (!approxEqual(optActual, optExpected, tc.tolerance * 10)) {
                        return {
                            pass: false, time: performance.now() - t0,
                            actual,
                            error: `Optimization broke result: expected ${fmt(optExpected)}, got ${fmt(optActual)}`
                        };
                    }
                } catch (optErr) {
                    // Optimization error is non-fatal for the main test
                    return { pass: true, time: performance.now() - t0, actual, optWarning: optErr.message };
                }
            }

            return { pass: true, time: dt, actual };
        } catch (e) {
            return { pass: false, time: performance.now() - t0, error: `Exception: ${e.message}` };
        }
    }

    async runAll(filterFn, onProgress) {
        this.running = true;
        this.stopRequested = false;
        this.results = [];

        const filtered = filterFn ? this.tests.filter(filterFn) : this.tests;
        const total = filtered.length;
        let pass = 0, fail = 0, skip = 0;
        const t0 = performance.now();

        for (let i = 0; i < filtered.length; i++) {
            if (this.stopRequested) { skip = total - i; break; }

            const tc = filtered[i];
            const res = this.runSingleTest(tc);
            res.name = tc.name;
            res.category = tc.category;
            this.results.push(res);

            if (res.pass) pass++; else fail++;

            // Yield to UI every 20 tests
            if (i % 20 === 19) {
                onProgress?.({ total, pass, fail, skip, elapsed: performance.now() - t0, current: i + 1 });
                await new Promise(r => setTimeout(r, 0));
            }
        }

        const elapsed = performance.now() - t0;
        this.running = false;
        onProgress?.({ total, pass, fail, skip, elapsed, current: total, done: true });
        return { total, pass, fail, skip, elapsed, results: this.results };
    }

    stop() { this.stopRequested = true; }
}

// ============================================================================
// UI
// ============================================================================

const tester = new MtlxNodeTester();
tester.loadAllTests();

const $results = document.getElementById('results');
const $summary = document.getElementById('summary');
const $total = document.getElementById('s-total');
const $pass = document.getElementById('s-pass');
const $fail = document.getElementById('s-fail');
const $skip = document.getElementById('s-skip');
const $time = document.getElementById('s-time');
const $bar = document.getElementById('s-bar');
const $btnRun = document.getElementById('btn-run');
const $btnStop = document.getElementById('btn-stop');
const $filter = document.getElementById('filter');
const $log = document.getElementById('log');
const $showLog = document.getElementById('show-log');

function log(msg) {
    $log.textContent += msg + '\n';
    $log.scrollTop = $log.scrollHeight;
    console.log('[mtlx-tester]', msg);
}

$showLog.addEventListener('change', () => $log.classList.toggle('visible', $showLog.checked));

function buildResultsUI(results) {
    $results.innerHTML = '';
    const categories = new Map();
    for (const r of results) {
        const cat = r.category || 'Uncategorized';
        if (!categories.has(cat)) categories.set(cat, []);
        categories.get(cat).push(r);
    }

    for (const [cat, items] of categories) {
        const passCount = items.filter(r => r.pass).length;
        const allPass = passCount === items.length;

        const $cat = document.createElement('div');
        $cat.className = 'category';

        const $header = document.createElement('div');
        $header.className = 'cat-header';
        $header.innerHTML = `<span><span class="arrow">&#9660;</span>${cat} (${passCount}/${items.length} passed)</span>
            <span class="cat-stats">${allPass ? '&#10003;' : `${items.length - passCount} failed`}</span>`;
        $header.style.color = allPass ? '#4CAF50' : '#f44336';
        $header.addEventListener('click', () => {
            $header.classList.toggle('collapsed');
        });

        const $body = document.createElement('div');
        $body.className = 'cat-body';

        for (const r of items) {
            const $row = document.createElement('div');
            $row.className = `test-row ${r.pass ? 'pass' : 'fail'}`;
            $row.innerHTML = `<span><span class="icon">${r.pass ? '&#10003;' : '&#10007;'}</span>${r.name}</span>
                <span class="test-time">${r.time?.toFixed(1) ?? '-'}ms</span>`;

            const $detail = document.createElement('div');
            $detail.className = 'test-detail';
            let detailText = '';
            if (r.error) detailText += `<span class="err">${r.error}</span>\n`;
            if (r.actual !== undefined) detailText += `Actual: ${fmt(r.actual)}\n`;
            if (r.optWarning) detailText += `Opt warning: ${r.optWarning}\n`;
            $detail.innerHTML = detailText || 'Passed';

            $row.addEventListener('click', (e) => {
                e.stopPropagation();
                $detail.classList.toggle('open');
            });

            $body.appendChild($row);
            $body.appendChild($detail);
        }

        $cat.appendChild($header);
        $cat.appendChild($body);
        $results.appendChild($cat);
    }
}

function updateSummary({ total, pass, fail, skip, elapsed }) {
    $summary.style.display = '';
    $total.textContent = total;
    $pass.textContent = pass;
    $fail.textContent = fail;
    $skip.textContent = skip;
    $time.textContent = `${(elapsed / 1000).toFixed(2)}s`;
    const pct = total > 0 ? ((pass + fail) / total * 100) : 0;
    const passPct = total > 0 ? (pass / total * 100) : 0;
    $bar.style.width = `${pct}%`;
    if (fail > 0) {
        $bar.classList.add('has-fail');
        $bar.style.setProperty('--pass-pct', `${passPct / pct * 100}%`);
    } else {
        $bar.classList.remove('has-fail');
    }
}

async function runTests() {
    const filterText = $filter.value.trim().toLowerCase();
    const filterFn = filterText
        ? (tc) => tc.name.toLowerCase().includes(filterText) || tc.category.toLowerCase().includes(filterText)
        : null;

    $btnRun.disabled = true;
    $btnStop.disabled = false;
    $results.innerHTML = '<div style="color:#aaa;padding:10px;">Running...</div>';

    log(`Starting test run (${tester.tests.length} tests loaded)`);

    const { total, pass, fail, skip, elapsed, results } = await tester.runAll(filterFn, (progress) => {
        updateSummary(progress);
    });

    buildResultsUI(results);
    updateSummary({ total, pass, fail, skip, elapsed });

    $btnRun.disabled = false;
    $btnStop.disabled = true;

    log(`Done: ${pass} pass, ${fail} fail, ${skip} skip in ${(elapsed / 1000).toFixed(2)}s`);
}

$btnRun.addEventListener('click', runTests);
$btnStop.addEventListener('click', () => tester.stop());
$filter.addEventListener('keydown', (e) => { if (e.key === 'Enter') runTests(); });

// Show test count on load
log(`Loaded ${tester.tests.length} tests`);
$summary.style.display = '';
$total.textContent = tester.tests.length;
