# Resume: `src/next` pcp composition — remaining relocate/specialize gaps

Handoff for a fresh coding-agent session. The next-vs-pxr **flatten differential**
gate is at **743 pass / 13 untagged / 0 FAIL** of 798 inputs (campaign started at
181 listed / 597 passing). This doc lists the **13 remaining untagged cases**,
their **precise pcp.txt-derived root causes**, what has already been tried and
reverted, and the recommended next arc.

The authoritative spec for every composed prim is its corpus **`pcp.txt`** — a
prim-stack dump listing the exact ordered `(layer, path)` sources pxr composes.
**Always model a fix against `pcp.txt` before implementing.**

## Orientation

- **Repo / branch:** `/mnt/nvme02/work/tinyusdz-repo/dev`, branch `dev`.
- **Module:** `src/next/pcp/` — the composition cache. Hot files:
  `cache-arc-expansion.inc` (arc expansion, `SourcesForRelocateSource`,
  `AddRelocatedSources`, `DeriveChildSources`, `WithStackRelocates`, the
  implied-class block ~line 662), `cache-layer-stack.inc` (`BuildStackRelocates`),
  `cache-compose.inc` (`ComposeOpinions`, `ComposeChildNames`), `prim-index.hh`
  (`StackRelocates`), `cache.cc` (the `Src` struct, Impl members).
- **Memory:** `~/.claude/projects/.../memory/aousd-pxr-diff-gates.md` has the full
  round-by-round history and every reverted-experiment finding.

### Build & test

```sh
cmake --build build-next -j16                       # NOT the top-level build/
# Oracle: pinned OpenUSD 26.05
OR=/mnt/nvme02/work/tinyusdz-repo/OpenUSD/dist/bin/usdcat
SUITE=~/.cache/tinyusdz/core-spec-supplemental-release_dec2025/releases/1.0.1

# Full flatten gate (run 2-3x: pxr instancing numbering is nondeterministic)
node tests/next/run-next-pxr-flatten-diff.mjs --next-usdcat build-next/next_usdcat \
  --suite-root $SUITE --report-only            # want: 0 FAIL
# Supplemental composition (137/138; ratchet 0)
python3 tests/next/run-aousd-supplemental.py --suite-root $SUITE \
  --next-usdcat build-next/next_usdcat --aousd-test build-next/test_aousd_conformance \
  --max-composition-fail 99
ctest --test-dir build-next                         # 36/36
cd build && ctest && cd ..                          # main 37/37 (src/next links in)
bash tests/run-usdcat-compare.sh                    # roundtrip 222/1

# Per case: compare next vs pxr, then cross-check the prim stack against pcp.txt
CASE=$SUITE/composition/tests/assets/<Name>_root
build-next/next_usdcat -f --instance-mode prototypes --prototype-numbering usdcat $CASE/root.usd
$OR --flatten $CASE/root.usd
grep -A12 'composing </Some/Prim>' $CASE/pcp.txt
```

### Hard regression bar (every commit)
743 pass / 0 FAIL flatten, supplemental 138, build-next 36/36, main 37/37,
roundtrip 222/1. **Any net regression that can't be reconciled against `pcp.txt`
= revert that step.** Prune newly-passing entries from
`tests/next/next-pxr-flatten-xfail.txt`; keep `INTENTIONAL:`/`ORACLE-` tagged
lines verbatim.

## The 13 remaining cases, grouped by root cause

### A. Context-lost relocate-source resolution — 5 cases (HIGHEST VALUE)
`SourcesForRelocateSource(stack, site)` composes the relocate source in the
relocating stack's **isolated** namespace, losing the caller's composition
context, so opinions that need that context never reach the relocated prim.

