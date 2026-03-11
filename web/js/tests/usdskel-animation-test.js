import assert from 'node:assert/strict';
import * as THREE from 'three';
import {
  convertUSDSkeletalAnimationsToThreeJS,
  convertUSDNodeAnimationsToThreeJS
} from '../src/tinyusdz/USDAnimationConverter.js';
import {
  extractUSDSceneAnimations,
  createUSDSceneAnimationPlayback,
  computeUSDSceneTimelineDuration
} from '../src/tinyusdz/USDSceneAnimationPipeline.js';
import { createThreeSkeletonFromUSD } from '../src/tinyusdz/USDSkeletalHelper.js';

function near(a, b, eps = 1e-4) {
  return Math.abs(a - b) <= eps;
}

function assertNear(actual, expected, msg, eps = 1e-4) {
  assert.ok(
    near(actual, expected, eps),
    `${msg}: expected ${expected}, got ${actual}`
  );
}

function makeTRSMatrix({
  tx = 0,
  ty = 0,
  tz = 0,
  rx = 0,
  ry = 0,
  rz = 0,
  sx = 1,
  sy = 1,
  sz = 1
} = {}) {
  const position = new THREE.Vector3(tx, ty, tz);
  const rotation = new THREE.Quaternion().setFromEuler(
    new THREE.Euler(rx, ry, rz, 'XYZ')
  );
  const scale = new THREE.Vector3(sx, sy, sz);
  return new THREE.Matrix4().compose(position, rotation, scale);
}

function assertVecNear(actual, expected, msg, eps = 1e-4) {
  assertNear(actual.x, expected.x, `${msg}.x`, eps);
  assertNear(actual.y, expected.y, `${msg}.y`, eps);
  assertNear(actual.z, expected.z, `${msg}.z`, eps);
}

function createMockUSDScene(animations, infos = null) {
  const animationInfos = infos || animations.map((anim) => ({
    name: anim.name,
    has_skeletal_animation: !!(anim.channels || []).find((c) => c.target_type === 'SkeletonJoint')
  }));

  return {
    numAnimations() {
      return animations.length;
    },
    getAnimation(i) {
      return animations[i];
    },
    getAllAnimationInfos() {
      return animationInfos;
    }
  };
}

function findTrack(clip, suffix) {
  return clip.tracks.find((track) => track.name.endsWith(suffix));
}

async function runTest(name, fn) {
  try {
    await fn();
    console.log(`PASS ${name}`);
    return true;
  } catch (err) {
    console.error(`FAIL ${name}`);
    console.error(err && err.stack ? err.stack : err);
    return false;
  }
}

async function testSkeletalConversionAndEvaluation() {
  const bone = new THREE.Bone();
  bone.name = 'skel0_root';
  const root = new THREE.Group();
  root.add(bone);

  const boneMap = new Map([[0, bone]]);
  const boneMaps = new Map([[0, boneMap]]);

  const usdScene = createMockUSDScene([
    {
      name: 'Walk',
      duration: 63,
      channels: [
        {
          target_type: 'SkeletonJoint',
          skeleton_id: 0,
          joint_id: 0,
          path: 'Translation',
          sampler: 0
        }
      ],
      samplers: [
        {
          times: new Float32Array([0]),
          values: new Float32Array([1, 2, 3]),
          interpolation: 'Linear'
        }
      ]
    }
  ]);

  const clips = convertUSDSkeletalAnimationsToThreeJS(usdScene, boneMaps, 24);
  assert.equal(clips.length, 1, 'should convert one skeletal clip');

  const clip = clips[0];
  const posTrack = findTrack(clip, '.position');
  assert.ok(posTrack, 'position track should exist');
  assert.equal(posTrack.times.length, 2, 'single-keyframe track should be expanded');
  assertNear(posTrack.times[0], 0, 'track start should be 0');
  assertNear(posTrack.times[1], 63, 'track end should match animation duration');

  const mixer = new THREE.AnimationMixer(root);
  const action = mixer.clipAction(clip);
  action.play();
  mixer.setTime(40);

  assertNear(bone.position.x, 1, 'bone.x at t=40');
  assertNear(bone.position.y, 2, 'bone.y at t=40');
  assertNear(bone.position.z, 3, 'bone.z at t=40');
}

