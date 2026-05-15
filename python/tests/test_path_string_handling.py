"""Prim path / asset path string handling and escaping.

Asset paths use `@...@` delimiters; long paths are normalized; namespaced
attribute names (`prefix:name`) parse correctly.
"""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_relative_asset_path(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    asset a = @./relative/path/file.png@
}
''')
    assert "@./relative/path/file.png@" in txt


def test_absolute_asset_path(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    asset a = @/abs/path/to/file.exr@
}
''')
    assert "@/abs/path/to/file.exr@" in txt


def test_asset_path_with_query(tmp_path):
    """File paths with no actual extension still parse."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    asset a = @./generated@
}
''')
    assert "@./generated@" in txt


def test_namespaced_attribute_names(tmp_path):
    """Attribute names like `primvars:displayColor` use `:` separators."""
    txt = _rt(tmp_path, '''#usda 1.0
def Mesh "M" {
    color3f[] primvars:displayColor = [(1, 0, 0)] (
        interpolation = "constant"
    )
    int[] primvars:displayColor:indices = [0]
    string custom:user:tag = "tagged"
}
''')
    assert "primvars:displayColor" in txt
    assert "primvars:displayColor:indices" in txt
    assert "custom:user:tag" in txt


def test_long_prim_path(tmp_path):
    """Deep prim paths preserve all segments."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "very" {
    def Xform "long" {
        def Xform "deep" {
            def Xform "path" {
                def Xform "leaf" {
                    rel link = </very/long/deep/path/leaf>
                }
            }
        }
    }
}
''')
    assert "</very/long/deep/path/leaf>" in txt


def test_prim_name_with_underscore_and_digit(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "_underscore_first" {}
def Xform "trailing_digit_42" {}
def Xform "Mixed_Case_Name" {}
''')
    assert "_underscore_first" in txt
    assert "trailing_digit_42" in txt
    assert "Mixed_Case_Name" in txt


def test_array_of_asset_paths(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    asset[] textures = [@./a.png@, @./b.png@, @./c.png@]
}
''')
    assert "@./a.png@" in txt
    assert "@./b.png@" in txt
    assert "@./c.png@" in txt