| Case | pcp.txt shows | next misses |
|---|---|---|
| `TrickyRelocationOfPrimFromVariant` `/Char/Anim/Tail` | `TailRig.usd /TailRig/Tail` (def) | Tail is under the root-selected `Standard` variant; isolated walk uses CharRig's default `None` |
| `TrickySpookyVariantSelectionInClass` `/Char/Anim/LeftLeg` | def | same variant-selection context |
| `TrickyConnectionToRelocatedAttribute` `/HumanRig/Anim/Face/LEye` | `root.usd /HumanRig/rig/Face/rig/SymEyeRig/Anim` (`baz`) + `eye_rig.usd /EyeRig/Anim` (`foo`) | `baz` is an IMPLIED-class over at the `SymEyeRig` inherit path in an ancestor stack |
| `TrickyMultipleRelocationsAndClasses2` `/CharRig/Anim/Legs/LHip/Knee` | `root.usd .../SymLegRig/TentacleRig/Tentacle/Seg2` (`JointBlend`) | same implied-class-through-relocation |
| `TrickyInheritsAndRelocates5` `/CharRig/Anim/Arms/R1Arm/Knot03` | `foo` "from CharRig" | implied-inherit opinion reaching the relocated prim |

**Verified architecture:** the fix is to derive the arrival CONTENT from the
source's **composed parent** (`SourcesForSite(0, composed_src_parent)` +
`DeriveChildSources` with the source's departure suppressed + a redirect rename
`composed_src -> arrival_dst`). Confirmed the composed parent (e.g. `/Char/TailRig`)
DOES carry the Standard-variant + reference context the isolated walk loses.

**Reverted attempts (2 rounds) and their exact blockers:**
1. Threading `ps.arc_chain` into the isolated walk's seed (+ chain-keyed cache):
   0 FAIL but `baz` still absent — arc_chain alone is insufficient; the
   implied-class block must map `SymEyeRig/Anim` (FaceRig ns) ->
   `/HumanRig/rig/Face/rig/SymEyeRig/Anim` (root) via the chain's per-node
   `map_idx`, and the seeded maps don't line up through the relocate.
2. The composed-parent rewrite itself: **crashes** (gdb: mutual
   `AddRelocatedSources<->DeriveChildSources` recursion when the composed parent
   is itself an arrival re-deriving the same child — chained shapes like
   `Path->Anim/Path` then `Anim->AnimScope`). A **recursion guard**
   (`reloc_content_in_progress` set in `cache.cc`, keyed by `child_composed`,
   falling back to the isolated walk on re-entry) **stops all crashes** — keep
   this. But even guarded it (a) didn't fix the targets and (b) broke **13
   passing** relocate cases, because the redirect-mapping
   `Compose{composed_src->child_composed}` + departure-suppression-across-a-
   composed-parent are **not equivalent** to the isolated walk's
   `outer=renamed_map` in the relocating-stack namespace.

**Next step (the real task):** with the recursion guard in place, make the
composed-parent derivation **bit-for-bit equivalent to the isolated walk on all
~30 currently-passing relocate cases FIRST** (fix the redirect/departure model),
THEN the 5 context cases fall out for free. This is a multi-step debugging arc,
not a one-shot. Broken-canary list to diff against: `BasicRelocateToAnimInterface`,
`TrickyMultipleRelocations`/`2`/`3`/`4`/`5`, `TrickyInheritsAndRelocates`/`2`,
`RelocateToNone`, `TrickyVariantOverrideOfRelocatedPrim`,
`TypicalReferenceToChargroupWithRename`, `TrickySpecializesAndRelocates`,
`ErrorInvalidConflictingRelocates`.

### B. Per-arc scoping — 4 cases: relocates apply ONLY to content/targets
introduced by arcs BENEATH the relocating stack, never authored in it.
- `RelocatePrimsWithSameName` `/ChainedReferences/Child_1`: EMPTY stack -> `over`.
  Content Srcs (instrumented): `ChainedRef_1/Child` (stk0, INTERNAL ref — same
  stack, must be excluded) + `base.usd/Base/Child` (stk2 — must have DEPARTED via
  the chained `/ChainedRef_2/Child->Child_2` relocate in the content walk). next
  applies neither (two coupled rules).
- `ErrorArcCycle` `/RelocatedInheritOfChild/Object`: EMPTY -> `over`. Object is
  INHERITED within the relocating stack -> does not follow (same rule 1).
