#!/usr/bin/env python
"""
Compare USD file parsing between OpenUSD and TinyUSDZ
"""

import sys
import os
import json
import subprocess

def analyze_with_openusd(filepath):
    """Analyze USD file with OpenUSD Python API"""
    try:
        from pxr import Usd, UsdGeom, Sdf

        stage = Usd.Stage.Open(filepath)
        if not stage:
            return None

        result = {
            "library": "OpenUSD",
            "file": filepath,
            "root_layer": stage.GetRootLayer().identifier,
            "prim_count": len(list(stage.Traverse())),
            "up_axis": str(UsdGeom.GetStageUpAxis(stage)),
            "meters_per_unit": UsdGeom.GetStageMetersPerUnit(stage),
            "prims": []
        }

        for prim in stage.Traverse():
            prim_info = {
                "path": str(prim.GetPath()),
                "type": str(prim.GetTypeName()),
                "attributes": []
            }

            # Get attributes
            for attr in prim.GetAttributes():
                attr_info = {
                    "name": attr.GetName(),
                    "type": str(attr.GetTypeName()),
                    "has_value": attr.HasValue()
                }
                prim_info["attributes"].append(attr_info)

            result["prims"].append(prim_info)

        return result

    except ImportError as e:
        print(f"Error: OpenUSD Python bindings not available: {e}")
        return None
    except Exception as e:
        print(f"Error analyzing with OpenUSD: {e}")
        return None

def analyze_with_tinyusdz(filepath):
    """Analyze USD file with TinyUSDZ tusdcat tool"""
    try:
        # Use tusdcat to dump the file
        tusdcat_path = os.path.join(os.path.dirname(os.path.dirname(filepath)), "build", "tusdcat")
        if not os.path.exists(tusdcat_path):
            tusdcat_path = "../build/tusdcat"  # Try relative path

        # Suppress debug output by redirecting stderr
        result = subprocess.run(
            [tusdcat_path, filepath],
            capture_output=True,
            text=True
        )

        if result.returncode != 0:
            print(f"Error running tusdcat: {result.stderr}")
            return None

        # Parse the output
        lines = result.stdout.strip().split('\n')

        # Extract metadata from the header
        tinyusdz_result = {
            "library": "TinyUSDZ",
            "file": filepath,
            "output": result.stdout[:1000],  # First 1000 chars
            "exit_code": result.returncode
        }

        # Try to parse some basic info from output
        for line in lines:
            if "metersPerUnit" in line:
                try:
                    tinyusdz_result["meters_per_unit"] = float(line.split('=')[1].strip())
                except:
                    pass
            elif "upAxis" in line:
                try:
                    tinyusdz_result["up_axis"] = line.split('=')[1].strip().strip('"')
                except:
                    pass

        return tinyusdz_result

    except Exception as e:
        print(f"Error analyzing with TinyUSDZ: {e}")
        return None

def main():
    if len(sys.argv) < 2:
        print("Usage: python compare_usd_example.py <usd_file>")
        sys.exit(1)

    filepath = sys.argv[1]

    if not os.path.exists(filepath):
        print(f"File not found: {filepath}")
        sys.exit(1)

    print("=" * 60)
    print(f"Comparing USD file: {filepath}")
    print("=" * 60)

    # Analyze with OpenUSD
    print("\n### OpenUSD Analysis ###")
    openusd_result = analyze_with_openusd(filepath)
    if openusd_result:
        print(f"Prim count: {openusd_result['prim_count']}")
        print(f"Up axis: {openusd_result['up_axis']}")
        print(f"Meters per unit: {openusd_result['meters_per_unit']}")
        print(f"\nFirst 3 prims:")
        for prim in openusd_result['prims'][:3]:
            print(f"  {prim['path']}: {prim['type']} ({len(prim['attributes'])} attributes)")

    # Analyze with TinyUSDZ
    print("\n### TinyUSDZ Analysis ###")
    tinyusdz_result = analyze_with_tinyusdz(filepath)
    if tinyusdz_result:
        print(f"Exit code: {tinyusdz_result['exit_code']}")
        if 'up_axis' in tinyusdz_result:
            print(f"Up axis: {tinyusdz_result['up_axis']}")
        if 'meters_per_unit' in tinyusdz_result:
            print(f"Meters per unit: {tinyusdz_result['meters_per_unit']}")
        print(f"\nOutput preview:")
        print(tinyusdz_result['output'][:500])

    # Compare results
    print("\n### Comparison ###")
    if openusd_result and tinyusdz_result:
        print("Both libraries successfully parsed the file")

        # Compare metadata if available
        if 'up_axis' in tinyusdz_result:
            if openusd_result['up_axis'] == tinyusdz_result['up_axis']:
                print(f"✓ Up axis matches: {openusd_result['up_axis']}")
            else:
                print(f"✗ Up axis differs - OpenUSD: {openusd_result['up_axis']}, TinyUSDZ: {tinyusdz_result['up_axis']}")

        if 'meters_per_unit' in tinyusdz_result:
            if abs(openusd_result['meters_per_unit'] - tinyusdz_result['meters_per_unit']) < 0.001:
                print(f"✓ Meters per unit matches: {openusd_result['meters_per_unit']}")
            else:
                print(f"✗ Meters per unit differs - OpenUSD: {openusd_result['meters_per_unit']}, TinyUSDZ: {tinyusdz_result['meters_per_unit']}")

    print("=" * 60)

if __name__ == "__main__":
    main()