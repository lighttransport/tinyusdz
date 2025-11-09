import * as THREE from 'three';
import * as MaterialXModule from 'three/examples/jsm/loaders/MaterialXLoader.js';

/*
 TODO / Tasks for TinyUSDZMaterialX.js (status)

 1) Add example JSON fixture (done)
        - Created `materialx_example.json` demonstrating image nodes, input-level
            `connect` entries and a top-level `connections` array.

 2) Unit tests / smoke tests (not-started)
        - Add JS tests that exercise extendMaterialXWithJSONSupport, parseJSON and
            convertOpenPBRToMaterialXShader with the fixture. (Next recommended step)

 3) Improve JSON -> MaterialX fidelity (in-progress)
        - Parser now supports node creation and connection resolution (input-level
            connects and top-level connections array). It attempts to call real
            MaterialXNode wiring APIs when available; otherwise it attaches connection
            metadata to node inputs.
        - Remaining: full nodegraph nesting, interface sockets, typed sockets.

 4) Texture / channel mapping (in-progress)
        - materialXToOpenPBR extracts image node `file` paths and places them in
            the OpenPBR object as placeholders. Next: implement channel swizzling and
            automatic texture loading/encoding handling.

 5) Demo page (not-started)
        - Create a small web demo that loads a JSON material, parses to MaterialX,
            converts to ShaderMaterial and renders with Three.js.

 Limitations & Notes
 - The parser is conservative and aims for compatibility across different
     runtime implementations. It provides fallbacks when `MaterialX`/`MaterialXNode`
     classes or specific wiring APIs are not present.
 - Connection wiring: when possible the code calls node APIs like
     `connectInput`, `setInputConnection`, `setConnection`, `connect`. If those
     aren't available the connection is stored as metadata at
     `node.inputs[name].connection = { node, output }`.
 - Texture handling: the converter will return file path strings for textures
     if actual `THREE.Texture` instances are not provided. To get a working
     ShaderMaterial you must load those files (e.g., with `THREE.TextureLoader`)
     and assign textures to the `TinyUSDZOpenPBR` instance before conversion, or
     use an async helper that loads textures for you.
 - MaterialX node/type names vary between tools. The surface node detection
     currently looks for type names containing `standard`, `surface`, or `pbr`.
     If your shading network uses different names, update `materialXToOpenPBR`
     accordingly.

 Recommended next steps
 - Add a small unit test that parses `materialx_example.json` and asserts that
     the pbrShader inputs were resolved and that texture file paths are present.
 - Add an async conversion helper that loads textures automatically and returns
     a ShaderMaterial Promise.
*/


// A small mock representation of an OpenPBR (Open Materials / MaterialX-like)
// material. This is intentionally minimal - it captures the common metallic-roughness
// PBR parameters and a few texture slots.
class TinyUSDZOpenPBR {

    constructor(opts = {}) {
        // base color (albedo)
        this.baseColor = opts.baseColor !== undefined ? new THREE.Color(opts.baseColor) : new THREE.Color(1, 1, 1);
        // opacity (alpha)
        this.opacity = opts.opacity !== undefined ? opts.opacity : 1.0;
        // metallic [0..1]
        this.metallic = opts.metallic !== undefined ? opts.metallic : 0.0;
        // roughness [0..1]
        this.roughness = opts.roughness !== undefined ? opts.roughness : 0.5;
        // emissive color
        this.emissive = opts.emissive !== undefined ? new THREE.Color(opts.emissive) : new THREE.Color(0, 0, 0);

        // Texture map placeholders. Expect THREE.Texture or null.
        this.baseColorMap = opts.baseColorMap || null; // map -> base color / albedo
        this.metallicRoughnessMap = opts.metallicRoughnessMap || null; // combined metalness (B) / roughness (G) or user-defined layout
        this.normalMap = opts.normalMap || null;
        this.aoMap = opts.aoMap || null;
        this.emissiveMap = opts.emissiveMap || null;

        // UV scale/offset if needed (Vector2 or array)
        this.uvScale = opts.uvScale || new THREE.Vector2(1, 1);
        this.uvOffset = opts.uvOffset || new THREE.Vector2(0, 0);

        // metadata / name
        this.name = opts.name || '';
    }

}

