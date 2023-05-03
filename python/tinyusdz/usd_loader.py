# SPDX-License-Identifier: Apache 2.0
#
# USDA/USDZ/USDC loader using TinyUSDZ C API
#
# Requiement: Python 3.7+

import io
from typing import Union, IO

try:
    import ctinyusd
    is_ctinyusd_available = True
except ImportError:
    is_ctinyusd_available = False


def load(file_like: Union[str, io.PathLike, IO[str], IO[bytes]], *,
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

