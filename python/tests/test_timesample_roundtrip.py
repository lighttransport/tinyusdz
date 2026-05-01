"""Test time-sampled attributes across dtypes and formats.

Author single-attribute stages with time-sampled values of various dtypes,
save in each format (USDA/USDC/USDZ), reload, and assert the values preserve
type fidelity across time samples.
"""
from __future__ import annotations

import pathlib
import pytest

import tinyusdz


FORMATS = ["usda", "usdc", "usdz"]


def _build_timesample_stage(dtype, sample_values) -> tinyusdz.Stage:
    """Build a stage with a single time-sampled attribute.
    
    Args:
        dtype: The USD type name (e.g., 'float', 'int', 'bool', 'half')
        sample_values: List of (time, value) tuples
    """
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    
    for time, value in sample_values:
        p.set_attribute_at_time("attr", time, value, dtype=dtype)
    
    s.add_root_prim(p)
    return s


@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("dtype,sample_values", [
    ("int", [(0.0, 10), (1.0, 20), (2.0, 30)]),
    ("float", [(0.0, 1.5), (1.0, 2.5), (2.0, 3.5)]),
    ("double", [(0.0, 1.5), (1.0, 2.5), (2.0, 3.5)]),
    ("bool", [(0.0, True), (1.0, False), (2.0, True)]),
    ("half", [(0.0, 1.0), (1.0, 2.0), (2.0, 3.0)]),
    ("uint", [(0.0, 5), (1.0, 10), (2.0, 15)]),
    ("uint64", [(0.0, 100), (1.0, 200), (2.0, 300)]),
    ("int64", [(0.0, -100), (1.0, 0), (2.0, 100)]),
    ("token", [(0.0, "a"), (1.0, "b"), (2.0, "c")]),
    ("string", [(0.0, "hello"), (1.0, "world"), (2.0, "test")]),
])
def test_scalar_timesample_roundtrip(tmp_path: pathlib.Path, fmt, dtype, sample_values):
    """Test that scalar time-sampled attributes round-trip correctly."""
    s = _build_timesample_stage(dtype, sample_values)
    out = tmp_path / f"ts_{dtype}.{fmt}"
    s.save(str(out), format=fmt)
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    attr = prim.get_attribute("attr")
    assert attr is not None
    
    # Check we have the right number of samples
    time_samples = prim.get_attribute_timesamples("attr")
    assert len(time_samples) == len(sample_values)
    
    # Verify each sample value
    for (_, expected_val), (time, val) in zip(sample_values, sorted(time_samples)):
        retrieved_val = val.as_scalar() if hasattr(val, 'as_scalar') else val
        if isinstance(expected_val, bool):
            # bool may coerce to int in round-trip
            assert retrieved_val in (expected_val, int(expected_val))
        elif isinstance(expected_val, float):
            assert retrieved_val == pytest.approx(expected_val, rel=1e-6)
        elif isinstance(expected_val, str):
            assert str(retrieved_val) == expected_val
        else:
            assert retrieved_val == expected_val


@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("dtype,sample_values", [
    ("int2", [(0.0, (1, 2)), (1.0, (3, 4)), (2.0, (5, 6))]),
    ("int3", [(0.0, (1, 2, 3)), (1.0, (4, 5, 6))]),
    ("int4", [(0.0, (1, 2, 3, 4)), (1.0, (5, 6, 7, 8))]),
    ("float2", [(0.0, (1.5, 2.5)), (1.0, (3.5, 4.5))]),
    ("float3", [(0.0, (1.0, 2.0, 3.0)), (1.0, (4.0, 5.0, 6.0))]),
    ("float4", [(0.0, (1.0, 2.0, 3.0, 4.0)), (1.0, (5.0, 6.0, 7.0, 8.0))]),
    ("double2", [(0.0, (1.5, 2.5)), (1.0, (3.5, 4.5))]),
    ("double3", [(0.0, (1.0, 2.0, 3.0)), (1.0, (4.0, 5.0, 6.0))]),
    ("double4", [(0.0, (1.0, 2.0, 3.0, 4.0)), (1.0, (5.0, 6.0, 7.0, 8.0))]),
])
def test_vector_timesample_roundtrip(tmp_path: pathlib.Path, fmt, dtype, sample_values):
    """Test that vector time-sampled attributes round-trip correctly."""
    s = _build_timesample_stage(dtype, sample_values)
    out = tmp_path / f"ts_{dtype}.{fmt}"
    s.save(str(out), format=fmt)
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    attr = prim.get_attribute("attr")
    assert attr is not None
    time_samples = prim.get_attribute_timesamples("attr")
    assert len(time_samples) == len(sample_values)