// Convert the TinyUSDZOpenPBR mock to a Three.js ShaderMaterial that re-uses
// the built-in standard PBR shader from THREE.ShaderLib. This approach gives a
// MaterialX-like mapping (uniform names are preserved in userData.materialX)
// while relying on Three.js' proven PBR implementation.
//
// Basic contract:
// - input: TinyUSDZOpenPBR instance
// - output: THREE.ShaderMaterial instance (lights: true) configured with
//   the appropriate uniforms and maps. The material will have userData.materialX
//   containing a simple mapping describing the MaterialX-equivalent parameters.
//
// Notes / limitations:
// - This is a foundation implementation. It intentionally avoids reimplementing
//   a full MaterialX shader. Instead it clones Three.js' standard shader and
//   injects the OpenPBR values. Texture packing (e.g., metallic/roughness channels)
//   must match what the user provides in `metallicRoughnessMap`.
// - If you need a pure GLSL MaterialX implementation, extend/replace the shader
//   strings below.
function convertOpenPBRToMaterialXShader(openPBR, opts = {}) {
    if (!(openPBR instanceof TinyUSDZOpenPBR)) {
        // If a MaterialX graph/result was passed, try to convert it to an OpenPBR
        // instance first. Otherwise warn.
        if (openPBR && (openPBR.nodes || openPBR.nodes instanceof Array)) {
            openPBR = materialXToOpenPBR(openPBR);
        } else {
            console.warn('convertOpenPBRToMaterialXShader: expected TinyUSDZOpenPBR, got', openPBR);
        }
    }

    // Use the built-in standard shader as a foundation
    const standard = THREE.ShaderLib['standard'];
    const uniforms = THREE.UniformsUtils.clone(standard.uniforms);

    // Map simple scalar/color properties
    if (uniforms.diffuse !== undefined) {
        // three.js standard shader uses 'diffuse' uniform for the base color
        uniforms.diffuse.value = (openPBR && openPBR.baseColor) ? openPBR.baseColor.clone() : new THREE.Color(1, 1, 1);
    }
    if (uniforms.opacity !== undefined) {
        uniforms.opacity.value = (openPBR && openPBR.opacity !== undefined) ? openPBR.opacity : 1.0;
    }
    if (uniforms.emissive !== undefined) {
        uniforms.emissive.value = (openPBR && openPBR.emissive) ? openPBR.emissive.clone() : new THREE.Color(0, 0, 0);
    }
    if (uniforms.roughness !== undefined) {
        uniforms.roughness.value = (openPBR && openPBR.roughness !== undefined) ? openPBR.roughness : 0.5;
    }
    if (uniforms.metalness !== undefined) {
        uniforms.metalness.value = (openPBR && openPBR.metallic !== undefined) ? openPBR.metallic : 0.0;
    }

    // Textures
    if (openPBR && openPBR.baseColorMap) {
        if (uniforms.map !== undefined) uniforms.map.value = openPBR.baseColorMap;
    }
    // Three.js supports separate metalnessMap and roughnessMap, or a single
    // combined map depending on the user. We'll set metalnessMap/roughnessMap
    // if present; otherwise, if a combined map was provided, attempt to set
    // it to both (user should ensure packing).
    if (openPBR && openPBR.metallicRoughnessMap) {
        if (uniforms.metalnessMap !== undefined) uniforms.metalnessMap.value = openPBR.metallicRoughnessMap;
        if (uniforms.roughnessMap !== undefined) uniforms.roughnessMap.value = openPBR.metallicRoughnessMap;
        // also set map channels metadata in userData below
    }
    if (openPBR && openPBR.normalMap) {
        if (uniforms.normalMap !== undefined) uniforms.normalMap.value = openPBR.normalMap;
    }
    if (openPBR && openPBR.aoMap) {
        if (uniforms.aoMap !== undefined) uniforms.aoMap.value = openPBR.aoMap;
    }
    if (openPBR && openPBR.emissiveMap) {
        if (uniforms.emissiveMap !== undefined) uniforms.emissiveMap.value = openPBR.emissiveMap;
    }

    // UV transform support (Three.js doesn't have a built-in uniform for generic UV transform
    // in the standard shader; users should pre-transform textures or use custom shader chunks.
    // We still store the values in userData to make them visible to consumers.

    // Build the ShaderMaterial using the standard shader code. This preserves
    // Three.js lighting and BRDF implementation but gives us a ShaderMaterial
    // that can be labelled as MaterialX-backed.
    const material = new THREE.ShaderMaterial({
        defines: Object.assign({}, standard.defines),
        uniforms: uniforms,
        vertexShader: standard.vertexShader,
        fragmentShader: standard.fragmentShader,
        lights: true,
        fog: true,
        transparent: (openPBR && openPBR.opacity < 1.0) || false,
        extensions: { derivatives: true }
    });

    // Copy common material flags so the returned ShaderMaterial behaves like
    // a MeshStandardMaterial in common code paths
    material.side = THREE.DoubleSide;

    // Provide materialX metadata describing the mapping from OpenPBR -> MaterialX
    material.userData = material.userData || {};
    material.userData.materialX = {
        name: openPBR && openPBR.name ? openPBR.name : 'tinyusdz_openpbr_mx_standard_surface',
        mapping: {
            baseColor: 'diffuse',
            opacity: 'opacity',
            emissive: 'emissive',
            roughness: 'roughness',
            metallic: 'metalness',
            baseColorMap: 'map',
            metallicRoughnessMap: 'metalnessMap/roughnessMap',
            normalMap: 'normalMap',
            aoMap: 'aoMap',
            emissiveMap: 'emissiveMap'
        },
        // include original openPBR values for debugging/round-tripping
        openPBR: openPBR
    };

    return material;
}

