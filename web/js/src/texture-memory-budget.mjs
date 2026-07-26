// Shared best-effort concurrency cap for Node and browser texture processors.
// The budget is an RSS/working-set target, not a hard allocator limit: one
// unusually large decoded image can exceed it by itself.

const MIB = 1024 * 1024;
export const TEXTURE_PIPELINE_BASE_BYTES = 384 * MIB;
export const TEXTURE_WORKER_ESTIMATE_BYTES = 192 * MIB;

export function textureConcurrencyForBudget(requested, budgetBytes = 0) {
  const concurrency = Math.max(1, Math.floor(Number(requested) || 1));
  const budget = Number(budgetBytes) || 0;
  if (budget <= 0) return concurrency;

  const available = Math.max(0, budget - TEXTURE_PIPELINE_BASE_BYTES);
  const budgetWorkers = Math.floor(available / TEXTURE_WORKER_ESTIMATE_BYTES);
  // A processor still needs one worker to make progress. Callers selecting
  // between JS and WASM may choose WASM instead when this estimate is too low.
  return Math.max(1, Math.min(concurrency, budgetWorkers));
}
