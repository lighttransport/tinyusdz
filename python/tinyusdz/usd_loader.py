# SPDX-License-Identifier: Apache 2.0
#
# USDA/USDZ/USDC loader using TinyUSDZ C API
#
# Requiement: Python 3.7+

import os
import warnings
from typing import Union, IO

from .compat_typing_extensions import TypeAlias  # python 3.10+

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
    from . import ctinyusd
    is_ctinyusd_available = True
except ImportError:
    is_ctinyusd_available = False


@typechecked
def is_usd(file_like: FILE_LIKE):
    if is_ctinyusd_available:

        if isinstance(file_like, str):
            ret = ctinyusd.c_tinyusd_is_usd_file(file_like)
            if ret == 0:
                return False
            else:
                return True

        elif isinstance(file_like, os.PathLike):
            # get string(filename) representation
            fspath = os.path.abspath(file_like)

            ret = ctinyusd.c_tinyusd_is_usd_file(fspath)
            if ret == 0:
                return False
            else:
                return True
        else:
            # Assume binary
            raise RuntimeError("TODO: Implement")
            

    else:
        warnings.warn(
            "Need `ctinyusd` native module to determine a given file is USD file.")
        return False


@typechecked
def load(file_like: FILE_LIKE,
         *,
         encoding: Union[str, None] = None,
         format: str = "usda"):

    if format.lower() == "usda":
        pass
    elif format.lower() == "usdc":
        pass
    elif format.lower() == "usdz":
        pass
    else:
        # auto detect
        pass

    raise RuntimeError("TODO: Implement")
    
