"""Comprehensive test suite for all USD value types with format round-tripping.

This test ensures all value types (scalars, vectors, typed-vecs, matrices,
quaternions, texCoord, frame4d, and their array forms) round-trip correctly
across USDA, USDC, and USDZ formats.
"""
from __future__ import annotations

import pathlib
import pytest

import tinyusdz


FORMATS = ["usda", "usdc", "usdz"]


def _build_attr_stage(attr_name: str, value, dtype=None) -> tinyusdz.Stage:
    """Build a stage with a single attribute of the given value."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    
    if dtype:
        p.set_attribute(attr_name, value, dtype=dtype)
    else:
        p.set_attribute(attr_name, value)
    
    s.add_root_prim(p)
    return s


# ============================================================================
# Basic Scalar Types
# ============================================================================

@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("value,expected_type", [
    (42, "int"),
    (3.14, "float"),
    (2.718, "double"),
    (True, "bool"),
    ("hello", "string"),
    ("token_value", "token"),
])
def test_scalar_value_roundtrip(tmp_path: pathlib.Path, fmt, value, expected_type):
    """Test basic scalar types round-trip."""
    s = _build_attr_stage("attr", value)
    out = tmp_path / f"scalar_{expected_type}.{fmt}"
    s.save(str(out), format=fmt)
    
    s2 = tinyusdz.load(str(out))
    attr = s2.get_prim_at_path("/X").get_attribute("attr")
    assert attr is not None


# ============================================================================
# Numeric Arrays
# ============================================================================

@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("value,dtype,expected_type", [
    ([1, 2, 3], "int[]", "int[]"),
    ([1.5, 2.5, 3.5], "float[]", "float[]"),
    ([1.5, 2.5], "double[]", "double[]"),
    ([True, False, True], "bool[]", "bool[]"),
    ([1, 2, 3], "uint[]", "uint[]"),
    ([1000000, 2000000], "int64[]", "int64[]"),
    ([1000000, 2000000], "uint64[]", "uint64[]"),
    ([1.0, 2.0, 3.0], "half[]", "half[]"),
])
def test_array_value_roundtrip(tmp_path: pathlib.Path, fmt, value, dtype, expected_type):
    """Test numeric array types round-trip."""
    s = _build_attr_stage("attr", value, dtype=dtype)
    out = tmp_path / f"array_{expected_type}.{fmt}"
    s.save(str(out), format=fmt)
    
    s2 = tinyusdz.load(str(out))
    attr = s2.get_prim_at_path("/X").get_attribute("attr")
    assert attr is not None
    assert attr.type_name == expected_type


# ============================================================================
# Vector Types (float2, float3, float4, double*, etc.)
# ============================================================================

@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("value,expected_type", [
    ((1, 2), "int2"),
    ((1, 2, 3), "int3"),
    ((1, 2, 3, 4), "int4"),
    ((1.5, 2.5), "float2"),
    ((1.5, 2.5, 3.5), "float3"),
    ((1.5, 2.5, 3.5, 4.5), "float4"),
    ((1.5, 2.5), "double2"),
    ((1.5, 2.5, 3.5), "double3"),
    ((1.5, 2.5, 3.5, 4.5), "double4"),
])
def test_vector_value_roundtrip(tmp_path: pathlib.Path, fmt, value, expected_type):
    """Test vector types round-trip."""
    s = _build_attr_stage("attr", value)
    out = tmp_path / f"vec_{expected_type}.{fmt}"
    s.save(str(out), format=fmt)
    
    s2 = tinyusdz.load(str(out))
    attr = s2.get_prim_at_path("/X").get_attribute("attr")
    assert attr is not None


# ============================================================================
# Vector Arrays (float2[], float3[], etc.)
# ============================================================================

@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("value,expected_type", [
    ([[1, 2], [3, 4]], "int2[]"),
    ([[1, 2, 3], [4, 5, 6]], "int3[]"),
    ([[1, 2, 3, 4], [5, 6, 7, 8]], "int4[]"),
    ([[1.5, 2.5], [3.5, 4.5]], "float2[]"),
    ([[1.5, 2.5, 3.5]], "float3[]"),
    ([[1.5, 2.5, 3.5, 4.5]], "float4[]"),
    ([[1.5, 2.5]], "double2[]"),
    ([[1.5, 2.5, 3.5]], "double3[]"),
    ([[1.5, 2.5, 3.5, 4.5]], "double4[]"),
])
def test_vector_array_value_roundtrip(tmp_path: pathlib.Path, fmt, value, expected_type):
    """Test vector array types round-trip."""
    s = _build_attr_stage("attr", value)
    out = tmp_path / f"vecarr_{expected_type}.{fmt}"
    s.save(str(out), format=fmt)
    
    s2 = tinyusdz.load(str(out))
    attr = s2.get_prim_at_path("/X").get_attribute("attr")
    assert attr is not None


# ============================================================================
# Typed-Vec Types (color3f, point3f, normal3f, vector3f, etc.)
# ============================================================================

@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("value,dtype,expected_type", [
    ((0.1, 0.2, 0.3), "color3f", "color3f"),
    ((0.1, 0.2, 0.3), "color3d", "color3d"),
    ((1.0, 2.0, 3.0), "point3f", "point3f"),
    ((1.0, 2.0, 3.0), "point3d", "point3d"),
    ((0.1, 0.2, 0.3), "normal3f", "normal3f"),
    ((0.1, 0.2, 0.3), "normal3d", "normal3d"),
    ((1.0, 2.0, 3.0), "vector3f", "vector3f"),
    ((1.0, 2.0, 3.0), "vector3d", "vector3d"),
])
def test_typed_vec_value_roundtrip(tmp_path: pathlib.Path, fmt, value, dtype, expected_type):
    """Test typed-vec scalar types round-trip with correct type name."""
    s = _build_attr_stage("attr", value, dtype=dtype)
    out = tmp_path / f"tv_{expected_type}.{fmt}"
    s.save(str(out), format=fmt)
    
    s2 = tinyusdz.load(str(out))
    attr = s2.get_prim_at_path("/X").get_attribute("attr")
    assert attr is not None
    assert attr.type_name == expected_type


# ============================================================================
# Typed-Vec Array Types (color3f[], point3f[], etc.)
# ============================================================================

@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("value,dtype,expected_type", [
    ([[0.1, 0.2, 0.3]], "color3f[]", "color3f[]"),
    ([[0.1, 0.2, 0.3]], "color3d[]", "color3d[]"),
    ([[1, 2, 3], [4, 5, 6]], "point3f[]", "point3f[]"),
    ([[1, 2, 3]], "point3d[]", "point3d[]"),
    ([[0.1, 0.2, 0.3]], "normal3f[]", "normal3f[]"),
    ([[0.1, 0.2, 0.3]], "normal3d[]", "normal3d[]"),
    ([[1, 2, 3]], "vector3f[]", "vector3f[]"),
    ([[1, 2, 3]], "vector3d[]", "vector3d[]"),
])
def test_typed_vec_array_value_roundtrip(tmp_path: pathlib.Path, fmt, value, dtype, expected_type):
    """Test typed-vec array types round-trip."""
    s = _build_attr_stage("attr", value, dtype=dtype)
    out = tmp_path / f"tva_{expected_type}.{fmt}"
    s.save(str(out), format=fmt)
    
    s2 = tinyusdz.load(str(out))
    attr = s2.get_prim_at_path("/X").get_attribute("attr")
    assert attr is not None
    assert attr.type_name == expected_type


# ============================================================================
# TexCoord Types (NEW!)
# ============================================================================

@pytest.mark.parametrize("fmt", ["usda", "usdc"])
@pytest.mark.parametrize("value,dtype,expected_type", [
    ((0.1, 0.2), "texCoord2f", "texCoord2f"),
    ((0.1, 0.2), "texCoord2d", "texCoord2d"),
    ((0.1, 0.2, 0.3), "texCoord3f", "texCoord3f"),
    ((0.1, 0.2, 0.3), "texCoord3d", "texCoord3d"),
])
def test_texcoord_value_roundtrip(tmp_path: pathlib.Path, fmt, value, dtype, expected_type):
    """Test texCoord scalar types round-trip."""
    s = _build_attr_stage("attr", value, dtype=dtype)
    out = tmp_path / f"tc_{expected_type}.{fmt}"
    s.save(str(out), format=fmt)
    
    s2 = tinyusdz.load(str(out))
    attr = s2.get_prim_at_path("/X").get_attribute("attr")
    assert attr is not None
    assert attr.type_name == expected_type


@pytest.mark.parametrize("fmt", ["usda", "usdc"])
@pytest.mark.parametrize("value,dtype,expected_type", [
    ([[0.1, 0.2]], "texCoord2f[]", "texCoord2f[]"),
    ([[0.1, 0.2]], "texCoord2d[]", "texCoord2d[]"),
    ([[0.1, 0.2, 0.3]], "texCoord3f[]", "texCoord3f[]"),
    ([[0.1, 0.2, 0.3]], "texCoord3d[]", "texCoord3d[]"),
])
def test_texcoord_array_value_roundtrip(tmp_path: pathlib.Path, fmt, value, dtype, expected_type):
    """Test texCoord array types round-trip."""
    s = _build_attr_stage("attr", value, dtype=dtype)
    out = tmp_path / f"tca_{expected_type}.{fmt}"
    s.save(str(out), format=fmt)
    
    s2 = tinyusdz.load(str(out))
    attr = s2.get_prim_at_path("/X").get_attribute("attr")
    assert attr is not None
    assert attr.type_name == expected_type


# ============================================================================
# Matrix Types
# ============================================================================

@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("dtype,expected_type", [
    ("matrix2d", "matrix2d"),
    ("matrix3d", "matrix3d"),
    ("matrix4d", "matrix4d"),
])
def test_matrix_value_roundtrip(tmp_path: pathlib.Path, fmt, dtype, expected_type):
    """Test matrix types round-trip."""
    identity_4x4 = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
    identity_3x3 = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
    identity_2x2 = [[1, 0], [0, 1]]
    
    if dtype == "matrix2d":
        value = identity_2x2
    elif dtype == "matrix3d":
        value = identity_3x3
    else:
        value = identity_4x4
    
    s = _build_attr_stage("attr", value, dtype=dtype)
    out = tmp_path / f"mat_{expected_type}.{fmt}"
    s.save(str(out), format=fmt)
    
    s2 = tinyusdz.load(str(out))
    attr = s2.get_prim_at_path("/X").get_attribute("attr")
    assert attr is not None


# ============================================================================
# frame4d Type (NEW!)
# ============================================================================

@pytest.mark.parametrize("fmt", ["usda", "usdc"])
def test_frame4d_value_roundtrip(tmp_path: pathlib.Path, fmt):
    """Test frame4d type round-trips correctly."""
    identity_4x4 = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
    s = _build_attr_stage("attr", identity_4x4, dtype="frame4d")
    out = tmp_path / f"frame4d.{fmt}"
    s.save(str(out), format=fmt)
    
    s2 = tinyusdz.load(str(out))
    attr = s2.get_prim_at_path("/X").get_attribute("attr")
    assert attr is not None
    assert attr.type_name == "frame4d"


@pytest.mark.parametrize("fmt", ["usda", "usdc"])
def test_frame4d_array_value_roundtrip(tmp_path: pathlib.Path, fmt):
    """Test frame4d[] type round-trips correctly."""
    identity_4x4 = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
    s = _build_attr_stage("attr", [identity_4x4], dtype="frame4d[]")
    out = tmp_path / f"frame4d_arr.{fmt}"
    s.save(str(out), format=fmt)
    
    s2 = tinyusdz.load(str(out))
    attr = s2.get_prim_at_path("/X").get_attribute("attr")
    assert attr is not None
    assert attr.type_name == "frame4d[]"


# ============================================================================
# Quaternion Types
# ============================================================================

@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("dtype,expected_type", [
    ("quatf", "quatf"),
    ("quatd", "quatd"),
    ("quath", "quath"),
])
def test_quat_value_roundtrip(tmp_path: pathlib.Path, fmt, dtype, expected_type):
    """Test quaternion types round-trip."""
    # (w, x, y, z)
    value = (1.0, 0.0, 0.0, 0.0)
    s = _build_attr_stage("attr", value, dtype=dtype)
    out = tmp_path / f"quat_{expected_type}.{fmt}"
    s.save(str(out), format=fmt)
    
    s2 = tinyusdz.load(str(out))
    attr = s2.get_prim_at_path("/X").get_attribute("attr")
    assert attr is not None
    assert attr.type_name == expected_type


@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("dtype,expected_type", [
    ("quatf[]", "quatf[]"),
    ("quatd[]", "quatd[]"),
    ("quath[]", "quath[]"),
])
def test_quat_array_value_roundtrip(tmp_path: pathlib.Path, fmt, dtype, expected_type):
    """Test quaternion array types round-trip."""
    # Array of (w, x, y, z)
    value = [(1.0, 0.0, 0.0, 0.0), (0.0, 1.0, 0.0, 0.0)]
    s = _build_attr_stage("attr", value, dtype=dtype)
    out = tmp_path / f"quatarr_{expected_type}.{fmt}"
    s.save(str(out), format=fmt)
    
    s2 = tinyusdz.load(str(out))
    attr = s2.get_prim_at_path("/X").get_attribute("attr")
    assert attr is not None
    assert attr.type_name == expected_type


# ============================================================================
# Special Types (asset, token, string)
# ============================================================================

@pytest.mark.parametrize("fmt", ["usda", "usdc"])
def test_asset_value_roundtrip(tmp_path: pathlib.Path, fmt):
    """Test asset type round-trips."""
    s = _build_attr_stage("attr", "@./image.png@", dtype="asset")
    out = tmp_path / f"asset.{fmt}"
    s.save(str(out), format=fmt)
    
    s2 = tinyusdz.load(str(out))
    attr = s2.get_prim_at_path("/X").get_attribute("attr")
    assert attr is not None


@pytest.mark.parametrize("fmt", FORMATS)
def test_asset_array_value_roundtrip(tmp_path: pathlib.Path, fmt):
    """Test asset[] type round-trips."""
    s = _build_attr_stage("attr", ["@./img1.png@", "@./img2.png@"], dtype="asset[]")
    out = tmp_path / f"assetarr.{fmt}"
    s.save(str(out), format=fmt)
    
    s2 = tinyusdz.load(str(out))
    attr = s2.get_prim_at_path("/X").get_attribute("attr")
    assert attr is not None


@pytest.mark.parametrize("fmt", FORMATS)
def test_token_array_value_roundtrip(tmp_path: pathlib.Path, fmt):
    """Test token[] type round-trips."""
    s = _build_attr_stage("attr", ["a", "b", "c"], dtype="token[]")
    out = tmp_path / f"tokenarr.{fmt}"
    s.save(str(out), format=fmt)
    
    s2 = tinyusdz.load(str(out))
    attr = s2.get_prim_at_path("/X").get_attribute("attr")
    assert attr is not None
    assert attr.type_name == "token[]"


@pytest.mark.parametrize("fmt", FORMATS)
def test_string_array_value_roundtrip(tmp_path: pathlib.Path, fmt):
    """Test string[] type round-trips."""
    s = _build_attr_stage("attr", ["hello", "world"], dtype="string[]")
    out = tmp_path / f"stringarr.{fmt}"
    s.save(str(out), format=fmt)
    
    s2 = tinyusdz.load(str(out))
    attr = s2.get_prim_at_path("/X").get_attribute("attr")
    assert attr is not None
    assert attr.type_name == "string[]"
