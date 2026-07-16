// Smoke test for usdzconvert with the next-core + tydra-next only WASM module.
//
// Runs on the default wasm32 next glue; set TINYUSDZ_WASM64=1 to use
// tinyusdz_next_64.js.

import assert from 'node:assert/strict';
import * as THREE from 'three';

import { convertFolderToUSDZ, loadWasm, unpackUSDZ } from '../src/usdzconvert.js';
import { TinyUSDZLoader } from '../src/tinyusdz/TinyUSDZLoader.js';
import {
  buildNextThreeNode,
  createNextMaterial,
  materialXTextureSpecForParam,
  NextTextureLoadingManager,
  textureColorRole
} from '../src/tinyusdz/NextRenderSceneUtils.js';

const SCENE_USDA = `#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
)
def Xform "World"
{
    def Mesh "Tri"
    {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    }
}
`;

const OPENPBR_NODEGRAPH_SPHERE_USDA = `#usda 1.0
(
    defaultPrim = "World"
)
def Xform "World"
{
    def Sphere "Ball"
    {
        rel material:binding = </World/Mat>
    }
    def Material "Mat"
    {
        token outputs:surface.connect = </World/Mat/PreviewFallback.outputs:surface>
        token outputs:mtlx:surface.connect = </World/Mat/Surface.outputs:surface>
        string config:mtlx:version = "1.39"
        def Shader "PreviewFallback"
        {
            uniform token info:id = "UsdPreviewSurface"
            color3f inputs:diffuseColor = (1, 1, 1)
            token outputs:surface
        }
        def Shader "Surface"
        {
            uniform token info:id = "ND_open_pbr_surface_surfaceshader"
            color3f inputs:base_color.connect = </World/Mat/Graph.outputs:result>
            token outputs:surface
        }
        def NodeGraph "Graph"
        {
            color3f outputs:result.connect = </World/Mat/Graph/invert.outputs:out>
            def Shader "color"
            {
                uniform token info:id = "ND_constant_color3"
                color3f inputs:value = (1, 0.5, 0.1)
                color3f outputs:out
            }
            def Shader "invert"
            {
                uniform token info:id = "ND_subtract_color3"
                color3f inputs:in1 = (1, 1, 1)
                color3f inputs:in2.connect = </World/Mat/Graph/color.outputs:out>
                color3f outputs:out
            }
        }
    }
}
`;

const ENTITY_SCENE_USDA = `#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
    metersPerUnit = 1
)
def Xform "World"
{
    def Xform "Animated"
    {
        double3 xformOp:translate.timeSamples = {
            0: (0, 0, 0),
            1: (1, 0, 0)
        }
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def SkelAnimation "SkelAnim"
    {
        uniform token[] joints = ["root", "root/child"]
        uniform token[] blendShapes = ["smile"]
        float3[] translations.timeSamples = {
            0: [(0, 0, 0), (1, 0, 0)],
            1: [(0, 1, 0), (1, 1, 0)]
        }
        quatf[] rotations.timeSamples = {
            0: [(1, 0, 0, 0), (1, 0, 0, 0)],
            1: [(1, 0, 0, 0), (0, 0, 0, 1)]
        }
        float[] blendShapeWeights.timeSamples = {
            0: [0],
            1: [1]
        }
    }

    def Skeleton "Skel"
    {
        uniform token[] joints = ["root", "root/child"]
        rel skel:animationSource = </World/SkelAnim>
    }

    def Camera "MainCamera"
    {
        float focalLength = 35
    }

    def SphereLight "KeyLight"
    {
        float inputs:intensity = 450
        float inputs:radius = 2
        rel light:link = </World/Tri>
        rel shadow:link = </World/Dust>
        rel filters = </World/LightFilterPath>
    }

    def Points "Dust"
    {
        point3f[] points = [(0, 0, 0), (1, 0, 0)]
        float[] widths = [0.1, 0.2]
    }

    def BasisCurves "Curve"
    {
        int[] curveVertexCounts = [2]
        point3f[] points = [(0, 0, 0), (0, 1, 0)]
    }

    def HermiteCurves "Hermite"
    {
        int[] curveVertexCounts = [2]
        point3f[] points = [(0, 0, 0), (0, 1, 0)]
    }

    def Material "Mat"
    {
        token outputs:surface.connect = </World/Mat/Surface.outputs:surface>

        def Shader "Surface"
        {
            uniform token info:id = "UsdPreviewSurface"
            color3f inputs:diffuseColor.connect = </World/Mat/BaseTex.outputs:rgb>
            float inputs:opacity.connect = </World/Mat/BaseTex.outputs:r>
            token outputs:surface
        }

        def Shader "BaseTex"
        {
            uniform token info:id = "UsdUVTexture"
            asset inputs:file = @textures/diffuse.<UDIM>.png@
            token inputs:sourceColorSpace = "raw"
            token inputs:wrapS = "clamp"
            token inputs:wrapT = "repeat"
            color3f outputs:rgb
            float outputs:r
        }
    }

    def Mesh "Tri"
    {
        rel material:binding = </World/Mat>
        uniform bool doubleSided = true
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    }

    def Mesh "Concave"
    {
        int[] faceVertexCounts = [5]
        int[] faceVertexIndices = [0, 1, 2, 3, 4]
        point3f[] points = [
            (0, 0, 0), (3, 0, 0), (3, 3, 0), (2, 1, 0), (0, 3, 0)
        ]
    }

    def Mesh "ConcaveFaceVarying"
    {
        int[] faceVertexCounts = [5]
        int[] faceVertexIndices = [0, 1, 2, 3, 4]
        point3f[] points = [
            (0, 0, 0), (3, 0, 0), (3, 3, 0), (2, 1, 0), (0, 3, 0)
        ]
        texCoord2f[] primvars:st = [(0, 0), (1, 0), (1, 1), (0.66, 0.33), (0, 1)] (
            interpolation = "faceVarying"
        )
        int[] primvars:st:indices = [0, 1, 2, 3, 4]
    }
}
`;

