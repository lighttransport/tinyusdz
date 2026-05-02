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


def rewrite_asset_paths(
    stage: "Stage",
    mapping: "dict[str, str]",
) -> int:
    """Rewrite ``asset`` and ``asset[]`` attribute values in-place.

    Walks every Prim in ``stage`` and, for every attribute whose
    ``type_name`` is ``"asset"`` or ``"asset[]"``, replaces any value
    that appears as a key in ``mapping`` with the matching value. The
    USD ``@…@`` delimiters are added automatically by ``set_attribute``;
    the keys/values in ``mapping`` should be the bare paths (e.g.
    ``{"./external/diffuse.png": "diffuse.png"}``).

    Returns the number of attribute values that were rewritten.

    Typical use is right before ``stage.save("out.usdz", assets=...)``
    to make the in-USD references match the names you packed into the
    archive.
    """
    if not isinstance(mapping, dict):
        raise TypeError("mapping must be a dict[str, str]")
    rewrites = 0
    for prim in traverse(stage):
        for name in prim.property_names():
            attr = prim.get_attribute(name)
            if attr is None:
                continue
            tname = attr.type_name
            if tname == "asset":
                v = attr.value
                if v is None:
                    continue
                # Value.to_string() emits "@<path>@"; strip the
                # delimiters to get the bare path.
                s = v.to_string()
                if s.startswith("@") and s.endswith("@"):
                    bare = s.strip("@")
                else:
                    bare = s
                if bare in mapping:
                    prim.set_attribute(name, mapping[bare], dtype="asset")
                    rewrites += 1
            elif tname == "asset[]":
                v = attr.value
                if v is None:
                    continue
                # Array form: the to_string is "[@a@, @b@, ...]".
                s = v.to_string().strip()
                if s.startswith("[") and s.endswith("]"):
                    s = s[1:-1]
                items = [
                    p.strip().strip("@") for p in s.split(",") if p.strip()
                ]
                if not items:
                    continue
                changed = False
                new_items = []
                for it in items:
                    if it in mapping:
                        new_items.append(mapping[it])
                        changed = True
                        rewrites += 1
                    else:
                        new_items.append(it)
                if changed:
                    prim.set_attribute(name, new_items, dtype="asset[]")
    return rewrites


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
    "rewrite_asset_paths",
    "traverse",
    "tydra",
]
