import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

import {
  buildMujocoPayload,
  expandMujocoIncludes,
  parseArgs
} from '../cli/urdf-to-usd.js';

function test(name, fn) {
  try {
    fn();
    console.log(`ok - ${name}`);
  } catch (err) {
    console.error(`not ok - ${name}`);
    console.error(err);
    process.exitCode = 1;
  }
}

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

function withTempDir(fn) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'tinyusdz-urdf-cli-'));
  let result;
  try {
    result = fn(dir);
  } catch (err) {
    fs.rmSync(dir, { recursive: true, force: true });
    throw err;
  }
  if (result && typeof result.finally === 'function') {
    return result.finally(() => {
      fs.rmSync(dir, { recursive: true, force: true });
    });
  }
  fs.rmSync(dir, { recursive: true, force: true });
  return result;
}

test('parseArgs recognizes unsafe path escape hatch', () => {
  const opts = parseArgs(['scene.xml', '--input-format', 'mjcf', '--allow-unsafe-paths']);
  assert.equal(opts.allowUnsafePaths, true);
});

test('MJCF includes inside input root are allowed', () => withTempDir((dir) => {
  const modelDir = path.join(dir, 'model');
  const partsDir = path.join(modelDir, 'parts');
  fs.mkdirSync(partsDir, { recursive: true });
  fs.writeFileSync(
    path.join(partsDir, 'child.xml'),
    '<mujoco><worldbody><body name="included"/></worldbody></mujoco>\n',
    'utf8'
  );

  const expanded = expandMujocoIncludes(
    '<mujoco><include file="parts/child.xml"/><worldbody/></mujoco>',
    modelDir,
    { assetDirs: [modelDir] }
  );
  assert.match(expanded, /body name="included"/);
}));

test('MJCF includes outside allowed roots are blocked by default', () => withTempDir((dir) => {
  const modelDir = path.join(dir, 'model');
  fs.mkdirSync(modelDir, { recursive: true });
  fs.writeFileSync(
    path.join(dir, 'outside.xml'),
    '<mujoco><worldbody><body name="outside"/></worldbody></mujoco>\n',
    'utf8'
  );

  assert.throws(
    () => expandMujocoIncludes(
      '<mujoco><include file="../outside.xml"/><worldbody/></mujoco>',
      modelDir,
      { assetDirs: [modelDir] }
    ),
    /Blocked MJCF include outside allowed roots/
  );
}));

test('unsafe path flag preserves trusted legacy MJCF include behavior', () => withTempDir((dir) => {
  const modelDir = path.join(dir, 'model');
  fs.mkdirSync(modelDir, { recursive: true });
  fs.writeFileSync(
    path.join(dir, 'outside.xml'),
    '<mujoco><worldbody><body name="outside"/></worldbody></mujoco>\n',
    'utf8'
  );

  const expanded = expandMujocoIncludes(
    '<mujoco><include file="../outside.xml"/><worldbody/></mujoco>',
    modelDir,
    { assetDirs: [modelDir], allowUnsafePaths: true }
  );
  assert.match(expanded, /body name="outside"/);
}));

await testAsync('MJCF ball joints and joint actuators are exported to payload', async () => {
  await withTempDir(async (dir) => {
    const xml = `<?xml version="1.0"?>
<mujoco model="ActuatedBall">
  <worldbody>
    <body name="base">
      <body name="tip">
        <joint name="ball" type="ball"/>
        <geom name="tip_geom" type="sphere" size="0.05"/>
      </body>
    </body>
  </worldbody>
  <actuator>
    <position name="ball_drive" joint="ball" kp="5" kv="0.25" forcerange="-3 4"/>
    <motor name="tendon_motor" tendon="tendon0"/>
  </actuator>
</mujoco>`;

    const { payload, stats } = await buildMujocoPayload(
      xml,
      { assetDirs: [dir], upAxis: 'Z' },
      dir
    );

    assert.equal(payload.joints.length, 1);
    assert.equal(payload.joints[0].type, 'spherical');
    assert.equal(payload.actuators.length, 1);
    assert.equal(payload.actuators[0].name, 'ball_drive');
    assert.equal(payload.actuators[0].joint, 'ball');
    assert.equal(payload.actuators[0].kp, 5);
    assert.equal(payload.actuators[0].kd, 0.25);
    assert.equal(payload.actuators[0].maxEffort, 4);
    assert.equal(stats.actuators, 1);
  });
});