async function testAsync(name, fn) {
  try {
    await fn();
    console.log(`ok - ${name}`);
  } catch (err) {
    console.error(`not ok - ${name}`);
    console.error(err);
    process.exitCode = 1;
  }
}

const wasm64 = process.env.TINYUSDZ_WASM64 === '1';
const glue = wasm64 ? '../src/tinyusdz/tinyusdz_next_64.js'
                    : '../src/tinyusdz/tinyusdz_next.js';
const glueUrl = new URL(glue, import.meta.url).href;
const wasmDir = new URL('../src/tinyusdz/', import.meta.url);
const native = await loadWasm(() => import(glueUrl), {
  locateFile: (file) => new URL(file, wasmDir).pathname,
});

await testAsync('next texture roles preserve linear wide-gamut inputs', async () => {
  assert.equal(textureColorRole('map', 'lin_rec2020'), 'data');
  assert.equal(textureColorRole('map', 'lin_displayp3'), 'data');
  assert.equal(textureColorRole('map', 'acescg'), 'data');
  assert.equal(textureColorRole('map', 'srgb_displayp3'), 'color');
});

await testAsync('next texture graph preserves image power operations', async () => {
  const graph = {
    nodegraph: {
      nodes: [
        {
          name: 'image', category: 'image_color4', inputs: [
            { name: 'file', value: './checker.png', colorspace: 'lin_rec709' }
          ]
        },
        {
          name: 'convert', category: 'convert_color4_color3', inputs: [
            { name: 'in', nodename: 'image' }
          ]
        },
        {
          name: 'gamma', category: 'power_color3', inputs: [
            { name: 'in1', nodename: 'convert' },
            { name: 'in2', value: [0.4545, 0.4545, 0.4545] }
          ]
        }
      ],
      outputs: [{ name: 'gamma_out', nodename: 'gamma' }]
    },
    connections: [{ input: 'base_color', output: 'gamma_out' }]
  };
  const spec = materialXTextureSpecForParam(graph, 'base_color');
  assert.equal(spec.filename, './checker.png');
  assert.equal(spec.colorspace, 'lin_rec709');
  assert.deepEqual(spec.ops.map((op) => op.category), ['power']);
  assert.deepEqual(spec.ops[0].node.inputs[1].value, [0.4545, 0.4545, 0.4545]);
});

await testAsync('next queues graph-derived opacity from a shared color image', async () => {
  const graph = {
    nodegraph: {
      nodes: [
        {
          name: 'image', category: 'image', inputs: [
            { name: 'file', value: './beam.png' }
          ]
        },
        {
          name: 'extract', category: 'extract', inputs: [
            { name: 'in', nodename: 'image' },
            { name: 'index', value: 0 }
          ]
        }
      ],
      outputs: [{ name: 'opacity_out', nodename: 'extract' }]
    },
    connections: [{ input: 'geometry_opacity', output: 'opacity_out' }]
  };
  const manager = new NextTextureLoadingManager();
  const material = createNextMaterial({
    material: {
      baseColor: [1, 1, 1],
      opacity: 1,
      openPBRNodeGraphJson: JSON.stringify(graph),
      textureMetadata: {
        baseColor: { sourceColorSpace: 'sRGB' },
        opacity: { sourceColorSpace: 'sRGB' }
      }
    },
    texturePaths: {
      baseColor: './beam.png',
      opacity: './beam.png'
    }
  }, {}, manager, false);
  assert.equal(material.transparent, true);
  assert.equal(manager.tasks.length, 2,
    'shared color/opacity image needs separate direct and baked tasks');
  const alphaTask = manager.tasks.find((task) =>
    task.bindings.some((binding) => binding.mapProperty === 'alphaMap'));
  assert.ok(alphaTask, 'graph-derived opacity must queue an alphaMap');
  assert.deepEqual(alphaTask.materialXOps.map((op) => op.category), ['extract']);
});

