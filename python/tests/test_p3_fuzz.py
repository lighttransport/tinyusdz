# SPDX-License-Identifier: Apache-2.0
"""P3: Fuzz-style robustness — random bytes, truncated USDC, max_memory.

No external corpus required; uses Python stdlib random + hypothesis if
available. All cases must raise UsdError/ValueError, never crash or hang.
Marked slow: run with `pytest -m "not slow"` to skip in CI.
"""
import random
import pytest

import lightusd

np = pytest.importorskip("numpy")


def _random_usda_snippet(rnd: random.Random) -> str:
    # Generate a plausible but possibly invalid USDA fragment
    prim = rnd.choice(["Xform", "Mesh", "Scope", "Sphere"])
    name = f"P{rnd.randint(0,9999)}"
    attrs = []
    if rnd.random() < 0.5:
        attrs.append(f'float v = {rnd.uniform(-1e6,1e6):.6f}')
    if rnd.random() < 0.3:
        attrs.append(f'int a = {rnd.randint(-2**31, 2**31-1)}')
    if rnd.random() < 0.2:
        attrs.append(f'token t = "{rnd.choice(["hello","world","","x"*100])}"')
    body = "\n  ".join(attrs)
    return f'#usda 1.0\ndef {prim} "{name}" {{\n  {body}\n}}\n'


@pytest.mark.slow
def test_fuzz_random_bytes_no_crash():
    rnd = random.Random(0)
    for _ in range(200):
        size = rnd.randint(0, 4096)
        blob = bytes(rnd.getrandbits(8) for _ in range(size))
        try:
            lightusd.load_bytes(blob)
        except (lightusd.UsdError, ValueError):
            pass
        # also try with max_memory tiny
        try:
            lightusd.load_bytes(blob, max_memory=1024)
        except (lightusd.UsdError, ValueError):
            pass


@pytest.mark.slow
def test_fuzz_random_usda_no_crash():
    rnd = random.Random(1)
    for _ in range(200):
        txt = _random_usda_snippet(rnd)
        # randomly truncate or inject garbage
        if rnd.random() < 0.3:
            txt = txt[: rnd.randint(0, len(txt))]
        if rnd.random() < 0.2:
            txt += "\x00\xff" * rnd.randint(1, 10)
        try:
            lightusd.loads(txt)
        except (lightusd.UsdError, ValueError):
            pass


@pytest.mark.slow
def test_fuzz_usdc_truncation_and_max_memory():
    st = lightusd.Stage.create()
    st.define_prim("/X", "Xform").set("v", 1.0, type="float")
    blob = st.export_usdc()
    assert blob[:8] == b"PXR-USDC"
    rnd = random.Random(2)
    for _ in range(50):
        cut = rnd.randint(0, len(blob))
        truncated = blob[:cut]
        try:
            lightusd.load_bytes(truncated)
        except (lightusd.UsdError, ValueError):
            pass
        # max_memory enforcement
        try:
            lightusd.load_bytes(blob, max_memory=1024)
        except (lightusd.UsdError, ValueError):
            pass


def test_fuzz_overlong_token():
    tok = "a" * 10000
    st = lightusd.Stage.create()
    p = st.define_prim("/P", "Xform")
    p.set("tok", tok, type="token")
    txt = st.export_usda()
    # either preserves or errors, but must not crash
    try:
        st2 = lightusd.loads(txt)
        assert st2.prim_at("/P") is not None
    except (lightusd.UsdError, ValueError):
        pass


def test_fuzz_hypothesis_if_available():
    try:
        from hypothesis import given, strategies as st
    except ImportError:
        pytest.skip("hypothesis not installed")
    @given(st.binary(min_size=0, max_size=2048))
    def inner(blob):
        try:
            lightusd.load_bytes(blob)
        except (lightusd.UsdError, ValueError):
            pass
    # run a small number of examples
    inner.hypothesis.inner_test._hypothesis_internal_settings = None  # avoid warning
    # Use hypothesis' default 100 examples but limit time
    inner()
