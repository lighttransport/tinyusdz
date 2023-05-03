# SPDX-License-Identifier: Apache 2.0
#
# Requiement: Python 3.7+
#
# USD primitives

from typing import List, Any

from compat_typing_extensions import Literal

try:
    from typeguard import typechecked
    is_typegurad_available = True
except ImportError:
    is_typegurad_available = False

    # no-op
    def typechecked(cls):
        return cls


@typechecked
class Prim:
    def __init__(self,
                 name: str,
                 specifier: Literal["def", "over", "class"] = "def"):

        # assert specifier in Specifiers

        self._name: str = name

        # Corresponding Prim in C++ world.
        # self._prim = ctinyusdz.Prim()

        # Corresponding Prim index in C++ world.
        # 0 or None => Invalid
        self._prim_idx: int = 0

        self._specifier = specifier

        self._primChildren: List[Prim] = []

        # custom properties
        self._props = {}

    def specifier(self):
        return self._specifier

    def primChildren(self):
        return self._primChildren

    def set_prop(self, key: str, value: Any):
        self._props[key] = value
