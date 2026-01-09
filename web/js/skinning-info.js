// Node.js script to load USD files and print skinning information
// Usage: npx vite-node skinning-info.js <path-to-usd-file> [options]
// Example: npx vite-node skinning-info.js ../../models/skinned-character.usdc
// Example: npx vite-node skinning-info.js character.usd --detailed
// Example: npx vite-node skinning-info.js character.usd --keyframes
//
// The JavaScript/WebAssembly binding exposes skinning data (jointIndices, jointWeights,
// geomBindTransform, skel_id, elementSize) from RenderMesh, allowing full inspection
// of skinning information from USD files.

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

// Print skeleton hierarchy information
function printSkeletonInfo(usd, detailed = false) {
  console.log('\n=== Skeleton Information ===');

  // Collect skeleton IDs from meshes
  const skeletonIds = new Set();
  const numMeshes = usd.numMeshes();

  for (let i = 0; i < numMeshes; i++) {
    try {
      const mesh = usd.getMesh(i);
      if (mesh && mesh.skel_id !== undefined && mesh.skel_id >= 0) {
        skeletonIds.add(mesh.skel_id);
      }
    } catch (e) {
      // Ignore errors
    }
  }

  if (skeletonIds.size === 0) {
    console.log('No skeletons found in this USD file.\n');
    return;
  }

  console.log(`Total skeletons: ${skeletonIds.size}\n`);

  // For each skeleton, collect joint information from animations
  for (const skelId of Array.from(skeletonIds).sort()) {
    console.log(`--- Skeleton ${skelId} ---`);

    // Collect joint information from animations
    const jointInfo = new Map();
    const numAnims = usd.numAnimations();

    for (let animIdx = 0; animIdx < numAnims; animIdx++) {
      try {
        const anim = usd.getAnimation(animIdx);
        if (!anim || !anim.channels) continue;

        for (const channel of anim.channels) {
          if (channel.skeleton_id === skelId && channel.joint_id !== undefined && channel.joint_id >= 0) {
            const jointId = channel.joint_id;
            if (!jointInfo.has(jointId)) {
              jointInfo.set(jointId, {
                id: jointId,
                channels: new Set()
              });
            }
            jointInfo.get(jointId).channels.add(channel.path);
          }
        }
      } catch (e) {
        // Ignore errors
      }
    }

    if (jointInfo.size > 0) {
      console.log(`  Joints: ${jointInfo.size}`);

      if (detailed) {
        const sortedJoints = Array.from(jointInfo.keys()).sort((a, b) => a - b);
        console.log(`  Joint IDs: [${sortedJoints.join(', ')}]`);

        console.log('\n  Joint Details:');
        for (const jointId of sortedJoints.slice(0, 10)) {
          const info = jointInfo.get(jointId);
          const channels = Array.from(info.channels).sort().join(', ');
          console.log(`    Joint ${jointId}: animated channels = [${channels}]`);
        }

        if (sortedJoints.length > 10) {
          console.log(`    ... and ${sortedJoints.length - 10} more joints`);
        }
      }
    } else {
      console.log('  No animation data available for this skeleton');
    }

    // Find meshes using this skeleton
    const meshesUsingSkeleton = [];
    for (let i = 0; i < numMeshes; i++) {
      try {
        const mesh = usd.getMesh(i);
        if (mesh && mesh.skel_id === skelId) {
          meshesUsingSkeleton.push(mesh.primName || mesh.displayName || `Mesh_${i}`);
        }
      } catch (e) {
        // Ignore
      }
    }

    if (meshesUsingSkeleton.length > 0) {
      console.log(`  Meshes using this skeleton: ${meshesUsingSkeleton.length}`);
      if (detailed) {
        console.log(`    ${meshesUsingSkeleton.join(', ')}`);
      }
    }

    console.log();
  }

  console.log('Note: Full skeleton hierarchy (parent-child relationships) requires');
  console.log('      direct USD prim access, which is not yet exposed in the WASM binding.\n');
}

