import * as THREE from 'three';
import {
  convertUSDSkeletalAnimationsToThreeJS,
  convertUSDNodeAnimationsToThreeJS
} from './USDAnimationConverter.js';

/**
 * Extract skeletal and node animation clips from a TinyUSDZ scene.
 */
export function extractUSDSceneAnimations(usdScene, options = {}) {
  const logger = options.logger || console;
  const boneMaps = options.boneMaps || new Map();
  const nodeIndexMap = options.nodeIndexMap || new Map();
  const timeCodesPerSecond =
    options.timeCodesPerSecond !== undefined ? options.timeCodesPerSecond : 24;

  const animationInfos = usdScene.getAllAnimationInfos
    ? usdScene.getAllAnimationInfos()
    : [];

  let usdAnimations = [];
  if (boneMaps.size > 0) {
    usdAnimations = convertUSDSkeletalAnimationsToThreeJS(
      usdScene,
      boneMaps,
      timeCodesPerSecond
    );
    logger.log(
      `Converted ${usdAnimations.length} skeletal animations (fps: ${timeCodesPerSecond})`
    );
  } else {
    logger.log('No skeleton data available - skipping skeletal animation extraction');
  }

  const usdNodeAnimations = convertUSDNodeAnimationsToThreeJS(usdScene, nodeIndexMap);
  if (usdNodeAnimations.length > 0) {
    logger.log(
      `Extracted ${usdNodeAnimations.length} node animation clip(s) for scene graph xformOps`
    );
  }

  const hasAnyAnimation =
    usdAnimations.length > 0 || usdNodeAnimations.length > 0;
  if (hasAnyAnimation) {
    const totalClips = usdAnimations.length + usdNodeAnimations.length;
    logger.log(
      `Extracted ${totalClips} animation clip(s): ${usdAnimations.length} skeletal, ${usdNodeAnimations.length} node`
    );

    logger.log('=== Animation-to-Skeleton Targeting ===');
    const numAnimations = usdScene.numAnimations ? usdScene.numAnimations() : 0;
    for (let i = 0; i < numAnimations; i++) {
      const usdAnim = usdScene.getAnimation(i);
      if (!usdAnim || !usdAnim.channels) continue;
      const skelIdsInAnim = new Set();
      for (const channel of usdAnim.channels) {
        if (channel.target_type === 'SkeletonJoint') {
          const skelId =
            channel.skeleton_id !== undefined ? channel.skeleton_id : 0;
          skelIdsInAnim.add(skelId);
        }
      }
      logger.log(
        `Animation ${i} (${usdAnim.name}, ${usdAnim.duration} frames): targets skeleton(s) [${Array.from(
          skelIdsInAnim
        )
          .sort()
          .join(', ')}]`
      );
    }
    logger.log('=== End Animation Targeting ===');
  }

  const animationEnabled = usdAnimations.map((clip) => clip.duration > 0);
  const disabledCount = animationEnabled.filter((enabled) => !enabled).length;

  return {
    animationInfos,
    usdAnimations,
    usdNodeAnimations,
    animationEnabled,
    disabledCount,
    hasAnyAnimation
  };
}

/**
 * Return max timeline duration across scene metadata and loaded animation clips.
 */
export function computeUSDSceneTimelineDuration(
  endTimeCode,
  usdAnimations,
  usdNodeAnimations
) {
  let maxDuration = endTimeCode;
  for (const clip of usdAnimations || []) {
    if (clip.duration > maxDuration) {
      maxDuration = clip.duration;
    }
  }
  for (const clip of usdNodeAnimations || []) {
    if (clip.duration > maxDuration) {
      maxDuration = clip.duration;
    }
  }
  return maxDuration;
}

/**
 * Create playback controller around a Three.js AnimationMixer.
 */