assert.equal(typeof native.NextUSDZConverterNative, 'function',
  'next-only glue should expose NextUSDZConverterNative');
assert.equal(typeof native.TinyUSDZLoaderNative, 'undefined',
  'next-only glue must not depend on the legacy converter binding');

await testAsync('next RenderStream exposes analytic geometry and MaterialX node graphs', async () => {
  const stream = new native.RenderStream();
  try {
    const bytes = new TextEncoder().encode(OPENPBR_NODEGRAPH_SPHERE_USDA);
    const result = stream.begin(bytes);
    assert.ok(result?.success, result?.error || stream.error());
    assert.equal(result.meshCount, 1,
      'analytic Sphere should be exposed as a render mesh');
    const mesh = stream.getMesh(0);
    assert.equal(mesh.primPath, '/World/Ball');
    assert.ok(mesh.points?.length > 0, 'generated sphere should expose positions');
    assert.ok(mesh.normals?.length > 0, 'generated sphere should expose normals');
    assert.equal(mesh.material?.shaderType, 'OpenPBR');
    assert.ok(mesh.material?.baseColor?.every((value, index) =>
      Math.abs(value - [0, 0.5, 0.9][index]) < 1e-6),
    'constant MaterialX subtract network should drive the fallback color');
    assert.deepEqual(mesh.material?.emissive, [0, 0, 0],
      'zero OpenPBR emission luminance should suppress authored emission color');
    const graph = JSON.parse(mesh.material.openPBRNodeGraphJson);
    assert.equal(graph.nodegraph.name, 'Graph');
    assert.deepEqual(graph.nodegraph.nodes.map((node) => node.name),
      ['color', 'invert']);
    assert.equal(graph.nodegraph.nodes[1].inputs[1].nodename, 'color');
    assert.deepEqual(graph.connections, [{
      input: 'base_color', nodegraph: 'Graph', output: 'result'
    }]);
  } finally {
    stream.end();
    stream.delete();
  }
});

await testAsync('next mesh merge preserves singleton mesh transforms', async () => {
  const fixture = `#usda 1.0
def Xform "Parent" {
  double3 xformOp:translate = (10, 0, 0)
  uniform token[] xformOpOrder = ["xformOp:translate"]
  def Mesh "Mesh" {
    point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
  }
}`;
  const stream = new native.RenderStream();
  try {
    stream.setMeshMerge(true);
    stream.setMeshMergeBakeTransform(true);
    stream.setMeshOnly(true);
    const result = stream.begin(new TextEncoder().encode(fixture));
    assert.ok(result?.success, result?.error || stream.error());
    assert.equal(stream.meshCount(), 1);
    const mesh = stream.getMesh(0);
    assert.equal(mesh.primPath, '/Parent/Mesh',
      'a one-mesh group must retain its authored identity');
    assert.equal(mesh.worldMatrix[12], 10,
      'a one-mesh group must retain its authored transform');
  } finally {
    stream.end();
    stream.delete();
  }
});

await testAsync('next mesh merge preserves geometry in native instances', async () => {
  const fixture = `#usda 1.0
def Xform "Prototype" {
  def Mesh "Mesh" {
    point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
  }
}
def Xform "First" (
  instanceable = true
  prepend references = </Prototype>
) {
  double3 xformOp:translate = (10, 0, 0)
  uniform token[] xformOpOrder = ["xformOp:translate"]
}
def Xform "Second" (
  instanceable = true
  prepend references = </Prototype>
) {
  double3 xformOp:translate = (20, 0, 0)
  uniform token[] xformOpOrder = ["xformOp:translate"]
}`;
  const stream = new native.RenderStream();
  try {
    stream.setMeshMerge(true);
    stream.setMeshMergeBakeTransform(true);
    stream.setMeshOnly(true);
    const result = stream.begin(new TextEncoder().encode(fixture));
    assert.ok(result?.success, result?.error || stream.error());
    let merged = null;
    for (let i = 0; i < stream.meshCount(); ++i) {
      const candidate = stream.getMesh(i);
      if (candidate.primPath?.includes('__tinyusdz_next_merged')) {
        merged = candidate;
        break;
      }
    }
    assert.ok(merged, 'instance meshes sharing a material should merge');
    const points = new Float32Array(native.HEAPU8.buffer,
      Number(merged.points.ptr), Number(merged.points.length));
    let minX = Infinity;
    let maxX = -Infinity;
    for (let i = 0; i < points.length; i += 3) {
      minX = Math.min(minX, points[i]);
      maxX = Math.max(maxX, points[i]);
    }
    assert.ok(maxX - minX >= 1,
      'baked instance triangles must retain non-zero spatial extent');
  } finally {
    stream.end();
    stream.delete();
  }
});

