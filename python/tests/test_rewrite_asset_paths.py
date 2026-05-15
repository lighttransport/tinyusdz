"""tinyusdz.rewrite_asset_paths in-place rewriting."""
import tinyusdz


def test_rewrite_single_path(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    asset tex = @./external/diffuse.png@
}
''')
    s = tinyusdz.load(str(src))
    n = tinyusdz.rewrite_asset_paths(
        s, {"./external/diffuse.png": "diffuse.png"})
    assert n == 1
    txt = s.export_to_string()
    assert "diffuse.png" in txt
    assert "./external/diffuse.png" not in txt


def test_rewrite_no_match_count_zero(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    asset tex = @./a.png@
}
''')
    s = tinyusdz.load(str(src))
    n = tinyusdz.rewrite_asset_paths(s, {"./other.png": "x.png"})
    assert n == 0


def test_rewrite_array_of_assets(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    asset[] textures = [@./a.png@, @./b.png@, @./c.png@]
}
''')
    s = tinyusdz.load(str(src))
    n = tinyusdz.rewrite_asset_paths(
        s, {"./a.png": "a.png", "./b.png": "b.png"})
    assert n == 2
    txt = s.export_to_string()
    assert "@a.png@" in txt
    assert "@b.png@" in txt
    assert "@./c.png@" in txt


def test_rewrite_multiple_prims(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "A" {
    asset tex = @./shared.png@
}
def Xform "B" {
    asset tex = @./shared.png@
}
''')
    s = tinyusdz.load(str(src))
    n = tinyusdz.rewrite_asset_paths(s, {"./shared.png": "shared.png"})
    assert n == 2


def test_rewrite_empty_mapping(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    asset tex = @./a.png@
}
''')
    s = tinyusdz.load(str(src))
    n = tinyusdz.rewrite_asset_paths(s, {})
    assert n == 0