export function createUSDSceneAnimationPlayback(rootObject, options = {}) {
  const logger = options.logger || console;
  const usdAnimations = options.usdAnimations || [];
  const usdNodeAnimations = options.usdNodeAnimations || [];
  let playbackSpeed =
    options.speed !== undefined && isFinite(options.speed) ? options.speed : 24;

  const mixer = options.mixer || new THREE.AnimationMixer(rootObject);

  let animationAction = null;
  let animationActions = [];
  let playAllMode = false;
  const actionDurations = new Map();

  function registerAction(action, clip) {
    if (action && clip && Number.isFinite(clip.duration)) {
      actionDurations.set(action, clip.duration);
    }
  }

  function clearActionDurations() {
    actionDurations.clear();
  }

  function getState() {
    return {
      mixer,
      animationAction,
      animationActions: [...animationActions],
      playAllMode
    };
  }

  function stopAllAnimations() {
    if (animationAction) {
      animationAction.stop();
      animationAction = null;
    }

    for (const action of animationActions) {
      action.stop();
    }
    animationActions = [];
    playAllMode = false;
    clearActionDurations();

    return getState();
  }

  function playNodeAnimations(nodeOptions = {}) {
    const loop = nodeOptions.loop ?? THREE.LoopRepeat;
    const clampWhenFinished = nodeOptions.clampWhenFinished ?? false;
    const paused = !!nodeOptions.paused;

    if (usdNodeAnimations.length === 0) {
      return getState();
    }

    for (const nodeClip of usdNodeAnimations) {
      if (nodeClip.duration <= 0) continue;
      const action = mixer.clipAction(nodeClip);
      action.loop = loop;
      action.clampWhenFinished = clampWhenFinished;
      action.setEffectiveTimeScale(playbackSpeed);
      action.paused = paused;
      action.play();
      animationActions.push(action);
      registerAction(action, nodeClip);
    }

    return getState();
  }

  function playAnimation(index) {
    if (index < 0 || index >= usdAnimations.length) {
      return {
        ...getState(),
        played: false,
        clip: null
      };
    }

    stopAllAnimations();
    playAllMode = false;

    const clip = usdAnimations[index];
    animationAction = mixer.clipAction(clip);
    animationAction.loop = THREE.LoopRepeat;
    animationAction.clampWhenFinished = false;
    animationAction.setEffectiveTimeScale(playbackSpeed);
    animationAction.play();
    registerAction(animationAction, clip);

    playNodeAnimations({
      loop: THREE.LoopRepeat,
      clampWhenFinished: false,
      paused: false
    });

    return {
      ...getState(),
      played: true,
      clip
    };
  }

  function playAllAnimations(animationEnabled = []) {
    stopAllAnimations();
    playAllMode = true;

    let enabledCount = 0;
    let skippedCount = 0;

    for (let i = 0; i < usdAnimations.length; i++) {
      if (!animationEnabled[i]) continue;

      const clip = usdAnimations[i];
      if (clip.duration <= 0) {
        logger.log(
          `Skipping animation ${i} (${clip.name}): 0 duration (rest pose only)`
        );
        skippedCount++;
        continue;
      }

      const action = mixer.clipAction(clip);
      action.loop = THREE.LoopOnce;
      action.clampWhenFinished = true;
      action.setEffectiveTimeScale(playbackSpeed);
      action.paused = true;
      action.play();
      animationActions.push(action);
      registerAction(action, clip);
      enabledCount++;
    }

    playNodeAnimations({
      loop: THREE.LoopOnce,
      clampWhenFinished: true,
      paused: true
    });

    return {
      ...getState(),
      enabledCount,
      skippedCount
    };
  }

  function setPaused(paused) {
    if (playAllMode) {
      // In play-all mode we sample from global timeline manually.
      return getState();
    }
    if (animationAction) {
      animationAction.paused = paused;
    }
    for (const action of animationActions) {
      action.paused = paused;
    }
    return getState();
  }

  function reset() {
    if (playAllMode) {
      for (const action of animationActions) {
        action.time = 0;
      }
      mixer.update(0);
      return getState();
    }
    if (animationAction) {
      animationAction.reset();
      animationAction.play();
    }
    for (const action of animationActions) {
      action.reset();
      action.play();
    }
    return getState();
  }

  function setSpeed(nextSpeed) {
    if (nextSpeed !== undefined && isFinite(nextSpeed)) {
      playbackSpeed = nextSpeed;
    }
    if (animationAction) {
      animationAction.setEffectiveTimeScale(playbackSpeed);
    }
    for (const action of animationActions) {
      action.setEffectiveTimeScale(playbackSpeed);
    }
    return getState();
  }

  function setPlayAllModeTime(time) {
    const globalTime = Number.isFinite(time) ? time : 0;
    for (const action of animationActions) {
      const clipDuration = actionDurations.get(action);
      if (Number.isFinite(clipDuration) && clipDuration > 0) {
        action.time = Math.min(Math.max(globalTime, 0), clipDuration);
      } else {
        action.time = 0;
      }
    }
  }

  function setTime(time, updatePose = false) {
    if (playAllMode) {
      setPlayAllModeTime(time);
    } else {
      if (animationAction) {
        animationAction.time = time;
      }
      for (const action of animationActions) {
        action.time = time;
      }
    }
    if (updatePose) {
      mixer.update(0);
    }
    return getState();
  }

  function dispose() {
    stopAllAnimations();
    mixer.stopAllAction();
    if (typeof mixer.getRoot === 'function') {
      mixer.uncacheRoot(mixer.getRoot());
    }
  }

  return {
    mixer,
    getState,
    stopAllAnimations,
    playNodeAnimations,
    playAnimation,
    playAllAnimations,
    setPaused,
    reset,
    setSpeed,
    setTime,
    dispose
  };
}
