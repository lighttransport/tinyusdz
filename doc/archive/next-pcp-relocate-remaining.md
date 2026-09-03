# Resume: `src/next` pcp composition — relocate/specialize gaps (COMPLETE)

> **ARCHIVED — completed.** The next-vs-pxr flatten differential gate is at 0
> untagged FAIL (the xfail ledger `tests/next/next-pxr-flatten-xfail.txt` has no
> untagged entries). The "REMAINING"/"STILL FAILING" clusters A–E in the body
> were superseded — the fixes landed in the commits listed at the top of this
> doc. Current composition status lives in
> [`../pcp.md`](../pcp.md) and [`../ousd-vs-tusdz.md`](../ousd-vs-tusdz.md).
> Last verified 2026-07.

Handoff for a fresh coding-agent session. The next-vs-pxr **flatten differential**
gate is at **756 pass / 0 untagged / 0 FAIL** of 798 inputs (campaign started at
181 listed / 597 passing). **All untagged differential cases are now fixed.** This
doc records the final arc's root causes and fixes for reference; the DESIGN-PASS
STATUS section below has the per-case detail.

## DESIGN-PASS STATUS (gate 756 pass / 0 untagged / 0 FAIL — ALL FIXED)

- **2 `ErrorInvalidInstanceTargetPath`** — ✅ **FIXED (b2ff45f49)**. An inherited-class
  connection (SymBrow.sculpt.amount.connect) into relocated instances was dropped: the
  sculpt binds the implied-class map (class subtree only), so sibling-instance targets
  strict-dropped. Two parts in the target-remap lambda: (1) fall back to the Src's
  `arc_chain` reference map when the class's own map can't express the target; (2)
  invalid SELF-instance target — a target that un-relocates (in the referenced stack)
  UNDER the inheriting instance is kept at the reference-mapped PRE-relocate path
  (`FullyUnrelocate(0, .)`), while other-instance targets relocate normally.
- **4 `VariantSpecializesAndReferenceSurprisingBehavior`** — ✅ **FIXED (07227930c)**.
  Material_Child specializes /Model/Material, whose implied-specialize target composes
  via arcs with no raw spec (the block only fired for a raw spec). Extend: compose the
  target's contributors and route to spec_out (ahead of the direct target) ONLY those
  the DIRECT target (arc_site) does not itself deliver — the ancestral prepend-reference,
  not variant-internal references. Distinguishes SurprisingBehavior (ref on /Model,
  ancestral -> myInt=0) from VariantSpecializesAndReference (ref inside the variant ->
  myInt=1, unchanged via the direct_sites filter).

- **3 `TrickySpookyVariantSelectionInClass`** — ✅ **FIXED (65272912a)**. Instance-beats-
  class variant selection through inherit+relocate. Two coupled defects: (1) the
  implied-class block unconditionally overwrote `sels` with the class's ancestor-stack
  selection (SymLegRig=1Leg), clobbering the instance's stronger own selection
  (RightLegRig=2Leg) — now read the inheriting prim's OWN selections at that ancestor
  stack (`Specs(as, dest_in_as)`) and never let the class overwrite them; (2) with (1)
  fixed, derived held BOTH the instance's 2Leg (arc=Variant) AND the class's default
  1Leg (arc=Inherit, which outranks Variant) — added a post-`ProcessDeferredVariants`
  filter in `ExpandList` that drops any source whose site bakes a `{Set=Val}` selector
  conflicting with the finally-resolved `sels[Set]` (pxr resolves one selection per set
  across the prim index; only fires when the set was actually re-selected).


### FIXED this arc
- **1 `TrickyMultipleRelocationsAndClasses2`** — ✅ **FIXED (2ca7930f5)**. Arrival
  CONTENT walk (isolated, chained) missed the root implied-class `JointBlend` over.
  `FullyUnrelocate()` walks a source back through the stack's CHAINED relocates to its
  fully-pre-relocation path; chained arrivals SUPPLEMENT the isolated walk with the
  composed walk's stk0 implied-class sources. `ComposedRelocApplicable` → split into
  `ComposedBaseApplicable` + `IsChainedArrival`.
