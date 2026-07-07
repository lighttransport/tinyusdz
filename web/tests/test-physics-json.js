#!/usr/bin/env node

const assert = require('assert');

function loadTinyUSDZModule() {
  const candidates = process.env.TINYUSDZ_WASM64 === '1'
      ? ['../js/src/tinyusdz/tinyusdz_64.js']
      : ['../js/src/tinyusdz/tinyusdz.js'];

  for (const candidate of candidates) {
    try {
      const moduleObject = require(candidate);
      return moduleObject.default || moduleObject;
    } catch (error) {
      console.log(`⚪ Failed to load ${candidate}: ${error.message}`);
    }
  }

  throw new Error('Failed to load any TinyUSDZ JS module candidate');
}

const TinyUSDZInit = loadTinyUSDZModule();

const PHYSICS_USDA = `#usda 1.0

def Xform "World"
{
    def Xform "Body0" {}
    def Xform "Body1" {}

    def PhysicsScene "Scene" (
        prepend apiSchemas = ["MjcSceneAPI"]
    )
    {
        vector3f physics:gravityDirection = (0, 0, -1)
        float physics:gravityMagnitude = 9.81
        uniform double mjc:option:timestep = 0.002
        uniform token mjc:option:integrator = "implicit"
        uniform int mjc:option:iterations = 150
    }

    def PhysicsRevoluteJoint "Hinge" (
        prepend apiSchemas = ["MjcJointAPI"]
    )
    {
        rel physics:body0 = </World/Body0>
        rel physics:body1 = </World/Body1>
        point3f physics:localPos0 = (1, 2, 3)
        quatf physics:localRot0 = (1, 0, 0, 0)
        token physics:axis = "Z"
        uniform double[] mjc:solreflimit = [0.03, 1.1]
        uniform double[] mjc:solimplimit = [0.8, 0.9, 0.01, 0.5, 2]
        uniform double[] mjc:solreffriction = [0.04, 1.2]
        uniform double[] mjc:solimpfriction = [0.7, 0.8, 0.02, 0.4, 1.5]
        uniform token mjc:actuatorfrclimited = "true"
    }

    def PhysicsDistanceJoint "Distance"
    {
        rel physics:body0 = </World/Body0>
        rel physics:body1 = </World/Body1>
        float physics:minDistance = 0.25
        float physics:maxDistance = 2.75
    }
}
`;

function findPrim(payload, path) {
  return (payload.prims || []).find((prim) => prim.path === path);
}

async function runTest() {
  const tinyusdz = await TinyUSDZInit();
  const loader = new tinyusdz.TinyUSDZLoaderNative();

  try {
    const bytes = new TextEncoder().encode(PHYSICS_USDA);
    assert.equal(loader.loadFromBinary(bytes, 'physics-json.usda'), true,
      loader.error());

    const jsonText = loader.extractPhysicsSceneJSON();
    assert.ok(jsonText && jsonText.length > 0, 'physics JSON should be present');
    const payload = JSON.parse(jsonText);

    const scene = findPrim(payload, '/World/Scene');
    assert.ok(scene, 'PhysicsScene should be emitted');
    assert.equal(scene.properties['mjc:option:integrator'], 'implicit');
    assert.equal(scene.properties['mjc:option:iterations'], 150);
    assert.equal(scene.properties['mjc:timestep'], undefined,
      'legacy non-schema mjc:timestep key should not be emitted');

    const hinge = findPrim(payload, '/World/Hinge');
    assert.ok(hinge, 'PhysicsRevoluteJoint should be emitted');
    assert.deepEqual(hinge.properties['physics:localPos0'], [1, 2, 3]);
    assert.deepEqual(hinge.properties['physics:localRot0'], [1, 0, 0, 0]);
    assert.deepEqual(hinge.properties['mjc:solreflimit'], [0.03, 1.1]);
    assert.deepEqual(hinge.properties['mjc:solimplimit'],
      [0.8, 0.9, 0.01, 0.5, 2]);
    assert.deepEqual(hinge.properties['mjc:solreffriction'], [0.04, 1.2]);
    assert.deepEqual(hinge.properties['mjc:solimpfriction'],
      [0.7, 0.8, 0.02, 0.4, 1.5]);
    assert.equal(hinge.properties['mjc:actuatorfrclimited'], 'true');

    const distance = findPrim(payload, '/World/Distance');
    assert.ok(distance, 'PhysicsDistanceJoint should be emitted');
    assert.equal(distance.properties['physics:minDistance'], 0.25);
    assert.equal(distance.properties['physics:maxDistance'], 2.75);

    console.log('✓ physics JSON binding test passed');
  } finally {
    loader.delete();
  }
}

runTest().catch((error) => {
  console.error(error);
  process.exit(1);
});