async function testNodeXformConversionAndEvaluation() {
  const root = new THREE.Group();
  root.name = 'RootNode';
  const node = new THREE.Object3D();
  node.name = 'NodeA';
  root.add(node);

  const nodeIndexMap = new Map([
    [0, root],
    [1, node]
  ]);

  const usdScene = createMockUSDScene([
    {
      name: 'NodeMove',
      duration: 100,
      channels: [
        {
          target_type: 'SceneNode',
          target_node: 1,
          path: 'Translation',
          sampler: 0
        }
      ],
      samplers: [
        {
          times: new Float32Array([0, 100]),
          values: new Float32Array([0, 0, 0, 10, 0, 0]),
          interpolation: 'Linear'
        }
      ]
    }
  ]);

  const clips = convertUSDNodeAnimationsToThreeJS(usdScene, nodeIndexMap);
  assert.equal(clips.length, 1, 'should convert one node clip');
  const clip = clips[0];

  assert.ok(
    clip.tracks.some((track) => track.name === `${node.uuid}.position`),
    'track should target node UUID'
  );

  const mixer = new THREE.AnimationMixer(root);
  const action = mixer.clipAction(clip);
  action.play();
  mixer.setTime(50);

  assertNear(node.position.x, 5, 'node translation at t=50');
  assertNear(node.position.y, 0, 'node y at t=50');
}

async function testExtractPipelineMixedAnimation() {
  const bone = new THREE.Bone();
  bone.name = 'skel0_root';
  const boneMap = new Map([[0, bone]]);
  const boneMaps = new Map([[0, boneMap]]);

  const root = new THREE.Group();
  root.name = 'Root';
  const node = new THREE.Object3D();
  node.name = 'XformNode';
  root.add(node);
  const nodeIndexMap = new Map([
    [0, root],
    [1, node]
  ]);

  const usdScene = createMockUSDScene([
    {
      name: 'Mixed',
      duration: 100,
      channels: [
        {
          target_type: 'SkeletonJoint',
          skeleton_id: 0,
          joint_id: 0,
          path: 'Translation',
          sampler: 0
        },
        {
          target_type: 'SceneNode',
          target_node: 1,
          path: 'Translation',
          sampler: 1
        }
      ],
      samplers: [
        {
          times: new Float32Array([0, 100]),
          values: new Float32Array([0, 0, 0, 1, 0, 0]),
          interpolation: 'Linear'
        },
        {
          times: new Float32Array([0, 100]),
          values: new Float32Array([0, 0, 0, 0, 1, 0]),
          interpolation: 'Linear'
        }
      ]
    }
  ]);

  const extracted = extractUSDSceneAnimations(usdScene, {
    boneMaps,
    nodeIndexMap,
    timeCodesPerSecond: 24,
    logger: { log() {} }
  });

  assert.equal(extracted.hasAnyAnimation, true, 'should detect animations');
  assert.equal(extracted.usdAnimations.length, 1, 'should have one skeletal clip');
  assert.equal(extracted.usdNodeAnimations.length, 1, 'should have one node clip');

  const maxDuration = computeUSDSceneTimelineDuration(
    100,
    extracted.usdAnimations,
    extracted.usdNodeAnimations
  );
  assertNear(maxDuration, 100, 'timeline duration should match max of stage/clips');
}

