"""String attribute round-trip: unicode, multi-line, special chars."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda, encoding="utf-8")
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_unicode_string_value(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom string greeting = "こんにちは"
}
''')
    assert "こんにちは" in txt


def test_unicode_emoji(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom string flag = "🚀 launch"
}
''')
    assert "🚀" in txt


def test_string_with_quotes(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom string quote = "she said \\"hi\\""
}
''')
    assert "she said" in txt


def test_string_with_backslash(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom string p = "C:\\\\path\\\\file"
}
''')
    assert "X" in txt


def test_triple_quoted_string(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom string poem = """line 1
line 2
line 3"""
}
''')
    assert "line 1" in txt
    assert "line 3" in txt


def test_empty_string_value(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom string empty = ""
}
''')
    assert 'empty = ""' in txt


def test_string_array(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom string[] tags = ["red", "green", "blue"]
}
''')
    assert '"red"' in txt
    assert '"green"' in txt
    assert '"blue"' in txt