await testAsync('next mesh-only path uses robust concave triangulation', async () => {
  const stream = new native.RenderStream();
  try {
    stream.setMeshOnly(true);
    const result = stream.begin(new TextEncoder().encode(ENTITY_SCENE_USDA));
    assert.ok(result?.success, result?.error || stream.error());
    let mesh = null;
    for (let i = 0; i < stream.meshCount(); ++i) {
      const candidate = stream.getMesh(i);
      if (candidate.primPath === '/World/Concave') {
        mesh = candidate;
        break;
      }
    }
    assert.ok(mesh, 'concave fixture mesh should be present');
    const points = new Float32Array(native.HEAPU8.buffer,
      Number(mesh.points.ptr), Number(mesh.points.length));
    const indices = new Uint32Array(native.HEAPU8.buffer,
      Number(mesh.indices.ptr), Number(mesh.indices.length));
    assert.equal(indices.length, 9, 'pentagon should produce three triangles');
    let area = 0;
    for (let i = 0; i < indices.length; i += 3) {
      const a = indices[i] * 3;
      const b = indices[i + 1] * 3;
      const c = indices[i + 2] * 3;
      const cross = (points[b] - points[a]) *
          (points[c + 1] - points[a + 1]) -
        (points[b + 1] - points[a + 1]) *
          (points[c] - points[a]);
      area += Math.abs(cross) * 0.5;
    }
    assert.ok(Math.abs(area - 6) < 1e-5,
      `mesh-only earcut must preserve the concave notch (area ${area})`);
  } finally {
    stream.end();
    stream.delete();
  }
});

function assertReloadsWithRenderStream(usdz, label) {
  const stream = new native.RenderStream();
  try {
    const result = stream.begin(usdz);
    assert.ok(result && result.success,
      `${label}: RenderStream should load converted USDZ: ${result?.error || stream.error()}`);
    assert.equal(result.meshCount, 1, `${label}: expected one mesh`);
    assert.equal(typeof stream.numNodes, 'function', `${label}: RenderStream should expose node count`);
    assert.equal(typeof stream.getNode, 'function', `${label}: RenderStream should expose node getter`);
    assert.equal(typeof stream.numLights, 'function', `${label}: RenderStream should expose light count`);
    assert.equal(typeof stream.getLight, 'function', `${label}: RenderStream should expose light getter`);
    assert.equal(typeof stream.numCameras, 'function', `${label}: RenderStream should expose camera count`);
    assert.equal(typeof stream.getCamera, 'function', `${label}: RenderStream should expose camera getter`);
    assert.equal(typeof stream.numPointInstancers, 'function', `${label}: RenderStream should expose point-instancer count`);
    assert.equal(typeof stream.getPointInstancer, 'function', `${label}: RenderStream should expose point-instancer getter`);
    assert.equal(typeof stream.numPointInstanceDraws, 'function', `${label}: RenderStream should expose point-instance-draw count`);
    assert.equal(typeof stream.getPointInstanceDraw, 'function', `${label}: RenderStream should expose point-instance-draw getter`);
    assert.equal(typeof stream.numSkeletons, 'function', `${label}: RenderStream should expose skeleton count`);
    assert.equal(typeof stream.getSkeleton, 'function', `${label}: RenderStream should expose skeleton getter`);
    assert.equal(typeof stream.numAnimations, 'function', `${label}: RenderStream should expose animation count`);
    assert.equal(typeof stream.getAnimation, 'function', `${label}: RenderStream should expose animation getter`);
    assert.equal(typeof stream.getAnimationView, 'function', `${label}: RenderStream should expose fast animation getter`);
    assert.equal(typeof stream.getAllAnimations, 'function', `${label}: RenderStream should expose all animations getter`);
    assert.equal(typeof stream.getAnimationInfo, 'function', `${label}: RenderStream should expose animation info getter`);
    assert.equal(typeof stream.getAllAnimationInfos, 'function', `${label}: RenderStream should expose all animation info getter`);
    assert.equal(typeof stream.getUnsupportedRenderables, 'function',
      `${label}: RenderStream should expose unsupported renderable diagnostics`);
  } finally {
    stream.end();
    stream.delete();
  }
}

