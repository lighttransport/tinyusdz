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
