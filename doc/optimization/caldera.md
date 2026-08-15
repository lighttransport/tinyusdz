# Caldera USD hierarchy

This document describes the authored USD layer and namespace hierarchy used by
the Caldera dataset. The dataset is external to the repository; the paths below
are relative to `/mnt/disk1/data/caldera`.

## Layer hierarchy

The entry layer is `caldera.usda`. It is a small USDA control layer that
combines the map, cameras, player breadcrumbs, and endpoint data, then authors
per-district variant selections:

```text
caldera.usda
├── subLayers
│   ├── map_source/mp_wz_island.usd
│   ├── layers/cameras.usd
│   ├── layers/breadcrumbs.usd
│   └── layers/endpoints.usd
└── /world                                      (over)
    └── /world/mp_wz_island                      (over)
        └── /world/mp_wz_island/mp_wz_island_paths (over)
            └── /world/mp_wz_island/mp_wz_island_geo (over)
                ├── district roots                       (districtLod = "proxy")
                └── /st_main
                    ├── /st_b  ─┐
                    ├── /st_c  │
                    ├── /st_d  │
                    ├── /st_e  │
                    ├── /st_f  │
                    ├── /st_g  │
                    ├── /st_h  │
                    ├── /st_i  │  districtLod = "proxy"
                    ├── /st_j  │
                    ├── /st_k  │
                    ├── /st_l  │
                    ├── /st_n  │
                    ├── /st_o  │
                    └── /st_p  ┘
```

The composed stage also contains the layer-owned children below the map root.
These are not district geometry, but they participate in the same root stage:

```text
/world/mp_wz_island
├── mp_wz_island_audio
├── mp_wz_island_lighting
├── mp_wz_island_paths
├── mp_wz_island_s4_cfx
├── mp_wz_island_skybox
├── map_redeploy
└── info_player_start_4                         (guide Cube)
```

`cameras.usd` contributes `/cameras/*` Camera prims, including district
overview cameras such as `map_airfield_overview` and
`phospate_mine_overview`. `breadcrumbs.usd` and `endpoints.usd` contribute
large `Points`/guide-purpose data used for navigation and gameplay display.

## District roots

The root layer authors `districtLod = "proxy"` on the following map districts:

```text
map_phosphate_mine    map_tile_p            map_infil_ch3
map_gulags            map_tile_d            map_tile_c
map_exfil_ch3         map_tile_e            map_tile_f
map_tile_h            map_tile_j            map_tile_k
map_tile_l            map_tile_o            map_ruins
mv_intel              map_vista             map_village
map_tile_i            map_tile_b            map_airstrip
map_docks             map_tile_g            map_beachhead
map_subpen            map_tile_n            map_agricultural_center
map_airfield          map_arsenal           map_capital
map_caldera
```

The `st_*` roots are grouped under `st_main` rather than being top-level map
districts. The corresponding source layers are primarily under:

```text
map_source/prefabs/br/wz_vg/mp_wz_island/superterrrain/st_main.usd
map_source/prefabs/br/wz_vg/mp_wz_island/superterrrain/season_4/st_*.usd
```

Other district and prefab layers are organized below:

```text
map_source/prefabs/br/wz_vg/mp_wz_island/
├── chem_factory/
├── commercial/
├── industrial/
├── military/
├── residential/
├── urban/
├── infrastructure/
├── map_airfield/
├── map_arsenal/
├── map_capital/
├── map_docks/
├── map_phosphate_mine/
├── map_subpen/
├── map_tile_*/
├── map_village/
├── map_ruins/
└── superterrrain/
```

These directories contain both standalone sub-scenes and lower-level building
layers. A file in a prefab directory is not necessarily a scene root; many
files are nested component layers referenced by a parent prefab.

## Geometry and composition boundary

Each district/prefab generally follows this composition shape:

```text
district root layer
├── local Xform / model namespace
├── references to prefab/build-kit layers
├── variants
│   └── districtLod = proxy | full
├── payloads
│   └── deferred render or splined geometry
└── nested references/payloads
    └── assets/xmodel/...
```

The main geometry repositories are:

```text
assets/xmodel/generated_proxies/     proxy-purpose district geometry
assets/xmodel/generated_splined/     large spline/payload geometry
assets/xmodel/build_kits/             reusable construction assemblies
assets/xmodel/vehicles/               vehicle geometry
assets/xmodel/props/                  prop geometry
```

Caldera uses many `..`-relative asset paths. For example, a prefab under
`map_source/prefabs/...` can reference a payload under `assets/xmodel/...` by
walking up from the authoring layer directory. The path must be resolved
relative to the layer that authored the arc, not relative to `caldera.usda`.

The root-authored proxy selections are important: a global variant override of
`districtLod = "full"` does not replace the per-district selections authored
by `caldera.usda`. To test full geometry, use a wrapper layer or explicitly
edit the district selections.

## Scale and optimization relevance