function assertEntityAccessorsWithRenderStream(usdz, label) {
  const stream = new native.RenderStream();
  try {
    const result = stream.begin(usdz);
    assert.ok(result && result.success,
      `${label}: RenderStream should load entity scene: ${result?.error || stream.error()}`);
    assert.ok(stream.numNodes() >= 4, `${label}: expected next node hierarchy`);
    assert.ok(stream.numLights() >= 1, `${label}: expected next light extraction`);
    assert.ok(stream.numCameras() >= 1, `${label}: expected next camera extraction`);
    assert.ok(stream.numPoints() >= 1, `${label}: expected next Points extraction`);
    assert.ok(stream.numSkeletons() >= 1, `${label}: expected next Skeleton extraction`);
    assert.ok(stream.numUnsupportedRenderables() >= 1,
      `${label}: expected unsupported renderable diagnostics`);

    const node = stream.getNode(0);
    assert.equal(typeof node.primPath, 'string', `${label}: node should expose primPath`);
    assert.ok(Array.isArray(node.children), `${label}: node should expose child node IDs`);

    const light = stream.getLight(0);
    assert.equal(light.type, 'sphere', `${label}: light should preserve concrete type`);
    assert.equal(typeof light.intensity, 'number', `${label}: light should expose intensity`);
    assert.ok(Array.isArray(light.lightLinkTargets), `${label}: light should expose light-link targets`);
    assert.ok(light.lightLinkTargets.includes('/World/Tri'),
      `${label}: light-link targets should preserve relationship paths`);
    assert.ok(light.shadowLinkTargets.includes('/World/Dust'),
      `${label}: shadow-link targets should preserve relationship paths`);
    assert.ok(light.filterTargets.includes('/World/LightFilterPath'),
      `${label}: filter targets should preserve relationship paths`);

    const camera = stream.getCamera(0);
    assert.equal(camera.type, 'perspective', `${label}: camera should expose type`);
    assert.equal(typeof camera.focalLength, 'number', `${label}: camera should expose focal length`);

    const skeleton = stream.getSkeleton(0);
    assert.equal(skeleton.animationSourcePath, '/World/SkelAnim',
      `${label}: skeleton should expose animation source relationship`);

    const points = stream.getPoints(0);
    assert.equal(points.pointCount, 2, `${label}: point cloud should expose point count`);
    assert.ok(points.points && points.points.length === 6,
      `${label}: point cloud should expose xyz buffer descriptor`);

    const mesh = stream.getMesh(0);
    const baseMeta = mesh?.material?.textureMetadata?.baseColor;
    assert.equal(typeof mesh?.material?.materialXJson, 'string',
      `${label}: material should expose MaterialX JSON string`);
    const materialJson = JSON.parse(mesh.material.materialXJson);
    assert.equal(materialJson.primPath, '/World/Mat',
      `${label}: material JSON should identify source material`);
    assert.ok(materialJson.previewSurface,
      `${label}: material JSON should include shader parameter export`);
    assert.ok(baseMeta, `${label}: material should expose baseColor texture metadata`);
    assert.equal(baseMeta.path, 'textures/diffuse.<UDIM>.png',
      `${label}: material metadata should preserve texture asset path`);
    assert.equal(baseMeta.sourceColorSpace, 'raw',
      `${label}: material metadata should preserve sourceColorSpace`);
    assert.equal(baseMeta.wrapS, 'clamp', `${label}: material metadata should preserve wrapS`);
    assert.equal(baseMeta.wrapT, 'repeat', `${label}: material metadata should preserve wrapT`);
    assert.equal(baseMeta.isUdim, true, `${label}: material metadata should flag UDIM paths`);
    assert.equal(mesh.material.opacityTexture, 'textures/diffuse.<UDIM>.png',
      `${label}: material should preserve connected opacity texture path`);

    const unsupported = stream.getUnsupportedRenderables();
    assert.ok(Array.isArray(unsupported), `${label}: unsupported renderables should be an array`);
    // BasisCurves converts now; HermiteCurves stays reported-unsupported.
    assert.ok(unsupported.some((item) => item.type === 'HermiteCurves' || /HermiteCurves/i.test(item.reason || '')),
      `${label}: unsupported diagnostics should include HermiteCurves`);
    assert.ok(!unsupported.some((item) => item.type === 'BasisCurves'),
      `${label}: BasisCurves should no longer be reported unsupported`);
    if (typeof stream.numCurves === 'function') {
      assert.ok(stream.numCurves() >= 1, `${label}: BasisCurves should convert to render curves`);
    }

    const animationCount = stream.numAnimations();
    assert.equal(typeof animationCount, 'number', `${label}: animation count should be numeric`);
    if (animationCount > 0) {
      const animation = stream.getAnimation(0);
      assert.ok(Array.isArray(animation.channels), `${label}: animation should expose channels`);
      const animationView = stream.getAnimationView(0);
      assert.ok(Array.isArray(animationView.channels),
        `${label}: fast animation should expose channels`);
      const viewArraySampler = animationView.samplers.find((sampler) => sampler?.arrayValues);
      if (viewArraySampler) {
        assert.equal(viewArraySampler.arrayValues.dtype, 'f32',
          `${label}: fast skeletal data should use a float heap descriptor`);
        assert.ok(viewArraySampler.arrayValues.ptr >= 0 && viewArraySampler.arrayValues.length >= 12,
          `${label}: fast skeletal descriptor should preserve the complete array`);
      }
      const skeletal = stream.getAllAnimations()
        .find((item) => item && item.has_skeletal_animation);
      assert.ok(skeletal, `${label}: animation should expose skeletal animation metadata`);
      const skeletalTrack = skeletal.tracks.find((track) => track && track.isSkeletal);
      assert.ok(skeletalTrack, `${label}: skeletal animation should expose skeletal track`);
      assert.ok(Array.isArray(skeletalTrack.arrayValues),
        `${label}: skeletal track should expose full array values`);
      assert.ok(skeletalTrack.arrayValues.length >= 12,
        `${label}: skeletal track should preserve flattened joint arrays`);
      assert.deepEqual(skeletalTrack.jointRemap, [0, 1],
        `${label}: skeletal track should expose joint remap`);
      assert.equal(skeletalTrack.targetSkeletonPath, '/World/Skel',
        `${label}: skeletal track should resolve target skeleton`);
      assert.ok(skeletalTrack.valueStride === 3 || skeletalTrack.valueStride === 4 ||
          skeletalTrack.valueStride === 1,
        `${label}: skeletal track should expose value stride`);
      assert.ok(Array.isArray(stream.getAllAnimationInfos()),
        `${label}: animation infos should be array-compatible`);
      assert.ok(stream.getAllAnimationInfos().some((info) => info.sourceType === 'SkelAnimation'),
        `${label}: animation infos should identify SkelAnimation source`);
    }
  } finally {
    stream.end();
    stream.delete();
  }
}

