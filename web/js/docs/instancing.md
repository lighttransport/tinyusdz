# USD Instancing (WASM/JS)

USD scene graph instancing support in the TinyUSDZ WASM binding and JavaScript layer.

Per AOUSD Core Spec 11.3.3, a prim is an **instance** when `instanceable = true` and it has at least one composition arc (reference, payload, inherit, specialize, or variantSet). Instances sharing the same composition arc signature share a single **prototype** — composed once, reused across all instances.

## WASM Native API

### Methods on `TinyUSDZLoaderNative`

| Method | Returns | Description |
|--------|---------|-------------|
| `numInstances()` | `int` | Total number of `RenderInstance` entries |
| `getInstance(id)` | `object \| null` | Get instance data by index |
| `getInstancesForMesh(meshId)` | `int[]` | Get all instance IDs sharing a given mesh |

### `getInstance(id)` Return Object

```js
{
  primName: "Tree_1",          // Element name of the instance prim
  absPath: "/World/Tree_1",    // Absolute prim path
  displayName: "",             // displayName metadata (may be empty)
  prototypeIndex: 0,           // Prototype group index
  meshId: 3,                   // Index into meshes[] (shared with other instances)
  materialId: -1,              // Material override (-1 = use mesh default)
  localMatrix: Float64Array(16),   // 4x4 local transform (row-major)
  globalMatrix: Float64Array(16),  // 4x4 world transform (row-major)
  visible: true
}
```

### Node Instance Fields

Every node returned by `getRootNode()` (recursive) now includes:

| Field | Type | Description |
|-------|------|-------------|
| `isInstance` | `boolean` | `true` if this node is a USD instance prim |
| `prototypeIndex` | `int` | Prototype group index (`-1` if not an instance) |
| `instanceId` | `int` | Index into `RenderScene::instances` (`-1` if not an instance) |

These fields are always present on every node (default: `false`, `-1`, `-1`).

## Usage Examples

### Basic: Query Instances

```js
const scene = Module.TinyUSDZLoaderNative();
await scene.loadFromBinary(buffer, "scene.usda");

const count = scene.numInstances();
console.log(`Scene has ${count} instances`);

for (let i = 0; i < count; i++) {
  const inst = scene.getInstance(i);
  console.log(`Instance ${i}: ${inst.absPath} → mesh[${inst.meshId}], proto=${inst.prototypeIndex}`);
}
```

### Find Instances Sharing a Mesh

```js
// Which instances share mesh 0?
const instanceIds = scene.getInstancesForMesh(0);
console.log(`Mesh 0 is instanced ${instanceIds.length} times`);

for (const id of instanceIds) {
  const inst = scene.getInstance(id);
  console.log(`  ${inst.absPath}`);
}
```

### Detect Instance Nodes in Hierarchy

```js
function walkNodes(node, depth = 0) {
  if (node.isInstance) {
    console.log(`${"  ".repeat(depth)}[INSTANCE] ${node.absPath} proto=${node.prototypeIndex}`);
  }
  for (const child of node.children) {
    walkNodes(child, depth + 1);
  }
}

const root = scene.getRootNode(0);
walkNodes(root);
```

## THREE.js Integration

### Shared Geometry/Material (Current)

When building the THREE.js scene graph via `TinyUSDZLoaderUtils.buildThreeNode()`, instance nodes with `isInstance === true` can share `BufferGeometry` and `Material` objects:

```js
const prototypeCache = {};  // prototypeIndex → { geometry, material }

function buildMeshNode(usdNode, usdScene) {
  if (usdNode.isInstance && usdNode.prototypeIndex >= 0) {
    const key = usdNode.prototypeIndex;

    if (!prototypeCache[key]) {
      // First instance: create geometry + material normally
      const mesh = usdScene.getMesh(usdNode.contentId);
      const geom = buildBufferGeometry(mesh);
      const mat = buildMaterial(mesh, usdScene);
      prototypeCache[key] = { geometry: geom, material: mat };
    }

    // Reuse cached geometry/material with this instance's transform
    const cached = prototypeCache[key];
    const threeMesh = new THREE.Mesh(cached.geometry, cached.material);
    threeMesh.matrixAutoUpdate = false;
    threeMesh.matrix.fromArray(usdNode.localMatrix);
    threeMesh.matrixWorldNeedsUpdate = true;
    return threeMesh;
  }

  // Non-instance: create unique geometry
  return buildRegularMesh(usdNode, usdScene);
}
```

### THREE.InstancedMesh (Future / Advanced)

For scenes with many instances of the same prototype (e.g., 1000 trees), `THREE.InstancedMesh` provides better GPU performance. This requires a post-traversal pass:

```js
// 1. Collect instances per prototype
const protoInstances = {};  // prototypeIndex → [{ matrix, ... }]

for (let i = 0; i < scene.numInstances(); i++) {
  const inst = scene.getInstance(i);
  if (!protoInstances[inst.prototypeIndex]) {
    protoInstances[inst.prototypeIndex] = [];
  }
  protoInstances[inst.prototypeIndex].push(inst);
}

// 2. Create InstancedMesh per prototype
for (const [protoIdx, instances] of Object.entries(protoInstances)) {
  const first = instances[0];
  const mesh = scene.getMesh(first.meshId);
  const geometry = buildBufferGeometry(mesh);
  const material = buildMaterial(mesh, scene);

  const instancedMesh = new THREE.InstancedMesh(geometry, material, instances.length);

  const mat4 = new THREE.Matrix4();
  instances.forEach((inst, i) => {
    mat4.fromArray(inst.globalMatrix);
    instancedMesh.setMatrixAt(i, mat4);
  });
  instancedMesh.instanceMatrix.needsUpdate = true;

  threeScene.add(instancedMesh);
}
```

## Pipeline Diagram

```
USD File (.usda/.usdc/.usdz)
  │
  ▼
TinyUSDZ WASM (C++)
  │
  ├─ Stage::BuildInstancePrototypes()
  │    └─ Groups by InstanceKey (128-bit hash of composition arcs)
  │
  ├─ ConvertToRenderScene()
  │    ├─ Converts meshes (shared across instances)
  │    ├─ Builds Node hierarchy with is_instance flags
  │    └─ Creates RenderInstance entries
  │
  ▼
JavaScript (binding.cc → embind)
  │
  ├─ numInstances() / getInstance(id) / getInstancesForMesh(meshId)
  ├─ getRootNode() → nodes with isInstance/prototypeIndex/instanceId
  │
  ▼
THREE.js Scene
  ├─ Shared BufferGeometry + Material (current)
  └─ THREE.InstancedMesh (future optimization)
```

## USDA Example

```usda
#usda 1.0

def Xform "World"
{
    def Xform "Tree_1" (
        instanceable = true
        references = @./tree.usda@
    )
    {
        double3 xformOp:translate = (0, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def Xform "Tree_2" (
        instanceable = true
        references = @./tree.usda@
    )
    {
        double3 xformOp:translate = (10, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }
}
```

After loading: `numInstances()` returns 2, both with `prototypeIndex = 0` (same reference = same prototype). `getInstancesForMesh(meshId)` returns `[0, 1]` if both instances contain the same child mesh.

## Related Files

| File | Purpose |
|------|---------|
| `web/binding.cc` | WASM bindings: `numInstances`, `getInstance`, `getInstancesForMesh`, node fields |
| `src/tydra/render-data.hh` | `RenderInstance` struct, `Node` instance fields |
| `src/stage.hh` | `Stage::BuildInstancePrototypes()` (InstanceKey-based grouping) |
| `doc/instancing.md` | Full instancing spec reference and architecture doc |
