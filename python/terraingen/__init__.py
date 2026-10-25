# SPDX-License-Identifier: BSD-3-Clause

"""TerrainGen: scientific terrain generation for planetary surfaces.

TerrainGen's log records go to the standard logging module on loggers named
"terraingen.<category>", such as "terraingen.craters". TerrainGen itself only produces records at
or above set_log_level(), which defaults to "warning"; lower it to see timing statements.
"""

import atexit
import logging as _logging

from . import _terraingen
from ._terraingen import *  # noqa: F403

#: The library's version, such as "0.1.0".
__version__: str = _terraingen.__version__

__all__ = sorted(name for name in dir(_terraingen) if not name.startswith("_"))
__all__ += ["__version__", "use_python_logging"]

_PYTHON_LEVELS = {
    "trace": 5,
    "debug": _logging.DEBUG,
    "info": _logging.INFO,
    "warning": _logging.WARNING,
    "error": _logging.ERROR,
}


def _forward_to_logging(level, category, message):
    _logging.getLogger(f"terraingen.{category}").log(_PYTHON_LEVELS[level], message)


def use_python_logging():
    """Send TerrainGen's log records to the logging module (done on import)."""
    _terraingen.set_log_sink(_forward_to_logging)


use_python_logging()
atexit.register(_terraingen.set_log_sink, None)
