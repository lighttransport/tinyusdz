# MJCF → USD → MJCF roundtrip

Verifies that a MuJoCo model (MJCF) survives a trip through USD Physics and back,
using the TinyUSDZ WASM build. The pipeline drives two Node CLIs plus a runner:

```
MJCF  ──cli/urdf-to-usd.js──▶  USD (PhysicsScene + Mjc* + Newton* schemas)
                                   │
                                   └──cli/usd-to-mjcf.js──▶  MJCF (+ placeholder meshes)
```

## Components

| File | Role |
| --- | --- |
| `cli/urdf-to-usd.js` | Forward leg. Parses URDF **or** MJCF (`--input-format mjcf`), registers mesh geometry via `setVisualMesh`/`setCollisionMesh` (binary, by `meshRef`), calls `createURDFPhysicsScene`, then exports USDA/USDC/USDZ. |
| `cli/usd-to-mjcf.js` | Return leg. Loads a USD stage, calls `extractPhysicsSceneJSON` for structure + `getMesh()` for real vertex data, rebuilds the kinematic tree, and emits MJCF with real `.obj` meshes. |
| `run-mjcf-roundtrip.sh` | Runner. Drives both legs over the [`mujoco_menagerie`](https://github.com/google-deepmind/mujoco_menagerie) dataset and compares the body/joint counts that survived the trip. |
| `tests/screenshot-urdf-batch.mjs` | **Visual** verification. Drives the real `urdf.html` web demo in headless Chrome (Puppeteer): index assets, import MJCF (source view), click *URDF/MJCF → USD* (converted view), screenshot the split-view comparison per robot. |

## Visual verification (screenshots)

The count-based roundtrip can't catch geometry/placement regressions; the screenshotter
renders the actual conversion in the browser. It spawns a throwaway vite dev server,
drives `urdf.html`, and captures the source-vs-USD split view for each robot.

```bash
cd web/js
# Curated set -> tests/screenshots (run under xvfb so Chrome has a display for WebGL):
xvfb-run -a node tests/screenshot-urdf-batch.mjs
npm run screenshot:urdf            # same, via package script
xvfb-run -a node tests/screenshot-urdf-batch.mjs --all --out /tmp/shots
xvfb-run -a node tests/screenshot-urdf-batch.mjs path/to/robot.xml
```

Options: `--out <dir>` (default `tests/screenshots`), `--menagerie <dir>`, `--all`,
`--port`, `--width`/`--height`, `--headful`, `--home-pose` (capture the home keyframe
pose), `--hw` (real GPU rendering), `--timeout` (default 180s). Output files are
`<robot_dir>__<model>.png` (and `*.FAIL.png` for failures).

By default Chrome renders with **software WebGL** (`--use-angle=swiftshader`) so no GPU
is needed and it runs headless under `xvfb-run`. Software rendering is very slow for
large scenes, though — `robot_soccer_kit` (65 links / **363 meshes** / ~104 MB USDC)
times out under SwiftShader but completes quickly with `--hw`. For `--hw`, run on a
GPU-backed display and **not** under xvfb, e.g.:

```bash
DISPLAY=:1 node tests/screenshot-urdf-batch.mjs --all --hw
```

Models that split the robot across many `<include>` files (e.g. `ms_human_700`, 44
includes) and very large exports (`apptronik_apollo` ~111 MB) are handled: the runner
uploads `.xml` includes, and the demo raises the WASM USDC writer cap
(`setUSDCExportLimitMB`) so big scenes export.

Joints initialize to the MuJoCo default **qpos0 = `ref`** (the state the MuJoCo viewer
shows on load): the displayed joint value is each joint's `ref` and the robot sits in
its XML-authored rest configuration (geometric displacement = `qpos - ref` = 0). Joint
slider ranges are converted from the model's `<compiler angle>` to radians. This matches
the viewer exactly — e.g. cassie shows knee=-0.785, tarsus=1.01, the rest 0, with
left-hip-roll limited to [-0.262, 0.393] rad. The **"Home pose"** toggle in the Robot
Controls panel switches to the model's `<keyframe>` "home" pose (DOF-count aware:
ball=4/free=7/hinge=1, with `ref` handling); the screenshotter exposes it as
`--home-pose`.

### MuJoCo orientation specifiers

Both the demo (`urdf.js`) and the CLI (`cli/urdf-to-usd.js`) resolve the full set of
MuJoCo frame orientations — `quat`, `axisangle`, `euler` (honoring `<compiler eulerseq>`
and the lowercase/uppercase intrinsic/extrinsic convention), `xyaxes`, and `zaxis` —
and the `<compiler angle>` degree/radian unit. Missing this was why robots authored with
`xyaxes` bodies + degree `euler` (e.g. `agility_cassie`) rendered as exploded parts;
robots using `angle="radian"` + `quat` (e.g. ur5e) were unaffected.

