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

await testAsync('MJCF <sensor> -> sensors payload', async () => {
  await withTempDir(async (dir) => {
    const xml = `<?xml version="1.0"?>
<mujoco model="Sens">
  <worldbody>
    <body name="b"><joint name="j" type="hinge"/><site name="imu"/><geom type="sphere" size="0.1"/></body>
  </worldbody>
  <sensor>
    <gyro name="g" site="imu" cutoff="34.9" noise="0.01"/>
    <jointpos name="jp" joint="j"/>
    <framepos name="fp" objtype="body" objname="b" reftype="body" refname="world"/>
  </sensor>
</mujoco>`;
    const { payload } = await buildMujocoPayload(xml, { assetDirs: [dir], upAxis: 'Z' }, dir);
    assert.equal(payload.sensors.length, 3);
    const g = payload.sensors.find((s) => s.name === 'g');
    assert.equal(g.type, 'gyro');
    assert.equal(g.objtype, 'site');
    assert.equal(g.objname, 'imu');
    assert.equal(g.cutoff, 34.9);
    const jp = payload.sensors.find((s) => s.name === 'jp');
    assert.equal(jp.type, 'jointpos');
    assert.equal(jp.objtype, 'joint');
    const fp = payload.sensors.find((s) => s.name === 'fp');
    assert.equal(fp.type, 'framepos');
    assert.equal(fp.reftype, 'body');
    assert.equal(fp.refname, 'world');
  });
});

await testAsync('MJCF <asset><material> -> materials payload + geom ref', async () => {
  await withTempDir(async (dir) => {
    const xml = `<?xml version="1.0"?>
<mujoco model="Mat">
  <asset>
    <material name="red" rgba="1 0 0 1" metallic="0.2" roughness="0.8"/>
    <material name="blue" rgba="0 0 1 1"/>
  </asset>
  <worldbody>
    <body name="b">
      <geom type="box" size="0.1 0.1 0.1" material="red"/>
    </body>
  </worldbody>
</mujoco>`;
    const { payload } = await buildMujocoPayload(xml, { assetDirs: [dir], upAxis: 'Z' }, dir);
    assert.equal(payload.materials.length, 2);
    const red = payload.materials.find((m) => m.name === 'red');
    assert.deepEqual(red.rgba, [1, 0, 0, 1]);
    assert.equal(red.metallic, 0.2);
    assert.equal(red.roughness, 0.8);
    // The box visual references the material.
    const vis = payload.links[0].visuals.find((v) => v.material === 'red');
    assert.ok(vis, 'visual geom references material red');
  });
});

await testAsync('MJCF <light>/<camera> -> lights/cameras payload', async () => {
  await withTempDir(async (dir) => {
    const xml = `<?xml version="1.0"?>
<mujoco model="Lit">
  <worldbody>
    <light name="sun" directional="true" pos="0 0 3" dir="0 0 -1" diffuse="0.8 0.8 0.7"/>
    <camera name="cam" pos="0 -2 1" fovy="60"/>
    <body name="b">
      <light name="bulb" pos="0 0 1" diffuse="1 1 1"/>
      <geom type="sphere" size="0.1"/>
    </body>
  </worldbody>
</mujoco>`;
    const { payload } = await buildMujocoPayload(xml, { assetDirs: [dir], upAxis: 'Z' }, dir);
    assert.equal(payload.lights.length, 2);
    const sun = payload.lights.find((l) => l.name === 'sun');
    const bulb = payload.lights.find((l) => l.name === 'bulb');
    assert.equal(sun.type, 'directional');
    assert.deepEqual(sun.color, [0.8, 0.8, 0.7]);
    assert.equal(sun.matrix.length, 16);
    assert.equal(bulb.type, 'spot');  // non-directional default
    assert.equal(payload.cameras.length, 1);
    assert.equal(payload.cameras[0].name, 'cam');
    assert.equal(payload.cameras[0].fovy, 60);
    assert.equal(payload.cameras[0].matrix.length, 16);
  });
});