async function assertEntityAccessorsWithAdapter(usdz, label) {
  const loader = new TinyUSDZLoader({ suppressNativeInfoLogs: true });
  await loader.init({ useMemory64: wasm64, useNextOnlyWasm: true });
  const adapter = await new Promise((resolve, reject) => {
    loader.parse(usdz, `${label}.usdz`, resolve, reject, { backend: 'next' });
  });
  try {
    assert.equal(adapter.__backend, 'next', `${label}: expected next adapter`);
    assert.ok(adapter.numNodes() >= 4, `${label}: adapter should expose node count`);
    assert.ok(adapter.numPoints() >= 1, `${label}: adapter should expose point cloud count`);
    assert.ok(adapter.numRootNodes() >= 1, `${label}: adapter should expose root node count`);
    const root = adapter.getDefaultRootNode();
    assert.ok(root, `${label}: adapter should expose default root node`);
    assert.equal(root.nodeType, 'xform', `${label}: root node should be buildThreeNode-compatible`);
    assert.ok(Array.isArray(root.children), `${label}: root node children should be nested nodes`);
    assert.ok(root.children.some((child) => child.nodeType === 'mesh' && child.contentId >= 0),
      `${label}: nested root should include mesh content IDs`);

    const mesh = adapter.getMeshCopy(0);
    assert.ok(mesh?.points instanceof Float32Array, `${label}: getMeshCopy should return typed points`);
    assert.ok(mesh?.uvs === null || mesh.uvs instanceof Float32Array,
      `${label}: getMeshCopy should expose legacy uvs alias`);
    assert.equal(JSON.parse(mesh.material.materialXJson).primPath, '/World/Mat',
      `${label}: adapter should preserve material JSON export`);
    assert.equal(mesh.doubleSided, true,
      `${label}: adapter should preserve authored mesh doubleSided`);

    let concave = null;
    let concaveFaceVarying = null;
    for (let i = 0; i < adapter.numMeshes(); ++i) {
      const candidate = adapter.getMeshCopy(i);
      if (candidate?.primPath === '/World/Concave') concave = candidate;
      if (candidate?.primPath === '/World/ConcaveFaceVarying') {
        concaveFaceVarying = candidate;
      }
    }
    assert.ok(concave?.indices?.length === 9,
      `${label}: concave pentagon should produce three triangles`);
    let triangleArea = 0;
    for (let i = 0; i < concave.indices.length; i += 3) {
      const a = concave.indices[i] * 3;
      const b = concave.indices[i + 1] * 3;
      const c = concave.indices[i + 2] * 3;
      const cross = (concave.points[b] - concave.points[a]) *
          (concave.points[c + 1] - concave.points[a + 1]) -
        (concave.points[b + 1] - concave.points[a + 1]) *
          (concave.points[c] - concave.points[a]);
      triangleArea += Math.abs(cross) * 0.5;
    }
    assert.ok(Math.abs(triangleArea - 6) < 1e-5,
      `${label}: robust triangulation must not overlap the concave notch (area ${triangleArea})`);
    assert.ok(concaveFaceVarying?.points?.length === 27,
      `${label}: face-varying concave pentagon should expand to nine triangle corners`);
    assert.ok(!concaveFaceVarying.indices || concaveFaceVarying.indices.length === 0,
      `${label}: face-varying fixture should exercise the non-indexed soup path`);
    let soupArea = 0;
    for (let a = 0; a < concaveFaceVarying.points.length; a += 9) {
      const b = a + 3;
      const c = a + 6;
      const cross = (concaveFaceVarying.points[b] - concaveFaceVarying.points[a]) *
          (concaveFaceVarying.points[c + 1] - concaveFaceVarying.points[a + 1]) -
        (concaveFaceVarying.points[b + 1] - concaveFaceVarying.points[a + 1]) *
          (concaveFaceVarying.points[c] - concaveFaceVarying.points[a]);
      soupArea += Math.abs(cross) * 0.5;
    }
    assert.ok(Math.abs(soupArea - 6) < 1e-5,
      `${label}: face-varying corner remap must preserve earcut area (area ${soupArea})`);
    assert.equal(typeof adapter.getUpAxis(), 'string', `${label}: adapter should expose up axis`);
    const points = adapter.getPoints(0);
    assert.ok(points?.points instanceof Float32Array, `${label}: adapter should expose point cloud data`);
    assert.equal(points.pointCount, 2, `${label}: adapter should expose point cloud point count`);
    assert.equal(adapter.numImages(), 0, `${label}: next adapter should report zero decoded images`);
    assert.equal(adapter.getStats().providedAssetBytes, 0,
      `${label}: root layer must not be duplicated in the value-clip asset map`);

    const adapterAnimation = adapter.getAllAnimations()
      .find((item) => item?.has_skeletal_animation);
    if (adapterAnimation) {
      const skeletalTrack = adapterAnimation.tracks.find((track) => track?.arrayValues);
      assert.ok(skeletalTrack?.arrayValues instanceof Float32Array,
        `${label}: adapter should own fast skeletal animation data`);
      const sampler = adapterAnimation.samplers[skeletalTrack.sampler];
      assert.equal(skeletalTrack.arrayValues, sampler.arrayValues,
        `${label}: sampler and track should share skeletal array storage`);
      assert.equal(skeletalTrack.times, sampler.times,
        `${label}: sampler and track should share time storage`);
    }

    const materialResult = adapter.getMaterialWithFormat(0, 'json');
    assert.equal(materialResult.error, null, `${label}: material JSON should be available`);
    assert.doesNotThrow(() => JSON.parse(materialResult.data),
      `${label}: material JSON should parse`);

    assert.ok(adapter.numLights() >= 1, `${label}: adapter should expose light count`);
    assert.equal(adapter.getLight(0).type, 'sphere', `${label}: adapter should expose light data`);
    assert.ok(adapter.numCameras() >= 1, `${label}: adapter should expose camera count`);
    assert.equal(adapter.getCamera(0).type, 'perspective', `${label}: adapter should expose camera data`);
    assert.ok(adapter.numUnsupportedRenderables() >= 1,
      `${label}: adapter should expose unsupported renderable count`);
    assert.ok(Array.isArray(adapter.getUnsupportedRenderables()),
      `${label}: adapter should expose unsupported renderable list`);
    const built = buildNextThreeNode(adapter, {
      skipTextures: true,
      showCurves: false,
      releaseBuildData: false,
    });
    const curveGroups = [];
    const authoredDoubleSidedMeshes = [];
    built.node.traverse((object) => {
      if (object.userData?.usdCurves) curveGroups.push(object);
      if (object.userData?.usdMesh?.doubleSided) authoredDoubleSidedMeshes.push(object);
    });
    assert.ok(curveGroups.length >= 1, `${label}: fixture should build curve primitives`);
    assert.ok(curveGroups.every((object) => object.visible === false),
      `${label}: curve primitives should honor the default-off viewer option`);
    assert.ok(authoredDoubleSidedMeshes.length >= 1,
      `${label}: fixture should build an authored double-sided mesh`);
    assert.ok(authoredDoubleSidedMeshes.every((object) => {
      const materials = Array.isArray(object.material) ? object.material : [object.material];
      return materials.every((material) => material.side === THREE.DoubleSide);
    }), `${label}: authored doubleSided should select Three.DoubleSide`);

    const pruned = buildNextThreeNode(adapter, {
      skipTextures: true,
      showCurves: false,
      releaseBuildData: false,
      pruneEmptyNodes: true,
    });
    let fullObjectCount = 0;
    let prunedObjectCount = 0;
    built.node.traverse(() => { fullObjectCount++; });
    pruned.node.traverse(() => { prunedObjectCount++; });
    assert.ok(prunedObjectCount < fullObjectCount,
      `${label}: pruning should omit empty non-rendering hierarchy nodes`);
    assert.ok(pruned.node.getObjectByName('Animated'),
      `${label}: pruning must retain animated transform targets`);
    assert.ok(pruned.node.getObjectByName('Tri'),
      `${label}: pruning must retain renderable prim transforms`);
    pruned.node.traverse((object) => {
      object.geometry?.dispose?.();
      object.material?.dispose?.();
    });
    built.node.traverse((object) => {
      object.geometry?.dispose?.();
      object.material?.dispose?.();
    });
  } finally {
    adapter.end();
  }
}

