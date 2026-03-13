import { TinyUSDZLoader } from '../src/tinyusdz/TinyUSDZLoader.js';

async function main() {
  const loader = new TinyUSDZLoader();
  await loader.init({ useMemory64: false });

  const usd = await new Promise((resolve, reject) => {
    loader.load('../../models/skintest-blender.usda', resolve, null, reject);
  });

  console.log('\n=== Animation Debug Info ===');
  const numAnims = usd.numAnimations();
  console.log(`Total animations: ${numAnims}`);

  for (let i = 0; i < numAnims; i++) {
    const animInfo = usd.getAnimationInfo(i);
    console.log(`\nAnimation ${i}:`);
    console.log('  animInfo:', JSON.stringify(animInfo, null, 2));

    const anim = usd.getAnimation(i);
    console.log('  anim.channels:', anim.channels ? anim.channels.length : 0);
    console.log('  anim.samplers:', anim.samplers ? anim.samplers.length : 0);

    if (anim.channels && anim.channels.length > 0) {
      console.log('\n  First few channels:');
      for (let j = 0; j < Math.min(3, anim.channels.length); j++) {
        console.log(`    Channel ${j}:`, JSON.stringify(anim.channels[j], null, 2));
      }
    }

    if (anim.samplers && anim.samplers.length > 0) {
      console.log('\n  First sampler:');
      const sampler = anim.samplers[0];
      console.log(`    times.length: ${sampler.times ? sampler.times.length : 0}`);
      console.log(`    values.length: ${sampler.values ? sampler.values.length : 0}`);
      if (sampler.times && sampler.times.length > 0) {
        console.log(`    First time: ${sampler.times[0]}`);
        console.log(`    First values: [${sampler.values.slice(0, 3).join(', ')}]`);
      }
    }
  }
}

main().catch(console.error);
