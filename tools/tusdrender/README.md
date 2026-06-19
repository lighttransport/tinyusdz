# tusdrender

CPU preview ray-tracer for USD scenes (LightRT BVH + the `next` lazy USDC
loader). Renders large, reference- and instance-composed scenes directly, within
a bounded memory budget.

## Quick start

```sh
tusdrender scene.usd out.png -rtPreview -w 960 -height 540 -autoframe
```

Common flags:

| flag | meaning |
|------|---------|
| `-rtPreview` | ray-traced preview (the `next` loader; default for USDC) |
| `-w N -height N` | image size (`-height` omitted → from camera aspect) |
| `-autoframe` | usdrecord-style auto camera framing |
| `-camera <path>` | render through a named `UsdGeomCamera` |
| `-mask <prim,...>` | restrict to these prim subtrees |
| `-complexity low\|med\|high\|veryhigh` | subdivision preset |
| `-maxMem <GiB>` | memory cap override (default `min(32, 0.5·MemAvailable)`) |
| `-stats` | print mesh/triangle/memory/timing stats |

## Composition, instancing, memory

* **Direct composition** — reference/payload/sublayer/variant/instancing arcs are
  resolved in place (full PCP engine), so reference-composed scenes (e.g. Caldera
  prefab stubs) render directly with no external `usdcat --flatten` step.
  Self-contained / pre-flattened inputs skip composition and render
  byte-identically to before.
* **Two-level (instanced) BVH** — native instances are placed by a LightRT TLAS
  with each prototype's geometry built once (BLAS), so a scene with millions of
  *visible* triangles only stores the *unique* prototype geometry.
* **Memory cap** — a process budget of `min(32 GiB, 0.5 × system MemAvailable)`
  (override with `-maxMem`). When a scene would exceed it, tusdrender aborts
  cleanly with an actionable message (raise `-maxMem`, narrow with `-mask`, or
  lower `-complexity`) instead of being OOM-killed.

## Validated benchmark — Activision Caldera maps

All six maps rendered directly from their raw `.usd` (no pre-flatten), 320×180,
`-autoframe`, on a host with ~49 GiB available (auto cap 24.5 GiB). "visible" is
the instance-expanded triangle count actually traced; "unique" is the prototype
geometry stored once.

| scene          | visible tris | unique stored | dedup | instances | peak RSS | wall  |
|----------------|--------------|---------------|-------|-----------|----------|-------|
| restaurant     | 2.4 M        | 764 K         | 3.2×  | 1.8 K     | 0.4 GB   | 1.2 s |
| power_station  | 19.8 M       | 5.7 M         | 3.5×  | 11 K      | 2.4 GB   | 6.9 s |
| beachhead      | 160 M        | 19.9 M        | 8.0×  | 105 K     | 9.9 GB   | 39 s  |
| phosphate_mine | 126 M        | 28.2 M        | 4.5×  | 73 K      | 11.7 GB  | 41 s  |
| airfield       | 163 M        | 35.3 M        | 4.6×  | 104 K     | 17.1 GB  | 64 s  |
| capital        | **195.7 M**  | 43.1 M        | 4.5×  | 136 K     | 21.3 GB  | 81 s  |

Notes:

* Instancing dedup is what keeps these in memory: capital's 195.7 M visible
  triangles would need ~40 GB as a flat array + BVH (which overflows the LightRT
  builder), but only 43.1 M unique are stored → 21.3 GB.
* These RSS figures scale with the host. On a 32 GiB machine the cap is ~16 GiB,
  so airfield/capital abort gracefully (use `-maxMem`/`-mask`) rather than
  OOM-killing.
