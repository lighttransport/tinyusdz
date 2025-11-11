#!/usr/bin/env python3
"""
Load GeomMesh from USD and convert to NumPy arrays

This example demonstrates:
1. Loading a USD file containing a mesh
2. Extracting mesh geometry (points, face indices, etc.)
3. Converting to NumPy arrays for processing
4. Accessing primvars (UV coordinates, normals, etc.)
5. Printing mesh statistics

Usage:
    python3 example_mesh_to_numpy.py <usd_file>
"""

import sys
import os

# Add parent directory to path to import the module
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

try:
    import numpy as np
except ImportError:
    print("Error: NumPy is required for this example")
    print("\nInstall with uv:")
    print("  uv pip install numpy")
    sys.exit(1)

try:
    import tinyusdz_abi3 as tusd
except ImportError as e:
    print(f"Error: Could not import tinyusdz_abi3: {e}")
    print("\nPlease build the module first:")
    print("  ./build.sh setup")
    sys.exit(1)


def print_array_info(name, array, max_items=5):
    """Print information about a numpy array"""
    print(f"\n{name}:")
    print(f"  Shape:     {array.shape}")
    print(f"  Dtype:     {array.dtype}")
    print(f"  Size:      {array.size} elements ({array.nbytes} bytes)")

    if array.size > 0:
        if array.ndim == 1:
            preview = array[:max_items]
            print(f"  First {min(max_items, len(array))}:  {preview}")
            if len(array) > max_items:
                print(f"  ...and {len(array) - max_items} more")
        else:
            preview = array[:max_items]
            print(f"  First {min(max_items, len(array))} rows:")
            for i, row in enumerate(preview):
                print(f"    [{i}] {row}")
            if len(array) > max_items:
                print(f"  ...and {len(array) - max_items} more rows")

        # Statistics for numeric data
        if array.dtype.kind in 'fiu':  # float, int, unsigned int
            if array.ndim == 1:
                print(f"  Min:       {array.min()}")
                print(f"  Max:       {array.max()}")
                print(f"  Mean:      {array.mean():.6f}")
            else:
                print(f"  Min (per axis):  {array.min(axis=0)}")
                print(f"  Max (per axis):  {array.max(axis=0)}")
                print(f"  Mean (per axis): {array.mean(axis=0)}")


def compute_mesh_statistics(positions, indices=None):
    """Compute and print mesh statistics"""
    print("\n" + "=" * 60)
    print("Mesh Statistics")
    print("=" * 60)

    num_vertices = len(positions)
    print(f"Number of vertices: {num_vertices:,}")

    if indices is not None:
        num_faces = len(indices)
        print(f"Number of faces:    {num_faces:,}")

        # Compute total indices if it's a face index array
        if indices.ndim == 1:
            total_indices = len(indices)
        else:
            total_indices = indices.size
        print(f"Total indices:      {total_indices:,}")

    # Bounding box
    bbox_min = positions.min(axis=0)
    bbox_max = positions.max(axis=0)
    bbox_size = bbox_max - bbox_min
    bbox_center = (bbox_min + bbox_max) / 2.0

    print(f"\nBounding Box:")
    print(f"  Min:    {bbox_min}")
    print(f"  Max:    {bbox_max}")
    print(f"  Size:   {bbox_size}")
    print(f"  Center: {bbox_center}")

    # Diagonal length
    diagonal = np.linalg.norm(bbox_size)
    print(f"  Diagonal length: {diagonal:.6f}")

    # Memory usage
    total_memory = positions.nbytes
    if indices is not None:
        total_memory += indices.nbytes
    print(f"\nMemory usage: {total_memory:,} bytes ({total_memory / (1024*1024):.2f} MB)")