await testAsync('MJCF <keyframe> -> keyframes payload', async () => {
  await withTempDir(async (dir) => {
    const xml = `<?xml version="1.0"?>
<mujoco model="Key">
  <worldbody><body name="b"><joint name="j" type="hinge"/><geom type="sphere" size="0.1"/></body></worldbody>
  <keyframe>
    <key name="init" qpos="0.5" ctrl="0.1"/>
    <key name="up" qpos="1.57"/>
  </keyframe>
</mujoco>`;
    const { payload } = await buildMujocoPayload(xml, { assetDirs: [dir], upAxis: 'Z' }, dir);
    assert.equal(payload.keyframes.length, 2);
    assert.equal(payload.keyframes[0].name, 'init');
    assert.deepEqual(payload.keyframes[0].qpos, [0.5]);
    assert.deepEqual(payload.keyframes[0].ctrl, [0.1]);
    assert.equal(payload.keyframes[1].name, 'up');
    assert.deepEqual(payload.keyframes[1].qpos, [1.57]);
  });
});

await testAsync('MJCF <option>/<flag>/<compiler> -> mjcScene payload', async () => {
  await withTempDir(async (dir) => {
    const xml = `<?xml version="1.0"?>
<mujoco model="Opt">
  <compiler angle="radian" autolimits="true" boundmass="0.0001"/>
  <option timestep="0.001" integrator="implicitfast" solver="newton" cone="elliptic" iterations="50" ls_iterations="8">
    <flag eulerdamp="disable" contact="enable"/>
  </option>
  <worldbody><body name="b"><geom type="sphere" size="0.1"/></body></worldbody>
</mujoco>`;
    const { payload } = await buildMujocoPayload(xml, { assetDirs: [dir], upAxis: 'Z' }, dir);
    assert.ok(payload.mjcScene, 'mjcScene present');
    assert.equal(payload.mjcScene.option.integrator, 'implicitfast');
    assert.equal(payload.mjcScene.option.solver, 'newton');
    assert.equal(payload.mjcScene.option.cone, 'elliptic');
    assert.equal(payload.mjcScene.option.iterations, 50);
    assert.equal(payload.mjcScene.option.ls_iterations, 8);
    assert.equal(payload.mjcScene.flag.eulerdamp, false);  // disable -> false
    assert.equal(payload.mjcScene.flag.contact, true);     // enable -> true
    assert.equal(payload.mjcScene.compiler.angle, 'radian');
    assert.equal(payload.mjcScene.compiler.autolimits, true);
    assert.equal(payload.mjcScene.compiler.boundmass, 0.0001);
  });
});

await testAsync('MJCF spatial (muscle) tendon + sites + muscle actuator -> payload', async () => {
  await withTempDir(async (dir) => {
    const xml = `<?xml version="1.0"?>
<mujoco model="Muscle">
  <default><tendon rgba="0.95 0.3 0.3 1" width="0.005"/></default>
  <worldbody>
    <body name="pelvis">
      <site name="m_p1" pos="0 0 0.1"/>
      <geom type="sphere" size="0.05"/>
      <body name="femur" pos="0 0 -0.3">
        <joint name="hip" type="hinge" axis="0 1 0"/>
        <site name="m_p2" pos="0.05 0 -0.2"/>
        <geom type="capsule" fromto="0 0 0 0 0 -0.4" size="0.04"/>
      </body>
    </body>
  </worldbody>
  <tendon>
    <spatial name="glute_tendon" stiffness="10">
      <site site="m_p1"/>
      <site site="m_p2"/>
    </spatial>
  </tendon>
  <actuator>
    <general name="glute" class="muscle" tendon="glute_tendon"
             lengthrange="0.18 0.29" gainprm="0.75 1.05 916.8" biasprm="0.75 1.05 916.8" ctrlrange="0 1"/>
  </actuator>
</mujoco>`;
    const { payload } = await buildMujocoPayload(xml, { assetDirs: [dir], upAxis: 'Z' }, dir);
    // Sites (with baked world matrices)
    assert.equal(payload.sites.length, 2);
    assert.ok(payload.sites.find((s) => s.name === 'm_p1'));
    assert.ok(payload.sites.find((s) => s.name === 'm_p2'));
    assert.equal(payload.sites[0].matrix.length, 16);
    // Spatial tendon routed through the two sites
    assert.equal(payload.tendons.length, 1);
    assert.equal(payload.tendons[0].type, 'spatial');
    assert.equal(payload.tendons[0].path.length, 2);
    assert.equal(payload.tendons[0].path[0].site, 'm_p1');
    // Muscle actuator targeting the tendon
    assert.equal(payload.mjcActuators.length, 1);
    assert.equal(payload.mjcActuators[0].targetTendon, 'glute_tendon');
    assert.deepEqual(payload.mjcActuators[0].gainPrm, [0.75, 1.05, 916.8]);
    assert.deepEqual(payload.mjcActuators[0].lengthRange, [0.18, 0.29]);
  });
});
