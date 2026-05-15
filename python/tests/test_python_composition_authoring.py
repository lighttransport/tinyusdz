"""Python composition arc authoring: add_reference / add_payload / inherits."""
import tinyusdz


def test_add_reference_external(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_reference("./asset.usda", "/Asset")
    s.add_root_prim(p)
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = out.read_text()
    assert "@./asset.usda@" in txt
    assert "</Asset>" in txt


def test_add_reference_internal(tmp_path):
    s = tinyusdz.Stage()
    src_prim = tinyusdz.Prim("Xform", name="Src")
    s.add_root_prim(src_prim)
    p = tinyusdz.Prim("Xform", name="X")
    p.add_reference("", "/Src")
    s.add_root_prim(p)
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = out.read_text()
    assert "</Src>" in txt


def test_add_payload(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_payload("./big.usda", "/Asset")
    s.add_root_prim(p)
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = out.read_text()
    assert "payload" in txt
    assert "@./big.usda@" in txt


def test_add_inherit(tmp_path):
    s = tinyusdz.Stage()
    base = tinyusdz.Prim("Xform", name="Base")
    s.add_root_prim(base)
    p = tinyusdz.Prim("Xform", name="X")
    p.add_inherit("/Base")
    s.add_root_prim(p)
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = out.read_text()
    assert "inherits" in txt
    assert "</Base>" in txt


def test_add_specialize(tmp_path):
    s = tinyusdz.Stage()
    base = tinyusdz.Prim("Xform", name="Base")
    s.add_root_prim(base)
    p = tinyusdz.Prim("Xform", name="X")
    p.add_specialize("/Base")
    s.add_root_prim(p)
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = out.read_text()
    assert "specializes" in txt


def test_clear_references(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_reference("./a.usda", "/A")
    p.clear_references()
    s.add_root_prim(p)
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = out.read_text()
    assert "@./a.usda@" not in txt


def test_clear_payload(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.add_payload("./b.usda", "/B")
    p.clear_payload()
    s.add_root_prim(p)
    out = tmp_path / "x.usda"
    s.save(str(out))
    txt = out.read_text()
    assert "@./b.usda@" not in txt