// Print skinning information from meshes
function printSkinningInfo(usd, detailed = false, boneReductionInfo = null) {
  const numMeshes = usd.numMeshes();

  if (numMeshes === 0) {
    console.log('No meshes found in this USD file.');
    return;
  }

  console.log('\n=== Skinning Information ===');
  if (boneReductionInfo) {
    console.log(`Bone Reduction: ${boneReductionInfo.enabled ? 'Enabled' : 'Disabled'}`);
    if (boneReductionInfo.enabled) {
      console.log(`Target Bones Per Vertex: ${boneReductionInfo.targetCount}`);
    }
  }
  console.log(`Total meshes: ${numMeshes}\n`);

  let skinnedMeshCount = 0;

  for (let i = 0; i < numMeshes; i++) {
    try {
      const mesh = usd.getMesh(i);

      if (!mesh) {
        console.log(`Mesh ${i}: (unable to load)`);
        continue;
      }

      const meshName = mesh.primName || mesh.displayName || `Mesh_${i}`;
      const hasJointIndices = mesh.jointIndices && mesh.jointIndices.length > 0;
      const hasJointWeights = mesh.jointWeights && mesh.jointWeights.length > 0;
      const isSkinned = hasJointIndices && hasJointWeights;

      if (!isSkinned) {
        if (detailed) {
          console.log(`--- Mesh ${i}: ${meshName} ---`);
          console.log(`  Skinned: No`);
          console.log();
        }
        continue;
      }

      skinnedMeshCount++;
      console.log(`--- Skinned Mesh ${i}: ${meshName} ---`);

      if (mesh.absPath) {
        console.log(`  Absolute Path: ${mesh.absPath}`);
      }

      // Vertex count
      const vertexCount = mesh.points ? mesh.points.length / 3 : 0;
      console.log(`  Vertices: ${vertexCount}`);

      // Joint/weight information
      if (hasJointIndices && hasJointWeights) {
        const jointIndicesCount = mesh.jointIndices.length;
        const jointWeightsCount = mesh.jointWeights.length;

        console.log(`  Joint Indices Count: ${jointIndicesCount}`);
        console.log(`  Joint Weights Count: ${jointWeightsCount}`);

        // Calculate influences per vertex (declare at function scope level for later use)
        let influencesPerVertex = 0;
        if (vertexCount > 0 && jointIndicesCount > 0) {
          influencesPerVertex = jointIndicesCount / vertexCount;
          console.log(`  Influences Per Vertex: ${influencesPerVertex}`);

          // Find unique joint IDs
          const uniqueJoints = new Set(Array.from(mesh.jointIndices).filter(idx => idx >= 0));
          console.log(`  Unique Joints Used: ${uniqueJoints.size}`);

          if (detailed && uniqueJoints.size > 0) {
            const sortedJoints = Array.from(uniqueJoints).sort((a, b) => a - b);
            console.log(`  Joint IDs: [${sortedJoints.join(', ')}]`);
          }
        }

        // Geom bind transform
        if (mesh.geomBindTransform && mesh.geomBindTransform.length === 16) {
          console.log(`  Has Geom Bind Transform: Yes`);
          if (detailed) {
            const mat = mesh.geomBindTransform;
            console.log(`  Geom Bind Transform:`);
            console.log(`    [${mat[0].toFixed(3)}, ${mat[1].toFixed(3)}, ${mat[2].toFixed(3)}, ${mat[3].toFixed(3)}]`);
            console.log(`    [${mat[4].toFixed(3)}, ${mat[5].toFixed(3)}, ${mat[6].toFixed(3)}, ${mat[7].toFixed(3)}]`);
            console.log(`    [${mat[8].toFixed(3)}, ${mat[9].toFixed(3)}, ${mat[10].toFixed(3)}, ${mat[11].toFixed(3)}]`);
            console.log(`    [${mat[12].toFixed(3)}, ${mat[13].toFixed(3)}, ${mat[14].toFixed(3)}, ${mat[15].toFixed(3)}]`);
          }
        }

        // Sample weight data for first few vertices
        if (detailed && vertexCount > 0 && influencesPerVertex > 0) {
          const samplesToShow = Math.min(5, vertexCount);
          console.log(`\n  Sample Skinning Data (first ${samplesToShow} vertices):`);

          for (let v = 0; v < samplesToShow; v++) {
            const startIdx = v * influencesPerVertex;
            const indices = [];
            const weights = [];

            for (let j = 0; j < influencesPerVertex; j++) {
              const idx = startIdx + j;
              if (idx < jointIndicesCount) {
                const jointIdx = mesh.jointIndices[idx];
                const weight = mesh.jointWeights[idx];
                if (weight > 0.0001) { // Only show non-zero weights
                  indices.push(jointIdx);
                  weights.push(weight.toFixed(4));
                }
              }
            }

            if (indices.length > 0) {
              console.log(`    Vertex ${v}: joints=[${indices.join(', ')}], weights=[${weights.join(', ')}]`);
            } else {
              console.log(`    Vertex ${v}: (no influences)`);
            }
          }
        }

        // Weight statistics
        if (detailed && mesh.jointWeights.length > 0) {
          const weights = Array.from(mesh.jointWeights);
          const nonZeroWeights = weights.filter(w => w > 0.0001);
          const sumWeights = nonZeroWeights.reduce((sum, w) => sum + w, 0);
          const avgWeight = nonZeroWeights.length > 0 ? sumWeights / nonZeroWeights.length : 0;
          const maxWeight = Math.max(...weights);
          const minNonZeroWeight = Math.min(...nonZeroWeights);

          console.log(`\n  Weight Statistics:`);
          console.log(`    Non-zero Weights: ${nonZeroWeights.length} / ${weights.length}`);
          console.log(`    Average Weight: ${avgWeight.toFixed(4)}`);
          console.log(`    Max Weight: ${maxWeight.toFixed(4)}`);
          console.log(`    Min Non-zero Weight: ${minNonZeroWeight.toFixed(4)}`);
        }
      }

      // Skeleton reference
      if (mesh.skel_id !== undefined && mesh.skel_id >= 0) {
        console.log(`  Skeleton ID: ${mesh.skel_id}`);
      }

      console.log();
    } catch (e) {
      console.log(`  Error reading mesh ${i}: ${e.message}\n`);
    }
  }

  console.log(`Summary: ${skinnedMeshCount} skinned meshes out of ${numMeshes} total meshes`);
}

