# Tasks — resume hub

Fresh-context resume prompts for the open `tinyusdz` workstreams. Pick one,
paste its **Resume prompt**, and go. Newest completion at top.

---

## ✅ Recently completed (don't re-open)

- **RT skinning under ray query (tusdview workstream A) — DONE** (branch
  `tusdview`, 2026-07-16, pushed through merge `2b0f47caf`). Per-pose **BLAS
  refit** (ALLOW_UPDATE + persistent scratch + MODE_UPDATE in place; skips the
  compaction wave) replaces the per-frame destroy+rebuild; `--play` gives
  deterministic headless playback; `[rt-skin]`/`[vk_rt]` timing under
  `TUSDVIEW_RT_TIMING=1`; `TUSDVIEW_NO_BLAS_REFIT=1` reverts. NOTE: BOTH
  machines implemented refit+--play in parallel; upstream's (RX 9070 XT)
  implementation won the merge — canonical names are `blasDynamic`/
  `blasRefitPending`. The other machine also: threaded the CPU pose
  (`DeformParallelFor`, bit-identical range-split, 3.2→1.67 ms @102k verts),
  landed the HIP 2-level BVH refit (~270→16.5 ms, gate
  `tusdview-hip-bvh-refit`), and moved `tusdview-blas-compaction` to a STATIC
  fixture (a skinned prototype's BLAS is deliberately uncompacted). It
  **considered and rejected default GPU-compute skinning** — GPU FMA/ULP drift
  breaks the byte-parity oracle gates (`check-rt-skinning` asserts RT re-pose
  == CPU bake exactly). This machine's unique addition survives as **OPT-IN
  GPU compute skinning** (`TUSDVIEW_RT_GPU_SKIN=1`, skin.comp; default-off so
  the parity gates hold) — also off because the RT vertex/joint/weight buffers
  are host-visible (compute streams over PCIe, ~30 ms vs ~7 ms CPU at 200k
  tris) and its matrix-skinned normals differ from the CPU path's regenerated
  smooth normals. Follow-ups if ever revisited: device-local RT geometry
  streams (+ staging for the CPU fallback); batch refits into one command
  buffer; per-buffer sync instead of the per-frame vkDeviceWaitIdle. Verified:
  parity harness max=0, refit==rebuild pixel-identical (both loaders),
  ctest 171/171.

- **refactor-next M3 (pcp hot-map perf) — DONE** (branch `dev`, 2026-07-16).
  Landed as open-addressed caches/memos (the full u32 rekeying stayed
  unnecessary): `StackSpecCache` per-stack Specs memo, self-resetting
  `Src::specs_`/`SpecsFor`, `SrcCache` for sources_cache, and a new
  `arc_target_memo_` (per-arc external-target resolution keyed by
  anchor+asset+expr-vars **fingerprint** — pointer identity never hits).
  Island/ALab/Caldera load+compose **−14–16%**, byte-identical (4 scenes),
  parallel==serial, TSan-clean; gates 36/36 + 37/37. Remaining compose cost is
  first-load layer parsing (parallelized by `--compose-threads`), not maps —
  the pcp string-map lever is exhausted on dev. Detail:
  `doc/memory-and-performance.md` §M3.

- **refactor-next Phase-5 leftovers — DONE** (branch `dev`, 2026-07-16).
  Exploration found most once-open Phase-5 items already landed (I1 instance-key
  variant-sel+offset, I2 tri-state `instanceable`, I4 PointInstancer compute API,
  M5 GraftSubtree child-walk — the `memory-and-performance.md` "reverted" note was
  stale). Closed the one genuine gap: **I3 instance↔prototype path-translation
  API** (`Cache::TranslatePathToPrototype`/`TranslatePathFromPrototype` in
  `src/next/pcp/cache.{hh,cc}`, with nested-instance recursion) + nested-instancing
  tests (`test_pcp.cc:test_path_translation`, two-level rewrite) + PointInstancer
  `ComputeMaskAtTime`. Docs reconciled (`doc/refator-next.md`,
  `doc/memory-and-performance.md`). Gates: build-next **36/36**, build **37/37**.
  (M3, then still deferred, landed the same day — see the entry above.)

- **next-vs-pxr flatten-differential burn-down — DONE** (branch `dev`, 2026-07-16).
  All untagged composition divergences fixed; gate **756 pass / 0 untagged xfail /
  0 FAIL** of 798 (build-next 36/36, build 37/37). 13 cases fixed this campaign;
  the last arc (this session) landed relocate/inherit/variant/specialize cases —
  see commits `65272912a`, `b2ff45f49`, `07227930c` and `doc/next-pcp-relocate-remaining.md`.
  The 26 remaining xfail entries are all `INTENTIONAL:`/`ORACLE-` tagged (pxr
  nondeterminism / known non-bugs), not defects. Detail in memory `aousd-pxr-diff-gates`.

---

## Open workstream C — intentional-tag triage (flatten differential)  [LOW]

### Resume prompt
Audit the 26 `INTENTIONAL:`/`ORACLE-` tagged entries in
`tests/next/next-pxr-flatten-xfail.txt` to see whether any were mis-tagged and are
now fixable (the untagged burn-down is complete, so the tagged set is the only
remaining differential surface). For each, re-derive pxr's pcp.txt prim stack and
confirm the divergence is genuinely pxr-nondeterminism / oracle-limited vs. a real
next bug. Reclassify or fix accordingly; keep the tag taxonomy from memory
`aousd-pxr-diff-gates`. Hard bar: gate stays ≥756 pass / 0 untagged / 0 FAIL,
build-next 36/36, build 37/37.
