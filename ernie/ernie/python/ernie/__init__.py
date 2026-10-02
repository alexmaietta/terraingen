"""Ernie: a standalone Vulkan renderer with a USD-style scene.

Build a Scene of typed prims named by path, set their attributes by USD name, write bulk data as
arrays from any DLPack-capable framework (NumPy, PyTorch, JAX, CuPy, Warp) without copying, and
render it::

    import numpy as np
    import ernie
    from ernie import tokens

    scene = ernie.Scene()
    scene.define("/World/Terrain", tokens.TERRAIN).write(tokens.HEIGHTS, heights).set(
        tokens.CELL_SIZE, 2.0)
    scene.define("/World/Camera", tokens.CAMERA).set(
        tokens.TRANSFORM, ernie.look_at_transform((0, -500, 300), (0, 0, 0)))
    image = ernie.RasterRenderer().render(scene)  # uint8 (rows, cols, 4)

Attribute values map from Python as follows:

- bool, int, float and str become bool, int, double and a token or asset path;
- a Path is a relationship target, such as material:binding;
- a sequence of two ints becomes an int2, such as a resolution; two, three or four numbers
  become a 2-, 3- or 4-vector; more become a float array;
- four rows of four numbers (or a 4 x 4 array) become a matrix in math convention, with column
  vectors and the translation in the last column. This is the transpose of USD's Gf.Matrix4d
  layout, which stores the translation in the last row.

Arrays given to write() are shared, not copied, and kept alive by the scene; arrays returned by
read() are read-only views. Write again after changing an array in place, so the change is
tracked. CUDA arrays are copied into Ernie's GPU buffers device to device, ordered on the GPU
after the work that produced them.

Rendering follows ovrtx: renderer.step(scene, render_products={"/Render/Camera"}) returns the
outputs by product, and product.frames[0].render_vars["/Render/Camera/LdrColor"].map(device=...)
delivers each output to Device.CPU, Device.CUDA (linear, filled on the GPU) or Device.CUDA_ARRAY
(the rendered image itself, no copy); unmap() hands a CUDA_ARRAY back. step_async() returns an
Operation to wait(timeout_ns=...) on or poll, then fetch().

renderer.bind(scene, path, attribute) binds a Mesh's points or a PointInstancer's positions,
orientations or scales for a simulation to write in place: binding.map(stream=...) returns the
renderer's own GPU array, writable through DLPack, and binding.unmap(stream=...) hands it back,
with both sides ordered on the GPU and nothing copied.

render(scene, device="cuda") returns a CudaArray in GPU memory shared with Vulkan. It returns as
soon as the render is submitted; torch.from_dlpack(result) (or CuPy, JAX) makes the consumer's
stream wait for the render on the GPU, so the host never blocks.
"""

from ._ernie import (
    AttributeBinding,
    ChangeCursor,
    CudaArray,
    CudaArrayMapping,
    Device,
    Dirty,
    Frame,
    Operation,
    Path,
    Prim,
    RasterRenderer,
    RenderProductOutput,
    RenderVar,
    Scene,
    dirty_of,
    distant_light_transform,
    look_at_transform,
    tokens,
)

__all__ = [
    "AttributeBinding",
    "ChangeCursor",
    "CudaArray",
    "CudaArrayMapping",
    "Device",
    "Dirty",
    "Frame",
    "Operation",
    "Path",
    "Prim",
    "RasterRenderer",
    "RenderProductOutput",
    "RenderVar",
    "Scene",
    "dirty_of",
    "distant_light_transform",
    "look_at_transform",
    "tokens",
]
