# SPDX-License-Identifier: Apache-2.0
"""Concurrency smoke tests. Meaningful on free-threaded CPython (cp31Xt),
but also valid (GIL-serialized) on regular builds."""
import concurrent.futures as cf

import pytest

import tinyusdz

from conftest import SIMPLE_USDA

np = pytest.importorskip("numpy")

N_THREADS = 16
N_ITER = 8


def test_concurrent_independent_loads():
    def work(_):
        st = tinyusdz.loads(SIMPLE_USDA)
        total = 0.0
        for prim in st:
            if "points" in prim:
                total += float(np.asarray(prim["points"]).sum())
        st.close()
        return total

    with cf.ThreadPoolExecutor(N_THREADS) as ex:
        results = list(ex.map(work, range(N_THREADS * N_ITER)))
    assert len(set(results)) == 1


def test_concurrent_reads_shared_stage():
    st = tinyusdz.loads(SIMPLE_USDA)

    def work(_):
        q = st.prim_at("/World/Quad")
        pts = np.asarray(q["points"])
        attr = q.attribute("xformOp:translate")
        v = attr.get(time=12.0)
        return (float(pts.sum()), v, [p.path for p in st][0])

    with cf.ThreadPoolExecutor(N_THREADS) as ex:
        results = list(ex.map(work, range(N_THREADS * N_ITER)))
    assert len(set(results)) == 1


def test_concurrent_export():
    st = tinyusdz.loads(SIMPLE_USDA)

    def work(_):
        return st.export_usda()

    with cf.ThreadPoolExecutor(8) as ex:
        outs = list(ex.map(work, range(32)))
    assert len(set(outs)) == 1
