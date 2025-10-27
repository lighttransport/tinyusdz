// Node.js script to load USD files and print animation information
// Usage: node animation-info.js <path-to-usd-file> [options]
// Example: node animation-info.js ../../models/suzanne-subd-lv4.usdc
// Example: node animation-info.js ../../models/animation.usd --detailed

import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import fs from 'node:fs';

// Format bytes to human readable format
function formatBytes(bytes) {
  if (bytes === 0) return '0 Bytes';
  const k = 1024;
  const sizes = ['Bytes', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return Math.round(bytes / Math.pow(k, i) * 100) / 100 + ' ' + sizes[i];
}

// Report memory usage
function reportMemUsage() {
  const used = process.memoryUsage();
  console.log('\n=== Memory Usage ===');
  for (const key in used) {
    console.log(`${key}: ${(used[key] / 1024 / 1024).toFixed(2)} MB`);
  }
}

// Print animation clip info
function printAnimationClips(usd, detailed = false) {
  const numAnims = usd.numAnimations();

  if (numAnims === 0) {
    console.log('No animation clips found in this USD file.');
    return;
  }

  console.log(`\n=== Animation Information ===`);
  console.log(`Total animation clips: ${numAnims}\n`);

  for (let i = 0; i < numAnims; i++) {
    try {
      const animInfo = usd.getAnimationInfo(i);

      console.log(`--- Animation Clip ${i} ---`);

      if (animInfo) {
        if (animInfo.name) console.log(`  Name: ${animInfo.name}`);
        if (animInfo.prim_name) console.log(`  Prim Name: ${animInfo.prim_name}`);
        if (animInfo.abs_path) console.log(`  Absolute Path: ${animInfo.abs_path}`);
        if (animInfo.display_name) console.log(`  Display Name: ${animInfo.display_name}`);
        if (animInfo.duration !== undefined) console.log(`  Duration: ${animInfo.duration}s`);
        if (animInfo.num_channels !== undefined) console.log(`  Channels: ${animInfo.num_channels}`);
        if (animInfo.num_samplers !== undefined) console.log(`  Samplers: ${animInfo.num_samplers}`);

        // Display animation type classification
        if (animInfo.has_skeletal_animation !== undefined || animInfo.has_node_animation !== undefined) {
          const types = [];
          if (animInfo.has_skeletal_animation) types.push('Skeletal');
          if (animInfo.has_node_animation) types.push('Node Transform');
          if (types.length > 0) {
            console.log(`  Animation Type: ${types.join(' + ')}`);
          }
        }
      }

      // Get detailed animation data if requested
      if (detailed) {
        try {
          const anim = usd.getAnimation(i);
          if (anim) {
            console.log(`\n  Channel Details:`);

            // Print channel information
            if (anim.channels && anim.channels.length) {
              let nodeChannels = 0;
              let skeletalChannels = 0;

              anim.channels.forEach((channel, idx) => {
                const targetType = channel.target_type || 'SceneNode'; // Default to SceneNode for backward compat
                const isSkeletalChannel = targetType === 'SkeletonJoint' || channel.skeleton_id !== undefined;

                if (isSkeletalChannel) {
                  skeletalChannels++;
                } else {
                  nodeChannels++;
                }

                console.log(`    Channel ${idx}:`);
                console.log(`      Target Type: ${targetType}`);
                console.log(`      Path: ${channel.path || 'unknown'}`);

                if (isSkeletalChannel) {
                  if (channel.skeleton_id !== undefined) {
                    console.log(`      Skeleton ID: ${channel.skeleton_id}`);
                  }
                  if (channel.joint_id !== undefined) {
                    console.log(`      Joint ID: ${channel.joint_id}`);
                  }
                } else {
                  if (channel.target_node !== undefined) {
                    console.log(`      Target Node: ${channel.target_node}`);
                  }
                }

                if (channel.sampler !== undefined) {
                  console.log(`      Sampler Index: ${channel.sampler}`);

                  // Show sampler details if available
                  if (anim.samplers && anim.samplers[channel.sampler]) {
                    const sampler = anim.samplers[channel.sampler];
                    if (sampler.times && sampler.times.length > 0) {
                      console.log(`      Keyframes: ${sampler.times.length}`);
                      console.log(`      Time Range: ${sampler.times[0].toFixed(3)}s - ${sampler.times[sampler.times.length - 1].toFixed(3)}s`);
                    }
                    if (sampler.interpolation) {
                      console.log(`      Interpolation: ${sampler.interpolation}`);
                    }
                  }
                }
              });

              console.log(`\n  Channel Summary:`);
              console.log(`    Node Transform Channels: ${nodeChannels}`);
              console.log(`    Skeletal Joint Channels: ${skeletalChannels}`);
            }

            // Legacy track information (if present)
            if (anim.tracks && anim.tracks.length) {
              console.log(`\n  Legacy Track Information:`);
              console.log(`    Tracks: ${anim.tracks.length}`);
              anim.tracks.forEach((track, idx) => {
                console.log(`      Track ${idx}: ${track.name || 'unnamed'}`);
                if (track.type) console.log(`        Type: ${track.type}`);
                if (track.keyframes && track.keyframes.length) {
                  console.log(`        Keyframes: ${track.keyframes.length}`);
                  const times = track.times || [];
                  if (times.length > 0) {
                    console.log(`        Time range: ${times[0].toFixed(3)}s - ${times[times.length - 1].toFixed(3)}s`);
                  }
                }
              });
            }
          }
        } catch (e) {
          console.log(`    (Could not retrieve detailed animation data: ${e.message})`);
        }
      }

      console.log();
    } catch (e) {
      console.log(`  Error reading animation: ${e.message}\n`);
    }
  }
}

// Print scene/model info
function printSceneInfo(usd) {
  console.log(`\n=== Scene Information ===`);

  try {
    // Try to get root prim name or other scene info
    if (usd.getDefaultPrim) {
      const defaultPrim = usd.getDefaultPrim();
      if (defaultPrim) {
        console.log(`Default Prim: ${defaultPrim}`);
      }
    }
  } catch (e) {
    // Method might not be available
  }
}

// Main function
async function main() {
  const args = process.argv.slice(2);

  if (args.length === 0) {
    console.log('USD Animation Information Viewer');
    console.log('================================\n');
    console.log('Usage: node animation-info.js <path-to-usd-file> [options]\n');
    console.log('Arguments:');
    console.log('  <path-to-usd-file>   Path to USD file (.usd, .usda, .usdc, .usdz)');
    console.log('  --detailed            Print detailed animation track information');
    console.log('  --memory              Print memory usage statistics');
    console.log('  --help                Show this help message\n');
    console.log('Examples:');
    console.log('  node animation-info.js ../../models/suzanne-subd-lv4.usdc');
    console.log('  node animation-info.js animation.usd --detailed');
    console.log('  node animation-info.js model.usdz --detailed --memory');
    return;
  }

  // Parse arguments
  const usdFilePath = args[0];
  const detailed = args.includes('--detailed');
  const showMemory = args.includes('--memory');
  const showHelp = args.includes('--help');

  if (showHelp) {
    console.log('node animation-info.js - USD Animation Information Viewer\n');
    console.log('Usage: node animation-info.js <path-to-usd-file> [options]\n');
    console.log('Arguments:');
    console.log('  <path-to-usd-file>   Path to USD file (.usd, .usda, .usdc, .usdz)');
    console.log('  --detailed            Print detailed animation track information');
    console.log('  --memory              Print memory usage statistics');
    console.log('  --help                Show this help message');
    return;
  }

  // Check if file exists
  if (!fs.existsSync(usdFilePath)) {
    console.error(`Error: File not found: ${usdFilePath}`);
    process.exit(1);
  }

  // Get file size
  const stats = fs.statSync(usdFilePath);
  console.log(`Loading: ${usdFilePath} (${formatBytes(stats.size)})\n`);

  try {
    // Initialize loader
    const loader = new TinyUSDZLoader();
    await loader.init({ useMemory64: false });
    loader.setMaxMemoryLimitMB(512);

    // Load USD file
    const startTime = Date.now();

    // In Node.js, pass the file path directly to the loader
    // The FileFetcher will handle reading the file with fs
    const url = usdFilePath;

    // Load asynchronously
    const usd = await new Promise((resolve, reject) => {
      loader.load(url, resolve,
        (progress) => {
          if (progress && progress.loaded && progress.total) {
            const percent = ((progress.loaded / progress.total) * 100).toFixed(1);
            console.log(`Loading: ${percent}%`);
          }
        },
        reject
      );
    });

    const loadTime = Date.now() - startTime;
    console.log(`\n✓ USD file loaded successfully (${loadTime}ms)\n`);

    // Print information
    printSceneInfo(usd);
    printAnimationClips(usd, detailed);

    // Print memory usage if requested
    if (showMemory) {
      reportMemUsage();
    }

  } catch (error) {
    console.error(`Error: ${error.message}`);
    if (error.stack) {
      console.error(error.stack);
    }
    process.exit(1);
  }
}

// Run main function
main().catch(error => {
  console.error('Unexpected error:', error);
  process.exit(1);
});
