"""Reference customData parsing + USDA round-trip.

Phase C.7: ascii-parser ParseReference now consumes
`customData = { ... }` inside the same `(...)` clause as offset/scale.

USDC writer support for Reference customData is incomplete (writes
fail with "Failed to write ReferenceListOp prepended items" — a
separate bug); these tests cover USDA only.
"""
import tinyusdz


_USDA_REFERENCE_CD = '''#usda 1.0
def Xform "X" (
    prepend references = @./other.usda@</Bar> (
        offset = 5
        scale = 2
        customData = {
            string note = "hello"
            int version = 3
        }
    )
)
{
}
'''


def test_parse_reference_with_customdata(tmp_path):
    p = tmp_path / "ref.usda"
    p.write_text(_USDA_REFERENCE_CD)
    s = tinyusdz.load(str(p))
    txt = s.export_to_string()
    # Both layerOffset and customData should re-emit.
    assert "offset = 5" in txt
    assert "scale = 2" in txt
    assert "string note = \"hello\"" in txt
    assert "int version = 3" in txt


def test_reference_customdata_only(tmp_path):
    """customData clause without an explicit offset/scale."""
    p = tmp_path / "ref.usda"
    p.write_text('''#usda 1.0
def Xform "X" (
    prepend references = @./foo.usda@</A> (
        customData = {
            string tag = "xyz"
        }
    )
)
{
}
''')
    s = tinyusdz.load(str(p))
    txt = s.export_to_string()
    assert "string tag = \"xyz\"" in txt


def test_parser_rejects_customdata_on_payload(tmp_path):
    """Payload has no customData field — parser must error."""
    p = tmp_path / "bad.usda"
    p.write_text('''#usda 1.0
def Xform "X" (
    prepend payload = @./foo.usda@ (
        customData = {
            string tag = "xyz"
        }
    )
)
{
}
''')
    import pytest
    with pytest.raises(tinyusdz.UsdParseError):
        tinyusdz.load(str(p))
