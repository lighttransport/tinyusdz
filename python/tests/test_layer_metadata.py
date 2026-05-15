"""Stage/layer metadata: defaultPrim, comment, customLayerData."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_default_prim(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
(
    defaultPrim = "Root"
)
def Xform "Root" {}
def Xform "Other" {}
''')
    assert 'defaultPrim = "Root"' in txt


def test_layer_comment(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
(
    doc = "scene description for episode 7"
)
def Xform "X" {}
''')
    assert "episode 7" in txt


def test_custom_layer_data(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
(
    customLayerData = {
        string author = "alice"
        int frame = 42
    }
)
def Xform "X" {}
''')
    assert "customLayerData" in txt
    assert '"alice"' in txt


def test_metersperunit_and_upaxis(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
(
    metersPerUnit = 0.01
    upAxis = "Z"
)
def Xform "X" {}
''')
    assert "metersPerUnit" in txt
    assert "0.01" in txt
    assert '"Z"' in txt


def test_multiple_roots_no_default(tmp_path):
    """No defaultPrim: both roots survive."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "A" {}
def Xform "B" {}
def Xform "C" {}
''')
    assert '"A"' in txt
    assert '"B"' in txt
    assert '"C"' in txt
