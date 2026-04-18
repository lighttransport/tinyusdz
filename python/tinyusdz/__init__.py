"""tinyusdz — CPython bindings for the tinyusdz USD loader.

This module is a thin Python-side facade over the C extension `tinyusdz._core`.
It adds Pythonic ergonomics (iteration, properties, context-manager-friendly
load/save) and bakes in string-to-Path handling.

Public API::

    tinyusdz.load(path, format=None) -> Stage
    tinyusdz.is_usd(path) -> bool
    tinyusdz.detect_format(path) -> str
    tinyusdz.UsdError
    tinyusdz.UsdParseError
    tinyusdz.UsdIoError
    tinyusdz.Stage
    tinyusdz.Prim
    tinyusdz.Attribute
    tinyusdz.Value

See tinyusdz/_core.pyi for the exact signatures.
"""
from __future__ import annotations

from typing import Callable, Iterator

from . import _core as _core
from ._core import (  # noqa: F401
    AnimationSampler,
    Attribute,
    BufferView,
    Prim,
    RenderAnimation,
    RenderBuffer,
    RenderCamera,
    RenderImage,
    RenderLight,
    RenderMaterial,
    RenderMesh,
    RenderNode,
    RenderScene,
    RenderSkeleton,
    RenderTexture,
    Stage,
    UsdError,
    UsdIoError,
    UsdParseError,
    Value,
    detect_format,
    is_usd,
    load,
    load_bytes,
    loads,
    tydra,
)

try:
    from ._version import __version__  # type: ignore[import-not-found]
except Exception:
    __version__ = "0.0.0+unknown"


def traverse(stage: "Stage") -> Iterator["Prim"]:
    """Depth-first iterator over every Prim in the Stage.

    Implemented on top of ``visit_prims`` so the C side streams prims via a
    callback; this wrapper just collects them lazily into a generator.
    """
    prims: list["Prim"] = []

    def _collect(prim: "Prim", path: str, depth: int) -> bool:
        prims.append(prim)
        return True

    stage.visit_prims(_collect)
    yield from prims


__all__ = [
    "AnimationSampler",
    "Attribute",
    "BufferView",
    "Prim",
    "RenderAnimation",
    "RenderBuffer",
    "RenderCamera",
    "RenderImage",
    "RenderLight",
    "RenderMaterial",
    "RenderMesh",
    "RenderNode",
    "RenderScene",
    "RenderSkeleton",
    "RenderTexture",
    "Stage",
    "UsdError",
    "UsdIoError",
    "UsdParseError",
    "Value",
    "__version__",
    "detect_format",
    "is_usd",
    "load",
    "load_bytes",
    "loads",
    "traverse",
    "tydra",
]
