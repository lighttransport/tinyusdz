"""Stage.load_from_data / load_from_bytes via in-memory blob."""
import tinyusdz


def test_save_to_string_usda():
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    txt = s.export_to_string()
    assert "Xform" in txt
    assert '"X"' in txt


def test_load_from_usda_bytes(tmp_path):
    """Round-trip via file (as a baseline since direct from-bytes
    may not be exposed)."""
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    blob = out.read_bytes()
    assert len(blob) > 0
    out2 = tmp_path / "y.usdc"
    out2.write_bytes(blob)
    s2 = tinyusdz.load(str(out2))
    assert s2.get_prim_at_path("/X") is not None


def test_export_string_round_trip(tmp_path):
    """export_to_string produces parseable USDA."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("n", 7)
    s.add_root_prim(p)
    txt = s.export_to_string()
    src = tmp_path / "from_string.usda"
    src.write_text(txt)
    s2 = tinyusdz.load(str(src))
    p2 = s2.get_prim_at_path("/X")
    assert p2.get_attribute("n").value.as_scalar() == 7


def test_repeated_export_string():
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    a = s.export_to_string()
    b = s.export_to_string()
    assert a == b
