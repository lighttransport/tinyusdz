/**
 * AnimClipUtils.js — Reusable Three.js animation mixing utilities.
 *
 * Provides helpers for crossfade transitions, weight blending, and
 * solo playback across multiple AnimationActions.
 */

/**
 * Crossfade from one action to another over a given duration.
 * Uses Three.js built-in crossFadeTo for smooth blending.
 *
 * @param {THREE.AnimationAction} fromAction - Currently playing action
 * @param {THREE.AnimationAction} toAction   - Target action to fade to
 * @param {number} duration - Crossfade duration in seconds
 */
export function crossfadeActions(fromAction, toAction, duration = 0.5) {
  if (!fromAction || !toAction) return;

  toAction.enabled = true;
  toAction.setEffectiveTimeScale(1);
  toAction.setEffectiveWeight(1);
  toAction.time = 0;

  if (!fromAction.isRunning()) {
    // Source not playing — just start target directly
    toAction.play();
    return;
  }

  fromAction.crossFadeTo(toAction, duration, true);
  toAction.play();
}

/**
 * Prepare all clips for simultaneous blending by playing them at weight 0.
 * Returns an array of AnimationActions ready for weight adjustment.
 *
 * @param {THREE.AnimationMixer} mixer
 * @param {THREE.AnimationClip[]} clips
 * @returns {THREE.AnimationAction[]}
 */
export function prepareClipsForBlending(mixer, clips) {
  const actions = clips.map((clip) => {
    const action = mixer.clipAction(clip);
    action.play();
    action.setEffectiveWeight(0);
    return action;
  });
  return actions;
}

/**
 * Solo a single action: set its weight to 1 and all others to 0.
 *
 * @param {THREE.AnimationAction[]} actions - All available actions
 * @param {number} index - Index of the action to solo
 */
export function soloAction(actions, index) {
  for (let i = 0; i < actions.length; i++) {
    const action = actions[i];
    action.enabled = true;
    action.setEffectiveWeight(i === index ? 1 : 0);
    if (i === index) {
      action.play();
    }
  }
}

/**
 * Set explicit weights for multiple actions.
 * Each entry is { action, weight }.
 *
 * @param {{ action: THREE.AnimationAction, weight: number }[]} entries
 */
export function setActionWeights(entries) {
  for (const { action, weight } of entries) {
    action.enabled = true;
    action.setEffectiveWeight(weight);
    if (weight > 0) {
      action.play();
    }
  }
}