// Convert a parsed MaterialX-like object (as produced by MaterialX.fromJSON or
// MaterialXLoader.parseJSON) into a TinyUSDZOpenPBR instance. This function
// does best-effort extraction of common PBR inputs (baseColor, metallic,
// roughness, normal) and resolves image nodes into file path placeholders.
function materialXToOpenPBR(mx) {
    if (!mx) return new TinyUSDZOpenPBR();

    const nodes = mx.nodes || [];
    const nodeMap = Object.create(null);
    nodes.forEach((n) => { if (n && n.name) nodeMap[n.name] = n; });

    // Helpers
    const resolveInput = (node, inputName) => {
        if (!node) return undefined;
        const inputs = node.inputs || {};
        const v = inputs[inputName];
        if (v === undefined) return undefined;

        // If value is an object with 'connection' metadata as added by parser
        if (v && typeof v === 'object' && v.connection) {
            const srcName = v.connection.node;
            const srcOutput = v.connection.output; // may be 'out' or channel
            const src = nodeMap[srcName];
            if (src && src.type === 'image') {
                // image nodes typically have inputs.file
                return { type: 'texture', path: (src.inputs && src.inputs.file) ? src.inputs.file : null, channel: srcOutput };
            }
            // if src has a direct value
            if (src && src.inputs && src.inputs[srcOutput]) return src.inputs[srcOutput];
            return v.connection;
        }

        // If input is a direct connect string like 'Other.out'
        if (typeof v === 'string') {
            const dot = v.indexOf('.');
            if (dot >= 0) {
                const srcName = v.substring(0, dot);
                const srcOutput = v.substring(dot + 1);
                const src = nodeMap[srcName];
                if (src && src.type === 'image') return { type: 'texture', path: (src.inputs && src.inputs.file) ? src.inputs.file : null, channel: srcOutput };
            }
            return v;
        }

        // Otherwise, return the value as-is (number, array, color etc.)
        return v;
    };

    // Find a surface node: common type names: 'standard_surface', 'pbr', 'surface', etc.
    let surf = null;
    for (const n of nodes) {
        if (!n) continue;
        const t = (n.type || '').toLowerCase();
        if (t.indexOf('standard') >= 0 || t.indexOf('surface') >= 0 || t.indexOf('pbr') >= 0) {
            surf = n; break;
        }
    }

    // If none, try to find by name 'pbrShader' as in fixture
    if (!surf) surf = nodeMap['pbrShader'] || null;

    const out = new TinyUSDZOpenPBR();
    if (!surf) return out;

    // baseColor may be color array or connected to an image
    const baseColorVal = resolveInput(surf, 'baseColor');
    if (baseColorVal) {
        if (baseColorVal.type === 'texture') out.baseColorMap = baseColorVal.path;
        else if (Array.isArray(baseColorVal)) out.baseColor = new THREE.Color(baseColorVal[0], baseColorVal[1], baseColorVal[2]);
        else if (typeof baseColorVal === 'number' || typeof baseColorVal === 'string') out.baseColor = new THREE.Color(baseColorVal);
    }

    const metallicVal = resolveInput(surf, 'metallic');
    if (metallicVal) {
        if (metallicVal.type === 'texture') out.metallicRoughnessMap = metallicVal.path;
        else out.metallic = Number(metallicVal);
    }

    const roughnessVal = resolveInput(surf, 'roughness');
    if (roughnessVal) {
        if (roughnessVal.type === 'texture') out.metallicRoughnessMap = roughnessVal.path;
        else out.roughness = Number(roughnessVal);
    }

    const normalVal = resolveInput(surf, 'normal');
    if (normalVal && normalVal.type === 'texture') out.normalMap = normalVal.path;

    // emissive
    const emissiveVal = resolveInput(surf, 'emissive');
    if (emissiveVal) {
        if (Array.isArray(emissiveVal)) out.emissive = new THREE.Color(emissiveVal[0], emissiveVal[1], emissiveVal[2]);
        else if (typeof emissiveVal === 'number' || typeof emissiveVal === 'string') out.emissive = new THREE.Color(emissiveVal);
    }

    // name
    out.name = surf.name || mx.name || out.name;

    return out;
}