async function testPlayAllGlobalTimelineClampsPerClip() {
  const root = new THREE.Group();

  const a = new THREE.Object3D();
  a.name = 'jointA';
  const b = new THREE.Object3D();
  b.name = 'jointB';
  root.add(a);
  root.add(b);

  const clipA = new THREE.AnimationClip('A', -1, [
    new THREE.VectorKeyframeTrack(
      'jointA.position',
      new Float32Array([0, 63]),
      new Float32Array([0, 0, 0, 63, 0, 0])
    )
  ]);

  const clipB = new THREE.AnimationClip('B', -1, [
    new THREE.VectorKeyframeTrack(
      'jointB.position',
      new Float32Array([0, 100]),
      new Float32Array([0, 0, 0, 100, 0, 0])
    )
  ]);

  const playback = createUSDSceneAnimationPlayback(root, {
    usdAnimations: [clipA, clipB],
    usdNodeAnimations: [],
    speed: 24,
    logger: { log() {} }
  });

  playback.playAllAnimations([true, true]);
  playback.setTime(80, true);

  const state80 = playback.getState();
  const actionA80 = state80.animationActions.find((action) => action._clip?.name === 'A');
  const actionB80 = state80.animationActions.find((action) => action._clip?.name === 'B');

  assert.ok(actionA80, 'action A should exist');
  assert.ok(actionB80, 'action B should exist');
  assertNear(actionA80.time, 63, 'clip A should clamp to its end in play-all global mode');
  assertNear(actionB80.time, 80, 'clip B should evaluate at global time in play-all mode');

  assertNear(a.position.x, 63, 'jointA position should remain at clip end');
  assertNear(b.position.x, 80, 'jointB position should continue with global timeline');

  playback.setTime(120, true);
  const state120 = playback.getState();
  const actionA120 = state120.animationActions.find((action) => action._clip?.name === 'A');
  const actionB120 = state120.animationActions.find((action) => action._clip?.name === 'B');
  assertNear(actionA120.time, 63, 'clip A stays clamped at 63 for later global times');
  assertNear(actionB120.time, 100, 'clip B clamps at 100');
}

async function testSkinEvalBindPoseIdentityFromDoc() {
  const A = makeTRSMatrix({ tx: 5, ty: -2, tz: 1, rz: Math.PI * 0.2 });
  const Mmesh = makeTRSMatrix({ tx: -1, ty: 3, tz: 0, ry: Math.PI * 0.15 });
  const Mskel = makeTRSMatrix({ tx: 2, ty: 0.5, tz: -4, rx: Math.PI * -0.1 });
  const Lbind = makeTRSMatrix({ tx: 0.5, ty: 1.0, tz: 0.0, rz: Math.PI * 0.1 });

  const WmeshBind = A.clone().multiply(Mmesh);
  const WboneBind = A.clone().multiply(Mskel).multiply(Lbind);
  const WboneNow = WboneBind.clone();

  const pLocal = new THREE.Vector3(0.3, -0.2, 1.4);
  const pDocThree = pLocal
    .clone()
    .applyMatrix4(WmeshBind)
    .applyMatrix4(WboneBind.clone().invert())
    .applyMatrix4(WboneNow);
  const expected = pLocal.clone().applyMatrix4(WmeshBind);

  assertVecNear(
    pDocThree,
    expected,
    'bind pose should keep mesh world placement (doc equation)'
  );
}