- **5 `RelocatePrimsWithSameName`** — ✅ **FIXED (144a1e8b3)**. Content-side arc-origin:
  `SaltInternalRefContent` = `FlowsThroughRelocateSource` (Reference-only; arc_sites
  trail intermediate matches `PathInRelocateSource`, excludes arrival's own src_site) +
  `ContentSiteDeparted` (same-stack Reference content not-under-source, guarded
  re-entrant `SourcesForSite(0,parent)`+`RelocationsAt` departed-name check).
- **6 `ErrorInvalidReferenceToRelocationSource`** — ✅ **FIXED (705230658)**. A relocate
  whose SOURCE is a relocation source (departed) in a CONTRIBUTING REFERENCED stack is
  invalid → empty `over`. `SourcesForRelocatedContent` composes the source's parent and
  checks `IsDeparted(s.site, name)` for each contributing stack ≠ the relocating one.

### REMAINING (2, each precisely diagnosed — see memory `aousd-pxr-diff-gates` Round 37)
- **2 `ErrorInvalidInstanceTargetPath`** — 2-to-3-PART deep fix (CT/IA-instrumented). next
  DROPS `amount.connect` ENTIRELY on the LBrow/RBrow inherited-instance sculpts (class
  SymBrow gets it fine). PART 1: the sculpt's connection-target map lacks the general
  `/BrowRig=>/FaceRig/BrowRig` reference pair, so out-of-class targets strict-drop ('').
  IA-instrumented finding: the SymBrow-inherit ProcessArc compose #1 (into LBrow, with the
  reference outer) DOES produce a map containing `/BrowRig=>/FaceRig/BrowRig` (intra=0), and
  `DeriveChildSources` shares `ps.map_idx` (L1839) — yet the observed sculpt target map had
  only the 3 SymBrow pairs. UNRESOLVED derivation discrepancy: the sculpt appears to bind a
  DIFFERENT (reduced) interned map than compose #1 — pin this before fixing. Naive fix:
  preserving `intra_stack` under the reference (namespace-mapping.hh L196-198) makes the
  connect APPEAR but with WRONG ref-ns targets (`/BrowRig/Anim/...` unmapped — intra_stack
  ApplyTarget returns identity outside the class, NOT reference-mapped); reverted. Correct
  part 1 = ensure the sculpt's target map carries+applies the reference pairs (map through
  the reference for out-of-class), NOT identity. PART 2 (harder): the SELF-instance target
  `/BrowRig/Anim/LBrow.InnUD` reverse-relocates to `/BrowRig/LBrow/...` = the inherited-into
  instance → INVALID (non-invertible); pxr keeps it UN-relocated
  (`/FaceRig/BrowRig/Anim/LBrow.InnUD`) while the OTHER-instance target relocates normally.
  BOTH parts needed to flip.
- **4 `VariantSpecializesAndReferenceSurprisingBehavior`** — pcp CONFIRMS order REVERSAL:
  `/Model/Material`'s own stack has New_Shading_Variant STRONGER (myInt=1), but for the
  specialize target Model_defaultShadingVariant the prepend ref is STRONGER (myInt=0).
  Needs pxr's specializes-propagation order (prepend/ancestral > variant-introduced for
  a specialize). LOWEST confidence.

## UPDATE (composed-parent relocated-content derivation LANDED, commit 9748515ec)

Cluster A's core architecture is now IN: `ComposedRelocatedContent`
(`cache-arc-expansion.inc`) derives a relocated prim's content from the source's
parent resolved in ROOT (stack-0) namespace, so the caller's variant/implied-class
context reaches it. It maps the source through ps's arc pairs (**relocate renames
stripped** via `ArcOnlyMapping` — `ps.map_idx` otherwise circularly sends src to
its own dst), composes the parent via `SourcesForSite(0,..)`, salts-BEFORE-expands
the source child, and redirects it onto the arrival dst. Gated by
`ComposedRelocApplicable` to the shapes proven bit-for-bit equivalent to the
isolated walk; everything else keeps `IsolatedRelocatedContent`. Reentrancy guards:
`isolated_reloc_depth_` (nested chained relocates) + `reloc_content_in_progress`
(outermost-only). **Fixed 4 cases, 0 regressions:** `TrickyRelocationOfPrimFrom-
Variant`, `TrickyInheritsAndRelocates5`, `ErrorArcCycle`,
`TrickyMultipleRelocationsAndClasses`.

