"""Test time-sampled array attributes across various array types and formats.

This suite focuses on array types (float[], int64[], uint64[], bool[], half[], 
and typed-vec arrays like color3f[], point3f[], etc.) with time samples.
"""
from __future__ import annotations

import pathlib
import pytest
import numpy as np

import tinyusdz


FORMATS = ["usda", "usdc", "usdz"]


def _build_timesample_array_stage(dtype, sample_values) -> tinyusdz.Stage:
    """Build a stage with a single time-sampled array attribute.
    
    Args:
        dtype: The USD array type name (e.g., 'float[]', 'int64[]', 'color3f[]')
        sample_values: List of (time, array_value) tuples
    """
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    
    for time, value in sample_values:
        p.set_attribute_at_time("data", time, value, dtype=dtype)
    
    s.add_root_prim(p)
    return s


@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("dtype,sample_values", [
    ("int[]", [(0.0, [1, 2, 3]), (1.0, [4, 5, 6]), (2.0, [7, 8, 9])]),
    ("float[]", [(0.0, [1.5, 2.5, 3.5]), (1.0, [4.5, 5.5, 6.5])]),
    ("double[]", [(0.0, [1.5, 2.5]), (1.0, [3.5, 4.5])]),
    ("uint[]", [(0.0, [1, 2, 3]), (1.0, [4, 5, 6])]),
])
def test_basic_array_timesample_roundtrip(tmp_path: pathlib.Path, fmt, dtype, sample_values):
    """Test that basic array time-sampled attributes round-trip correctly."""
    s = _build_timesample_array_stage(dtype, sample_values)
    out = tmp_path / f"ts_arr_{dtype}.{fmt}"
    s.save(str(out), format=fmt)
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    attr = prim.get_attribute("data")
    assert attr is not None
    time_samples = prim.get_attribute_timesamples("data")
    assert len(time_samples) == len(sample_values)


@pytest.mark.parametrize("fmt", ["usda", "usdc"])
def test_int64_array_timesample(tmp_path: pathlib.Path, fmt):
    """Test int64[] time-sampled attributes."""
    sample_values = [
        (0.0, [1000000, 2000000, 3000000]),
        (1.0, [-1000000, 0, 1000000]),
    ]
    s = _build_timesample_array_stage("int64[]", sample_values)
    out = tmp_path / f"ts_int64arr.{fmt}"
    s.save(str(out), format=fmt)
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    attr = prim.get_attribute("data")
    assert attr is not None
    time_samples = prim.get_attribute_timesamples("data")
    assert len(time_samples) == 2


@pytest.mark.parametrize("fmt", ["usda", "usdc"])
def test_uint64_array_timesample(tmp_path: pathlib.Path, fmt):
    """Test uint64[] time-sampled attributes."""
    sample_values = [
        (0.0, [1000000, 2000000, 3000000]),
        (1.0, [100, 200, 300]),
    ]
    s = _build_timesample_array_stage("uint64[]", sample_values)
    out = tmp_path / f"ts_uint64arr.{fmt}"
    s.save(str(out), format=fmt)
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    attr = prim.get_attribute("data")
    assert attr is not None
    time_samples = prim.get_attribute_timesamples("data")
    assert len(time_samples) == 2


@pytest.mark.parametrize("fmt", ["usda", "usdc"])
def test_bool_array_timesample(tmp_path: pathlib.Path, fmt):
    """Test bool[] time-sampled attributes."""
    sample_values = [
        (0.0, [True, False, True]),
        (1.0, [False, False, False]),
        (2.0, [True, True, True]),
    ]
    s = _build_timesample_array_stage("bool[]", sample_values)
    out = tmp_path / f"ts_boolarr.{fmt}"
    s.save(str(out), format=fmt)
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    attr = prim.get_attribute("data")
    assert attr is not None
    time_samples = prim.get_attribute_timesamples("data")
    assert len(time_samples) == 3


@pytest.mark.parametrize("fmt", ["usda", "usdc"])
def test_half_array_timesample(tmp_path: pathlib.Path, fmt):
    """Test half[] time-sampled attributes."""
    sample_values = [
        (0.0, [1.0, 2.0, 3.0]),
        (1.0, [4.0, 5.0, 6.0]),
    ]
    s = _build_timesample_array_stage("half[]", sample_values)
    out = tmp_path / f"ts_halfarr.{fmt}"
    s.save(str(out), format=fmt)
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    attr = prim.get_attribute("data")
    assert attr is not None
    time_samples = prim.get_attribute_timesamples("data")
    assert len(time_samples) == 2


@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("dtype,sample_values", [
    ("float2[]", [(0.0, [[1, 2], [3, 4]]), (1.0, [[5, 6], [7, 8]])]),
    ("float3[]", [(0.0, [[1, 2, 3], [4, 5, 6]]), (1.0, [[7, 8, 9], [10, 11, 12]])]),
    ("float4[]", [(0.0, [[1, 2, 3, 4], [5, 6, 7, 8]])]),
    ("double2[]", [(0.0, [[1.5, 2.5], [3.5, 4.5]])]),
    ("double3[]", [(0.0, [[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]])]),
    ("double4[]", [(0.0, [[1.0, 2.0, 3.0, 4.0], [5.0, 6.0, 7.0, 8.0]])]),
])
def test_vector_array_timesample_roundtrip(tmp_path: pathlib.Path, fmt, dtype, sample_values):
    """Test that vector array time-sampled attributes round-trip correctly."""
    s = _build_timesample_array_stage(dtype, sample_values)
    out = tmp_path / f"ts_{dtype}.{fmt}"
    s.save(str(out), format=fmt)
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    attr = prim.get_attribute("data")
    assert attr is not None
    time_samples = prim.get_attribute_timesamples("data")
    assert len(time_samples) == len(sample_values)