async function assertMeshOnlyAdapter(usdz, label) {
  const loader = new TinyUSDZLoader({ suppressNativeInfoLogs: true });
  await loader.init({ useMemory64: wasm64, useNextOnlyWasm: true });
  const adapter = await new Promise((resolve, reject) => {
    loader.parse(usdz, `${label}.usdz`, resolve, reject, {
      backend: 'next',
      meshOnly: true,
    });
  });
  try {
    assert.ok(adapter.numMeshes() >= 1, `${label}: mesh-only adapter should retain meshes`);
    assert.equal(adapter.numNodes(), 0, `${label}: mesh-only adapter should skip nodes`);
    assert.equal(adapter.numPointInstanceDraws(), 0,
      `${label}: mesh-only adapter should skip point-instance draws`);
    assert.equal(adapter.getStats().renderSceneNodes, 0,
      `${label}: mesh-only native stream should skip full render-scene conversion`);
  } finally {
    adapter.delete();
  }
}

async function assertWorkerModuleImports() {
  const previousSelf = globalThis.self;
  const messages = [];
  globalThis.self = {
    postMessage(message) {
      messages.push(message);
    },
    onmessage: null,
  };
  try {
    await import(new URL('../src/tinyusdz/TinyUSDZWorker.js?next-only-smoke', import.meta.url).href);
    assert.ok(messages.some((message) => message?.type === 'ready'),
      'TinyUSDZWorker module should signal ready after import');
  } finally {
    if (previousSelf === undefined) {
      delete globalThis.self;
    } else {
      globalThis.self = previousSelf;
    }
  }
}