// Print skeletal animation information from SkelAnimation prims
function printSkelAnimation(usd, detailed = false, dumpKeyframes = false) {
  const numAnims = usd.numAnimations();

  if (numAnims === 0) {
    console.log('\nNo skeletal animations found in this USD file.');
    return;
  }

  console.log('\n=== Skeletal Animation Information ===');
  console.log(`Total animation clips: ${numAnims}\n`);

  let skelAnimCount = 0;

  for (let i = 0; i < numAnims; i++) {
    try {
      const animInfo = usd.getAnimationInfo(i);
      const anim = detailed || dumpKeyframes ? usd.getAnimation(i) : null;

      // Check if this is skeletal animation
      const isSkeletal = animInfo && (
        animInfo.has_skeletal_animation === true ||
        (anim && anim.channels && anim.channels.some(ch =>
          ch.target_type === 'SkeletonJoint' || ch.skeleton_id !== undefined
        ))
      );

      if (!isSkeletal) {
        if (detailed) {
          console.log(`--- Animation ${i}: ${animInfo?.name || animInfo?.prim_name || 'unnamed'} ---`);
          console.log(`  Type: Not skeletal (node transform animation)`);
          console.log();
        }
        continue;
      }

      skelAnimCount++;
      console.log(`--- Skeletal Animation ${i}: ${animInfo?.name || animInfo?.prim_name || 'unnamed'} ---`);

      if (animInfo) {
        if (animInfo.abs_path) console.log(`  Absolute Path: ${animInfo.abs_path}`);
        if (animInfo.display_name) console.log(`  Display Name: ${animInfo.display_name}`);
        if (animInfo.duration !== undefined) console.log(`  Duration: ${animInfo.duration}s`);
        if (animInfo.num_channels !== undefined) console.log(`  Channels: ${animInfo.num_channels}`);
        if (animInfo.num_samplers !== undefined) console.log(`  Samplers: ${animInfo.num_samplers}`);
      }

      // Detailed channel information
      if ((detailed || dumpKeyframes) && anim && anim.channels) {
        const skelChannels = anim.channels.filter(ch =>
          ch.target_type === 'SkeletonJoint' || ch.skeleton_id !== undefined
        );

        if (skelChannels.length > 0) {
          console.log(`\n  Skeletal Channels: ${skelChannels.length}`);

          // Group channels by skeleton and joint
          const channelsByJoint = new Map();

          skelChannels.forEach((channel, idx) => {
            const skelId = channel.skeleton_id !== undefined ? channel.skeleton_id : 0;
            const jointId = channel.joint_id !== undefined ? channel.joint_id : -1;
            const key = `skel${skelId}_joint${jointId}`;

            if (!channelsByJoint.has(key)) {
              channelsByJoint.set(key, []);
            }
            channelsByJoint.get(key).push({ ...channel, originalIndex: idx });
          });

          // Count unique joints
          const uniqueJoints = channelsByJoint.size;
          console.log(`  Unique Animated Joints: ${uniqueJoints}`);

          if (detailed) {
            let jointNum = 0;
            for (const [key, channels] of channelsByJoint) {
              if (jointNum >= 10 && !dumpKeyframes) {
                const remaining = channelsByJoint.size - jointNum;
                console.log(`  ... and ${remaining} more joints`);
                break;
              }

              const firstCh = channels[0];
              console.log(`\n  Joint ${jointNum} (Skeleton ID: ${firstCh.skeleton_id}, Joint ID: ${firstCh.joint_id}):`);

              channels.forEach(ch => {
                const path = ch.path || 'unknown';
                console.log(`    Channel: ${path}`);

                if (ch.sampler !== undefined && anim.samplers && anim.samplers[ch.sampler]) {
                  const sampler = anim.samplers[ch.sampler];

                  if (sampler.times && sampler.times.length > 0) {
                    console.log(`      Keyframes: ${sampler.times.length}`);
                    console.log(`      Time Range: ${sampler.times[0].toFixed(3)}s - ${sampler.times[sampler.times.length - 1].toFixed(3)}s`);

                    if (sampler.interpolation) {
                      console.log(`      Interpolation: ${sampler.interpolation}`);
                    }

                    // Dump keyframe data if requested
                    if (dumpKeyframes && sampler.values) {
                      const componentsPerKey = sampler.values.length / sampler.times.length;
                      console.log(`      Keyframe Data:`);

                      const maxFramesToShow = 10;
                      const framesToShow = Math.min(maxFramesToShow, sampler.times.length);

                      for (let k = 0; k < framesToShow; k++) {
                        const time = sampler.times[k].toFixed(3);
                        const idx = k * componentsPerKey;
                        let valueStr = '';

                        if (componentsPerKey === 1) {
                          valueStr = sampler.values[idx].toFixed(4);
                        } else if (componentsPerKey === 3) {
                          // Translation or scale (vec3)
                          valueStr = `[${sampler.values[idx].toFixed(4)}, ${sampler.values[idx+1].toFixed(4)}, ${sampler.values[idx+2].toFixed(4)}]`;
                        } else if (componentsPerKey === 4) {
                          // Rotation (quaternion)
                          valueStr = `[${sampler.values[idx].toFixed(4)}, ${sampler.values[idx+1].toFixed(4)}, ${sampler.values[idx+2].toFixed(4)}, ${sampler.values[idx+3].toFixed(4)}]`;
                        } else {
                          // Generic
                          const components = [];
                          for (let c = 0; c < componentsPerKey; c++) {
                            components.push(sampler.values[idx + c].toFixed(4));
                          }
                          valueStr = `[${components.join(', ')}]`;
                        }

                        console.log(`        Frame ${k}: t=${time}s, value=${valueStr}`);
                      }

                      if (sampler.times.length > maxFramesToShow) {
                        console.log(`        ... and ${sampler.times.length - maxFramesToShow} more keyframes`);
                      }
                    }
                  }
                }
              });

              jointNum++;
            }
          }
        }
      }

      console.log();
    } catch (e) {
      console.log(`  Error reading animation ${i}: ${e.message}\n`);
    }
  }

  if (skelAnimCount === 0) {
    console.log('No skeletal animations found (only node transform animations).');
  } else {
    console.log(`Summary: ${skelAnimCount} skeletal animation clips found`);
  }
}

