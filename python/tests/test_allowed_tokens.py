"""`allowedTokens = [...]` attribute meta — USDA parsing + USDC round-trip."""
import pytest
import tinyusdz


_USDA = '''#usda 1.0
def Xform "X"
{
    custom token sides = "left" (
        allowedTokens = ["left", "right", "both"]
    )
}
'''


def test_parse_and_emit_allowedtokens(tmp_path):
    p = tmp_path / "a.usda"
    p.write_text(_USDA)
    s = tinyusdz.load(str(p))
    txt = s.export_to_string()
    assert "allowedTokens" in txt
    assert "\"left\"" in txt
    assert "\"right\"" in txt
    assert "\"both\"" in txt


def test_allowedtokens_survives_usdc_roundtrip(tmp_path):
    src = tmp_path / "a.usda"
    src.write_text(_USDA)
    s = tinyusdz.load(str(src))
    out = tmp_path / "a.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "allowedTokens" in txt
    assert "\"left\"" in txt
    assert "\"right\"" in txt
    assert "\"both\"" in txt


def test_unknown_attr_meta_still_errors(tmp_path):
    """Sanity: `allowedTokens` registration doesn't accidentally accept
    arbitrary attribute meta names."""
    p = tmp_path / "bad.usda"
    p.write_text('''#usda 1.0
def Xform "X"
{
    token foo = "x" (
        notAMetaKey = ["a"]
    )
}
''')
    with pytest.raises(tinyusdz.UsdParseError):
        tinyusdz.load(str(p))
