# TinyUSDZ Blender Scripts

## TinyUSDZ Animation Clips Exporter (`tinyusdz_anim_clips.py`)

A single-file Blender addon (v1.1) that exports **multiple Blender Actions** as USD animation clips.  Blender's built-in USD exporter only bakes the single active Action into one `UsdSkelAnimation`.  This addon fills the gap by discovering every Action in the scene (active + NLA) and emitting clip-boundary metadata that downstream consumers (TinyUSDZ viewer, custom pipelines) can use to split the timeline back into discrete clips.

Two export workflows are provided:

| Workflow | Description |
|----------|-------------|
| **Single asset** (via USD Hook) | Blender's normal USD export runs; the hook appends metadata to the same file |
| **Separate files** (Operator) | One USD file per Action, following the Pixar HumanFemale pattern |

### Requirements

- Blender 5.0 or later (tested on 5.0.1)
- Blender's built-in USD I/O module (ships the `pxr` Python package)

---

### 1. Installation

#### Option A — Install via Blender UI

1. Open Blender.
2. **Edit > Preferences > Add-ons**.
3. Click the gear icon > **Install from Disk**.
4. Browse to `tinyusdz_anim_clips.py` and select it.
5. Blender copies the file into its per-user addons directory (`~/.config/blender/<ver>/scripts/addons/` on Linux).
6. Tick the checkbox next to **"TinyUSDZ Animation Clips Exporter"** to enable it.

#### Option B — Manual copy

```bash
# Linux
cp tinyusdz_anim_clips.py ~/.config/blender/5.0/scripts/addons/

# macOS
cp tinyusdz_anim_clips.py ~/Library/Application\ Support/Blender/5.0/scripts/addons/

# Windows
copy tinyusdz_anim_clips.py %APPDATA%\Blender Foundation\Blender\5.0\scripts\addons\
```

Then enable in **Edit > Preferences > Add-ons** (search for "TinyUSDZ").

#### Verify installation

After enabling, the Blender system console (`Window > Toggle System Console` on Windows; terminal on Linux/macOS) should print:

```
[TinyUSDZ] Animation Clips Exporter v(1, 1, 0) registered
```

You should also see a **TinyUSDZ** tab in the 3D Viewport sidebar (press **N**).

---

### 2. How the USDHook Mechanism Works

Blender 5.0+ exposes a `bpy.types.USDHook` base class.  Addons subclass it and register it via `bpy.utils.register_class()`.  Once registered, the hook is **automatically called** on every USD export — no manual checkbox is needed.

#### Lifecycle

```
User: File > Export > USD (.usda/.usdc/.usdz)
         │
         ▼
  Blender builds the USD stage
  (scene graph, geometry, materials, active-action animation)
         │
         ▼
  Blender calls  hook.on_export(export_context)
  for every registered USDHook subclass
         │
         ├─ export_context.get_stage()  → Usd.Stage (read-write)
         ├─ bpy.context / bpy.data      → full Blender access
         │
         ▼
  Hook writes custom attributes to the stage
         │
         ▼
  Blender serialises the stage to disk (.usda / .usdc)
```

Key points:

- `on_export()` fires **after** Blender has converted the entire scene but **before** the file is written to disk.  The hook receives a fully-populated `Usd.Stage` that it can modify in-place.
- The hook has **full access to `bpy.data`** (actions, NLA tracks, objects, armatures, etc.) alongside the USD stage.  This is what allows it to enumerate all Actions.
- If the hook returns `False`, the export is aborted.  The addon returns `True` on success, or `True` (with a log warning) when there is only 1 clip and no metadata is needed.
- Hooks are called for **every** `bpy.ops.wm.usd_export()` invocation, including those triggered programmatically by the separate-file operator.

---

### 3. Preparing the Blender Scene

The addon discovers clips by scanning **all objects** in `bpy.data.objects` for animation data:

| Source | How it is found |
|--------|-----------------|
| **Active Action** | `obj.animation_data.action` (the action currently driving the object) |
| **NLA strip Actions** | Each strip's `.action` in every NLA track on the object |

Actions are **deduplicated per object** — if the same Action appears as both the active action and in an NLA strip, it is counted once.

#### Supported object types

