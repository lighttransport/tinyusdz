export function isNextScene() {
  return false;
}

export function buildNextThreeNode() {
  throw new Error('The next backend is available only in local demo development mode.');
}

export function readNextSceneMeta() {
  return { upAxis: 'Y', metersPerUnit: 1.0 };
}

export function nextCountsFromScene() {
  return {};
}
