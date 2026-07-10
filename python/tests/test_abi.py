# SPDX-License-Identifier: Apache-2.0
"""Wheel-mode invariants: abi3 tagging and free-threaded GIL behavior."""
import sys
import sysconfig

import tinyusdz
from tinyusdz import _core

FREE_THREADED = bool(sysconfig.get_config_var("Py_GIL_DISABLED"))


def test_extension_flavor():
    if FREE_THREADED:
        # Free-threaded builds cannot use the stable ABI.
        assert ".abi3" not in _core.__file__
    elif sys.platform != "win32" and "site-packages" in _core.__file__:
        # Installed wheels on posix carry the abi3 suffix. (A dev in-tree
        # build may be named differently; only assert for installed wheels.)
        assert ".abi3" in _core.__file__


def test_gil_stays_disabled():
    if not FREE_THREADED:
        return
    # If the module forgot Py_mod_gil, importing it re-enables the GIL.
    assert hasattr(sys, "_is_gil_enabled")
    assert sys._is_gil_enabled() is False


def test_version():
    assert isinstance(tinyusdz.__version__, str)
    assert _core.__version__