## Running

```bash
cd web/js

# Curated representative set (arms, quadrupeds, humanoids, hands, a drone):
./run-mjcf-roundtrip.sh

# Every robot directory in the dataset (one primary MJCF each):
./run-mjcf-roundtrip.sh --all

# Specific models:
./run-mjcf-roundtrip.sh /path/to/ur5e.xml /path/to/panda.xml

# Also re-parse each emitted MJCF back through the forward leg (full closure):
./run-mjcf-roundtrip.sh --closure
```

Environment overrides: `MENAGERIE_DIR` (default `/mnt/nvme02/work/mujoco_menagerie`),
`OUT_DIR` (default `/tmp/mjcf-roundtrip`), `MAX_USDC_MB` (default 2048), `MAX_MEM_MB`
(default 4096) — the last two raise the WASM USDC writer caps for large models. Each
row prints `forward→return` counts:

```
robot                      model                     links   joints  visuals   collis  result
universal_robots_ur5e      ur5e                      7→7    6→6  20→20    9→9  PASS
```

A roundtrip **PASSes** when the kinematic structure is preserved:
`forward links == return bodies` **and** `forward joints == return joints`.
With `--closure`, the emitted MJCF is also re-parsed and its counts must match.

## What round-trips, and what does not

Carried faithfully through USD and back:

- Link/body hierarchy (kinematic tree from `physics:body0`/`body1`)
- Joints: revolute→`hinge`, prismatic→`slide`, fixed→nested body, plus axis and
  limits (degrees→radians); fixed joints count as joint edges but emit no `<joint>`
- Primitive collision shapes (sphere/box/cylinder/capsule/plane), including their
  **orientation** — the prim transform's rotation is emitted as a MuJoCo `quat` (wxyz);
  e.g. ur5e's `quat="1 1 0 0"` collision capsules round-trip as `0.707 0.707 0 0`
- Inertials (mass, COM, diagonal inertia) and Newton actuators

- **Mesh vertex geometry**, recovered from the render scene. `extractPhysicsSceneJSON`
  only summarizes meshes (`pointCount`/`faceCount`), so the return leg additionally
  reads `getMesh()` (points + `faceVertexIndices`/`faceVertexCounts`, link-local with
  the prim transform baked) keyed by `absPath`, and writes real `.obj` files. A
  placeholder cube is emitted only as a fallback when a render mesh is missing.

## How mesh data crosses the WASM boundary (binary, not JSON)

Mesh geometry is **not** serialized into JSON in either direction — that would bloat
the payload ~4–5× and hit V8's max string length (~512M chars) on dense models:

- **Forward (JS → WASM):** `urdf-to-usd.js` registers each tessellated mesh via
  `setVisualMesh(meshRef, Float32Array positions, normals, uvs, Int32Array indices)`
  and references it by `meshRef` in the (tiny) structural JSON. The binding copies the
  typed arrays straight into the WASM heap (`typed_memory_view` + `TypedArray.set`).
- **Return (WASM → JS):** `usd-to-mjcf.js` reads `getMesh(i)`, whose `points`/indices
  are `typed_memory_view` windows into the heap, and copies them out before `delete()`.

Both directions are single-`memcpy`, no string, no `JSON.parse`. base64 would be a
regression (still a string, same ceiling, plus encode/decode cost).

## Forward-leg MJCF parser notes

`urdf-to-usd.js` resolves MuJoCo `<default>` class inheritance and `childclass`
propagation (`parseMujocoDefaults` + `resolveElementAttrs`), so geoms/joints that
inherit `type`/`group`/`contype`/limits from a `<default class="...">` block — or via
a body's `childclass` — are classified and tessellated correctly. This is what makes
visual meshes round-trip on models that tag visuals only through classes (e.g.
`agility_cassie`, `shadow_hand`, `apptronik_apollo` went from 0 recognized visuals to
their full visual-mesh count). Models without a `<compiler meshdir>` are resolved via
an `assets/` fallback; truly missing meshes are skipped under `--allow-missing`.

Mesh-dense models exceed the **WASM USDC writer's safety caps** (`crate-writer.hh`:
`max_file_size_bytes = 100 MB`, `max_memory_bytes = 256 MB` for WASM; 1 GB / 32 GB for
native). `apptronik_apollo`, for example, serializes to ~111 MB of USDC. These caps are
now overridable: the binding exposes `setUSDCExportLimitMB(fileMB, memMB)`, the CLI
takes `--max-usdc-mb` / `--max-mem-mb`, and the runner sets them via `MAX_USDC_MB`
(default 2048) / `MAX_MEM_MB` (default 4096) so such models pass. `ALLOW_MEMORY_GROWTH`
is enabled, so the wasm heap grows to fit. Leave the defaults low for untrusted input;
raise them only for trusted large assets like these robots.
