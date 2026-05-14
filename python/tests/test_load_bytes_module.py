"""Module-level tinyusdz.load_bytes / loads / detect_format / is_usd."""
import tinyusdz


def test_loads_usda():
    src = '''#usda 1.0
def Xform "X" {}
'''
    s = tinyusdz.loads(src)
    assert s.get_prim_at_path("/X") is not None


def test_load_bytes_usdc(tmp_path):
    s0 = tinyusdz.Stage()
    s0.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    out = tmp_path / "x.usdc"
    s0.save(str(out))
    blob = out.read_bytes()
    s = tinyusdz.load_bytes(blob)
    assert s.get_prim_at_path("/X") is not None


def test_load_bytes_usda_text():
    blob = b'#usda 1.0\ndef Xform "X" {}\n'
    s = tinyusdz.load_bytes(blob)
    assert s.get_prim_at_path("/X") is not None


def test_is_usd_usda(tmp_path):
    f = tmp_path / "x.usda"
    f.write_text('#usda 1.0\n')
    assert tinyusdz.is_usd(str(f))


def test_is_usd_random_file(tmp_path):
    f = tmp_path / "noise.bin"
    f.write_bytes(b"\x00\x01\x02\x03")
    assert not tinyusdz.is_usd(str(f))


def test_detect_format_usdc(tmp_path):
    s0 = tinyusdz.Stage()
    s0.add_root_prim(tinyusdz.Prim("Xform", name="X"))
    out = tmp_path / "x.usdc"
    s0.save(str(out))
    fmt = tinyusdz.detect_format(str(out))
    assert "usdc" in fmt.lower() or "crate" in fmt.lower()


def test_detect_format_usda(tmp_path):
    f = tmp_path / "x.usda"
    f.write_text("#usda 1.0\n")
    fmt = tinyusdz.detect_format(str(f))
    assert "usda" in fmt.lower() or "ascii" in fmt.lower()
