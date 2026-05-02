"""Reference customData parsing + USDA + USDC round-trip.

Phase C.7 / deferred-cleanup: ascii-parser ParseReference consumes
`customData = { ... }` inside the same `(...)` clause as offset/scale.
The crate writer + reader also handle Reference customData (int32,
int64, float, double, bool, string, StringData).
"""
import pytest
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
    with pytest.raises(tinyusdz.UsdParseError):
        tinyusdz.load(str(p))


@pytest.mark.parametrize("fmt", ["usda", "usdc"])
def test_reference_customdata_usdc_roundtrip(tmp_path, fmt):
    src = tmp_path / "src.usda"
    src.write_text(_USDA_REFERENCE_CD)
    s = tinyusdz.load(str(src))
    out = tmp_path / f"out.{fmt}"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "string note = \"hello\"" in txt
    assert "int version = 3" in txt
    assert "offset = 5" in txt
    assert "scale = 2" in txt


def test_reference_customdata_double_usdc_roundtrip(tmp_path):
    """Doubles can't fit in the inline ValueRep; previously the writer
    rejected them. The reserve-pack-seek refactor lets the writer emit
    the value out-of-line and the reader recovers it intact."""
    src = tmp_path / "src.usda"
    src.write_text('''#usda 1.0
def Xform "X" (
    prepend references = @./foo.usda@</A> (
        customData = {
            double d = 2.71828
            string note = "hi"
        }
    )
)
{
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "out.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "double d = 2.71828" in txt
    assert "string note = \"hi\"" in txt


def test_reference_customdata_large_int64_usdc_roundtrip(tmp_path):
    """int64 values above 2^47 can't inline in the ValueRep payload.
    They should round-trip via the out-of-line path."""
    big = 2 ** 50  # well above 2^47
    src = tmp_path / "src.usda"
    src.write_text(f'''#usda 1.0
def Xform "X" (
    prepend references = @./foo.usda@</A> (
        customData = {{
            int64 big = {big}
        }}
    )
)
{{
}}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "out.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert f"int64 big = {big}" in txt


def test_reference_customdata_mixed_inline_and_outofline(tmp_path):
    """Several entries where some inline and some don't, to exercise
    the ordering of the out-of-line writes vs. the dict frame."""
    src = tmp_path / "src.usda"
    src.write_text('''#usda 1.0
def Xform "X" (
    prepend references = @./foo.usda@</A> (
        customData = {
            int v = 42
            double d = 3.14159
            string note = "hi"
            int64 big = 1125899906842624
        }
    )
)
{
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "out.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "int v = 42" in txt
    assert "double d = 3.14159" in txt
    assert "string note = \"hi\"" in txt
    assert "int64 big = 1125899906842624" in txt
