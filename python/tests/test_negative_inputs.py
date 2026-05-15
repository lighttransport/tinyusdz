"""Negative tests — malformed USDA, malformed USDC, and various
invalid inputs that the loader should reject without crashing.
"""
import pytest

import tinyusdz


def test_malformed_usda_missing_brace_recovers_gracefully():
    """tinyusdz's parser is lenient about a trailing-brace EOF and
    recovers — verify it doesn't crash and produces *some* stage,
    rather than asserting rejection. (pxr is stricter here.)"""
    bad = '''#usda 1.0
def Xform "X" {
    custom int n = 1
'''
    s = tinyusdz.loads(bad)
    assert s is not None
    txt = s.export_to_string()
    assert "n = 1" in txt


def test_malformed_usda_unclosed_string():
    bad = '''#usda 1.0
def Xform "X" {
    custom string s = "unterminated
}
'''
    with pytest.raises(tinyusdz.UsdParseError):
        tinyusdz.loads(bad)


def test_malformed_usda_garbage_attribute():
    bad = '''#usda 1.0
def Xform "X" {
    %%% garbage line %%%
}
'''
    with pytest.raises(tinyusdz.UsdParseError):
        tinyusdz.loads(bad)


def test_usdc_truncated_magic():
    """A file shorter than the magic bytes."""
    with pytest.raises(tinyusdz.UsdError):
        tinyusdz.load_bytes(b"PXR")


def test_usdc_corrupt_magic():
    """4 bytes that aren't PXR-USDC."""
    with pytest.raises(tinyusdz.UsdError):
        tinyusdz.load_bytes(b"FAKE" + b"\x00" * 100)


def test_usdc_truncated_after_magic():
    """Valid magic + truncated header."""
    with pytest.raises(tinyusdz.UsdError):
        tinyusdz.load_bytes(b"PXR-USDC" + b"\x00" * 8)


def test_load_nonexistent_path_raises():
    with pytest.raises((tinyusdz.UsdIoError, tinyusdz.UsdError, OSError)):
        tinyusdz.load("/no/such/path/file.usda")


def test_loads_empty_string_rejected():
    with pytest.raises(tinyusdz.UsdError):
        tinyusdz.loads("")


def test_loads_missing_header():
    """USDA without the `#usda 1.0` header."""
    with pytest.raises(tinyusdz.UsdParseError):
        tinyusdz.loads('def Xform "X" {}')


def test_set_attribute_with_unsupported_object_type():
    """A Python object that has no known mapping (e.g. a dict) on a
    no-dtype call must error rather than silently produce junk."""
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    with pytest.raises((TypeError, ValueError)):
        p.set_attribute("a", {"key": "value"})  # no dtype hint


def test_define_variant_in_unknown_set_does_not_crash():
    """Calling define_variant before add_variant_set_name on the same
    name must not crash. Either it implicitly creates the set or
    raises — both are OK."""
    p = tinyusdz.Prim("Xform", name="X")
    try:
        p.define_variant("look", "red")
    except (RuntimeError, ValueError):
        pass
    # Must not segfault. Just by reaching this line, we pass.


def test_get_metadata_on_missing_key_returns_none():
    s = tinyusdz.Stage()
    assert s.get_metadata("nonexistent_meta") is None


def test_invalid_qualifier_for_reference_raises():
    """Reference qualifier must be one of the known ListEditQual
    spellings."""
    p = tinyusdz.Prim("Xform", name="X")
    with pytest.raises((ValueError, RuntimeError)):
        p.add_reference("./foo.usda", qualifier="bogus_qualifier")
