// Smoke test for usdzconvert with the next-core + tydra-next only WASM module.
//
// Runs on the default wasm32 next glue; set TINYUSDZ_WASM64=1 to use
// tinyusdz_next_64.js.

import assert from 'node:assert/strict';

import { convertFolderToUSDZ, loadWasm, unpackUSDZ } from '../src/usdzconvert.js';
import { TinyUSDZLoader } from '../src/tinyusdz/TinyUSDZLoader.js';

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
        }
    }

    def Mesh "Tri"
    {
        rel material:binding = </World/Mat>
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
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

assert.equal(typeof native.NextUSDZConverterNative, 'function',
  'next-only glue should expose NextUSDZConverterNative');
assert.equal(typeof native.TinyUSDZLoaderNative, 'undefined',
  'next-only glue must not depend on the legacy converter binding');

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
    assert.equal(typeof adapter.getUpAxis(), 'string', `${label}: adapter should expose up axis`);
    const points = adapter.getPoints(0);
    assert.ok(points?.points instanceof Float32Array, `${label}: adapter should expose point cloud data`);
    assert.equal(points.pointCount, 2, `${label}: adapter should expose point cloud point count`);
    assert.equal(adapter.numImages(), 0, `${label}: next adapter should report zero decoded images`);

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
  } finally {
    adapter.end();
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
});

await testAsync('next-only WASM worker module remains importable', async () => {
  await assertWorkerModuleImports();
});

console.log(`usdzconvert-next-only tests done (${wasm64 ? 'wasm64' : 'wasm32'})`);