await testAsync('MJCF multiple joints per body -> intermediate-link chain', async () => {
  await withTempDir(async (dir) => {
    const xml = `<?xml version="1.0"?>
<mujoco model="MultiDof">
  <worldbody>
    <body name="base">
      <body name="slider">
        <joint name="lift" type="slide" axis="0 0 1" range="0 0.5"/>
        <joint name="twist" type="hinge" axis="0 0 1" range="-90 90"/>
        <geom name="g" type="sphere" size="0.05"/>
      </body>
    </body>
  </worldbody>
</mujoco>`;
    const { payload } = await buildMujocoPayload(xml, { assetDirs: [dir], upAxis: 'Z' }, dir);
    // Two joints chained via one massless intermediate link.
    assert.equal(payload.joints.length, 2);
    const lift = payload.joints.find((j) => j.name === 'lift');
    const twist = payload.joints.find((j) => j.name === 'twist');
    assert.ok(lift && twist, 'both joints present');
    assert.equal(lift.type, 'prismatic');
    assert.equal(twist.type, 'revolute');
    assert.equal(lift.parent, 'base');
    assert.equal(lift.child, 'slider__mjcdof_1');
    assert.equal(twist.parent, 'slider__mjcdof_1');
    assert.equal(twist.child, 'slider');
    assert.ok(payload.links.some((l) => l.name === 'slider__mjcdof_1'), 'intermediate link emitted');
  });
});

await testAsync('MJCF tendon / equality / contact-exclude / fullinertia -> payload', async () => {
  await withTempDir(async (dir) => {
    const xml = `<?xml version="1.0"?>
<mujoco model="Constraints">
  <worldbody>
    <body name="base">
      <inertial pos="0 0 0" mass="2" fullinertia="4 4 6 1 0 0"/>
      <geom name="bg" type="sphere" size="0.1"/>
      <body name="l"><joint name="jl" type="hinge" axis="0 1 0"/><geom type="sphere" size="0.05"/></body>
      <body name="r"><joint name="jr" type="hinge" axis="0 1 0"/><geom type="sphere" size="0.05"/></body>
    </body>
  </worldbody>
  <tendon>
    <fixed name="couple" stiffness="120" damping="3" range="-1 1">
      <joint joint="jl" coef="1"/>
      <joint joint="jr" coef="-1"/>
    </fixed>
  </tendon>
  <equality>
    <joint name="mirror" joint1="jl" joint2="jr" polycoef="0 -1 0 0 0"/>
    <weld name="lockit" body1="l" body2="r" torquescale="0.5"/>
  </equality>
  <contact>
    <exclude body1="l" body2="r"/>
  </contact>
</mujoco>`;
    const { payload } = await buildMujocoPayload(xml, { assetDirs: [dir], upAxis: 'Z' }, dir);
    // Tendon
    assert.equal(payload.tendons.length, 1);
    assert.equal(payload.tendons[0].type, 'fixed');
    assert.equal(payload.tendons[0].joints.length, 2);
    assert.equal(payload.tendons[0].joints[0].coef, 1);
    assert.equal(payload.tendons[0].joints[1].coef, -1);
    assert.equal(payload.tendons[0].stiffness, 120);
    // Equality
    assert.equal(payload.equalities.length, 2);
    const mirror = payload.equalities.find((e) => e.name === 'mirror');
    const lockit = payload.equalities.find((e) => e.name === 'lockit');
    assert.equal(mirror.type, 'joint');
    assert.deepEqual(mirror.polycoef, [0, -1, 0, 0, 0]);
    assert.equal(lockit.type, 'weld');
    assert.equal(lockit.torquescale, 0.5);
    // Contact exclude -> filteredPairs
    assert.equal(payload.filteredPairs.length, 1);
    assert.equal(payload.filteredPairs[0].body1, 'l');
    // fullinertia carried as 6 components
    assert.deepEqual(payload.links[0].inertial.fullInertia, [4, 4, 6, 1, 0, 0]);
    // sourceFormat tag
    assert.equal(payload.sourceFormat, 'mjcf');
  });
});
