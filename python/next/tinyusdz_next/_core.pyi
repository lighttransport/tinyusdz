"""TinyUSDZ Next - C extension type stubs."""

from typing import Any, Dict, List, Optional, Tuple, Union

# Value types returned by get_property
ValueType = Union[
    bool,
    int,
    float,
    str,
    None,
    Tuple[float, float],
    Tuple[float, float, float],
    Tuple[float, float, float, float],
    Tuple[float, float, float, float, float, float, float, float,
          float, float, float, float, float, float, float, float],
    List[float],
    List[int],
]

class Prim:
    """Represents a USD prim in a scene."""

    name: str
    path: str
    type_name: str

    def get_property(self, name: str) -> ValueType:
        """Get a property value by name."""

    def has_property(self, name: str) -> bool:
        """Check if a property exists."""

    def get_property_names(self) -> Optional[List[str]]:
        """Get all property names, or None if empty."""

    def get_properties(self) -> Dict[str, ValueType]:
        """Get all properties as a dict."""

class Stage:
    """Represents a USD stage (composed scene)."""

    default_prim: Optional[str]

    def load(self, filename: str) -> None:
        """Load a USD file (auto-detects USDA/USDC/USDZ)."""

    def get_prim_at_path(self, path: str) -> Optional[Prim]:
        """Get a prim by path (e.g. '/root/child')."""

    def traverse(self) -> List[Prim]:
        """Traverse all prims depth-first."""

    def get_root_prims(self) -> List[Prim]:
        """Get all root-level prims."""

    def add_reference(
        self, prim_path: str, asset_path: str, ref_prim_path: str
    ) -> None:
        """Add a reference arc: @asset_path@</ref_prim_path>"""

    def add_payload(
        self, prim_path: str, asset_path: str, payload_prim_path: str
    ) -> None:
        """Add a payload arc: @asset_path@</payload_prim_path>"""

    def add_inherit(self, prim_path: str, inherited_prim_path: str) -> None:
        """Add an inherit arc: </inherited_prim_path>"""

    def add_specialize(self, prim_path: str, specialized_prim_path: str) -> None:
        """Add a specialize arc: </specialized_prim_path>"""

    def set_variant_selection(
        self, prim_path: str, variant_set: str, variant_name: str
    ) -> None:
        """Set variant selection on a prim."""

class UsdError(RuntimeError):
    """Base USD error."""

class UsdParseError(UsdError):
    """USD parse error."""

class UsdIoError(UsdError):
    """USD I/O error."""

def load(filename: str) -> Stage:
    """Load a USD file into a new Stage."""
