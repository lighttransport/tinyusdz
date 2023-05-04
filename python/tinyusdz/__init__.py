# SPDX-License-Identifier: Apache 2.0
#
# Requiement: Python 3.7+

# TODO
# - [ ] Refactor components

import os
import warnings
from pathlib import Path

from typing import Union, List, Any, IO
from enum import Enum, auto

#
# Local modules
#
from .compat_typing_extensions import Literal, TypeAlias
from . import version
from .prims import Prim

FILE_LIKE: TypeAlias = Union[str, os.PathLike, IO[str], IO[bytes]]

try:
    from typeguard import typechecked
    is_typegurad_available = True
except ImportError:
    is_typegurad_available = False

    # no-op
    def typechecked(cls):
        return cls


try:
    import ctinyusdz
except ImportError:
    import warnings
    warnings.warn(
        "Failed to import native module `ctinyusdz`(No corresponding dll/so exists?). Loading USDA/USDC/USDZ feature is disabled.")

try:
    import numpy as np
except:
    pass

try:
    import pandas as pd
except:
    pass

def is_ctinyusdz_available():
    import importlib.util

    if importlib.util.find_spec("ctinyusdz"):
        return True

    return False

def is_typeguard_available():
    import importlib.util

    if importlib.util.find_spec("typeguard"):
        return True

    return False


def is_numpy_available():
    import importlib.util

    if importlib.util.find_spec("numpy"):
        return True

    return False


def is_pandas_available():
    import importlib.util

    if importlib.util.find_spec("pandas"):
        return True

    return False


__version__ = version

"""
USD types
"""


class Type(Enum):
    bool = auto()
    int8 = auto()
    uint8 = auto()
    int16 = auto()
    uint16 = auto()
    int32 = auto()
    uint32 = auto()
    int64 = auto()
    uint64 = auto()
    int2 = auto()
    uint2 = auto()
    int3 = auto()
    uint3 = auto()
    int4 = auto()
    uint4 = auto()
    string = auto()
    vector3h = auto()
    vector3f = auto()
    vector3d = auto()
    color3h = auto()
    color3f = auto()
    color3d = auto()
    matrix2d = auto()
    matrix3d = auto()
    matrix4d = auto()


class XformOpType(Enum):
    # matrix4d
    Transform = auto()

    # vector3
    Translate = auto()
    Scale = auto()

    # scalar
    RotateX = auto()
    RotateY = auto()
    RotateZ = auto()

    # vector3
    RotateXYZ = auto()
    RotateXZY = auto()
    RotateYXZ = auto()
    RotateYZX = auto()
    RotateZXY = auto()
    RotateZYX = auto()

    # quat
    Orient = auto()

    # special token
    ResetXformStack = auto()

# USD ValueBlock(`None` in USDA)


class ValueBlock:
    def __init__(self):
        pass


"""
USD type in literal
"""

# Literal is not available in 3.7
USDTypes = Literal[
    "bool",
    "int8",
    "int16",
    "int",
    "int32",
    "int64",
    "uint8",
    "uint16",
    "uint",
    "uint32",
    "uint64",
    "string"]
SpecifierType: TypeAlias = Literal["def", "over", "class"]


"""
numpy-like ndarray for Attribute data(e.g. points, normals, ...)
"""


class NDArray:
    def __init__(self, dtype: str = "uint8"):

        assert dtype in USDTypes

        self.dtype: str = "uint8"
        self.dim: int = 1  # In USD, 1D or 2D only for array data

        self._data = None

    def from_numpy(self, nddata):
        if not is_numpy_available():
            raise ImportError("numpy is not installed.")

        assert isinstance(nddata, np.ndarray)

        assert nddata.dim < 2, "USD supports up to 2D array data"

        self.dtype = nddata.dtype
        self.dim
        self._data = nddata

    def to_numpy(self):
        """Convert data to numpy ndarray

        Returns:
            numpy.ndarray: numpy ndarray object
        """

        if not is_numpy_available():
            raise ImportError("numpy is not installed.")

        if isinstance(self._data, np.ndarray):
            return self._data

        raise RuntimeError("TODO")


@typechecked
class Token:
    def __init__(self, tok: str):
        self.tok = tok


class Property:
    """Represents Prim property.
    Base class for Attribute and Relationship.
    """

    def __init__(self):
        pass

    def is_property(self):
        return isinstance(self, Attribute)

    def is_relationship(self):
        return isinstance(self, Relationship)


class Attribute(Property):
    def __init__(self):
        super().__init__()


class Relationship(Property):
    def __init__(self):
        super().__init__()


class Model(Prim):
    def __init__(self, name: str, specifier: str = "def", **kwargs):
        super().__init__(name, specifier)
        pass


class Scope(Prim):
    def __init__(self, name: str, specifier: str = "def", **kwargs):
        super().__init__(self, name, specifier)
        pass


@typechecked
class XformOp:
    def __init__(self,
                 op_type: XformOpType = XformOpType.Translate,
                 value: Any = None):
        pass

        self.suffix = ""
        self.inverted = False

        self.op_type = op_type

        self._value = value


@typechecked
class Xform(Prim):
    def __init__(self, name: str, specifier: str = "def"):
        super().__init__(self, specifier)

        # TODO: Typecheck
        self.xformOps = []