- `ErrorInvalidPreRelocateTargetPath`, `BasicRelocateToAnimInterfaceAsNewRootPrim`:
  a rel/connection target authored IN the relocating layer pointing at a
  pre-relocate path is KEPT dangling (NOT remapped); a target from BENEATH is
  remapped. Needs per-opinion authoring-level tracking in `WithStackRelocates`.
- Needs **arc-origin tracking** (which layer-stack each opinion/target came from
  relative to each relocate). Real model addition, high regression risk.

### C. Implied-specialize composed-target / order quirk — 1 case
`VariantSpecializesAndReferenceSurprisingBehavior` `/Model/Material_Child`:
pcp.txt stack = `/New_Shading_Variant/Material_Child`,
`/Model_defaultShadingVariant/Material` (myInt=0), `/New_Shading_Variant/Material`
(myInt=1). The same-stack implied-specialize (landed for `SpecializesAndVariants4`,
commit dfcd318f1) handles the raw-spec case, but here the implied target
`/Model/Material` composes via a reference (no raw spec). Reverted composed-source
reuse gave myInt=1 not 0 because pxr's specialize propagation orders the
**prepend-reference contributor STRONGER than the variant-introduced** one — the
OPPOSITE of `/Model/Material`'s own composition order. Needs the pxr
specialize-propagation order model.

### D. Cross-stack relocate validity — 1 case
`ErrorInvalidReferenceToRelocationSource` `/Bad_ReloOfPreRelo`: a root relocate
whose SOURCE is itself a relocation source in a REFERENCED layer (char.usd) is
invalid -> pxr `over` (empty); next `def` + ModelChild. `BuildStackRelocates` is
per-stack; needs to detect the source departed/relocated in a contributing stack.
(Overlaps the chained-departure half of B / the content walk of A.)

### E. Connection through inherit+relocate — 1 case
`ErrorInvalidInstanceTargetPath` `/FaceRig/BrowRig/LBrow/...` `amount.connect`:
residual after the local-class-arc fix; connection target mapping through the
relocated + inherited chain.

## Recommended order
1. **A** (composed-parent relocate-source, keep the recursion guard) — unblocks 5
   cases and is the correct architecture; the remaining work is the mapping-
   equivalence debugging. Highest value.
2. **C** (specialize order) — 1 case, self-contained once the pxr order rule is
   modeled from pcp.txt.
3. **B** (per-arc scoping / arc-origin tracking) — 4 cases but a shared model
   addition; D and E likely fall out of A+B.

## Already-landed this campaign (do NOT redo)
Composed-namespace relocate keying redesign (`27a34d5c1`), arrival opinion-strength
(defer content + incremental collision-shadow, `8252db49d`), same-stack
implied-specialize (`dfcd318f1`), salted-earth suppression (`0e0f9bba0`),
chained-relocation child collapse (`11a445865`), ancestral cycle detection
(`a21bfa213`, `24662aec0`), prototype numbering + instanceable (`a3de0942f`),
implied-class chain order (`8d27a577d`). See `git log` and memory for details.

## Resuming prompt (paste into a fresh session)

> Continue the `src/next` pcp composition burn-down. Read
> `doc/next-pcp-relocate-remaining.md` first — it lists the 13 remaining
> `next_pxr_flatten_diff` untagged cases with their pcp.txt-derived root causes and
> the exact reverted-attempt blockers. Start with **Cluster A** (context-lost
> relocate-source resolution, 5 cases): re-establish the recursion guard
> (`reloc_content_in_progress` in `cache.cc`), then make the composed-parent
> content derivation in `AddRelocatedSources` bit-for-bit equivalent to the
> isolated `SourcesForRelocateSource` on all currently-passing relocate cases
> before adding the variant/implied-class context. Model every change against the
> per-case `pcp.txt` prim stack. Hard bar: 743 pass / 0 FAIL flatten, supplemental
> 138, build-next 36/36, main 37/37, roundtrip 222/1 — revert anything that can't
> be reconciled. Commit per case with the gate green; prune passing entries from
> `tests/next/next-pxr-flatten-xfail.txt`.
