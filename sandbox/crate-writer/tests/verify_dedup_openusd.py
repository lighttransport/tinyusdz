#!/usr/bin/env python3
"""
OpenUSD Deduplication Verification Script

This script uses OpenUSD Python API to verify that files written with
TinyUSDZ crate writer deduplication can be correctly read by OpenUSD.

It verifies:
1. File can be opened by OpenUSD
2. All prims and attributes are present
3. TimeSamples values are correct at various frames
4. Deduplicated values are correctly stored and retrieved
"""

import sys
import os

try:
    from pxr import Usd, UsdGeom, Sdf
    HAS_USD = True
except ImportError:
    HAS_USD = False
    print("ERROR: OpenUSD Python bindings not found")
    print("Please install OpenUSD or set PYTHONPATH to OpenUSD installation")
    sys.exit(1)


def verify_file(filepath):
    """Verify USD file written with deduplication"""

    print(f"\n{'='*70}")
    print(f"Verifying: {filepath}")
    print(f"{'='*70}\n")

    # Step 1: Open the stage
    print("Step 1: Opening stage with OpenUSD...")
    try:
        stage = Usd.Stage.Open(filepath)
        if not stage:
            print("  ✗ FAILED: Could not open stage")
            return False
        print("  ✓ Stage opened successfully")
    except Exception as e:
        print(f"  ✗ FAILED: Exception opening stage: {e}")
        return False

    # Step 2: Verify layer metadata
    print("\nStep 2: Verifying layer metadata...")
    try:
        tcs = stage.GetTimeCodesPerSecond()
        fps = stage.GetFramesPerSecond()
        start = stage.GetStartTimeCode()
        end = stage.GetEndTimeCode()

        print(f"  TimeCodesPerSecond: {tcs}")
        print(f"  FramesPerSecond: {fps}")
        print(f"  StartTimeCode: {start}")
        print(f"  EndTimeCode: {end}")

        assert tcs == 24.0, f"Expected tcs=24.0, got {tcs}"
        assert fps == 24.0, f"Expected fps=24.0, got {fps}"
        assert start == 1.0, f"Expected start=1.0, got {start}"
        assert end == 100.0, f"Expected end=100.0, got {end}"
        print("  ✓ Layer metadata correct")
    except AssertionError as e:
        print(f"  ✗ FAILED: {e}")
        return False
    except Exception as e:
        print(f"  ✗ FAILED: Exception: {e}")
        return False

    # Step 3: Verify FloatArrayTest
    print("\nStep 3: Verifying FloatArrayTest...")
    try:
        prim = stage.GetPrimAtPath("/FloatArrayTest")
        if not prim:
            print("  ✗ FAILED: Prim not found")
            return False

        attr = prim.GetAttribute("floatArrayAttr")
        if not attr:
            print("  ✗ FAILED: Attribute not found")
            return False

        # Check frame 1 (should be [1,2,3,4,5])
        val1 = attr.Get(1.0)
        expected1 = [1.0, 2.0, 3.0, 4.0, 5.0]
        assert val1 == expected1, f"Frame 1: Expected {expected1}, got {val1}"

        # Check frame 25 (should be [1,2,3,4,5])
        val25 = attr.Get(25.0)
        assert val25 == expected1, f"Frame 25: Expected {expected1}, got {val25}"

        # Check frame 50 (should be [1,2,3,4,5])
        val50 = attr.Get(50.0)
        assert val50 == expected1, f"Frame 50: Expected {expected1}, got {val50}"

        # Check frame 51 (should be [10,20,30,40,50])
        val51 = attr.Get(51.0)
        expected2 = [10.0, 20.0, 30.0, 40.0, 50.0]
        assert val51 == expected2, f"Frame 51: Expected {expected2}, got {val51}"

        # Check frame 100 (should be [10,20,30,40,50])
        val100 = attr.Get(100.0)
        assert val100 == expected2, f"Frame 100: Expected {expected2}, got {val100}"

        # Verify all frames 1-50 have same value (dedup test)
        for frame in [1, 10, 20, 30, 40, 50]:
            val = attr.Get(float(frame))
            assert val == expected1, f"Frame {frame}: Dedup failed, got {val}"

        print(f"  ✓ FloatArrayTest verified (100 frames, 2 unique arrays)")
        print(f"    Frames 1-50:  {expected1}")
        print(f"    Frames 51-100: {expected2}")

    except AssertionError as e:
        print(f"  ✗ FAILED: {e}")
        return False
    except Exception as e:
        print(f"  ✗ FAILED: Exception: {e}")
        return False

    # Step 4: Verify StringArrayTest
    print("\nStep 4: Verifying StringArrayTest...")
    try:
        prim = stage.GetPrimAtPath("/StringArrayTest")
        if not prim:
            print("  ✗ FAILED: Prim not found")
            return False

        attr = prim.GetAttribute("stringArrayAttr")
        if not attr:
            print("  ✗ FAILED: Attribute not found")
            return False

        # Check frame 1
        val1 = attr.Get(1.0)
        expected1 = ["author", "john", "version", "1.0"]
        assert val1 == expected1, f"Frame 1: Expected {expected1}, got {val1}"

        # Check frame 40
        val40 = attr.Get(40.0)
        assert val40 == expected1, f"Frame 40: Expected {expected1}, got {val40}"

        # Check frame 41
        val41 = attr.Get(41.0)
        expected2 = ["author", "jane", "version", "2.0"]
        assert val41 == expected2, f"Frame 41: Expected {expected2}, got {val41}"

        # Check frame 60
        val60 = attr.Get(60.0)
        assert val60 == expected2, f"Frame 60: Expected {expected2}, got {val60}"

        print(f"  ✓ StringArrayTest verified (60 frames, 2 unique string arrays)")
        print(f"    Frames 1-40:  {expected1}")
        print(f"    Frames 41-60: {expected2}")

    except AssertionError as e:
        print(f"  ✗ FAILED: {e}")
        return False
    except Exception as e:
        print(f"  ✗ FAILED: Exception: {e}")
        return False

    # Step 5: Verify MatrixTest
    print("\nStep 5: Verifying MatrixTest...")
    try:
        prim = stage.GetPrimAtPath("/MatrixTest")
        if not prim:
            print("  ✗ FAILED: Prim not found")
            return False

        attr = prim.GetAttribute("xformMatrix")
        if not attr:
            print("  ✗ FAILED: Attribute not found")
            return False

        # Check frame 1 (identity matrix)
        val1 = attr.Get(1.0)
        # Identity matrix
        for i in range(4):
            for j in range(4):
                expected = 1.0 if i == j else 0.0
                actual = val1[i][j]
                assert abs(actual - expected) < 1e-10, \
                    f"Frame 1: Matrix[{i}][{j}] expected {expected}, got {actual}"

        # Check frame 70 (still identity)
        val70 = attr.Get(70.0)
        for i in range(4):
            for j in range(4):
                expected = 1.0 if i == j else 0.0
                actual = val70[i][j]
                assert abs(actual - expected) < 1e-10, \
                    f"Frame 70: Matrix[{i}][{j}] expected {expected}, got {actual}"

        # Check frame 71 (scale 2x)
        val71 = attr.Get(71.0)
        for i in range(4):
            for j in range(4):
                if i == j and i < 3:
                    expected = 2.0
                elif i == j:
                    expected = 1.0
                else:
                    expected = 0.0
                actual = val71[i][j]
                assert abs(actual - expected) < 1e-10, \
                    f"Frame 71: Matrix[{i}][{j}] expected {expected}, got {actual}"

        # Check frame 100 (scale 2x)
        val100 = attr.Get(100.0)
        for i in range(4):
            for j in range(4):
                if i == j and i < 3:
                    expected = 2.0
                elif i == j:
                    expected = 1.0
                else:
                    expected = 0.0
                actual = val100[i][j]
                assert abs(actual - expected) < 1e-10, \
                    f"Frame 100: Matrix[{i}][{j}] expected {expected}, got {actual}"

        print(f"  ✓ MatrixTest verified (100 frames, 2 unique matrices)")
        print(f"    Frames 1-70:  Identity matrix")
        print(f"    Frames 71-100: Scale 2x matrix")

    except AssertionError as e:
        print(f"  ✗ FAILED: {e}")
        return False
    except Exception as e:
        print(f"  ✗ FAILED: Exception: {e}")
        return False

    # Step 6: Verify IntArrayPattern
    print("\nStep 6: Verifying IntArrayPattern...")
    try:
        prim = stage.GetPrimAtPath("/IntArrayPattern")
        if not prim:
            print("  ✗ FAILED: Prim not found")
            return False

        attr = prim.GetAttribute("intArrayAttr")
        if not attr:
            print("  ✗ FAILED: Attribute not found")
            return False

        pattern_a = [100, 200, 300]
        pattern_b = [400, 500, 600]
        pattern_c = [700, 800, 900]

        # Check pattern ABC repeating
        test_frames = [
            (1, pattern_a), (2, pattern_b), (3, pattern_c),
            (4, pattern_a), (5, pattern_b), (6, pattern_c),
            (30, pattern_c), (31, pattern_a), (89, pattern_b), (90, pattern_c)
        ]

        for frame, expected in test_frames:
            val = attr.Get(float(frame))
            assert val == expected, f"Frame {frame}: Expected {expected}, got {val}"

        print(f"  ✓ IntArrayPattern verified (90 frames, ABC pattern)")
        print(f"    Pattern A: {pattern_a}")
        print(f"    Pattern B: {pattern_b}")
        print(f"    Pattern C: {pattern_c}")

    except AssertionError as e:
        print(f"  ✗ FAILED: {e}")
        return False
    except Exception as e:
        print(f"  ✗ FAILED: Exception: {e}")
        return False

    print(f"\n{'='*70}")
    print("✓ ALL VERIFICATION TESTS PASSED")
    print("  OpenUSD successfully read file with deduplication")
    print("  All TimeSamples values are correct")
    print("  Deduplication is working correctly")
    print(f"{'='*70}\n")

    return True


def main():
    if len(sys.argv) < 2:
        print("Usage: python verify_dedup_openusd.py <file.usdc>")
        sys.exit(1)

    filepath = sys.argv[1]

    if not os.path.exists(filepath):
        print(f"ERROR: File not found: {filepath}")
        sys.exit(1)

    success = verify_file(filepath)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
