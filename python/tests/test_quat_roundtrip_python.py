"""USDA→USDC→USDA quaternion fidelity via the Python API.

Complements the C++ unit tests in tests/unit/unit-usdc-writer.cc that
fence the same wire-format invariant (Crate is [x,y,z,w] = (imag,real);
USDA is [w,x,y,z] = (real,imag) — see value-types.hh:957).
"""
import struct

import tinyusdz


def test_quatf_usda_usdc_usda_roundtrip(tmp_path):
    src = tmp_path / "q.usda"
    src.write_text('''#usda 1.0
def Xform "X"
{
    custom quatf q = (0.5, 0.6, 0.7, 0.8)
    custom quatf[] qa = [(1, 0, 0, 0), (0.5, 0.5, 0.5, 0.5)]
}
''')
    s = tinyusdz.load(str(src))
    out_usdc = tmp_path / "q.usdc"
    s.save(str(out_usdc))
    s2 = tinyusdz.load(str(out_usdc))
    txt = s2.export_to_string()
    # Scalar
    assert "quatf q = (0.5, 0.6, 0.7, 0.8)" in txt
    # Array
    assert "(1, 0, 0, 0)" in txt
    assert "(0.5, 0.5, 0.5, 0.5)" in txt


def test_quatd_array_three_elements_roundtrip(tmp_path):
    src = tmp_path / "qd.usda"
    src.write_text('''#usda 1.0
def Xform "X"
{
    custom quatd[] qa = [(0.1, 0.2, 0.3, 0.4),
                         (0.5, 0.6, 0.7, 0.8),
                         (1.0, 0.0, 0.0, 0.0)]
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "qd.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "(0.1, 0.2, 0.3, 0.4)" in txt
    assert "(0.5, 0.6, 0.7, 0.8)" in txt
    assert "(1, 0, 0, 0)" in txt


def test_quat_wire_byte_order_matches_pxr(tmp_path):
    """Author a quatf with distinct components, then look for the
    expected (imag.x, imag.y, imag.z, real) byte sequence in the USDC
    output. This is the same invariant the C++
    `usdc_writer_quat_wire_byteorder_test` fences."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    # USDA spelling (1.0, 0.25, 0.5, 0.75) = real=1.0, imag=(0.25,0.5,0.75)
    p.set_attribute("q", (1.0, 0.25, 0.5, 0.75), dtype="quatf")
    s.add_root_prim(p)
    out = tmp_path / "q.usdc"
    s.save(str(out))
    data = out.read_bytes()
    # Wire bytes per Crate spec: imag.x, imag.y, imag.z, real
    needle = struct.pack("<ffff", 0.25, 0.5, 0.75, 1.0)
    assert needle in data
