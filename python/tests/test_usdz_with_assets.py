"""USDZ packing with embedded asset files."""
import zipfile
import tinyusdz


def test_usdz_pack_with_png(tmp_path):
    png = tmp_path / "tex.png"
    # Minimal 1x1 transparent PNG
    png.write_bytes(bytes.fromhex(
        "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4"
        "890000000a49444154789c6300010000000500010d0a2db40000000049454e44"
        "ae426082"))
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("tex", "tex.png", dtype="asset")
    s.add_root_prim(p)
    out = tmp_path / "x.usdz"
    s.save(str(out), assets={"tex.png": png.read_bytes()})
    assert out.exists()

    with zipfile.ZipFile(str(out)) as z:
        names = z.namelist()
    assert any(n.endswith(".usd") or n.endswith(".usda") or n.endswith(".usdc")
               for n in names)
    assert any("tex.png" in n for n in names)


def test_usdz_pack_no_assets(tmp_path):
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    out = tmp_path / "x.usdz"
    s.save(str(out))
    assert out.read_bytes()[:2] == b"PK"


def test_usdz_zip_is_uncompressed(tmp_path):
    """USDZ requires Store-only (no DEFLATE) per spec."""
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    out = tmp_path / "x.usdz"
    s.save(str(out))
    with zipfile.ZipFile(str(out)) as z:
        for info in z.infolist():
            assert info.compress_type == zipfile.ZIP_STORED