@typechecked
class GeomMesh(Prim):
    def __init__(self, name: str, specifier: str = "def"):
        super().__init__()
        pass


@typechecked
class Material(Prim):
    def __init__(self, name: str, specifier: str = "def"):
        pass


@typechecked
class Shader(Prim):
    def __init__(self):
        pass


@typechecked
class UsdPreviewSurface(Shader):
    def __init__(self):
        super().__init__(self)

        self.diffuseColor = [0.18, 0.18, 0.18]
        # TODO: More attrs


@typechecked
class Stage:

    def __init__(self):
        self._stage = None
        self.filename = ""

        self.upAxis: Union[Literal["X", "Y", "Z"], None] = None
        self.metersPerUnit: Union[float, None] = None
        self.framesPerSecond: Union[float, None] = None
        self.defaultPrim: Union[str, None] = None

        self.primChildren: List[Prim] = []

    def export_to_string(self):
        """Export Stage as USDA Ascii string.

        Returns:
          str: USDA string
        """
        return "TODO: export_to_string"

    def __repr__(self):
        s = "Stage:\n"
        if self.upAxis is not None:
            s += "  upAxis {}\n".format(self.upAxis)

        if self.metersPerUnit is not None:
            s += "  metersPerUnit {}\n".format(self.metersPerUnit)

        if self.framesPerSecond is not None:
            s += "  framesPerSecond {}\n".format(self.framesPerSecond)

        if self.defaultPrim is not None:
            s += "  defaultPrim {}\n".format(self.defaultPrim)

        return s

    def __str__(self):
        return self.__repr__()


def is_usd(file_like: FILE_LIKE) -> bool:
    """Test if input filename is a USD(USDC/USDA/UDSZ) file

    Args:
        file_like (FILE_LIKE): File-like object(Filename, file path, binary data)

    Returns:
        bool: True if USD file
    """

    from . import usd_loader

    ok: bool = usd_loader.is_usd(file_like)

    return ok


def is_usda(filename: Union[Path, str]) -> bool:
    """Test if input filename is a USDA file

    Args:
        filename (Path or str): Filename

    Returns:
        bool: True if USDA file
    """

    raise RuntimeError("TODO")


def is_usdc(filename: Union[Path, str]) -> bool:
    """Test if input filename is a USDC file

    Args:
        filename (Path or str): Filename

    Returns:
        bool: True if USDC file
    """

    raise RuntimeError("TODO")

def load_usd(filename: Union[Path, str]) -> Stage:
    """Loads USDC/USDA/UDSZ from a file

    Args:
        filename (Path or str): Filename

    Returns:
        Stage: Stage object
    """

    if isinstance(filename, str):
        filename = Path(filename)

    if not filename.exists():
        raise FileNotFoundError("File {} not found.".format(filename))

    if not filename.is_file():
        raise FileNotFoundError("File {} is not a file".format(filename))

    # TODO: Implement
    stage = Stage()
    stage.filename = filename

    return stage


def load_usd_from_string(input_str: str) -> Stage:
    """Loads USDA from string

    Args:
        input_str: Input string

    Returns:
        Stage: Stage object
    """

    # TODO: Implement
    stage = Stage()

    return stage


def load_usd_from_binary(input_binary: bytes) -> Stage:
    """Loads USDC/USDZ from binary

    Args:
        input_binary(bytes): Input binary data of USDC or USDZ

    Returns:
        Stage: Stage object
    """

    # TODO: Implement
    stage = Stage()

    return stage


@typechecked
def dumps(usd: Union[Stage, Prim],
          format: str = "usda",
          indent: int = 2) -> str:
    """Dump USD Stage or Prim tree to str.

    Args:
        format(str): dump format. `usda` only at this time.

    Returns:
        str: Dumped string.
    """

    return "TODO"

# TODO: Python 3.10+
# from typing_extensions import TypeAlias
# FILE_LIKE: TypeAlias =  Union[str, os.PathLike, TextIO]


@typechecked
def save(stage: Stage,
         file_like: Union[str, os.PathLike, IO[str], IO[bytes]],
         *,
         format: Literal["auto", "usda", "usdc", "usdz"] = "usda",
         indent: int = 2) -> None:
    """Save Stage to USD(ASCII only for now)

    Args:
        stage(Stage): Stage
        file_like(Union[str, os.PathLike, TextIO]): File like object
        format(str): USD format. Currently `usda`(ASCII) only
        indent(int): Indent size for ASCII output.
                     (applicable only for `usda` format)

    Returns:
        None. Exception will be raised when error.

    """

    from . import usd_saver

    usd_saver.save(stage, file_like, format=format, indent=indent)


@typechecked
def load(file_like: Union[str, os.PathLike, IO[str], IO[bytes]],
         *,
         format: Literal["auto", "usda", "usdc", "usdz"],
         encoding: str = None) -> Stage:
    """Load USD

    Args:
        file_like(Union[str, os.PathLike, IO[str], IO[bytes]]): File like object.
        format(str): Specify USD format.
        encoding(str): Optional. Explicitly specify encoding of the file.

    Returns:
        Stage. USD Stage.

    """

    from . import usd_loader

    usd = usd_loader.load(file_like, encoding=encoding, format=format)

    return usd


__all__ = ['Stage', 'version']
