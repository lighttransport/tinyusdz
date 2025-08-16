/**
 * Example usage of the USDA JavaScript parser
 */

const { UsdaParser } = require('./usda-parser.js');

// Example USDA content
const exampleUsda = `#usda 1.0

def Xform "Scene" (
    kind = "assembly"
    doc = "A simple scene with a sphere and cube"
) {
    def Sphere "Ball" (
        kind = "component"
    ) {
        float3 translate = (2, 0, 0)
        float radius = 1.5
        color3f[] displayColor = [(1, 0, 0)]
    }
    
    def Cube "Box" {
        float3 translate = (-2, 0, 0)
        float3 size = (2, 2, 2)
        color3f[] displayColor = [(0, 1, 0)]
    }
    
    def Mesh "CustomMesh" {
        int[] faceVertexCounts = [4, 4, 4, 4, 4, 4]
        int[] faceVertexIndices = [
            0, 1, 3, 2,
            2, 3, 5, 4, 
            4, 5, 7, 6,
            6, 7, 1, 0,
            1, 7, 5, 3,
            6, 0, 2, 4
        ]
        point3f[] points = [
            (-1, -1, 1), (1, -1, 1), (-1, 1, 1), (1, 1, 1),
            (-1, 1, -1), (1, 1, -1), (-1, -1, -1), (1, -1, -1)
        ]
        normal3f[] normals = [
            (0, 0, 1), (0, 1, 0), (0, 0, -1),
            (0, -1, 0), (1, 0, 0), (-1, 0, 0)
        ]
        float2[] primvars:st (interpolation = "faceVarying") = [
            (0, 0), (1, 0), (1, 1), (0, 1),
            (0, 0), (1, 0), (1, 1), (0, 1),
            (0, 0), (1, 0), (1, 1), (0, 1),
            (0, 0), (1, 0), (1, 1), (0, 1),
            (0, 0), (1, 0), (1, 1), (0, 1),
            (0, 0), (1, 0), (1, 1), (0, 1)
        ]
    }
}`;

function parseAndPrint(usdaContent) {
    console.log('Parsing USDA content...\n');
    
    const parser = new UsdaParser(usdaContent);
    const layer = parser.parse();
    
    if (!layer) {
        console.error('Parse failed!');
        parser.getErrors().forEach(err => console.error(`  ${err.toString()}`));
        return null;
    }
    
    console.log('Parse successful!\n');
    
    // Print warnings if any
    const warnings = parser.getWarnings();
    if (warnings.length > 0) {
        console.log('Warnings:');
        warnings.forEach(warning => console.log(`  ${warning}`));
        console.log('');
    }
    
    return layer;
}

function printPrimRecursive(prim, indent = 0) {
    const spaces = '  '.repeat(indent);
    console.log(`${spaces}${prim.specifier} ${prim.type} "${prim.name}"`);
    
    // Print metadata
    if (Object.keys(prim.metadata).length > 0) {
        console.log(`${spaces}  Metadata:`);
        for (const [key, value] of Object.entries(prim.metadata)) {
            console.log(`${spaces}    ${key} = ${JSON.stringify(value.value)}`);
        }
    }
    
    // Print attributes
    if (prim.attributes.size > 0) {
        console.log(`${spaces}  Attributes:`);
        for (const [name, attr] of prim.attributes) {
            let valueStr = 'None';
            if (attr.value) {
                if (attr.value.type === 'array' || attr.value.type === 'tuple') {
                    valueStr = `[${attr.value.value.length} items]`;
                } else {
                    valueStr = JSON.stringify(attr.value.value);
                }
            }
            console.log(`${spaces}    ${attr.type} ${name} = ${valueStr}`);
            
            // Print attribute metadata
            if (Object.keys(attr.metadata).length > 0) {
                for (const [metaKey, metaValue] of Object.entries(attr.metadata)) {
                    console.log(`${spaces}      (${metaKey} = ${JSON.stringify(metaValue.value)})`);
                }
            }
        }
    }
    
    // Print children
    if (prim.children.size > 0) {
        console.log(`${spaces}  Children:`);
        for (const [name, child] of prim.children) {
            printPrimRecursive(child, indent + 2);
        }
    }
}

function analyzeLayer(layer) {
    console.log('=== USD Layer Analysis ===\n');
    
    if (layer.rootPrim) {
        console.log('Root Primitive:');
        printPrimRecursive(layer.rootPrim);
    }
    
    console.log(`\nTotal prims: ${layer.prims.size}`);
    
    // Count different prim types
    const primTypes = {};
    for (const [name, prim] of layer.prims) {
        primTypes[prim.type] = (primTypes[prim.type] || 0) + 1;
    }
    
    console.log('Prim types:');
    for (const [type, count] of Object.entries(primTypes)) {
        console.log(`  ${type}: ${count}`);
    }
}

// Main execution
function main() {
    console.log('USDA JavaScript Parser Example\n');
    console.log('=============================\n');
    
    const layer = parseAndPrint(exampleUsda);
    if (layer) {
        analyzeLayer(layer);
    }
}

if (require.main === module) {
    main();
}

module.exports = { parseAndPrint, analyzeLayer };