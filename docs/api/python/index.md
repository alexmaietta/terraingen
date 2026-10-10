# Python API

The `terraingen` package wraps the C++ library with nanobind: every public class and function of the C++ library has a Python counterpart (a test checks this against the headers). Rasters share their cells with NumPy without copying, long-running functions release the GIL, and the base classes (distributions, age and degradation models, profiles, sources, layers, filters and albedo) can be subclassed in Python, as [Writing models in Python](../../extending/python.md) describes.

The pages that follow group the API by topic.
