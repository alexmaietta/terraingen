#pragma once

// Python.h, through nanobind, must come before any system header.
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>

#include "ernie/interop/tensor.h"
#include "ernie/scene/value.h"

namespace ernie::python {

namespace nb = nanobind;

/// Any array a DLPack-capable framework hands over (NumPy, PyTorch, JAX, CuPy, Warp), read-only
/// arrays included, since Ernie never writes into them.
using AnyArray = nb::ndarray<nb::ro>;

/// Adopts `array` without copying; the tensor keeps the Python array alive and releases it with
/// the GIL held.
/// @throws std::invalid_argument for an element type Ernie does not exchange.
Tensor TensorFromArray(const AnyArray& array);

/// The DLPack element type of `type`.
nb::dlpack::dtype DlpackOf(DType type);

/// A read-only view of `tensor` that keeps it alive: a NumPy array for CPU tensors, an
/// array-API object with `__dlpack__` (for torch.from_dlpack, CuPy, JAX) otherwise.
nb::object ArrayFromTensor(const Tensor& tensor);

/// Converts a Python value to a scene `Value`; see the module documentation for the mapping.
/// @throws nb::type_error for values with no `Value` equivalent.
Value ValueFromPython(nb::handle object);

nb::object ValueToPython(const Value& value);

void BindTensor(nb::module_& m);
void BindScene(nb::module_& m);
void BindRender(nb::module_& m);

/// The `gui` submodule; built only with Ernie's GUI (`ERNIE_HAS_GUI`).
void BindGui(nb::module_& m);

}  // namespace ernie::python