**Applicability boundaries discovered (the remaining 3 Cluster A cases live outside
them — this is the next arc):** the composed branch falls back to isolated when
(a) **chained** — an ancestor of the source is itself a relocate dst (double-count);
(b) **subroot-ref source** — the source path lies outside every arc pair, so
`SourcesForSite(0,..)` can't resolve it (`SubrootReferenceAndRelocates`);
(c) **spec-at-source** — the relocating stack authors an `over` at the source path,
whose salted-earth descendant-suppression the composed derivation does not model
(`TrickyInheritsAndRelocates`/`...ToNewRootPrim`). The remaining 3 need either the
implied-class site mapped through the relocate ((b)/implied-class) or the spooky
variant-selection carried through an inherit into the relocated prim ((c)-adjacent);
extending the composed branch to those shapes without breaking the fall-back
canaries is the work.

The authoritative spec for every composed prim is its corpus **`pcp.txt`** — a
prim-stack dump listing the exact ordered `(layer, path)` sources pxr composes.
**Always model a fix against `pcp.txt` before implementing.**

## Orientation

- **Repo / branch:** `/mnt/nvme02/work/lightusd-repo/dev`, branch `dev`.
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
OR=/mnt/nvme02/work/lightusd-repo/OpenUSD/dist/bin/usdcat
SUITE=~/.cache/lightusd/core-spec-supplemental-release_dec2025/releases/1.0.1

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
753 pass / 0 FAIL flatten, supplemental 138, build-next 36/36, main 37/37,
roundtrip 222/1. **Any net regression that can't be reconciled against `pcp.txt`
= revert that step.** Prune newly-passing entries from
`tests/next/next-pxr-flatten-xfail.txt`; keep `INTENTIONAL:`/`ORACLE-` tagged
lines verbatim.

## The 3 remaining cases, grouped by root cause

### A. Context-lost relocate-source resolution — 2 cases REMAIN (was 5; 3 FIXED)
Fixed: `TrickyRelocationOfPrimFromVariant` + `TrickyInheritsAndRelocates5`
(composed-parent derivation, 9748515ec); `TrickyConnectionToRelocatedAttribute`
(deeper-stack relocate chained into the implied-class opinion's map, b2fc9a9d0 —
the implied block ~line 719 now adds, per relocate authored in the arc's stack
whose PRE-relocate composed source lands under the inheriting prim, a
class-namespace rename so one longest-prefix ApplyTarget does inherit-then-
relocate). The 2 below still miss:

| Case | remaining residual |
|---|---|
| `TrickySpookyVariantSelectionInClass` `/Char/Anim/LeftLeg`/`RightLeg` | `RightLeg` picks the WRONG variant (next `avarFor1LegStyle` vs pxr `avarFor2LegStyle`) — the variant selection overridden on `SymLegRig` must propagate through an INHERIT (LeftLegRig inherits SymLeg) into the relocated prim; "spooky" ancestral-selection-through-inherit (pxr's own comment: the TrickySpookyVariantSelection fix is insufficient). |
| `TrickyMultipleRelocationsAndClasses2` `/CharRig/Anim/Legs/LHip/Knee` | `JointBlend` (implied-class over at the class `.../SymLegRig/.../Seg2`) missing at the relocated `Knee`. IMP2-confirmed: the b2fc9a9d0 implied-class relocate-chaining ALREADY gives the implied opinion the RIGHT map (it has the pair `.../SymLegRig/.../Seg2 → /CharRig/Anim/Legs/LHip/Knee`). The problem is the ATTACHMENT POINT: the implied opinion is anchored under the inheriting prim `.../LLegRig` (destination), but `Knee` is composed via a CHAINED relocate in the `Anim` subtree, so the composed walk for Knee never pulls this LLegRig-anchored implied opinion. Needs the implied opinion threaded through the chained-relocate CONTENT resolution (not just the map) — deeper than the connection-target fix. Relaxing the chained-exclusion rule breaks 5 relocate cases and doesn't fix this. |

**Note:** the `custom`-qualifier residual that used to dominate these 3 diffs was a
GENERAL flatten-writer bug (not relocate-specific) — `emit_custom` defaulted false
so `next_usdcat -f` dropped `custom` even on a plain local/referenced attribute.
Fixed for the composed-stage path (commit f28a7574f); pxr always emits it.

**Landed architecture (keep):** `ComposedRelocatedContent` derives arrival CONTENT
from the source's **composed parent** (`SourcesForSite(0, composed_src_parent)` +
`DeriveChildSources` departure-suppressed + salt-before-`ExpandList` + redirect
rename `src_root -> child_composed`). The **key fix vs the earlier reverts** was
`ArcOnlyMapping` (strip the stack's relocate-rename pairs from `ps.map_idx` before
mapping src to root — otherwise `Apply` sends src to its own dst, circular) and
gating to the equivalence-safe shapes instead of chasing strict Src-list identity
(functional equivalence = the flatten gate). The 3 remaining cases need the branch
EXTENDED to the excluded shapes (implied-class site mapping / spooky-inherit
selection) WITHOUT breaking the fall-back canaries.

