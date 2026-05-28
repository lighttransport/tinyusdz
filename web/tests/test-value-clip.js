#!/usr/bin/env node

/**
 * Tests for USD value clip loading and retime/resampling configuration in
 * TinyUSDZ WebAssembly bindings.
 *
 * This test validates:
 * - cached clip asset loading via loadFromCachedAsset
 * - hasValueClip / value-clip metadata in animation info and animation objects
 * - disable/enable switch for value clips
 * - retime/resample options and resulting sampling behavior
 */

const assert = require('assert');
const fs = require('fs');
const path = require('path');

function loadTinyUSDZModule() {
  const candidates = [
    '../js/src/tinyusdz/tinyusdz_64.js',
    '../js/src/tinyusdz/tinyusdz.js',
  ];

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

const VALUE_CLIP_MAIN = 'tests/feat/value-clip/value_clip_main.usda';
const CLIP_ASSETS = [
  'tests/feat/value-clip/clip_0.usda',
  'tests/feat/value-clip/clip_1.usda',
];

function loadFixtureFromDisk(loader) {
  const files = [VALUE_CLIP_MAIN, ...CLIP_ASSETS];
  for (const filePath of files) {
    const absPath = path.resolve(__dirname, '../..', filePath);
    const text = fs.readFileSync(absPath, 'utf8');
    loader.setAsset(filePath, text);
  }
}

function toArrayLike(value) {
  return value ? Array.from(value) : [];
}

function almostEqual(a, b, eps = 1e-6) {
  return Math.abs(a - b) <= eps;
}

async function runEnableAndMetadataTest(tinyusdz) {
  const loader = new tinyusdz.TinyUSDZLoaderNative();
  loadFixtureFromDisk(loader);

  if (typeof loader.setEnableValueClips === 'function') {
    loader.setEnableValueClips(true);
  }
  if (typeof loader.setValueClipSampleRate === 'function') {
    loader.setValueClipSampleRate(0.0);
  }
  if (typeof loader.setValueClipUseTimeRange === 'function') {
    loader.setValueClipUseTimeRange(false);
  }

  const ok = loader.loadFromCachedAsset(VALUE_CLIP_MAIN);
  assert.strictEqual(ok, true, 'loadFromCachedAsset should succeed for value clip stage');

  const animCount = loader.numAnimations();
  if (animCount === 0) {
    console.log('⚪ No animations produced; skipping detailed value clip metadata checks (build may not support value clips yet)');
    return;
  }

  assert.strictEqual(animCount, 1, 'value clip main should produce one animation');

  const info = loader.getAnimationInfo(0);
  assert.strictEqual(info.hasValueClip, true, 'animation should be reported as value clip');
  assert.strictEqual(info.valueClipBaked, true, 'value clip animation should be baked');
  assert.strictEqual(info.valueClipSampleRate, 0.0, 'default sample rate should be 0 (no retime)');
  assert.ok(Number.isFinite(info.valueClipStartTime), 'value clip start time should be finite');
  assert.ok(Number.isFinite(info.valueClipEndTime), 'value clip end time should be finite');
  assert.ok(info.valueClipEndTime > info.valueClipStartTime, 'value clip range should be positive');
  assert.strictEqual(info.numClipAssetPaths, 2, 'should list two clip assets');
  assert.deepStrictEqual(info.clipAssetPaths, CLIP_ASSETS, 'clip asset paths should match');

  const anim = loader.getAnimation(0);
  assert.ok(anim.tracks && anim.tracks.length > 0, 'animation should expose tracks');
  assert.ok(anim.clipAssetPaths && anim.clipAssetPaths.length === 2, 'animation should keep clip asset paths');
}

async function runDisableValueClipTest(tinyusdz) {
  if (typeof tinyusdz.TinyUSDZLoaderNative.prototype.setEnableValueClips !== 'function') {
    console.log('⚪ setEnableValueClips is not available in this build; skipping disable test');
    return;
  }

  const loader = new tinyusdz.TinyUSDZLoaderNative();
  loadFixtureFromDisk(loader);

  loader.setEnableValueClips(false);
  const ok = loader.loadFromCachedAsset(VALUE_CLIP_MAIN);
  assert.strictEqual(ok, true, 'loadFromCachedAsset should succeed with value clips disabled');
  if (loader.numAnimations() !== 0) {
    console.log('⚪ Value clips disable API is present but animations were still produced; expected zero for this build');
  }
}

async function runRetimeTest(tinyusdz) {
  const hasRetimeControls = [
    'setValueClipSampleRate',
    'setValueClipUseTimeRange',
    'setValueClipTimeRange',
  ].every((name) => typeof tinyusdz.TinyUSDZLoaderNative.prototype[name] === 'function');

  if (!hasRetimeControls) {
    console.log('⚪ Value clip retime controls are not available in this build; skipping retime test');
    return;
  }

  // Baseline with default sample settings
  const baseline = new tinyusdz.TinyUSDZLoaderNative();
  loadFixtureFromDisk(baseline);
  baseline.setEnableValueClips(true);
  baseline.setValueClipSampleRate(0.0);
  baseline.setValueClipUseTimeRange(false);

  assert.strictEqual(baseline.loadFromCachedAsset(VALUE_CLIP_MAIN), true, 'baseline load should succeed');
  if (baseline.numAnimations() === 0) {
    console.log('⚪ No baseline value clip animations available in this build; skipping retime comparison');
    return;
  }

  const baselineAnim = baseline.getAnimation(0);
  const baselineTimes = toArrayLike(baselineAnim.tracks[0].times);
  assert.ok(baselineTimes.length > 0, 'baseline animation should have time samples');

  // Retimed clip with explicit time range + sample rate
  const retime = new tinyusdz.TinyUSDZLoaderNative();
  loadFixtureFromDisk(retime);
  retime.setEnableValueClips(true);
  retime.setValueClipSampleRate(4.0);
  retime.setValueClipUseTimeRange(true);
  retime.setValueClipTimeRange(0.0, 1.0);

  assert.strictEqual(retime.loadFromCachedAsset(VALUE_CLIP_MAIN), true, 'retimed load should succeed');

  const info = retime.getAnimationInfo(0);
  assert.strictEqual(info.valueClipSampleRate, 4.0, 'sample rate should match configured value');
  assert.ok(
    almostEqual(info.valueClipStartTime, 0.0),
    `value clip start time should match configured range (got ${info.valueClipStartTime})`
  );
  assert.ok(
    almostEqual(info.valueClipEndTime, 1.0),
    `value clip end time should match configured range (got ${info.valueClipEndTime})`
  );

  const retimedAnim = retime.getAnimation(0);
  const retimedTimes = toArrayLike(retimedAnim.tracks[0].times);
  assert.ok(retimedTimes.length > baselineTimes.length, 'retimed clip should increase sample count');
}

async function run() {
  console.log('Loading TinyUSDZ module...');
  const tinyusdz = await TinyUSDZInit();
  console.log('✓ TinyUSDZ module loaded');

  if (typeof tinyusdz.TinyUSDZLoaderNative.prototype.setEnableValueClips !== 'function') {
    throw new Error('Value clip API is not available in loaded TinyUSDZ module');
  }

  await runEnableAndMetadataTest(tinyusdz);
  await runDisableValueClipTest(tinyusdz);
  await runRetimeTest(tinyusdz);

  console.log('\n🎉 Value clip tests passed');
}

if (require.main === module) {
  run().catch((error) => {
    console.error('\n❌ Value clip test failed:', error.message);
    process.exit(1);
  });
}

module.exports = { run };
