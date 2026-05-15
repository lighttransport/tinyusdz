"""Stage-level `subLayers = [...]` parser + USDA round-trip.

The parser is wired (see ascii-parser.cc:2176); this fences USDA→USDA
fidelity. USDC writer/reader for stage-level subLayers is out of scope
of this fence (handled by other coverage if needed).
"""
import tinyusdz


def test_sublayers_single(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
(
    subLayers = [
        @./layer1.usda@
    ]
)
def Xform "X" {}
''')
    s = tinyusdz.load(str(src))
    txt = s.export_to_string()
    assert "subLayers = [@./layer1.usda@]" in txt


def test_sublayers_multiple(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
(
    subLayers = [
        @./layer1.usda@,
        @./layer2.usda@,
        @./layer3.usda@
    ]
)
def Xform "X" {}
''')
    s = tinyusdz.load(str(src))
    txt = s.export_to_string()
    assert "@./layer1.usda@" in txt
    assert "@./layer2.usda@" in txt
    assert "@./layer3.usda@" in txt


def test_sublayers_with_other_metadata(tmp_path):
    """subLayers alongside defaultPrim / upAxis must not interfere."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
(
    defaultPrim = "Hero"
    upAxis = "Y"
    subLayers = [
        @./common.usda@
    ]
)
def Xform "Hero" {}
''')
    s = tinyusdz.load(str(src))
    txt = s.export_to_string()
    assert 'defaultPrim = "Hero"' in txt
    assert 'upAxis = "Y"' in txt
    assert "@./common.usda@" in txt