async function testSkinEvalExpandedEquationEquivalenceFromDoc() {
  const A = makeTRSMatrix({ tx: 7, ty: 0.5, tz: -3, rz: Math.PI * 0.25 });
  const Mmesh = makeTRSMatrix({ tx: -2, ty: 1, tz: 2, ry: Math.PI * -0.2 });
  const Mskel = makeTRSMatrix({ tx: 1, ty: -1, tz: 0.5, rx: Math.PI * 0.3 });

  const Lbind0 = makeTRSMatrix({ tx: 0.0, ty: 0.0, tz: 0.0, rz: 0.0 });
  const Lbind1 = makeTRSMatrix({ tx: 0.0, ty: 1.5, tz: 0.0, rz: Math.PI * 0.15 });
  const Lnow0 = makeTRSMatrix({ tx: 0.5, ty: 0.2, tz: 0.0, rz: Math.PI * 0.1 });
  const Lnow1 = makeTRSMatrix({ tx: -0.2, ty: 2.0, tz: 0.3, rz: Math.PI * 0.35 });
  const weights = [0.35, 0.65];

  const WmeshBind = A.clone().multiply(Mmesh);
  const WboneBind0 = A.clone().multiply(Mskel).multiply(Lbind0);
  const WboneBind1 = A.clone().multiply(Mskel).multiply(Lbind1);
  const WboneNow0 = A.clone().multiply(Mskel).multiply(Lnow0);
  const WboneNow1 = A.clone().multiply(Mskel).multiply(Lnow1);

  const pLocal = new THREE.Vector3(0.8, -0.4, 0.3);

  const pThreeFormula = new THREE.Vector3(0, 0, 0)
    .add(
      pLocal
        .clone()
        .applyMatrix4(WmeshBind)
        .applyMatrix4(WboneBind0.clone().invert())
        .applyMatrix4(WboneNow0)
        .multiplyScalar(weights[0])
    )
    .add(
      pLocal
        .clone()
        .applyMatrix4(WmeshBind)
        .applyMatrix4(WboneBind1.clone().invert())
        .applyMatrix4(WboneNow1)
        .multiplyScalar(weights[1])
    );

  const pExpandedDoc = new THREE.Vector3(0, 0, 0)
    .add(
      pLocal
        .clone()
        .applyMatrix4(Mmesh)
        .applyMatrix4(Mskel.clone().invert())
        .applyMatrix4(Lbind0.clone().invert())
        .applyMatrix4(Lnow0)
        .applyMatrix4(Mskel)
        .applyMatrix4(A)
        .multiplyScalar(weights[0])
    )
    .add(
      pLocal
        .clone()
        .applyMatrix4(Mmesh)
        .applyMatrix4(Mskel.clone().invert())
        .applyMatrix4(Lbind1.clone().invert())
        .applyMatrix4(Lnow1)
        .applyMatrix4(Mskel)
        .applyMatrix4(A)
        .multiplyScalar(weights[1])
    );

  assertVecNear(
    pThreeFormula,
    pExpandedDoc,
    'expanded doc formula should match world-space Three.js formulation'
  );
}

async function testSkinnedMeshRuntimeMatchesDocMatrixEvaluation() {
  const sceneRoot = new THREE.Group();
  sceneRoot.position.set(4, -1, 2);
  sceneRoot.rotation.set(0, Math.PI * 0.1, Math.PI * -0.05);

  const skeletonBranch = new THREE.Group();
  skeletonBranch.position.set(1.5, 0.5, -2);
  skeletonBranch.rotation.set(Math.PI * 0.2, 0, 0);
  sceneRoot.add(skeletonBranch);

  const meshBranch = new THREE.Group();
  meshBranch.position.set(-0.5, 2.0, 1.0);
  meshBranch.rotation.set(0, Math.PI * -0.15, 0);
  sceneRoot.add(meshBranch);

  const geom = new THREE.BufferGeometry();
  geom.setAttribute('position', new THREE.Float32BufferAttribute([0.3, 0.2, 0.7], 3));
  geom.setAttribute('skinIndex', new THREE.Uint16BufferAttribute([0, 0, 0, 0], 4));
  geom.setAttribute('skinWeight', new THREE.Float32BufferAttribute([1, 0, 0, 0], 4));

  const mesh = new THREE.SkinnedMesh(geom, new THREE.MeshBasicMaterial());
  meshBranch.add(mesh);

  const bone = new THREE.Bone();
  bone.position.set(0.2, 1.2, -0.1);
  skeletonBranch.add(bone);

  const skeleton = new THREE.Skeleton([bone]);
  sceneRoot.updateMatrixWorld(true);
  mesh.bind(skeleton);
  sceneRoot.updateMatrixWorld(true);

  const WmeshBind = mesh.bindMatrix.clone();
  const WboneBind = skeleton.boneInverses[0].clone().invert();

  bone.position.set(1.0, 1.7, 0.3);
  bone.rotation.set(0, 0, Math.PI * 0.2);
  sceneRoot.updateMatrixWorld(true);
  skeleton.update();

  const WboneNow = bone.matrixWorld.clone();
  const localVertex = new THREE.Vector3(0.3, 0.2, 0.7);

  const runtimeLocal = localVertex.clone();
  mesh.applyBoneTransform(0, runtimeLocal);
  const runtimeWorld = runtimeLocal.clone().applyMatrix4(mesh.matrixWorld);

  const expectedWorld = localVertex
    .clone()
    .applyMatrix4(WmeshBind)
    .applyMatrix4(WboneBind.clone().invert())
    .applyMatrix4(WboneNow);

  assertVecNear(
    runtimeWorld,
    expectedWorld,
    'SkinnedMesh runtime result should match doc matrix equation'
  );
}

