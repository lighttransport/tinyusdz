"""`kind` metadata on a Relationship.

USD spec restricts `kind` to prims, but pxr's USDA parser accepts it on
properties (relationships and attributes) too. We mirror that
tolerance: the parser accepts it and the pretty-printer round-trips it
through generic-meta storage. USDC writer support is not implemented;
that path silently drops it (same as any unknown attr-meta key).
"""
import tinyusdz


_USDA_REL = '''#usda 1.0
def Xform "X"
{
    rel myRel = </X> (
        kind = "component"
    )
}
'''


def test_kind_on_relationship_parses_and_emits(tmp_path):
    p = tmp_path / "x.usda"
    p.write_text(_USDA_REL)
    s = tinyusdz.load(str(p))
    txt = s.export_to_string()
    # The kind metadatum should appear in the relationship's meta block.
    assert "kind" in txt
    assert "\"component\"" in txt


_USDA_ATTR = '''#usda 1.0
def Xform "X"
{
    custom int frob = 0 (
        kind = "subcomponent"
    )
}
'''


def test_kind_on_attribute_parses_and_emits(tmp_path):
    p = tmp_path / "x.usda"
    p.write_text(_USDA_ATTR)
    s = tinyusdz.load(str(p))
    txt = s.export_to_string()
    assert "kind" in txt
    assert "\"subcomponent\"" in txt