def load_mesh_data_from_stage(stage):
    """
    Extract mesh data from a stage

    Note: This is a demonstration of what the API would look like.
    The actual implementation needs to be completed in the C binding.
    """
    print("\n" + "=" * 60)
    print("Loading Mesh Data")
    print("=" * 60)

    # For demonstration, let's create synthetic mesh data
    # In the real implementation, this would come from stage traversal

    # Example: Cube mesh
    print("\nNote: This is synthetic data for demonstration.")
    print("TODO: Implement actual mesh extraction from USD stage")

    # Cube vertices
    positions = np.array([
        [-1.0, -1.0, -1.0],
        [ 1.0, -1.0, -1.0],
        [ 1.0,  1.0, -1.0],
        [-1.0,  1.0, -1.0],
        [-1.0, -1.0,  1.0],
        [ 1.0, -1.0,  1.0],
        [ 1.0,  1.0,  1.0],
        [-1.0,  1.0,  1.0],
    ], dtype=np.float32)

    # Face vertex indices (quads)
    face_vertex_indices = np.array([
        0, 1, 2, 3,  # back face
        4, 5, 6, 7,  # front face
        0, 1, 5, 4,  # bottom face
        2, 3, 7, 6,  # top face
        0, 3, 7, 4,  # left face
        1, 2, 6, 5,  # right face
    ], dtype=np.int32)

    # Face vertex counts
    face_vertex_counts = np.array([4, 4, 4, 4, 4, 4], dtype=np.int32)

    # Normals (per-vertex)
    normals = np.array([
        [-0.577, -0.577, -0.577],
        [ 0.577, -0.577, -0.577],
        [ 0.577,  0.577, -0.577],
        [-0.577,  0.577, -0.577],
        [-0.577, -0.577,  0.577],
        [ 0.577, -0.577,  0.577],
        [ 0.577,  0.577,  0.577],
        [-0.577,  0.577,  0.577],
    ], dtype=np.float32)

    # UV coordinates (per-vertex)
    uvs = np.array([
        [0.0, 0.0],
        [1.0, 0.0],
        [1.0, 1.0],
        [0.0, 1.0],
        [0.0, 0.0],
        [1.0, 0.0],
        [1.0, 1.0],
        [0.0, 1.0],
    ], dtype=np.float32)

    return {
        'positions': positions,
        'face_vertex_indices': face_vertex_indices,
        'face_vertex_counts': face_vertex_counts,
        'normals': normals,
        'uvs': uvs,
    }


def demonstrate_numpy_operations(positions, normals):
    """Demonstrate various NumPy operations on mesh data"""
    print("\n" + "=" * 60)
    print("NumPy Operations Examples")
    print("=" * 60)

    # Transform operations
    print("\n1. Transform Operations:")

    # Translation
    translation = np.array([10.0, 0.0, 0.0])
    positions_translated = positions + translation
    print(f"   Translated by {translation}")
    print(f"   New center: {positions_translated.mean(axis=0)}")

    # Scaling
    scale = 2.0
    positions_scaled = positions * scale
    print(f"   Scaled by {scale}x")
    print(f"   New bounds: {positions_scaled.min(axis=0)} to {positions_scaled.max(axis=0)}")

    # Rotation (90 degrees around Z axis)
    angle = np.pi / 2
    rotation_matrix = np.array([
        [np.cos(angle), -np.sin(angle), 0],
        [np.sin(angle),  np.cos(angle), 0],
        [0,              0,             1]
    ])
    positions_rotated = positions @ rotation_matrix.T
    print(f"   Rotated 90° around Z axis")
    print(f"   First vertex: {positions[0]} -> {positions_rotated[0]}")

    # Analysis operations
    print("\n2. Analysis Operations:")

    # Find extremes
    max_x_idx = positions[:, 0].argmax()
    min_y_idx = positions[:, 1].argmin()
    print(f"   Vertex with max X: index {max_x_idx}, position {positions[max_x_idx]}")
    print(f"   Vertex with min Y: index {min_y_idx}, position {min_y_idx]}")

    # Distance calculations
    origin = np.array([0, 0, 0])
    distances = np.linalg.norm(positions - origin, axis=1)
    furthest_idx = distances.argmax()
    print(f"   Furthest vertex from origin: index {furthest_idx}, distance {distances[furthest_idx]:.4f}")

    # Normal consistency check
    if normals is not None:
        normal_lengths = np.linalg.norm(normals, axis=1)
        print(f"   Normal lengths - min: {normal_lengths.min():.4f}, max: {normal_lengths.max():.4f}")
        if not np.allclose(normal_lengths, 1.0, atol=1e-3):
            print("   Warning: Some normals are not unit length!")


