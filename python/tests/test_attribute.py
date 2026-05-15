"""Attribute / Value tests including buffer protocol."""
from __future__ import annotations

import pathlib

import pytest

import tinyusdz


def test_attribute_basic(tiny_usda: pathlib.Path):
    stage = tinyusdz.load(str(tiny_usda))
    mesh = stage.get_prim_at_path("/World/Mesh")
    assert mesh is not None

    names = set(mesh.property_names())
    assert "points" in names
    assert "faceVertexIndices" in names

    points = mesh.get_attribute("points")
    assert points is not None
    assert points.name == "points"
    # USD `point3f[]` or similar — exact token is up to core. Just confirm
    # it advertises a type.
    assert isinstance(points.type_name, str) and points.type_name


def test_value_array_buffer_protocol(tiny_usda: pathlib.Path):
    stage = tinyusdz.load(str(tiny_usda))
    mesh = stage.get_prim_at_path("/World/Mesh")
    idx_attr = mesh.get_attribute("faceVertexIndices")
    assert idx_attr is not None
    val = idx_attr.value
    assert val is not None
    assert val.is_array is True

    mv = memoryview(val)
    assert mv.itemsize == 4            # int[]
    assert len(mv) == 3
    assert list(mv) == [0, 1, 2]


def test_value_scalar_read(tiny_usda: pathlib.Path):
    stage = tinyusdz.load(str(tiny_usda))
    mesh = stage.get_prim_at_path("/World/Mesh")
    # faceVertexCounts is int[], verify the array path works.
    counts = mesh.get_attribute("faceVertexCounts")
    assert counts is not None
    v = counts.value
    assert v.is_array
    assert list(memoryview(v)) == [3]


def test_numpy_zero_copy_if_installed(tiny_usda: pathlib.Path):
    np = pytest.importorskip("numpy")
    stage = tinyusdz.load(str(tiny_usda))
    mesh = stage.get_prim_at_path("/World/Mesh")
    v = mesh.get_attribute("faceVertexIndices").value
    arr = np.asarray(v)
    assert arr.dtype == np.int32
    assert arr.shape == (3,)
    assert list(arr) == [0, 1, 2]
