"""Multiple Stage objects in the same process don't interfere."""
import tinyusdz


def test_two_stages_independent(tmp_path):
    s1 = tinyusdz.Stage()
    s1.add_root_prim(tinyusdz.Prim("Xform", name="A"))
    s2 = tinyusdz.Stage()
    s2.add_root_prim(tinyusdz.Prim("Sphere", name="B"))

    assert len(s1.root_prims()) == 1
    assert len(s2.root_prims()) == 1
    assert s1.get_prim_at_path("/B") is None
    assert s2.get_prim_at_path("/A") is None


def test_load_two_files(tmp_path):
    a = tmp_path / "a.usda"
    a.write_text('#usda 1.0\ndef Xform "A" {}\n')
    b = tmp_path / "b.usda"
    b.write_text('#usda 1.0\ndef Xform "B" {}\n')
    s_a = tinyusdz.load(str(a))
    s_b = tinyusdz.load(str(b))
    assert s_a.get_prim_at_path("/A") is not None
    assert s_a.get_prim_at_path("/B") is None
    assert s_b.get_prim_at_path("/B") is not None


def test_modify_one_stage_doesnt_affect_other():
    s1 = tinyusdz.Stage()
    s1.add_root_prim(tinyusdz.Prim("Xform", name="A"))
    s2 = tinyusdz.Stage()

    s2.add_root_prim(tinyusdz.Prim("Xform", name="A"))
    s2.set_default_prim("A")

    assert s2.get_default_prim() == "A"
    assert s1.get_default_prim() in ("", None)


def test_stage_destruction_doesnt_corrupt_other(tmp_path):
    s1 = tinyusdz.Stage()
    s1.add_root_prim(tinyusdz.Prim("Xform", name="X"))

    def make_and_drop():
        s = tinyusdz.Stage()
        s.add_root_prim(tinyusdz.Prim("Sphere", name="Y"))
        return s.export_to_string()

    txt = make_and_drop()
    # garbage collected; s1 should still be usable
    out = tmp_path / "x.usda"
    s1.save(str(out))
    assert out.read_text().startswith("#usda")