// Print scene/model info
function printSceneInfo(usd) {
  console.log('=== Scene Information ===');

  try {
    // Try to get root prim name or other scene info
    if (usd.getDefaultPrim) {
      const defaultPrim = usd.getDefaultPrim();
      if (defaultPrim) {
        console.log(`Default Prim: ${defaultPrim}`);
      }
    }

    // Get up axis
    if (usd.getUpAxis) {
      const upAxis = usd.getUpAxis();
      if (upAxis) {
        console.log(`Up Axis: ${upAxis}`);
      }
    }
  } catch (e) {
    // Method might not be available
  }
}

// Main function
async function main() {
  const args = process.argv.slice(2);

  if (args.length === 0 || args.includes('--help')) {
    console.log('USD Skinning Information Viewer');
    console.log('===============================\n');
    console.log('Usage: npx vite-node skinning-info.js <path-to-usd-file> [options]\n');
    console.log('Arguments:');
    console.log('  <path-to-usd-file>      Path to USD file (.usd, .usda, .usdc, .usdz)');
    console.log('  --detailed              Print detailed skinning and animation information');
    console.log('  --keyframes             Dump skeletal animation keyframe data');
    console.log('  --memory                Print memory usage statistics');
    console.log('  --reduce-bones          Enable bone reduction');
    console.log('  --target-bones <N>      Target bone count per vertex (default: 4)');
    console.log('  --help                  Show this help message\n');
    console.log('Examples:');
    console.log('  npx vite-node skinning-info.js ../../models/character.usdc');
    console.log('  npx vite-node skinning-info.js skinned-mesh.usd --detailed');
    console.log('  npx vite-node skinning-info.js character.usda --detailed --keyframes');
    console.log('  npx vite-node skinning-info.js model.usdz --detailed --memory');
    console.log('  npx vite-node skinning-info.js character.usdc --reduce-bones --target-bones 2');
    console.log('  npx vite-node skinning-info.js model.usda --reduce-bones --detailed\n');
    console.log('This tool displays:');
    console.log('  - Mesh skinning data (joint indices, joint weights)');
    console.log('  - Skeleton hierarchy information');
    console.log('  - Skeletal animation keyframes from SkelAnimation prims');
    console.log('  - Bone reduction statistics when --reduce-bones is enabled');
    return;
  }

  // Parse arguments
  const usdFilePath = args[0];
  const detailed = args.includes('--detailed');
  const showMemory = args.includes('--memory');
  const dumpKeyframes = args.includes('--keyframes');
  const reduceBones = args.includes('--reduce-bones');

  // Parse --target-bones argument
  let targetBoneCount = 4; // Default value
  const targetBonesIndex = args.indexOf('--target-bones');
  if (targetBonesIndex !== -1 && targetBonesIndex + 1 < args.length) {
    const parsedValue = parseInt(args[targetBonesIndex + 1], 10);
    if (!isNaN(parsedValue) && parsedValue > 0) {
      targetBoneCount = parsedValue;
    } else {
      console.error(`Error: Invalid value for --target-bones: ${args[targetBonesIndex + 1]}`);
      console.error('Target bone count must be a positive integer.');
      process.exit(1);
    }
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

    // Configure bone reduction settings
    if (reduceBones) {
      console.log(`Bone Reduction: Enabled (target: ${targetBoneCount} bones per vertex)\n`);
      loader.setEnableBoneReduction(true);
      loader.setTargetBoneCount(targetBoneCount);
      console.log(`[DEBUG] Bone reduction configured: enabled=${loader.getEnableBoneReduction()}, target=${loader.getTargetBoneCount()}`);
    } else {
      loader.setEnableBoneReduction(false);
    }

    // Load USD file
    const startTime = Date.now();

    // In Node.js, read the file directly and parse the buffer
    const fileBuffer = fs.readFileSync(usdFilePath);
    const usd_binary = new Uint8Array(fileBuffer);

    console.log(`File loaded: ${formatBytes(usd_binary.length)}`);
    console.log('Parsing USD data...\n');

    // Parse the binary data
    const usd = await new Promise((resolve, reject) => {
      loader.parse(usd_binary, usdFilePath, resolve, reject);
    });

    const loadTime = Date.now() - startTime;
    console.log(`\n\n✓ USD file loaded successfully (${loadTime}ms)\n`);

    // Prepare bone reduction info
    const boneReductionInfo = {
      enabled: reduceBones,
      targetCount: targetBoneCount
    };

    // Print information
    printSceneInfo(usd);
    printSkinningInfo(usd, detailed, boneReductionInfo);
    printSkeletonInfo(usd, detailed);
    printSkelAnimation(usd, detailed, dumpKeyframes);

    // Print memory usage if requested
    if (showMemory) {
      reportMemUsage();
    }

  } catch (error) {
    console.error(`\nError: ${error.message}`);
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