| Blender Type | USD Source Type | What gets animated |
|-------------|----------------|-------------------|
| `ARMATURE` | `SkelAnimation` | Joint rotations, translations, scales (via `UsdSkelAnimation`) |
| `CAMERA` | `XformOp` | Camera location, rotation (`xformOp:translate`, `xformOp:rotateXYZ`) |
| `LIGHT` | `XformOp` | Light location, rotation; energy/color exported by Blender natively as `inputs:intensity.timeSamples` etc. |
| `MESH` | `XformOp` | Object-level translate, rotate, scale (`xformOp:*`) |
| `EMPTY` | `XformOp` | Transform animation |

#### Typical scene setup

1. Create your objects (armature, camera, lights, meshes).
2. Animate them — each animation becomes a Blender **Action**.
3. **Push** completed Actions to NLA tracks:
   - In the Action Editor or Dopesheet, click the "shield" icon (Fake User) to prevent the Action from being purged.
   - Click the **Push Down** button (↓ arrow) to move the Action into an NLA strip.  You can mute the strip so it does not play.
4. Create a **new Action** on the same object for the next animation.
5. Repeat.  The current (active) Action and all NLA strip Actions are discovered by the addon.

> **Tip**: If you only have one Action on one object, the hook deliberately does **not** write metadata — there is nothing to split.  You need at least 2 Actions across all objects combined for the hook to activate.

---

### 4. Workflow 1 — Single Asset: Concatenated Timeline + Metadata (via USD Hook)

This is the simplest workflow.  Blender's standard USD export runs, and the hook appends clip metadata to the exported file.

#### Steps

1. **File > Export > Universal Scene Description** (.usda / .usdc / .usdz).
2. Enable **Animation** in the export dialog.
3. Set the frame range to cover at least the longest Action.
4. Click **Export**.
5. The hook fires, discovers all Actions, and writes metadata.

Console output:

```
[TinyUSDZ] Wrote 8 clip(s) to /root/CharacterRig:
  [0] MonkeySpin (XformOp, Mesh:AnimMonkey) frames 1-48
  [1] Walk (SkelAnimation, Armature:CharacterRig) frames 1-72
  [2] CamOrbit (XformOp, Camera:SceneCamera) frames 1-60
  ...
```

#### Metadata format (v1)

Attributes are written to the **first non-generated child prim** of the default prim.  (Blender's generated root Xform — tagged with `customData.Blender.generated=1` — is stripped on re-import, so writing there would lose the data.)

All multi-value fields use **`|` (pipe)** as separator.  Comma is avoided because action names may contain commas.

Full USD attribute prefix: `userProperties:tinyusdz:animClips:`

| Attribute suffix | USD Type | Description |
|-----------------|----------|-------------|
| `version` | `int` | Metadata format version (currently `1`) |
| `clipCount` | `int` | Number of clips |
| `fps` | `float` | Scene FPS at export time |
| `names` | `string` | `Walk\|Idle\|CamOrbit` |
| `startFrames` | `string` | `1\|1\|1` |
| `endFrames` | `string` | `72\|48\|60` |
| `sourceTypes` | `string` | `SkelAnimation\|SkelAnimation\|XformOp` |
| `objectNames` | `string` | `CharacterRig\|CharacterRig\|SceneCamera` |
| `objectTypes` | `string` | `Armature\|Armature\|Camera` |

#### Blender round-trip

When re-importing the USD with `property_import_mode='USER'` (or `'ALL'`), Blender strips the `userProperties:` prefix and creates custom properties on the corresponding Blender object:

```
tinyusdz:animClips:clipCount = 8
tinyusdz:animClips:names = MonkeySpin|MonkeyBounce|Walk|Idle|...
tinyusdz:animClips:objectTypes = Mesh|Mesh|Armature|Armature|...
```

#### Design decisions

| Decision | Rationale |
|----------|-----------|
| Scalar strings, not arrays | Blender's USD importer silently drops `string[]` and `int[]` custom attributes.  Only scalar types (`string`, `int`, `float`) survive the round-trip. |
| `\|` separator, not `,` | Action names like `"Walk, Fast"` would break CSV parsing.  Pipe is safe because `_sanitize_filename()` replaces `\|` when used in filenames. |
| `userProperties:tinyusdz:` namespace | `userProperties:` is the Blender-recognized namespace for custom properties.  `tinyusdz:` sub-namespace avoids collision with other addons. |
| Write to first child, not default prim | Blender tags its generated root Xform with `customData.Blender.generated=1` and strips it on re-import.  Writing to the first real child ensures survival. |

#### Bake modes

Configurable in the addon preferences or in the panel:

- **Metadata Only** (default): The exported USD contains whatever Blender's standard exporter produces (typically the active Action's timeSamples).  The hook only writes clip boundary metadata.  Suitable when you have arranged NLA strips manually.
- **Auto-Bake**: Also writes `concatStartFrames` / `concatEndFrames` offset metadata.  Use the "Export Concatenated USD" button in the panel to trigger a full concatenated export.