def demonstrate_buffer_protocol():
    """Demonstrate the buffer protocol advantages"""
    print("\n" + "=" * 60)
    print("Buffer Protocol Demonstration")
    print("=" * 60)

    print("\nThe buffer protocol enables zero-copy data access:")
    print("  1. C++ std::vector<GfVec3f> in TinyUSDZ")
    print("  2. → ValueArray (C wrapper with pointer)")
    print("  3. → np.asarray() creates view (NO COPY!)")
    print("  4. → NumPy operations work directly on USD data")

    print("\nAdvantages:")
    print("  ✓ No memory duplication")
    print("  ✓ Instant access (O(1) instead of O(n))")
    print("  ✓ Lower memory footprint (1x instead of 2-3x)")
    print("  ✓ Can modify data in-place (if writable)")

    # Create large synthetic data to show performance
    print("\nPerformance comparison (1M vertices):")
    num_vertices = 1_000_000

    # Simulate copy-based approach
    import time

    # Method 1: Creating from Python list (copying)
    positions_list = [[float(i), float(i), float(i)] for i in range(num_vertices)]
    start = time.time()
    positions_copy = np.array(positions_list, dtype=np.float32)
    copy_time = time.time() - start

    # Method 2: Direct NumPy creation (similar to buffer protocol)
    start = time.time()
    positions_direct = np.zeros((num_vertices, 3), dtype=np.float32)
    direct_time = time.time() - start

    print(f"  Copy-based approach:   {copy_time*1000:.2f} ms")
    print(f"  Direct creation:       {direct_time*1000:.2f} ms")
    print(f"  Speedup:               {copy_time/direct_time:.1f}x")

    # Memory comparison
    copy_memory = positions_copy.nbytes
    direct_memory = positions_direct.nbytes
    print(f"\n  Memory (copy):         {copy_memory / (1024*1024):.2f} MB")
    print(f"  Memory (direct):       {direct_memory / (1024*1024):.2f} MB")
    print(f"  Savings:               {(copy_memory - direct_memory) / (1024*1024):.2f} MB")


def main():
    print("=" * 60)
    print("TinyUSDZ GeomMesh to NumPy Example")
    print("=" * 60)

    # Check for input file
    if len(sys.argv) > 1:
        usd_file = sys.argv[1]
        if not os.path.exists(usd_file):
            print(f"\nError: File not found: {usd_file}")
            return 1

        print(f"\nLoading USD file: {usd_file}")

        try:
            # Load the stage
            stage = tusd.Stage.load_from_file(usd_file)
            print("✓ Stage loaded successfully")

            # Print stage info
            print("\nStage contents:")
            print(stage.to_string())

            # Extract mesh data
            mesh_data = load_mesh_data_from_stage(stage)

        except Exception as e:
            print(f"\nError loading USD file: {e}")
            import traceback
            traceback.print_exc()
            print("\nUsing synthetic mesh data for demonstration...")
            mesh_data = load_mesh_data_from_stage(None)
    else:
        print("\nNo USD file provided, using synthetic mesh data")
        print("Usage: python3 example_mesh_to_numpy.py <usd_file>")
        mesh_data = load_mesh_data_from_stage(None)

    # Print array information
    print("\n" + "=" * 60)
    print("Mesh Data Arrays")
    print("=" * 60)

    positions = mesh_data['positions']
    face_vertex_indices = mesh_data['face_vertex_indices']
    face_vertex_counts = mesh_data['face_vertex_counts']
    normals = mesh_data.get('normals')
    uvs = mesh_data.get('uvs')

    print_array_info("Positions (points)", positions)
    print_array_info("Face Vertex Indices", face_vertex_indices, max_items=24)
    print_array_info("Face Vertex Counts", face_vertex_counts)

    if normals is not None:
        print_array_info("Normals", normals)

    if uvs is not None:
        print_array_info("UV Coordinates", uvs)

    # Compute statistics
    compute_mesh_statistics(positions, face_vertex_indices)

    # Demonstrate NumPy operations
    demonstrate_numpy_operations(positions, normals)

    # Demonstrate buffer protocol
    demonstrate_buffer_protocol()

    # Export example
    print("\n" + "=" * 60)
    print("Data Export Examples")
    print("=" * 60)

    print("\nExporting to various formats:")

    # NumPy binary format
    print("\n1. NumPy binary (.npz):")
    print("   np.savez('mesh.npz',")
    print("            positions=positions,")
    print("            indices=face_vertex_indices,")
    print("            normals=normals)")

    # CSV format
    print("\n2. CSV format:")
    print("   np.savetxt('positions.csv', positions,")
    print("              delimiter=',', header='x,y,z')")

    # PLY format (simple)
    print("\n3. PLY format (example):")
    print("   # Write header and vertex data")
    print("   # See example_export_ply() function")

    print("\n" + "=" * 60)
    print("Example completed successfully!")
    print("=" * 60)

    return 0


if __name__ == "__main__":
    sys.exit(main())
