"""Prim `sceneName` metadata (USDZ scene-library extension).

Regression tests for two related bugs fixed together:

1. The Python set_metadata C API was storing `sceneName` as a
   `value::token`, but PrimMetas::get_sceneName() reads via
   `get<std::string>(...)`. The result was a silent type mismatch:
   `set_metadata("sceneName", "Foo")` came back as `""`.

2. The USDC writer was wrapping the value as `token` in the on-disk
   field, but the USDC reader (matching pxrUSD) demands `string`. The
   error surfaced on USDC reload as
   `` `sceneName` must be type `string`, but got type `token` ``.

The fixed reference is the pxrUSD `usdcat` round-trip of
tests/usda/sceneLibrary-001.usda.
"""
import pytest
import tinyusdz


FORMATS = ["usda", "usdc", "usdz"]


def _stage_with_scene_name(name):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="Root")
    p.set_metadata("sceneName", name)
    s.add_root_prim(p)
    return s


def _roundtrip(stage, fmt, tmp_path):
    out = tmp_path / f"x.{fmt}"
    stage.save(str(out))
    return tinyusdz.load(str(out))


@pytest.mark.parametrize("fmt", FORMATS)
def test_scene_name_authoring_roundtrips(tmp_path, fmt):
    s = _stage_with_scene_name("Primary Scene")
    s2 = _roundtrip(s, fmt, tmp_path)
    txt = s2.export_to_string()
    assert "sceneName = \"Primary Scene\"" in txt, (
        f"expected sceneName preserved, got:\n{txt}")


def test_scene_name_in_memory_roundtrip():
    """Even before serialization, get_metadata should return what we
    just set — this is the bug that was silently dropping the value."""
    p = tinyusdz.Prim("Xform", name="Root")
    p.set_metadata("sceneName", "My Scene")
    assert p.get_metadata("sceneName") == "My Scene"


def test_scene_name_with_special_chars(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="Root")
    # Spaces, punctuation, mixed case — pxr permits arbitrary
    # human-readable strings here.
    p.set_metadata("sceneName", "Scene 02 — Final / Reviewed")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    assert "Scene 02 — Final / Reviewed" in s2.export_to_string()


@pytest.mark.parametrize("fmt", FORMATS)
def test_scene_name_on_over_prim(tmp_path, fmt):
    """sceneLibrary-001.usda has sceneName on both `def` and `over`
    prims; mirror the over case via a USDA reload (the Python authoring
    API doesn't expose `over` directly)."""
    src = tmp_path / "src.usda"
    src.write_text('''#usda 1.0
def Xform "Root"
(
    kind = "sceneLibrary"
)
{
    def Xform "PrimaryScene"
    (
        sceneName = "Primary Scene"
    )
    {
    }
    over Xform "SecondaryScene"
    (
        sceneName = "Secondary Scene"
    )
    {
    }
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / f"x.{fmt}"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "sceneName = \"Primary Scene\"" in txt
    assert "sceneName = \"Secondary Scene\"" in txt
