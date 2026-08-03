// Shared best-effort concurrency cap for Node and browser texture processors.
// The budget is an RSS/working-set target, not a hard allocator limit: one
// unusually large decoded image can exceed it by itself.

const MIB = 1024 * 1024;
export const TEXTURE_PIPELINE_BASE_BYTES = 384 * MIB;
export const TEXTURE_WORKER_ESTIMATE_BYTES = 192 * MIB;
export const MAX_TEXTURE_CONCURRENCY = 64;

export function normalizeTextureConcurrency(requested, fallback = 1) {
  const fallbackNumber = Number(fallback);
  const safeFallback = Number.isFinite(fallbackNumber) && fallbackNumber > 0
    ? Math.max(1, Math.floor(fallbackNumber))
    : 1;
  const value = Number(requested);
  if (!Number.isFinite(value) || value <= 0) {
    return Math.min(MAX_TEXTURE_CONCURRENCY, safeFallback);
  }
  return Math.max(1, Math.min(MAX_TEXTURE_CONCURRENCY, Math.floor(value)));
}

export function textureConcurrencyForBudget(requested, budgetBytes = 0) {
  const concurrency = normalizeTextureConcurrency(requested);
  const budgetNumber = Number(budgetBytes);
  const budget = Number.isFinite(budgetNumber) ? budgetNumber : 0;
  if (budget <= 0) return concurrency;

  const available = Math.max(0, budget - TEXTURE_PIPELINE_BASE_BYTES);
  const budgetWorkers = Math.floor(available / TEXTURE_WORKER_ESTIMATE_BYTES);
  // A processor still needs one worker to make progress. Callers selecting
  // between JS and WASM may choose WASM instead when this estimate is too low.
  return Math.max(1, Math.min(concurrency, budgetWorkers));
}
