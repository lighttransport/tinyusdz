# import ctinyusdz

from pathlib import Path
from typing import Union, List, Literal


class Prim:
    def __init__(self):
        self.element_name: str = ""

        # Corresponding Prim index in ctinyusdz's Prim.
        self._prim_idx: int = 0


class GeomMesh(Prim):
    def __init__(self):
        super().__init__()
        pass


class Stage:
    Axis = Literal["X", "Y", "Z"]

    def __init__(self):
        self.filename = ""

        self.upAxis: Axis = "Y"

        self.children: List[Prim]


def load_usd(filename: Union[Path, str]) -> Stage:
    """
    Load USDC/USDA/UDSZ from a file

    Args:
        filename(Path or str): Filename

    Returns:
        Stage object
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
    """
    Load USDA from string

    Args:
        input_str: Input string

    Returns:
        Stage object
    """

    # TODO: Implement
    stage = Stage()

    return stage


def load_usd_from_binary(input_str: bytes) -> Stage:
    """
    Load USDC/USDZ from binary

    Args:
        input_binary(bytes): Input binary

    Returns:
        Stage object
    """

    # TODO: Implement
    stage = Stage()

    return stage