async function convert(rootLayerFormat) {
  const map = new Map([
    ['scene.usda', new TextEncoder().encode(SCENE_USDA)],
    ['textures/dummy.png', new Uint8Array([137, 80, 78, 71])],
  ]);
  return convertFolderToUSDZ(native, map, {
    rootPath: 'scene.usda',
    rootLayerFormat,
    reencode: false,
  });
}

async function convertEntityScene(rootLayerFormat) {
  const map = new Map([
    ['scene.usda', new TextEncoder().encode(ENTITY_SCENE_USDA)],
  ]);
  return convertFolderToUSDZ(native, map, {
    rootPath: 'scene.usda',
    rootLayerFormat,
    reencode: false,
  });
}

await testAsync('next-only WASM usdzconvert writes USDA root USDZ', async () => {
  const { usdz, stats } = await convert('usda');
  assert.equal(stats.pipeline, 'next-only');
  assert.equal(stats.rootLayerFormat, 'usda');
  const { entries, order } = unpackUSDZ(usdz);
  assert.equal(order[0], 'root.usda');
  assert.ok(new TextDecoder().decode(entries.get('root.usda')).startsWith('#usda'));
  assert.ok(entries.has('textures/dummy.png'), 'pass-through assets should be packaged');
  assertReloadsWithRenderStream(usdz, 'USDA-root USDZ');
});

await testAsync('next-only WASM usdzconvert writes USDC root USDZ', async () => {
  const { usdz, stats } = await convert('usdc');
  assert.equal(stats.pipeline, 'next-only');
  assert.equal(stats.rootLayerFormat, 'usdc');
  const { entries, order } = unpackUSDZ(usdz);
  assert.equal(order[0], 'root.usdc');
  assert.equal(new TextDecoder().decode(entries.get('root.usdc').slice(0, 8)), 'PXR-USDC');
  assertReloadsWithRenderStream(usdz, 'USDC-root USDZ');
});

await testAsync('next-only WASM exposes next scene entities to web adapters', async () => {
  const { usdz, stats } = await convertEntityScene('usdc');
  assert.equal(stats.pipeline, 'next-only');
  assertEntityAccessorsWithRenderStream(usdz, 'entity-scene RenderStream');
  await assertEntityAccessorsWithAdapter(usdz, 'entity-scene adapter');
  await assertMeshOnlyAdapter(usdz, 'entity-scene mesh-only adapter');
});

await testAsync('next-only WASM worker module remains importable', async () => {
  await assertWorkerModuleImports();
});

console.log(`usdzconvert-next-only tests done (${wasm64 ? 'wasm64' : 'wasm32'})`);
