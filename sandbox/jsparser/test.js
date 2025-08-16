/**
 * Test suite for USDA JavaScript parser
 */

const { UsdaParser } = require('./usda-parser.js');
const { UsdaLexer, TokenType } = require('./usda-lexer.js');

function testLexer() {
    console.log('Testing Lexer...');
    
    const input = `def Sphere "ball" {
        float3 translate = (0, 1, 2)
        string name = "test"
    }`;
    
    const lexer = new UsdaLexer(input);
    const tokens = [];
    
    let token;
    do {
        token = lexer.nextToken();
        tokens.push(token);
        console.log(token.toString());
    } while (token.type !== TokenType.EOF);
    
    console.log(`Tokenized ${tokens.length - 1} tokens\n`);
}

function testSimplePrim() {
    console.log('Testing Simple Prim Parsing...');
    
    const input = `def Sphere "ball" {
        float3 translate = (0, 1, 2)
        string name = "test_sphere"
    }`;
    
    const parser = new UsdaParser(input);
    const layer = parser.parse();
    
    if (layer) {
        console.log('Parse successful!');
        console.log(`Root prim: ${layer.rootPrim.name} (${layer.rootPrim.type})`);
        
        const translateAttr = layer.rootPrim.getAttribute('translate');
        if (translateAttr) {
            console.log(`translate: ${translateAttr.type} = ${JSON.stringify(translateAttr.value)}`);
        }
        
        const nameAttr = layer.rootPrim.getAttribute('name');
        if (nameAttr) {
            console.log(`name: ${nameAttr.type} = ${JSON.stringify(nameAttr.value)}`);
        }
    } else {
        console.log('Parse failed!');
        parser.getErrors().forEach(err => console.error(err.toString()));
    }
    console.log('');
}

function testNestedPrims() {
    console.log('Testing Nested Prims...');
    
    const input = `def Xform "root" {
        def Sphere "ball" {
            float radius = 1.0
        }
        def Cube "box" {
            float3 size = (2, 2, 2)
        }
    }`;
    
    const parser = new UsdaParser(input);
    const layer = parser.parse();
    
    if (layer) {
        console.log('Parse successful!');
        console.log(`Root prim: ${layer.rootPrim.name} (${layer.rootPrim.type})`);
        console.log(`Children: ${layer.rootPrim.children.size}`);
        
        for (const [name, child] of layer.rootPrim.children) {
            console.log(`  Child: ${name} (${child.type})`);
            for (const [attrName, attr] of child.attributes) {
                console.log(`    ${attrName}: ${attr.type} = ${JSON.stringify(attr.value)}`);
            }
        }
    } else {
        console.log('Parse failed!');
        parser.getErrors().forEach(err => console.error(err.toString()));
    }
    console.log('');
}

function testArrayValues() {
    console.log('Testing Array Values...');
    
    const input = `def Mesh "mesh" {
        int[] faceVertexIndices = [0, 1, 2, 2, 3, 0]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
    }`;
    
    const parser = new UsdaParser(input);
    const layer = parser.parse();
    
    if (layer) {
        console.log('Parse successful!');
        const mesh = layer.rootPrim;
        
        const indices = mesh.getAttribute('faceVertexIndices');
        if (indices) {
            console.log(`faceVertexIndices: ${indices.type}`);
            console.log(`  Value: ${JSON.stringify(indices.value)}`);
        }
        
        const points = mesh.getAttribute('points');
        if (points) {
            console.log(`points: ${points.type}`);
            console.log(`  Value: ${JSON.stringify(points.value)}`);
        }
    } else {
        console.log('Parse failed!');
        parser.getErrors().forEach(err => console.error(err.toString()));
    }
    console.log('');
}

function testMetadata() {
    console.log('Testing Metadata...');
    
    const input = `def Sphere "ball" (
        kind = "component"
        active = true
    ) {
        float radius (interpolation = "constant") = 1.0
    }`;
    
    const parser = new UsdaParser(input);
    const layer = parser.parse();
    
    if (layer) {
        console.log('Parse successful!');
        const sphere = layer.rootPrim;
        
        console.log(`Prim metadata: ${JSON.stringify(sphere.metadata)}`);
        
        const radius = sphere.getAttribute('radius');
        if (radius) {
            console.log(`radius metadata: ${JSON.stringify(radius.metadata)}`);
        }
    } else {
        console.log('Parse failed!');
        parser.getErrors().forEach(err => console.error(err.toString()));
    }
    console.log('');
}

function testComments() {
    console.log('Testing Comments...');
    
    const input = `# This is a comment
def Sphere "ball" {
    # Another comment
    float radius = 1.0  # Inline comment
    string name = "test"
}`;
    
    const parser = new UsdaParser(input);
    const layer = parser.parse();
    
    if (layer) {
        console.log('Parse successful!');
        console.log(`Root prim: ${layer.rootPrim.name} (${layer.rootPrim.type})`);
    } else {
        console.log('Parse failed!');
        parser.getErrors().forEach(err => console.error(err.toString()));
    }
    console.log('');
}

// Run all tests
function runTests() {
    console.log('=== USDA JavaScript Parser Tests ===\n');
    
    try {
        testLexer();
        testSimplePrim();
        testNestedPrims();
        testArrayValues();
        testMetadata();
        testComments();
        
        console.log('All tests completed!');
    } catch (error) {
        console.error('Test failed with error:', error);
    }
}

// Run tests if this file is executed directly
if (require.main === module) {
    runTests();
}

module.exports = { runTests };