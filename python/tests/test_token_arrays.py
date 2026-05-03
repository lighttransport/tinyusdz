"""token and token[] round-trip with various value forms."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_token_simple(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom token kind = "component"
}
''')
    assert '"component"' in txt


def test_token_array(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom token[] phases = ["start", "middle", "end"]
}
''')
    assert '"start"' in txt
    assert '"middle"' in txt
    assert '"end"' in txt


def test_token_with_namespace_value(tmp_path):
    """Token values can contain colons (namespaced names)."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom token role = "primvars:displayColor"
}
''')
    assert '"primvars:displayColor"' in txt


def test_token_empty_array(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom token[] empty = []
}
''')
    assert "token[] empty" in txt


def test_token_with_special_chars(tmp_path):
    """Token values can carry slashes (path-like)."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom token path = "a/b/c"
}
''')
    assert '"a/b/c"' in txt