// Export both the data model and the conversion function
// Extend MaterialXLoader / MaterialX classes (when available) so they can
// construct instances from a simple JSON description. This helper will
// prefer using the real MaterialX/MaterialXNode constructors exported by the
// module, but will gracefully fall back to a lightweight plain-object
// representation when those classes aren't available (useful for testing or
// environments that don't export them separately).
function extendMaterialXWithJSONSupport(module = MaterialXModule) {
    if (!module) return null;

    // Ensure MaterialX.fromJSON exists
    if (module.MaterialX) {
        if (!module.MaterialX.fromJSON) {
            module.MaterialX.fromJSON = function (json) {
                const mx = new module.MaterialX();

                // some implementations may expose an API like addNode or nodes[]
                const addNode = (node) => {
                    if (typeof mx.addNode === 'function') mx.addNode(node);
                    else {
                        mx.nodes = mx.nodes || [];
                        mx.nodes.push(node);
                    }
                };

                // First pass: create nodes and build a lookup map by name
                const nodeMap = Object.create(null);
                (json.nodes || []).forEach((nj) => {
                    let node;
                    if (module.MaterialXNode) {
                        try {
                            node = new module.MaterialXNode(nj.name || nj.type || 'node', nj.type || 'nodedef');
                        } catch (e) {
                            node = { name: nj.name || nj.type, type: nj.type, inputs: {}, outputs: {} };
                        }
                    } else {
                        node = { name: nj.name || nj.type, type: nj.type, inputs: {}, outputs: {} };
                    }

                    // Populate inputs/parameters (values only) in first pass
                    if (nj.inputs) {
                        Object.keys(nj.inputs).forEach((k) => {
                            const v = nj.inputs[k];
                            if (node.setInput) node.setInput(k, v);
                            else if (node.setParameter) node.setParameter(k, v);
                            else node.inputs[k] = v;
                        });
                    }

                    addNode(node);
                    nodeMap[node.name] = node;
                });

                // Second pass: resolve connections. Support two styles:
                // 1) input-level connect specification: node.inputs.someInput = { connect: { node: 'Other', output: 'out' } }
                // 2) top-level connections array: { connections: [ { from: 'NodeA.out', to: 'NodeB.in' }, ... ] }
                const resolveConnection = (fromRef, toRef) => {
                    // parse refs like 'NodeName.outputName' or plain node names
                    const parseRef = (ref) => {
                        if (!ref) return null;
                        if (typeof ref === 'string') {
                            const dot = ref.indexOf('.');
                            if (dot >= 0) return { node: ref.substring(0, dot), name: ref.substring(dot + 1) };
                            return { node: ref, name: null };
                        }
                        return null;
                    };

                    const from = parseRef(fromRef);
                    const to = parseRef(toRef);
                    if (!from || !to) return;

                    const fromNode = nodeMap[from.node];
                    const toNode = nodeMap[to.node];
                    if (!fromNode || !toNode) return;

                    const inputName = to.name || 'output';

                    // Prefer wiring using native APIs if available on the node objects.
                    // Common API names observed across implementations: connectInput, setInputConnection, connect
                    const connectionPayload = { node: from.node, output: from.name || 'output' };

                    // If destination node exposes connectInput(name, sourceNode, sourceOutput) use it
                    if (typeof toNode.connectInput === 'function') {
                        try { toNode.connectInput(inputName, fromNode, connectionPayload.output); return; } catch (e) { /* fallback */ }
                    }

                    // If destination node exposes setInputConnection(name, srcRef) or setConnection
                    if (typeof toNode.setInputConnection === 'function') {
                        try { toNode.setInputConnection(inputName, connectionPayload); return; } catch (e) { /* fallback */ }
                    }

                    if (typeof toNode.setConnection === 'function') {
                        try { toNode.setConnection(inputName, fromNode, connectionPayload.output); return; } catch (e) { /* fallback */ }
                    }

                    // Some nodes accept a connect method where you pass a source reference
                    if (typeof toNode.connect === 'function') {
                        try { toNode.connect(inputName, fromNode, connectionPayload.output); return; } catch (e) { /* fallback */ }
                    }

                    // Fallback: attach a simple connections list on destination input
                    toNode.inputs = toNode.inputs || {};
                    toNode.inputs[inputName] = toNode.inputs[inputName] || {};
                    toNode.inputs[inputName].connection = connectionPayload;
                };

                // Resolve input-level connects
                (json.nodes || []).forEach((nj) => {
                    const dstName = nj.name || nj.type;
                    const dstNode = nodeMap[dstName];
                    if (!dstNode) return;
                    if (nj.inputs) {
                        Object.keys(nj.inputs).forEach((k) => {
                            const v = nj.inputs[k];
                            if (v && typeof v === 'object' && v.connect) {
                                const conn = v.connect;
                                // conn may be { node: 'Other', output: 'out' } or 'Other.out'
                                if (typeof conn === 'string') resolveConnection(conn, dstName + '.' + k);
                                else resolveConnection(conn.node + (conn.output ? '.' + conn.output : ''), dstName + '.' + k);
                            }
                        });
                    }
                });

                // Resolve top-level connections array
                (json.connections || []).forEach((c) => {
                    // connections: { from: 'NodeA.out', to: 'NodeB.in' }
                    if (c.from && c.to) resolveConnection(c.from, c.to);
                });

                // Copy other top-level properties
                if (json.name) mx.name = json.name;
                if (json.doc) mx.doc = json.doc;

                return mx;
            };
        }
    }

    // Add parser to MaterialXLoader prototype if available
    if (module.MaterialXLoader && module.MaterialXLoader.prototype) {
        const proto = module.MaterialXLoader.prototype;
        if (!proto.parseJSON) {
            proto.parseJSON = function (json) {
                // Prefer module-level MaterialX.fromJSON
                if (module.MaterialX && typeof module.MaterialX.fromJSON === 'function') {
                    return module.MaterialX.fromJSON(json);
                }

                // Fallback: construct a minimal representation
                const mx = { nodes: [] };
                (json.nodes || []).forEach((nj) => {
                    const node = { name: nj.name || nj.type, type: nj.type, inputs: nj.inputs || {} };
                    mx.nodes.push(node);
                });
                if (json.name) mx.name = json.name;
                return mx;
            };
        }
    }

    return module;
}

