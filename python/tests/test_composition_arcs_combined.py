"""Stacked composition arcs on the same prim.

Each arc kind has independent listop storage on `PrimMeta`, so a single
prim can carry references + payload + inherits + specializes
simultaneously and they should all survive USDA→USDC→USDA round-trip.
"""
import tinyusdz


def test_all_four_arc_kinds_on_one_prim(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_reference("./ref.usda", prim_path="/Foo")
    p.add_payload("./pay.usda", prim_path="/Bar")
    p.add_inherit("/_class_Base")
    p.add_specialize("/_specials_Variant")
    s.add_root_prim(p)

    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()

    assert "references = @./ref.usda@</Foo>" in txt
    assert "payload = @./pay.usda@</Bar>" in txt
    assert "inherits = </_class_Base>" in txt
    assert "specializes = </_specials_Variant>" in txt


def test_multiple_qualifiers_on_same_arc_kind(tmp_path):
    """references can have both 'prepend' and 'append' lists at once
    via independent listop slots."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_reference("./prep.usda", qualifier="prepend")
    p.add_reference("./app.usda", qualifier="append")
    s.add_root_prim(p)

    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()

    assert "prepend references = @./prep.usda@" in txt
    assert "append references = @./app.usda@" in txt


def test_clear_one_arc_does_not_affect_others(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_reference("./ref.usda")
    p.add_payload("./pay.usda")
    p.add_inherit("/_class_Foo")

    p.clear_payload()
    s.add_root_prim(p)

    out = tmp_path / "x.usda"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()

    assert "references = @./ref.usda@" in txt
    assert "inherits = </_class_Foo>" in txt
    assert "@./pay.usda@" not in txt


def test_reference_with_layeroffset_and_customdata(tmp_path):
    """Reference offset + scale + customData round-trip via USDC."""
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" (
    prepend references = @./ref.usda@</Foo> (
        offset = 5
        scale = 2
        customData = {
            string note = "tag"
            int version = 7
        }
    )
)
{
}
''')
    s = tinyusdz.load(str(src))
    out_usdc = tmp_path / "x.usdc"
    s.save(str(out_usdc))
    s2 = tinyusdz.load(str(out_usdc))
    txt = s2.export_to_string()
    assert "offset = 5" in txt
    assert "scale = 2" in txt
    assert "string note = \"tag\"" in txt
    assert "int version = 7" in txt
