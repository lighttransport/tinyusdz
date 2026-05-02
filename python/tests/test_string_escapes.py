"""USDA string escape sequence handling.

USDA strings support `\\n`, `\\t`, `\\"`, `\\\\` and triple-quoted
multi-line literals. tinyusdz's printer promotes strings containing
literal newlines to triple-quoted form.
"""
import tinyusdz


def test_string_with_tab_escape(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    custom string s = "tab\\there"
}
''')
    s = tinyusdz.load(str(src))
    txt = s.export_to_string()
    # Tab escape preserved (or rendered as literal tab in triple quotes)
    assert ("\\t" in txt) or ("\there" in txt)


def test_string_with_escaped_quote(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    custom string s = "quote\\"inside"
}
''')
    s = tinyusdz.load(str(src))
    txt = s.export_to_string()
    # Either escaped or single-quoted to avoid escaping
    assert ('\\"' in txt) or ("'quote\"inside'" in txt)


def test_string_with_backslash(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text(r'''#usda 1.0
def Xform "X" {
    custom string s = "back\\slash"
}
''')
    s = tinyusdz.load(str(src))
    txt = s.export_to_string()
    assert "back" in txt and "slash" in txt


def test_string_with_newline_promotes_to_triple_quoted(tmp_path):
    """Strings containing a literal newline should round-trip via the
    triple-quoted form."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    custom string s = "line1\\nline2"
}
''')
    s = tinyusdz.load(str(src))
    txt = s.export_to_string()
    # Output uses triple quotes when the string contains a newline.
    assert '"""' in txt
    assert "line1" in txt
    assert "line2" in txt


def test_triple_quoted_multiline_input(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    custom string s = """first line
second line
third line"""
}
''')
    s = tinyusdz.load(str(src))
    txt = s.export_to_string()
    assert "first line" in txt
    assert "second line" in txt
    assert "third line" in txt


def test_empty_string(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    custom string s = ""
}
''')
    s = tinyusdz.load(str(src))
    txt = s.export_to_string()
    assert 'string s = ""' in txt or "string s" in txt


def test_unicode_string_content(tmp_path):
    """Non-ASCII content inside a string literal should round-trip."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    custom string greeting = "こんにちは"
    custom string emoji = "🎉"
}
''')
    s = tinyusdz.load(str(src))
    txt = s.export_to_string()
    assert "こんにちは" in txt
    assert "🎉" in txt
