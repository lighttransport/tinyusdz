# Moana Island USD hierarchy and cold-load profile

Dataset root: `/mnt/disk1/data/island`. Measurements use the local
`usd/island.usda` entry layer, the `next` loader, eight composition/conversion
workers, preview cache disabled, and the OpenGL/llvmpipe headless path. GPU
timings are therefore not representative of a discrete GPU.

## Authored hierarchy

```text
usd/island.usda                         # 21.9 KiB USDA entry layer
└── /island                             # Xform, kind = assembly
    ├── 20 referenced element roots     # coral, beach, dunes, mountains, flora
    ├── cameras and approximately 30 lights
    └── shotCam / birdseyeCam / beachCam and other camera data

elements/<element>/element.usda
└── instance.usda
    └── geometry.usda
        ├── payload: model.usd
        └── payload: xgenInstances/*.usd and archives/*.usd
```

The entry layer is structurally small: each element is a reference, and heavy
geometry is behind payload arcs. The scene uses scenegraph instancing
(`instanceable = true`), not a top-level `PointInstancer`. The existing
composition inventory reports 3,924 references, 214 payloads, 101 component
instances, and 60 unique prototypes. XGen instancing is baked into binary USD
archives, so it does not appear as a PointInstancer in the root USDA.

The dataset contains 937 USD-family files in the extracted tree. The separate
flattened file `openusd-island-flatten.usdc` is about 2.64 GB and is a format
stress case, not the interactive entry point.

## Largest payload targets

| File | Size |
|---|---:|
| `elements/isBeach/xgenInstances/xgGroundCover.usd` | 683.5 MB |
| `elements/isMountainB/xgenInstances/xgLowGrowth__mountainb0004_geo.usd` | 298.5 MB |
| `elements/isMountainB/xgenInstances/xgLowGrowth__mountainb0003_geo.usd` | 174.6 MB |
| `elements/isMountainB/xgenInstances/xgLowGrowth__mountainb0002_geo.usd` | 127.4 MB |
| `elements/isMountainB/xgenInstances/xgLowGrowth__mountainb0001_geo.usd` | 121.3 MB |
| `elements/osOcean/model.usd` | 109.1 MB |
| `elements/isIronwoodA1/xgenInstances/xgBonsai_curves.usd` | 99.7 MB |
| `elements/isIronwoodB/isIronwoodB_curves.usd` | 97.5 MB |
| `elements/isCoastline/xgenInstances/xgPalmDebris.usd` | 81.0 MB |
| `elements/isMountainB/xgenInstances/archives/xgBreadFruit_archiveBreadFruitBaked.usd` | 74.5 MB |

These files explain why a full-payload cold load differs sharply from a
deferred proxy load. The payload boundary is the primary optimization control;
source-cache work is secondary once these archives are opened.

## Cold measurements

| Mode | Compose | First useful frame | Full presentation | Peak RSS | Result |
|---|---:|---:|---:|---:|---|
| deferred payloads | 0.014 s | 3.142 s | 3.142 s | 345 MiB | 1 proxy mesh, 108 tris |
| full payloads (baseline) | 29.173 s | 31.383 s | 85.829 s CPU conversion | 13.26 GiB | completed CPU conversion |

The deferred run discovered 187 sources and deferred nine payload roots. Its
stage estimate was only 1.1 MiB after composition. The repeated 200-second-
supervised full run reached the composition preview at 13.322 s, loaded
2,496.8 MiB of layers, and reached the first useful frame at 31.383 s.
Point-instancer extraction took 27.708 s, curves took 5.983 s, native instances
took 4.237 s, and mesh conversion took 8.268 s. CPU conversion completed at
85.829 s with a measured 13,577 MiB peak RSS. This baseline used the Island
interactive profile's 20,000-source-mesh cap, so it is not a full-fidelity
geometry baseline.

The optimized path releases composition caches before extraction, releases
PointInstancer source arrays after packing, uses larger bounded transform
chunks, computes transformed bounds from center/extents, and frees CPU instance
transforms after raster upload when raster LOD is disabled. The last item can
remove roughly 1.96 GiB of duplicate CPU transform storage for this scene while
retaining the GPU copy. A subsequent full-fidelity run reached composition in
28.234 s at 5,718 MiB RSS before the external benchmark supervisor ended the
process; no final full-fidelity speed or peak-RSS claim is made from that
partial run.

Element-level measurements identify the likely conversion hot spots:
`isDunesB` has 2.14 s source discovery and 14.47 s extraction, `isCoral` has
8.60 s compose and 14.47 s conversion, and `osOcean` spends 12.64 s in
conversion despite a small composition phase.

## Optimization opportunities

1. Keep the entry layer and element wrappers lazy. Do not resolve all XGen
   payloads merely to obtain bounds or camera data.
2. Add a cheap bounds/preview index for the 20 element roots so the viewer can
   frame the scene without opening `xgGroundCover` or mountain archives.
3. Treat XGen archives as independently budgeted chunks. The full run crosses
   5 GiB RSS before upload; per-element admission would avoid the spike.
4. Reuse source discovery and prototype registration across element payloads;
   the repeated `instance.usda -> geometry.usda` pattern is a good cache-key
   target.
5. Profile `isCoral`, `isDunesB`, and `osOcean` separately after payload
   admission. Their bottleneck is extraction/conversion, not root composition.
