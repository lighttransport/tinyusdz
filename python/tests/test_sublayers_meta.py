"""subLayers stage metadata."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usda"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_subLayers_simple(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
(
    subLayers = [@./layer_a.usda@, @./layer_b.usda@]
)
def Xform "X" {}
''')
    assert "subLayers" in txt
    assert "@./layer_a.usda@" in txt
    assert "@./layer_b.usda@" in txt


def test_subLayers_single(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
(
    subLayers = [@./shared.usda@]
)
def Xform "X" {}
''')
    assert "@./shared.usda@" in txt


def test_subLayers_with_other_metadata(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
(
    defaultPrim = "Root"
    subLayers = [@./extra.usda@]
    upAxis = "Y"
)
def Xform "Root" {}
''')
    assert "subLayers" in txt
    assert "@./extra.usda@" in txt
    assert "defaultPrim" in txt


def test_no_metadata_block(tmp_path):
    """No `(...)` block at the top should still parse."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {}
''')
    assert '"X"' in txt