---

### 5. Workflow 2 — Separate Files (HumanFemale Pattern)

Exports one USD file per Action, following the Pixar convention (`UsdSkelExamples/HumanFemale/`):

```
output/
  ComplexScene.usda                 # Base: geometry + skeleton (no animation)
  ComplexScene.Idle.usda            # Animation only (references base)
  ComplexScene.Walk.usda            # Animation only (references base)
  ComplexScene.CamOrbit.usda        # Animation only (references base)
  ComplexScene.LightCircle.usda     # Animation only (references base)
  ComplexScene.MonkeyBounce.usda    # Animation only (references base)
```

Each animation file contains:

```usda
def Xform "root" (
    prepend references = @./ComplexScene.usda@
)
```

#### Steps

1. Open the **TinyUSDZ** tab in the 3D Viewport sidebar (press **N**).
2. Review the detected clips in the panel list.
3. Click **Export Separate Files**.
4. Choose an output directory and a base name (defaults to the blend filename).
5. Choose format (.usda or .usdc).

The operator:

1. Exports the base scene with `export_animation=False`.
2. For each Action: temporarily sets it as the active action, adjusts the scene frame range, exports with `export_animation=True`, and post-processes the file to add `prepend references` to the base.
3. Restores the original scene state (active actions, frame range).

---

### 6. Panel Location

**3D Viewport > Sidebar (N) > TinyUSDZ tab**

The panel shows:

- A list of all detected clips with per-type icons:
  - Armature icon for `SkelAnimation` clips
  - Camera icon for camera clips
  - Light icon for light clips
  - Mesh icon for mesh/object clips
- Frame range for each clip
- Bake mode dropdown
- Two export buttons (Separate Files / Concatenated)

---

### 7. Consumer-Side Integration

#### JavaScript / TinyUSDZ Viewer

```javascript
// After loading the USD file, read custom attributes from the target prim.
// Values are pipe-separated strings ("|").
const SEP = '|';
const NS = 'userProperties:tinyusdz:animClips:';
const version   = prim.getIntAttribute(NS + 'version');
const clipCount = prim.getIntAttribute(NS + 'clipCount');
const fps       = prim.getFloatAttribute(NS + 'fps');
const names       = prim.getAttribute(NS + 'names').split(SEP);
const startFrames = prim.getAttribute(NS + 'startFrames').split(SEP).map(Number);
const endFrames   = prim.getAttribute(NS + 'endFrames').split(SEP).map(Number);
const sourceTypes = prim.getAttribute(NS + 'sourceTypes').split(SEP);
const objectTypes = prim.getAttribute(NS + 'objectTypes').split(SEP);

// Split the single AnimationClip into sub-clips by frame range
for (let i = 0; i < clipCount; i++) {
    const subClip = THREE.AnimationUtils.subclip(
        fullClip, names[i], startFrames[i], endFrames[i], fps
    );
    mixer.clipAction(subClip);
}
```

#### TinyUSDZ C++ (`render-data.cc`) — Future TODO

1. Read `userProperties:tinyusdz:animClips:*` attributes from child prims.
2. Parse `|`-separated strings into vectors.
3. Split the concatenated timeline into multiple `AnimationClip` objects.
4. Tag each with `sourceType` / `objectType`.
5. Expose via `RenderScene::animation_clips`.

This is **not yet implemented**.

---

### 8. Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `[TinyUSDZ] pxr modules not available` | Running outside Blender, or Blender build without USD | Use official Blender 5.0+ build |
| Hook does not fire | Addon not enabled | Edit > Preferences > Add-ons, search "TinyUSDZ", enable |
| "Only 1 clip — no multi-clip metadata needed" | Only one Action exists across all objects | Push additional Actions to NLA tracks |
| Metadata missing after re-import | Imported with `property_import_mode='NONE'` | Use `'USER'` or `'ALL'` |
| Metadata on unexpected object | Hook writes to the first non-generated child prim of the default prim | Expected; the prim is just a carrier for global metadata |
| Separate-file export missing `prepend references` | `pxr` Python module unavailable | Files still export correctly; cross-references just won't be added |
| Duplicate hook output in console | The hook fires for each `bpy.ops.wm.usd_export()` call, including sub-exports in the separate-file operator | Expected; each sub-export sees the remaining actions |
