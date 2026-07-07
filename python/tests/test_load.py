# SPDX-License-Identifier: Apache-2.0
import pytest

import tinyusdz


def test_loads_and_repr(simple_stage):
    st = simple_stage
    assert len(st) > 0
    assert "Stage(" in repr(st)
    assert st.up_axis == "Y"
    assert st.meters_per_unit == 1.0
    assert st.default_prim_path == "World"
    assert st.default_prim.name == "World"


def test_load_files(assets_dir):
    count = 0
    for name in ("cube.usda", "cube-000.usda"):
        path = assets_dir / name
        if not path.is_file():
            continue
        st = tinyusdz.load(path)  # accepts pathlib.Path
        assert len(st) > 0
        st.close()
        count += 1
    if not count:
        pytest.skip("no known assets present")


def test_export_reload_bytes(simple_stage):
    usdc = simple_stage.export_usdc()
    assert usdc[:8] == b"PXR-USDC"
    st = tinyusdz.load_bytes(usdc)
    assert st.prim_at("/World/Quad")["radius"] == 2.5

    usda = simple_stage.export_usda()
    st2 = tinyusdz.loads(usda)
    assert st2.prim_at("/World/Quad")["radius"] == 2.5


def test_save_load_roundtrip(tmp_path, simple_stage):
    for ext in ("usda", "usdc", "usdz"):
        fn = tmp_path / f"scene.{ext}"
        simple_stage.save(str(fn))
        assert fn.is_file()
        st = tinyusdz.load(fn)
        assert st.prim_at("/World/Quad")["radius"] == 2.5
        st.close()


def test_load_errors():
    with pytest.raises(tinyusdz.UsdError):
        tinyusdz.load("/nonexistent/path/to/file.usda")
    with pytest.raises((tinyusdz.UsdParseError, ValueError)):
        tinyusdz.load_bytes(b"\xff\xfe\x00garbage-bytes-here")
    with pytest.raises(tinyusdz.UsdParseError):
        tinyusdz.loads("#usda 1.0\ndef Xform {")  # malformed


def test_exception_hierarchy():
    assert issubclass(tinyusdz.UsdParseError, tinyusdz.UsdError)
    assert issubclass(tinyusdz.UsdParseError, ValueError)
    assert issubclass(tinyusdz.UsdIoError, tinyusdz.UsdError)
    assert issubclass(tinyusdz.UsdIoError, OSError)
    assert issubclass(tinyusdz.StaleHandleError, tinyusdz.UsdError)


def test_context_manager():
    with tinyusdz.loads("#usda 1.0\ndef Xform \"a\" {}") as st:
        assert "/a" in st
    with pytest.raises(tinyusdz.UsdError):
        st.prim_at("/a")  # closed


def test_is_usd(tmp_path, simple_stage):
    fn = tmp_path / "x.usdc"
    simple_stage.save(str(fn))
    assert tinyusdz.is_usd(fn)
    bad = tmp_path / "bad.usda"
    bad.write_text("not usd at all {")
    assert not tinyusdz.is_usd(bad)
