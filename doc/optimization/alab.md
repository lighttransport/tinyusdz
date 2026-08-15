# ALab USD hierarchy and cold-load profile

Dataset root: `/mnt/disk1/data/alab`. Measurements use the extracted merged
entry layer `/mnt/disk1/data/alab/_merged_ALab/entry.usda`, the `next` loader,
eight composition/conversion workers, preview cache disabled, and headless
OpenGL/llvmpipe. The merged tree is the correct local resolver root.

## Authored hierarchy

```text
_merged_ALab/entry.usda
└── subLayers: ALab set/department control layers
    └── /root
        └── /alab_set01
            ├── entity/<asset>                     # references to fragments
            │   ├── GEO / GEO_PROXY variants
            │   ├── payload: base | display_high | render_high | collision
            │   └── instanceable assembly references
            ├── baked_procedurals                   # animated/cache payloads
            │   ├── stoat.usd
            │   ├── clip.topology.usd / clip.manifest.usd / uvs.usd
            │   └── payload/stoat_body_main.<frame>.usd
            └── extras
                ├── alab_sdr_splat.usdc
                ├── alab_hdr_splat.usdc
                └── camera.usda

fragment/
├── geo/modelling/.../mesh/*.usd
├── geocache/animbase/<character>/render_high/cache/payload.usdc
├── geocache/layout/<character>/render_high/cache/payload.usdc
└── fxcache/.../payload/animated_data.<frame>.usdc
```

ALab is asset-centric: department layers are composed through subLayers,
entities reference reusable fragments, and variant sets select geometry LOD
before payload loading. `inherits`/`specializes` provide class opinions and
assembly references provide reuse. The merged tree contains 4,481 USD-family
files. A deferred root run composed 2,475 stage nodes and reported 764
deferred payload roots; the full run composed 24,466 nodes after expansion.

## Largest geometry and cache layers

| File | Size |
|---|---:|
| `usd/baked_procedurals/payload/stoat_body_main.1034.usd` | 3.45 GB |
| `usd/baked_procedurals/payload/stoat_body_main.1014.usd` | 3.45 GB |
| `usd/baked_procedurals/payload/stoat_body_main.1024.usd` | 3.45 GB |
| `usd/baked_procedurals/payload/stoat_body_main.1044.usd` | 3.45 GB |
| `usd/baked_procedurals/payload/stoat_body_main.1004.usd` | 3.45 GB |
| `usd/baked_procedurals/stoat.usd` | 2.18 GB |
| `usd/baked_procedurals/clip.topology.usd` | 993.1 MB |
| `usd/baked_procedurals/uvs.usd` | 930.4 MB |
| `extras/alab_sdr_splat.usdc` | 536.8 MB |
| `extras/alab_hdr_splat.usdc` | 382.4 MB |
| `baked_procedurals/clip.manifest.usd` | 223.2 MB |
| `baked_procedurals/remi.usd` | 147.7 MB |

The frame-numbered stoat files are the clearest cold-load hazard: selecting a
working frame must not cause all frame payloads to be read. Cache and variant
selection should remain lazy until the selected character/LOD is visible.

## Cold measurements

| Mode | Compose | First useful frame | Full presentation | Peak RSS | Result |
|---|---:|---:|---:|---:|---|
| deferred payloads | 0.926 s | 1.671 s | 1.671 s | 408 MiB | 766 proxy meshes, 9,568 tris |
| full payloads | 7.043 s | 7.963 s | 48.888 s | 3.07 GiB | 4,222 meshes, 13.21M unique tris |

Full-payload extraction adds 1.216 s for PointInstancer data, 12.892 s for
points/curves conversion, and 3.830 s for native-instance extraction. Mesh
conversion/batching takes 4.990 s; finalization takes 0.695 s. The full scene
has 4,255 draws, 11,532 instances, 922 materials, and 512 texture references.
One `alfro_tail_main/curves` conversion failed because its `curves.points`
data is invalid; this is a data/validation issue, not a performance win to
hide in the timing.

The deferred path is already close to the desired interactive target. Its
warm/build sequence reported 10,281 source misses and 14,185 source hits, with
24,466 final nodes in the full path. This points to source discovery and
payload admission as the next cold-start targets, rather than mesh conversion
for proxy mode.

## Optimization opportunities

1. Keep all frame-numbered baked-procedural payloads deferred and select one
   frame before opening geometry arrays.
2. Cache the entity/fragment variant decision independently from payload data;
   this makes repeated LOD changes cheap without retaining multi-GB arrays.
3. Add a lightweight proxy index for the 764 deferred roots and their bounds so
   the scene can frame and draw proxies without opening cache layers.
4. Parallelize or batch points/curves conversion. It is the largest measured
   full-load extraction phase at 12.9 s.
5. Preserve native instances through mesh conversion. The full run reports
   11,532 instances, so flattening them into duplicate geometry would increase
   memory and upload cost substantially.
6. Validate the bad `alfro_tail_main` curves payload during source admission;
   quarantine it early instead of discovering it after the 3 GiB scene is
   converted.