**Superseded — earlier reverted attempts:** threading `arc_chain` into the isolated
seed (baz still absent) and the un-gated composed-parent rewrite (crashed +
broke 13 passing) are both obsolete; the landed approach uses the reentrancy guards
and `ArcOnlyMapping` they lacked.

**Next step (the real task):** the 3 residuals above are each a distinct deep
interaction (connection-target-remap-through-relocate; spooky-variant-through-
inherit; implied-class-through-chained-relocate) — NOT a shared applicability
widening (the chained rule is load-bearing, proven empirically). Treat each as its
own sub-arc. Extend `ComposedRelocatedContent` / `ComposedRelocApplicable` only
where an isolated experiment proves gate-safe. A useful debugging aid:
re-add the temporary A/B equivalence harness (compute both `Isolated`- and
`Composed`-RelocatedContent, diff Src lists to stderr keyed by `child_composed`) —
it was how the current fix's boundaries were found. Fall-back canaries that MUST
stay green while widening applicability: `BasicRelocateToAnimInterface`,
`SubrootReferenceAndRelocates`, `ErrorOpinionAtRelocationSource`,
`TrickyInheritsAndRelocates`/`...ToNewRootPrim`, `TrickyMultipleRelocations`/`2`/`3`/
`4`/`5`, `RelocateToNone`, `TrickyVariantOverrideOfRelocatedPrim`,
`TypicalReferenceToChargroupWithRename`, `TrickySpecializesAndRelocates`,
`ErrorInvalidConflictingRelocates`.

### B. Per-arc scoping — 1 case REMAINS (was 4; 2 FIXED, 1 was cluster A)
**FIXED (arc-origin target remap, commit cbd00dd98):** `ErrorInvalidPreRelocate-
TargetPath` + `BasicRelocateToAnimInterfaceAsNewRootPrim` — a rel/connection
target authored IN the relocating layer pointing at a pre-relocate path is now
KEPT (mapped up through outer arcs), while a target from BENEATH is still
relocated. The model: every source composed in `ComposeInto` (cache-compose.inc)
is a site-own opinion of `s.stack_idx`, so its target-remap uses `ArcOnlyMapping(s)`
(that stack's OWN relocate renames stripped; ancestor stacks' relocates carried
through the arc are kept). This is the per-opinion arc-origin distinction — a
relocate skips its own layer's opinions. (`ErrorArcCycle` was already fixed by the
composed-parent arc.)
- `RelocatePrimsWithSameName` `/ChainedReferences/Child_1` (STILL FAILING): EMPTY
  stack -> `over`. RPWS-instrumented content Srcs: `ChainedRef_1/Child` (stk0,
  INTERNAL ref — same stack, must be excluded) + `base.usd/Base/Child` (stk2 — must
  have DEPARTED via the chained `/ChainedRef_2/Child->Child_2` relocate). next
  applies neither (TWO coupled rules). CONTENT-side arc-origin ATTEMPTED (reverted):
  a blanket "suppress content sources with `stack_idx == ps.stack_idx`" broke 4
  cases (TrickyInheritsAndRelocates5, TrickyLocalClassHierarchyWithRelocates,
  TrickyMultipleRelocations4, TrickySpookyInheritsInSymmetricBrowRig) AND didn't fix
  this one. Blanket same-stack is WRONG: some same-stack sources DELIVER legitimate
  cross-stack content (TrickyMultipleRelocations2 `over Model_2 {def Rig references
  rig.usd}`). Unlike the target-side (clean per-Src signal), the content-side needs
  to know whether a same-stack source's content ORIGINATES locally vs is delivered
  through a chain to ANOTHER stack — true per-opinion origin tracking through the
  composition graph, PLUS chained-departure-through-internal-ref-chains. This is the
  genuine "real model addition" — high risk, deferred.

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

