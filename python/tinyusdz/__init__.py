# SPDX-License-Identifier: Apache 2.0
#
# Requiement: Python 3.7+

# TODO
# - [ ] Refactor components

import os
from pathlib import Path

from typing import Union, List, Any, TextIO
from enum import Enum, auto

from . import version

try:
  from typeguard import typechecked
  is_typegurad_available = True
except:
  is_typegurad_available = False

  # no-op 
  def typechecked(cls):
      return cls


#try:
#    import ctinyusdz
#except:
#    raise ImportError("ctinyusdz dll/so not found. Please check if ctinyusdz.*.dll/so exists in your python envrionemnt(Set PYTHONPATH if requred.), or Python version may differ.")

try:
    import numpy as np
except:
    pass

try:
    import pandas as pd
except:
    pass

def is_typeguard_available():
    import importlib

    if importlib.util.find_spec("typeguard"):
        return True

    return False

def is_numpy_available():
    import importlib

    if importlib.util.find_spec("numpy"):
        return True

    return False


def is_pandas_available():
    import importlib
   
    if importlib.util.find_spec("pandas"):
        return True

    return False

__version__ = "0.8.0rc2"

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
# TODO: use importlib?
try:
    from typing import Literal
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
    Specifiers = Literal[ "def", "over", "class" ]
except ImportError:
    # try backported Literal
    try:
        from typing_extensions import Literal
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
        Specifiers = Literal[ "def", "over", "class" ]
    except ImportError:
        USDTypes = [
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
        Specifiers = [ "def", "over", "class" ]
    

"""
numpy-like ndarray for Attribute data(e.g. points, normals, ...) 
"""
class NDArray:
    def __init__(self, dtype: str = "uint8"):

        assert dtype in USDTypes

        self.dtype: str = "uint8" 
        self.dim: int = 1 # In USD, 1D or 2D only for array data

        self._data = None

    def from_numpy(self, nddata):
        if not is_numpy_available():
            raise ImportError("numpy is not installed.")

        import numpy as np
        
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

        import numpy as np
        
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

class Prim:
    def __init__(self, name: str, specifier: str = "def"):

        #assert specifier in Specifiers

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

    def set_prop(self, key:str, value: Any):
        self._props[key] = value

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
    def __init__(self, op_type: XformOpType = XformOpType.Translate, value: Any = None):
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
    #Axis = Literal["X", "Y", "Z"]

    def __init__(self):
        self._stage = None 
        self.filename = ""

        self.upAxis: Union[str, None] = None
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

def is_usd(filename: Union[Path, str]) -> bool:
    """Test if input filename is a USD(USDC/USDA/UDSZ) file

    Args:
        filename (Path or str): Filename

    Returns:
        bool: True if USD file
    """

def is_usda(filename: Union[Path, str]) -> bool:
    """Test if input filename is a USDA file

    Args:
        filename (Path or str): Filename

    Returns:
        bool: True if USDA file
    """

def is_usdc(filename: Union[Path, str]) -> bool:
    """Test if input filename is a USDC file

    Args:
        filename (Path or str): Filename

    Returns:
        bool: True if USDC file
    """

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
def dumps(usd: Union[Stage, Prim], format: str = "usda", indent: int = 2) -> str:
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
def save(usd: Stage, file_like: Union[str, os.PathLike, TextIO], *, format: str = "usda", indent: int = 2) -> None:

    if isinstance(file_like, str):
        # filename
        f = open(file_like, 'w', encoding='utf-8')
    else:
        f = file_like
    
    s: str = dumps(usd, format=format, indent=indent)

    f.write(s)

    f.close()

    return True
        
        

__all__ = ['Stage', 'version']