async function testCreateThreeSkeletonFromUSDBindMatrixReconstruction() {
  const rootBind = makeTRSMatrix({ tx: 2.0, ty: 1.0, tz: -1.5, rz: Math.PI * 0.2 });
  const childLocal = makeTRSMatrix({ tx: 0.5, ty: 1.2, tz: 0.0, rx: Math.PI * -0.1 });
  const childBind = rootBind.clone().multiply(childLocal);

  const usdSkeleton = {
    root_node: {
      joint_id: 0,
      joint_name: 'Root',
      joint_path: '/Model/Skeleton/Root',
      bind_transform: rootBind.elements,
      rest_transform: makeTRSMatrix({ tx: 100, ty: 0, tz: 0 }).elements,
      children: [
        {
          joint_id: 1,
          joint_name: 'Root/Child',
          joint_path: '/Model/Skeleton/Root/Child',
          bind_transform: childBind.elements,
          rest_transform: makeTRSMatrix({ tx: -50, ty: 0, tz: 0 }).elements,
          children: []
        }
      ]
    }
  };

  const skeletonData = createThreeSkeletonFromUSD(usdSkeleton, {
    skelId: 3,
    useBindTransforms: true
  });

  assert.equal(skeletonData.bones.length, 2, 'two bones should be created');
  assert.equal(
    skeletonData.rootBone.name,
    'skel3_Root',
    'root name should include skeleton prefix'
  );

  const container = new THREE.Group();
  container.add(skeletonData.rootBone);
  container.updateMatrixWorld(true);

  const rootBone = skeletonData.boneMap.get(0);
  const childBone = skeletonData.boneMap.get(1);
  assert.ok(rootBone && childBone, 'joint IDs should map to bones');

  assertVecNear(
    new THREE.Vector3().setFromMatrixPosition(rootBone.matrixWorld),
    new THREE.Vector3().setFromMatrixPosition(rootBind),
    'root bone world transform should match USD bind transform'
  );
  assertVecNear(
    new THREE.Vector3().setFromMatrixPosition(childBone.matrixWorld),
    new THREE.Vector3().setFromMatrixPosition(childBind),
    'child bone world transform should match USD bind transform'
  );

  const childExpectedInverse = childBind.clone().invert();
  const childInverse = skeletonData.boneInverses[1];
  for (let i = 0; i < 16; i++) {
    assertNear(
      childInverse.elements[i],
      childExpectedInverse.elements[i],
      `child inverse bind element[${i}]`
    );
  }
}

async function main() {
  const tests = [
    ['usdSkel conversion + evaluation', testSkeletalConversionAndEvaluation],
    ['node xform conversion + evaluation', testNodeXformConversionAndEvaluation],
    ['extract mixed skeletal/node animation', testExtractPipelineMixedAnimation],
    ['play-all global timeline clip clamping', testPlayAllGlobalTimelineClampsPerClip],
    ['skin-eval bind pose identity (doc)', testSkinEvalBindPoseIdentityFromDoc],
    ['skin-eval expanded equation equivalence (doc)', testSkinEvalExpandedEquationEquivalenceFromDoc],
    ['skinned mesh runtime matrix evaluation (doc)', testSkinnedMeshRuntimeMatchesDocMatrixEvaluation],
    ['createThreeSkeleton bind matrix reconstruction', testCreateThreeSkeletonFromUSDBindMatrixReconstruction]
  ];

  let passed = 0;
  for (const [name, fn] of tests) {
    if (await runTest(name, fn)) {
      passed++;
    }
  }

  const failed = tests.length - passed;
  console.log(`\nSummary: ${passed}/${tests.length} passed, ${failed} failed`);
  if (failed > 0) {
    process.exitCode = 1;
  }
}

main().catch((err) => {
  console.error(err && err.stack ? err.stack : err);
  process.exitCode = 1;
});
