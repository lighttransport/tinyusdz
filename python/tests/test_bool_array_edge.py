"""bool / bool[] round-trip via 0/1 and true/false syntax."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_bool_true_false_words(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom bool a = true
    custom bool b = false
}
''')
    assert "X" in txt


def test_bool_numeric_form(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom bool a = 1
    custom bool b = 0
}
''')
    assert "X" in txt


def test_bool_array_textual(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom bool[] flags = [1, 0, 1, 1, 0]
}
''')
    assert "bool[]" in txt
    assert "flags" in txt


def test_bool_single_element_array(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom bool[] flag = [1]
}
''')
    assert "flag" in txt


def test_bool_mixed_with_other_attrs(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "X" {
    custom bool active = 1
    custom int count = 5
    custom string name = "thing"
}
''')
    assert "active" in txt
    assert "count = 5" in txt
    assert '"thing"' in txt
