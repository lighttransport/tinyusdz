import { TinyUSDZLoader } from '../src/tinyusdz/TinyUSDZLoader.js';

async function testFile(filename) {
  console.log(`\n${'='.repeat(60)}`);
  console.log(`Testing: ${filename}`);
  console.log('='.repeat(60));

  const loader = new TinyUSDZLoader();
  await loader.init({ useMemory64: false });

  const usd = await new Promise((resolve, reject) => {
    loader.load(`../../tests/feat/skinning/${filename}`, resolve, null, reject);
  });

  const numAnims = usd.numAnimations();
  console.log(`Animations: ${numAnims}`);

  for (let i = 0; i < numAnims; i++) {
    const animInfo = usd.getAnimationInfo(i);
    console.log(`\nAnimation ${i}: ${animInfo.name}`);
    console.log(`  Duration: ${animInfo.duration}s`);
    console.log(`  Samplers: ${animInfo.numSamplers}`);
    console.log(`  Channels: ${animInfo.numTracks}`);

    const anim = usd.getAnimation(i);
    console.log(`  JS channels.length: ${anim.channels ? anim.channels.length : 0}`);
    console.log(`  JS samplers.length: ${anim.samplers ? anim.samplers.length : 0}`);

    if (anim.channels && anim.channels.length > 0) {
      console.log(`\n  First 3 channels:`);
      for (let j = 0; j < Math.min(3, anim.channels.length); j++) {
        const ch = anim.channels[j];
        console.log(`    [${j}] target_type:${ch.target_type}, skel:${ch.skeleton_id}, joint:${ch.joint_id}, path:${ch.path}`);
      }
    }

    if (anim.samplers && anim.samplers.length > 0) {
      console.log(`\n  First sampler:`);
      const samp = anim.samplers[0];
      console.log(`    times.length: ${samp.times ? samp.times.length : 0}`);
      console.log(`    values.length: ${samp.values ? samp.values.length : 0}`);
      console.log(`    interpolation: ${samp.interpolation}`);
      if (samp.times && samp.times.length > 0) {
        const timesArray = Array.from(samp.times);
        console.log(`    times: [${timesArray.join(', ')}]`);
        if (samp.values && samp.values.length > 0) {
          const valuesArray = Array.from(samp.values.slice(0, 3));
          console.log(`    first values: [${valuesArray.join(', ')}]`);
        }
      }
    }
  }
}

async function main() {
  const files = [
    'skelanim-complete-static.usda',
    'skelanim-complete-timesampled.usda',
    'skelanim-complete-mixed.usda'
  ];

  for (const file of files) {
    await testFile(file);
  }
}

main().catch(console.error);
