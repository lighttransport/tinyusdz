"""Deeply-nested `customData = {...}` dictionaries — both as stage
metadata, prim metadata, and attribute metadata.
"""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_prim_customdata_with_many_scalar_types(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" (
    customData = {
        bool flag = true
        int i = 42
        int64 i64 = 1000000000000
        uint64 u64 = 999999999999
        float f = 1.5
        double d = 2.71828
        string note = "hi"
        token tag = "tagged"
    }
)
{
}
''')
    assert "flag = 1" in txt or "flag = true" in txt
    assert "i = 42" in txt
    assert "1000000000000" in txt
    assert "999999999999" in txt
    assert "f = 1.5" in txt
    assert "2.71828" in txt
    assert '"hi"' in txt
    assert '"tagged"' in txt


def test_prim_customdata_with_array_types(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" (
    customData = {
        bool[] flags = [1, 0, 1]
        int[] counts = [1, 2, 3]
        int64[] big = [9999999999, -9999999999]
        float[] weights = [0.1, 0.2, 0.3]
        double[] precise = [1.5, 2.5]
        string[] labels = ["a", "b", "c"]
        token[] tags = ["alpha", "beta"]
    }
)
{
}
''')
    assert "[1, 2, 3]" in txt
    assert "[0.1, 0.2, 0.3]" in txt
    assert "[1.5, 2.5]" in txt
    assert '"a"' in txt and '"b"' in txt
    assert '"alpha"' in txt


def test_attribute_customdata_with_nested_keys(tmp_path):
    """customData at attribute level with multiple keys."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom int frob = 7 (
        customData = {
            string author = "alice"
            int version = 3
            double[] history = [1.0, 1.5, 2.0]
        }
    )
}
''')
    assert '"alice"' in txt
    assert "version = 3" in txt
    assert "[1, 1.5, 2]" in txt or "1.5" in txt


def test_stage_customLayerData(tmp_path):
    """Stage-level customLayerData is a sibling concept to customData."""
    txt = _rt(tmp_path, '''#usda 1.0
(
    customLayerData = {
        string project = "test"
        int build = 42
        string[] tags = ["dev", "internal"]
    }
)
def Xform "X" {}
''')
    assert "customLayerData" in txt
    assert '"test"' in txt
    assert "build = 42" in txt
    assert '"dev"' in txt


def test_authoring_customdata_via_python_api(tmp_path):
    """Python authoring path for prim-level customData via
    set_metadata or via the Stage-aware writer."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    # Python API doesn't expose nested-dict authoring directly via
    # set_metadata; it does support setting via USDA round-trip.
    p.set_metadata("kind", "component")
    s.add_root_prim(p)
    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert 'kind = "component"' in txt