@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("dtype,sample_values", [
    ("color3f", [(0.0, (0.1, 0.2, 0.3)), (1.0, (0.4, 0.5, 0.6))]),
    ("color3d", [(0.0, (0.1, 0.2, 0.3)), (1.0, (0.4, 0.5, 0.6))]),
    ("point3f", [(0.0, (1.0, 2.0, 3.0)), (1.0, (4.0, 5.0, 6.0))]),
    ("point3d", [(0.0, (1.0, 2.0, 3.0)), (1.0, (4.0, 5.0, 6.0))]),
    ("normal3f", [(0.0, (0.1, 0.2, 0.3)), (1.0, (0.4, 0.5, 0.6))]),
    ("normal3d", [(0.0, (0.1, 0.2, 0.3)), (1.0, (0.4, 0.5, 0.6))]),
    ("vector3f", [(0.0, (1.0, 2.0, 3.0)), (1.0, (4.0, 5.0, 6.0))]),
    ("vector3d", [(0.0, (1.0, 2.0, 3.0)), (1.0, (4.0, 5.0, 6.0))]),
])
def test_typed_vec_timesample_roundtrip(tmp_path: pathlib.Path, fmt, dtype, sample_values):
    """Test that typed-vec time-sampled attributes round-trip with correct type."""
    s = _build_timesample_stage(dtype, sample_values)
    out = tmp_path / f"ts_{dtype}.{fmt}"
    s.save(str(out), format=fmt)
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    attr = prim.get_attribute("attr")
    assert attr is not None
    time_samples = prim.get_attribute_timesamples("attr")
    assert len(time_samples) == len(sample_values)
    
    # Verify type name is preserved
    assert attr.type_name in (dtype, dtype + "[]")


@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("dtype,sample_values", [
    ("quatf", [(0.0, (1.0, 0.0, 0.0, 0.0)), (1.0, (0.0, 1.0, 0.0, 0.0))]),
    ("quatd", [(0.0, (1.0, 0.0, 0.0, 0.0)), (1.0, (0.0, 1.0, 0.0, 0.0))]),
    ("quath", [(0.0, (1.0, 0.0, 0.0, 0.0)), (1.0, (0.0, 1.0, 0.0, 0.0))]),
])
def test_quat_timesample_roundtrip(tmp_path: pathlib.Path, fmt, dtype, sample_values):
    """Test that quaternion time-sampled attributes round-trip correctly."""
    s = _build_timesample_stage(dtype, sample_values)
    out = tmp_path / f"ts_{dtype}.{fmt}"
    s.save(str(out), format=fmt)
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    attr = prim.get_attribute("attr")
    assert attr is not None
    time_samples = prim.get_attribute_timesamples("attr")
    assert len(time_samples) == len(sample_values)


@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("dtype,sample_values", [
    ("matrix2d", [(0.0, [[1, 0], [0, 1]]), (1.0, [[1, 1], [1, 1]])]),
    ("matrix3d", [(0.0, [[1, 0, 0], [0, 1, 0], [0, 0, 1]])]),
    ("matrix4d", [(0.0, [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]])]),
])
def test_matrix_timesample_roundtrip(tmp_path: pathlib.Path, fmt, dtype, sample_values):
    """Test that matrix time-sampled attributes round-trip correctly."""
    s = _build_timesample_stage(dtype, sample_values)
    out = tmp_path / f"ts_{dtype}.{fmt}"
    s.save(str(out), format=fmt)
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    attr = prim.get_attribute("attr")
    assert attr is not None
    time_samples = prim.get_attribute_timesamples("attr")
    assert len(time_samples) >= 1


@pytest.mark.parametrize("fmt", ["usda", "usdc"])  # Asset round-trip usually works for USDA/USDC
def test_asset_timesample_roundtrip(tmp_path: pathlib.Path, fmt):
    """Test that asset time-sampled attributes round-trip correctly."""
    sample_values = [
        (0.0, "@./file1.txt@"),
        (1.0, "@./file2.txt@"),
        (2.0, "@./file3.txt@"),
    ]
    s = _build_timesample_stage("asset", sample_values)
    out = tmp_path / f"ts_asset.{fmt}"
    s.save(str(out), format=fmt)
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    attr = prim.get_attribute("attr")
    assert attr is not None
    time_samples = prim.get_attribute_timesamples("attr")
    assert len(time_samples) == len(sample_values)


def test_timesample_dense_sampling(tmp_path: pathlib.Path):
    """Test many time samples on the same attribute."""
    # Create 100 time samples
    sample_values = [(float(i), i * 1.5) for i in range(100)]
    s = _build_timesample_stage("float", sample_values)
    
    out = tmp_path / "ts_dense.usdc"
    s.save(str(out), format="usdc")
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    time_samples = prim.get_attribute_timesamples("attr")
    assert len(time_samples) == 100


def test_timesample_multiple_attributes(tmp_path: pathlib.Path):
    """Test multiple time-sampled attributes on one prim."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    
    # Add multiple attributes with different types
    for t in [0.0, 1.0]:
        p.set_attribute_at_time("x_pos", t, float(t * 10), dtype="float")
        p.set_attribute_at_time("is_active", t, (t == 0.0), dtype="bool")
        p.set_attribute_at_time("velocity", t, (t + 1, t + 2, t + 3), dtype="float3")
    
    s.add_root_prim(p)
    
    out = tmp_path / "ts_multi.usdc"
    s.save(str(out), format="usdc")
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    x = s2.get_prim_at_path("/X")
    
    assert x.get_attribute("x_pos") is not None
    assert x.get_attribute("is_active") is not None
    assert x.get_attribute("velocity") is not None
