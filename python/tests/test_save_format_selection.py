"""Stage.save dispatches on file extension."""
import tinyusdz


def test_save_usda(tmp_path):
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    out = tmp_path / "x.usda"
    s.save(str(out))
    text = out.read_text()
    assert text.startswith("#usda 1.0")


def test_save_usdc(tmp_path):
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    head = out.read_bytes()[:8]
    assert head.startswith(b"PXR-USDC")


def test_save_usd_default(tmp_path):
    """`.usd` extension picks a default format and loads back."""
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    out = tmp_path / "x.usd"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    assert s2.get_prim_at_path("/X") is not None


def test_save_usdz(tmp_path):
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    out = tmp_path / "x.usdz"
    s.save(str(out))
    head = out.read_bytes()[:4]
    assert head[:2] == b"PK"


def test_load_then_save_preserves_format(tmp_path):
    src = tmp_path / "src.usda"
    src.write_text('''#usda 1.0
def Xform "X" {}
''')
    s = tinyusdz.load(str(src))
    out_a = tmp_path / "out.usda"
    out_c = tmp_path / "out.usdc"
    s.save(str(out_a))
    s.save(str(out_c))
    assert out_a.read_text().startswith("#usda")
    assert out_c.read_bytes().startswith(b"PXR-USDC")
