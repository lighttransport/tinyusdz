"""Repeated save/load operations are idempotent in semantic content."""
import tinyusdz


def _norm(txt):
    """Strip trailing whitespace per line for comparison."""
    return "\n".join(line.rstrip() for line in txt.splitlines())


def test_double_save_usdc_same_content(tmp_path):
    src = tmp_path / "x.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    custom int n = 5
    custom string s = "x"
}
''')
    s = tinyusdz.load(str(src))
    a = tmp_path / "a.usdc"
    b = tmp_path / "b.usdc"
    s.save(str(a))
    s.save(str(b))
    s_a = tinyusdz.load(str(a)).export_to_string()
    s_b = tinyusdz.load(str(b)).export_to_string()
    assert _norm(s_a) == _norm(s_b)


def test_save_load_save_load_stable(tmp_path):
    """Two iterations of save+load converge in semantic content."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("a", 1.5)
    p.set_attribute("b", "hello")
    s.add_root_prim(p)

    out1 = tmp_path / "1.usdc"
    s.save(str(out1))
    s1 = tinyusdz.load(str(out1))
    out2 = tmp_path / "2.usdc"
    s1.save(str(out2))
    s2 = tinyusdz.load(str(out2))

    assert _norm(s1.export_to_string()) == _norm(s2.export_to_string())


def test_usda_to_usdc_to_usda_stable(tmp_path):
    src = tmp_path / "src.usda"
    src.write_text('''#usda 1.0
def Xform "X" {
    custom int n = 7
    custom float f = 0.5
}
''')
    s = tinyusdz.load(str(src))
    out_c = tmp_path / "x.usdc"
    s.save(str(out_c))
    s_c = tinyusdz.load(str(out_c))
    out_a = tmp_path / "x_back.usda"
    s_c.save(str(out_a))
    s_back = tinyusdz.load(str(out_a))

    p = s_back.get_prim_at_path("/X")
    assert p.get_attribute("n").value.as_scalar() == 7
    assert abs(p.get_attribute("f").value.as_scalar() - 0.5) < 1e-6