export { TinyUSDZOpenPBR, convertOpenPBRToMaterialXShader, extendMaterialXWithJSONSupport };

/*
Usage example (quick smoke test):

import { TinyUSDZOpenPBR, convertOpenPBRToMaterialXShader } from './TinyUSDZMaterialX';

const pbr = new TinyUSDZOpenPBR({ baseColor: 0xffaa88, metallic: 0.2, roughness: 0.6, name: 'example' });
const mat = convertOpenPBRToMaterialXShader(pbr);
const geo = new THREE.SphereGeometry(1, 32, 16);
const mesh = new THREE.Mesh(geo, mat);
scene.add(mesh);

*/

/*
Example: parse `materialx_example.json` fixture and inspect resolved connections.

import * as MaterialXModule from 'three/examples/jsm/loaders/MaterialXLoader.js';
import { extendMaterialXWithJSONSupport } from './TinyUSDZMaterialX.js';

// Extend the MaterialX module with JSON parsing helpers (no-op if already present)
extendMaterialXWithJSONSupport(MaterialXModule);

// In-browser you would fetch the JSON; here we show the API usage
// fetch('/js/src/tinyusdz/materialx_example.json').then(r => r.json()).then(json => {
//   const loader = new MaterialXModule.MaterialXLoader();
//   const mx = loader.parseJSON(json);
//   console.log('Parsed MaterialX', mx);
// });

*/