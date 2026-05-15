"""Prim-level customData with array-typed values.

Regression tests for the USDC writer's CustomDataType packer dispatch
— previously missing `bool[]` (and related), which made any prim
with `customData = { bool[] zUp = [1] }` fail to save with
"Unsupported CustomDataType value type: bool[]".

These tests load a USDA fixture, save to USDC, reload, and confirm
all array entries survive round-trip. Verified equivalent to pxrUSD
output via `tests/compare-usda.js` against pxr usdcat.
"""
import pytest
import tinyusdz


_USDA = '''#usda 1.0
def Xform "X"
{
    def Mesh "M" (
        customData = {
            bool[] flags = [1, 0, 1]
            int[] ints = [1, 2, 3]
            int2[] i2val = [(1, 2), (3, 4)]
            int3[] i3val = [(1, 2, 3)]
            int4[] i4val = [(1, 2, 3, 4)]
            float[] floats = [0.5, 1.5]
            double[] doubles = [0.25, 0.5, 0.75]
            string[] tags = ["alpha", "beta"]
            dictionary nested = {
                bool generated = 1
                int version = 7
            }
        }
    )
    {
    }
}
'''


@pytest.mark.parametrize("fmt", ["usda", "usdc"])
def test_customdata_array_types_roundtrip(tmp_path, fmt):
    src = tmp_path / "src.usda"
    src.write_text(_USDA)
    s = tinyusdz.load(str(src))
    out = tmp_path / f"out.{fmt}"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    # Every array entry should re-emit. Use loose containment checks
    # since the dict key order in the emitted USDA isn't guaranteed
    # stable across formats.
    assert "bool[] flags = [1, 0, 1]" in txt
    assert "int[] ints = [1, 2, 3]" in txt
    assert "int2[] i2val = [(1, 2), (3, 4)]" in txt
    assert "int3[] i3val = [(1, 2, 3)]" in txt
    assert "int4[] i4val = [(1, 2, 3, 4)]" in txt
    assert "float[] floats" in txt and "0.5" in txt and "1.5" in txt
    assert "double[] doubles" in txt
    assert "string[] tags" in txt
    assert "\"alpha\"" in txt and "\"beta\"" in txt
    # Nested dictionary survives
    assert "dictionary nested" in txt
    assert "bool generated = 1" in txt
    assert "int version = 7" in txt


def test_customdata_bool_array_alone(tmp_path):
    """The exact failing pattern from the upstream regression fixture
    customData-prim-003.usda: a single bool[] entry."""
    src = tmp_path / "src.usda"
    src.write_text('''#usda 1.0
def Xform "X" (
    customData = {
        bool[] zUp = [1]
    }
)
{
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / "out.usdc"
    s.save(str(out))  # would have raised UsdIoError before the fix
    s2 = tinyusdz.load(str(out))
    assert "bool[] zUp = [1]" in s2.export_to_string()


@pytest.mark.parametrize("fmt", ["usda", "usdc"])
def test_customdata_int64_uint64_arrays(tmp_path, fmt):
    """int64/uint64 array dispatch was added at the same time."""
    src = tmp_path / "src.usda"
    src.write_text('''#usda 1.0
def Xform "X" (
    customData = {
        int64[] big_signed = [-9999999999, 9999999999]
        uint64[] big_unsigned = [12345678901234, 99]
    }
)
{
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / f"out.{fmt}"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "int64[] big_signed" in txt
    assert "-9999999999" in txt and "9999999999" in txt
    assert "uint64[] big_unsigned" in txt
    assert "12345678901234" in txt


@pytest.mark.parametrize("fmt", ["usda", "usdc"])
def test_customdata_half_array(tmp_path, fmt):
    src = tmp_path / "src.usda"
    src.write_text('''#usda 1.0
def Xform "X" (
    customData = {
        half[] hh = [0.5, 1.5]
    }
)
{
}
''')
    s = tinyusdz.load(str(src))
    out = tmp_path / f"out.{fmt}"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "half[] hh" in txt
    assert "0.5" in txt and "1.5" in txt


def test_fixture_customData_prim_003_usdc_roundtrips(tmp_path):
    """The original failing fixture itself."""
    import os
    repo = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    fixture = os.path.join(repo, "tests", "usda", "customData-prim-003.usda")
    if not os.path.isfile(fixture):
        pytest.skip(f"fixture not found: {fixture}")
    s = tinyusdz.load(fixture)
    out = tmp_path / "out.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "bool[] zUp = [1]" in txt
    assert "int2[] i2val = [(1, 2)]" in txt
    assert "int3[] i3val = [(1, 2, 3)]" in txt
    assert "int4[] i4val = [(1, 2, 3, 4)]" in txt