### E. Connection through inherit into a relocated-instance-context prim — 1 case
`ErrorInvalidInstanceTargetPath`: the `ConnectionToLocalClass` half now MATCHES
pxr (Instance_1/2 self-target drop works). Residual is the BrowRig half: there are
THREE `BrowInnUDPosBrowInnUDNeg_sculpt` prims (one direct under BrowRig, one under
the relocated `LBrow`, one under the relocated `RBrow`), each inheriting a class
with `amount.connect`. next composes `amount.connect` ONLY on the direct one (with
correctly-relocated targets `RBrow.InnUD`/`LBrow.InnUD` — the b2fc9a9d0 fix works
there); the two under the relocated LBrow/RBrow instances are MISSING it entirely
(same "opinion doesn't reach the relocated-context prim" as Classes2). Also pxr
keeps the SELF-instance target at its PRE-relocate path (LBrow's sculpt →
`/FaceRig/BrowRig/Anim/LBrow.InnUD`, un-relocated) while relocating the OTHER
instance's target — the arc-origin/local-target-keep nuance shared with cluster B.

## Recommended order
1. **A tail** (3 cases) — EXTEND the landed composed-parent branch to the
   implied-class-site-mapping and spooky-inherit-selection shapes (see §A). The
   architecture is in; this is widening `ComposedRelocApplicable` + the derivation.
2. **C** (specialize order) — 1 case, self-contained once the pxr order rule is
   modeled from pcp.txt.
3. **B** (per-arc scoping / arc-origin tracking) — 4 cases but a shared model
   addition; D and E likely fall out of A+B.

## Already-landed this campaign (do NOT redo)
Composed-parent relocated-content derivation (`ComposedRelocatedContent` +
`ArcOnlyMapping` + reentrancy guards, `9748515ec` — 4 cases),
composed-namespace relocate keying redesign (`27a34d5c1`), arrival opinion-strength
(defer content + incremental collision-shadow, `8252db49d`), same-stack
implied-specialize (`dfcd318f1`), salted-earth suppression (`0e0f9bba0`),
chained-relocation child collapse (`11a445865`), ancestral cycle detection
(`a21bfa213`, `24662aec0`), prototype numbering + instanceable (`a3de0942f`),
implied-class chain order (`8d27a577d`). See `git log` and memory for details.

## Resuming prompt (paste into a fresh session)

> Continue the `src/next` pcp composition burn-down. Read
> `doc/next-pcp-relocate-remaining.md` first — the composed-parent relocated-content
> derivation is LANDED (commit 9748515ec, `ComposedRelocatedContent` /
> `ComposedRelocApplicable` / `ArcOnlyMapping` in `cache-arc-expansion.inc`), fixing
> 4 cases with 0 regressions. 9 untagged cases remain. Start with the **Cluster A
> tail** (3 cases: `TrickySpookyVariantSelectionInClass`,
> `TrickyConnectionToRelocatedAttribute`, `TrickyMultipleRelocationsAndClasses2`) —
> EXTEND the composed branch to the excluded shapes (implied-class site mapped
> through the relocate; spooky variant-selection carried through an inherit) WITHOUT
> breaking the fall-back canaries listed in §A. The A/B Src-list equivalence harness
> (compute Isolated- vs Composed-RelocatedContent, diff to stderr) is the debugging
> aid — re-add it temporarily behind a compile flag. Model every change against the
> per-case `pcp.txt` prim stack. Hard bar: 753 pass / 0 FAIL flatten, supplemental
> 138, build-next 36/36, main 37/37, roundtrip 222/1 — revert anything that can't
> be reconciled. Commit per case with the gate green; prune passing entries from
> `tests/next/next-pxr-flatten-xfail.txt`.
