/**
 * Read normalized metadata values from a TinyUSDZ scene.
 */
export function getUSDSceneMetadata(usdScene) {
  const sceneMetadata = usdScene.getSceneMetadata ? usdScene.getSceneMetadata() : {};
  const fileUpAxis = sceneMetadata.upAxis || 'Y';
  const parsedTimeCodesPerSecond = Number(sceneMetadata.timeCodesPerSecond);
  const parsedStartTimeCode = Number(sceneMetadata.startTimeCode);
  const parsedEndTimeCode = Number(sceneMetadata.endTimeCode);
  const timeCodesPerSecond = Number.isFinite(parsedTimeCodesPerSecond)
    ? parsedTimeCodesPerSecond
    : 24;
  const startTimeCode = Number.isFinite(parsedStartTimeCode)
    ? parsedStartTimeCode
    : 0;
  const endTimeCode = Number.isFinite(parsedEndTimeCode)
    ? parsedEndTimeCode
    : 100;

  return {
    sceneMetadata,
    fileUpAxis,
    timeCodesPerSecond,
    startTimeCode,
    endTimeCode
  };
}