The external inventory contains approximately 63,685 USD files and about 7.03
GB of USD layer data. The composition scan reports approximately:

| Item | Scale |
|---|---:|
| payload arcs | 17,299 |
| variant selections/sets | 8,066 |
| references | 4,252 |
| PointInstancers | 14 |
| generated spline files | 25k+ files / ~4.9 GB |
| generated proxy data | ~177 MB |

The expensive cold-load path is concentrated in the composed namespace below
`/world/mp_wz_island/mp_wz_island_paths/mp_wz_island_geo`. Deferred payloads avoid
loading all render geometry, but composition still has to resolve the namespace,
variants, references, payload state, and opinions needed to construct the
authoritative Stage.

Measured examples from the cold-process viewer path:

| Scene | Composition | First useful frame | Full presentation |
|---|---:|---:|---:|
| `superterrrain/st_main.usd` | ~95 s | composition-bound | often exceeds 2 min |
| `chem_factory/chem_factory_01.usd` | ~20.9 s | ~36.2 s | ~40.1 s |
| `season_4/st_j.usd` | ~6.1 s | ~17.0 s | ~17.4 s |
| `map_airfield/airfield_grounds.usd` | ~3.5 s | ~6.0 s | ~6.1 s |
| `commercial/hotel_01/ext_01.usd` | ~0.8 s | ~3.9 s | ~3.9 s |

### Implemented cold-path changes (2026-08-15)

The next-composition path now lets parallel warm workers borrow the immutable
source cache instead of deep-copying it per worker, keeps broad namespace
fanout enabled, reserves the flattened layer from the discovered source count,
uses direct source-cache hits during stage construction, avoids identity target
remappers and no-relocate mapping copies, and dynamically balances the opinion
fill pass. Tusdview also releases the composition cache before render-data
extraction and provides `--full-fidelity --quit-after-convert` for repeatable
CPU-only gates.

On `chem_factory/chem_factory_01.usd` with cold process, deferred payloads,
8 composition/conversion workers, GL, and full fidelity, composition improved
from about 20.9 s to 16.83 s; first useful frame improved from about 36.2 s to
29.32 s; full CPU conversion completed in 31.89 s. Peak RSS was 4.21 GiB. The
remaining composition costs are approximately 4.42 s source warming, 0.94 s
source-cache merge, 2.66 s structure construction, and 6.89 s opinion fill.
This representative root is about 1.25x faster end-to-end, so the 2x target is
not yet claimed for the worst `st_main.usd` root.

## Top parse-time prefab/root candidates

The following are the ten slowest successful **prefab-candidate root files** in
the full 63,685-file inventory. The measurement is a cold parse/inventory
measurement, not a composed viewer load: `parse s` is layer read/decode time
and `stage bytes` is the source file's size. Leaf geometry files were excluded
because they are payload targets rather than useful entry points.

| Rank | Root layer | Parse s | Stage bytes |
|---:|---|---:|---:|
| 1 | `map_airfield/airfield_grounds.usd` | 1.81 | 117,459,358 |
| 2 | `commercial/hotel_01/ext_01.usd` | 1.75 | 120,453,973 |
| 3 | `urban/control_tower_02/ext_01.usd` | 1.37 | 91,028,671 |
| 4 | `superterrrain/season_4/st_k.usd` | 1.22 | 78,298,944 |
| 5 | `map_capital/ground/cliffs_set_river_01.usd` | 1.07 | 74,347,503 |
| 6 | `industrial/power_plant_01/int_01.usd` | 1.01 | 71,210,908 |
| 7 | `season_3/military/subpen_01/ext_01.usd` | 0.97 | 63,875,876 |
| 8 | `commercial/administration_01/geo_interior_01.usd` | 0.94 | 63,296,141 |
| 9 | `map_arsenal/drydock/drydock_scaffolding_01.usd` | 0.94 | 66,404,732 |
| 10 | `urban/courthouse_01/int_01.usd` | 0.87 | 61,439,070 |

These roots are optimization targets for source decode and composition-cache
reuse. They are not necessarily the same as the slowest full scenes: a small
root can expand into many references and payloads, while a large leaf can stay
deferred. The most valuable follow-up is to correlate each root with its
composed `source_misses`, variant expansion, and payload count.

For repeatable measurements, use:

```sh
# Parse/memory inventory for every USD layer.
CALDERA_ROOT=/mnt/disk1/data/caldera \
  examples/tusdview/tests/run-caldera-inventory.sh

# Full cold viewer timings for selected composed roots.
CALDERA_ROOT=/mnt/disk1/data/caldera \
  TIMEOUT_SECS=180 \
  examples/tusdview/tests/run-caldera-matrix.sh
```

The inventory measures leaf layers for parser cost but does not treat every
component file as a standalone scene. The viewer matrix is the authoritative
measurement for namespace composition, render extraction, geometry estimation,
first useful frame, and final upload.