@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("dtype,sample_values", [
    ("color3f[]", [(0.0, [[0.1, 0.2, 0.3], [0.4, 0.5, 0.6]])]),
    ("color3d[]", [(0.0, [[0.1, 0.2, 0.3], [0.4, 0.5, 0.6]])]),
    ("point3f[]", [(0.0, [[1, 2, 3], [4, 5, 6]])]),
    ("point3d[]", [(0.0, [[1, 2, 3], [4, 5, 6]])]),
    ("normal3f[]", [(0.0, [[0.1, 0.2, 0.3], [0.4, 0.5, 0.6]])]),
    ("normal3d[]", [(0.0, [[0.1, 0.2, 0.3], [0.4, 0.5, 0.6]])]),
    ("vector3f[]", [(0.0, [[1, 2, 3], [4, 5, 6]])]),
    ("vector3d[]", [(0.0, [[1, 2, 3], [4, 5, 6]])]),
])
def test_typed_vec_array_timesample_roundtrip(tmp_path: pathlib.Path, fmt, dtype, sample_values):
    """Test that typed-vec array time-sampled attributes round-trip with correct type."""
    s = _build_timesample_array_stage(dtype, sample_values)
    out = tmp_path / f"ts_{dtype}.{fmt}"
    s.save(str(out), format=fmt)
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    attr = prim.get_attribute("data")
    assert attr is not None
    time_samples = prim.get_attribute_timesamples("data")
    assert len(time_samples) == len(sample_values)
    assert attr.type_name == dtype


@pytest.mark.parametrize("fmt", ["usda", "usdc"])
def test_texcoord_array_timesample(tmp_path: pathlib.Path, fmt):
    """Test texCoord array time-sampled attributes (texCoord2f[], texCoord3f[], etc)."""
    for dtype, sample in [
        ("texCoord2f[]", [(0.0, [[0.1, 0.2], [0.3, 0.4]])]),
        ("texCoord2d[]", [(0.0, [[0.1, 0.2], [0.3, 0.4]])]),
        ("texCoord3f[]", [(0.0, [[0.1, 0.2, 0.3], [0.4, 0.5, 0.6]])]),
        ("texCoord3d[]", [(0.0, [[0.1, 0.2, 0.3], [0.4, 0.5, 0.6]])]),
    ]:
        s = _build_timesample_array_stage(dtype, sample)
        out = tmp_path / f"ts_{dtype}.{fmt}"
        s.save(str(out), format=fmt)
        
        # Reload and verify
        s2 = tinyusdz.load(str(out))
        prim = s2.get_prim_at_path("/X")
        attr = prim.get_attribute("data")
        assert attr is not None
        time_samples = prim.get_attribute_timesamples("data")
        assert len(time_samples) >= 1


@pytest.mark.parametrize("fmt", ["usda", "usdc"])
def test_frame4d_array_timesample(tmp_path: pathlib.Path, fmt):
    """Test frame4d[] time-sampled attributes."""
    identity_4x4 = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
    sample_values = [(0.0, [identity_4x4])]
    
    s = _build_timesample_array_stage("frame4d[]", sample_values)
    out = tmp_path / f"ts_frame4d_arr.{fmt}"
    s.save(str(out), format=fmt)
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    attr = prim.get_attribute("data")
    assert attr is not None
    time_samples = prim.get_attribute_timesamples("data")
    assert len(time_samples) >= 1


@pytest.mark.parametrize("fmt", FORMATS)
def test_token_array_timesample(tmp_path: pathlib.Path, fmt):
    """Test token[] time-sampled attributes."""
    sample_values = [
        (0.0, ["a", "b", "c"]),
        (1.0, ["x", "y", "z"]),
    ]
    s = _build_timesample_array_stage("token[]", sample_values)
    out = tmp_path / f"ts_tokenarr.{fmt}"
    s.save(str(out), format=fmt)
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    attr = prim.get_attribute("data")
    assert attr is not None
    time_samples = prim.get_attribute_timesamples("data")
    assert len(time_samples) == 2


@pytest.mark.parametrize("fmt", FORMATS)
def test_string_array_timesample(tmp_path: pathlib.Path, fmt):
    """Test string[] time-sampled attributes."""
    sample_values = [
        (0.0, ["hello", "world"]),
        (1.0, ["foo", "bar"]),
    ]
    s = _build_timesample_array_stage("string[]", sample_values)
    out = tmp_path / f"ts_stringarr.{fmt}"
    s.save(str(out), format=fmt)
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    attr = prim.get_attribute("data")
    assert attr is not None
    time_samples = prim.get_attribute_timesamples("data")
    assert len(time_samples) == 2


def test_large_array_timesample(tmp_path: pathlib.Path):
    """Test time-sampled attribute with large array."""
    # Create a 1000-element array
    large_array = list(range(1000))
    sample_values = [
        (0.0, large_array),
        (1.0, [x * 2 for x in large_array]),
    ]
    
    s = _build_timesample_array_stage("int[]", sample_values)
    out = tmp_path / "ts_large.usdc"
    s.save(str(out), format="usdc")
    
    # Reload and verify
    s2 = tinyusdz.load(str(out))
    prim = s2.get_prim_at_path("/X")
    attr = prim.get_attribute("data")
    assert attr is not None
    time_samples = prim.get_attribute_timesamples("data")
    assert len(time_samples) == 2
