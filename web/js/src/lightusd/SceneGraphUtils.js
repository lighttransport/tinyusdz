/**
 * Collect meshes from a Three.js subtree.
 */
export function collectMeshesInHierarchy(root) {
  const meshes = [];
  if (!root) return meshes;

  root.traverse((child) => {
    if (child.isMesh) {
      meshes.push(child);
    }
  });
  return meshes;
}

