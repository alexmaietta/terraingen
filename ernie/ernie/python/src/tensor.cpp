#include "bindings.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ernie::python {

namespace {

DType DTypeOf(nb::dlpack::dtype type) {
    using Code = nb::dlpack::dtype_code;
    const auto code = static_cast<Code>(type.code);
    if (type.lanes == 1) {
        if (code == Code::UInt && type.bits == 8) {
            return DType::UInt8;
        }
        if (code == Code::UInt && type.bits == 16) {
            return DType::UInt16;
        }
        if (code == Code::UInt && type.bits == 32) {
            return DType::UInt32;
        }
        if (code == Code::Int && type.bits == 32) {
            return DType::Int32;
        }
        if (code == Code::Int && type.bits == 64) {
            return DType::Int64;
        }
        if (code == Code::Float && type.bits == 16) {
            return DType::Float16;
        }
        if (code == Code::Float && type.bits == 32) {
            return DType::Float32;
        }
        if (code == Code::Float && type.bits == 64) {
            return DType::Float64;
        }
    }
    throw std::invalid_argument("Unsupported array element type (code " +
                                std::to_string(type.code) + ", " + std::to_string(type.bits) +
                                " bits); use uint8, uint16, uint32, int32, int64 or float16/32/64");
}

/// Owns a reference to a Python array; dropping it takes the GIL, since the last `Tensor` copy may
/// go away in C++ code that released it.
struct ArrayOwner {
    AnyArray array;

    ~ArrayOwner() {
        if (Py_IsInitialized() != 0) {
            nb::gil_scoped_acquire gil;
            array = AnyArray();
        }
    }
};

}  // namespace

nb::dlpack::dtype DlpackOf(DType type) {
    switch (type) {
    case DType::UInt8:
        return nb::dtype<std::uint8_t>();
    case DType::UInt16:
        return nb::dtype<std::uint16_t>();
    case DType::UInt32:
        return nb::dtype<std::uint32_t>();
    case DType::Int32:
        return nb::dtype<std::int32_t>();
    case DType::Int64:
        return nb::dtype<std::int64_t>();
    case DType::Float16:
        return {static_cast<std::uint8_t>(nb::dlpack::dtype_code::Float), 16, 1};
    case DType::Float32:
        return nb::dtype<float>();
    case DType::Float64:
        return nb::dtype<double>();
    }
    throw std::invalid_argument("Unknown element type");
}

Tensor TensorFromArray(const AnyArray& array) {
    const DType type = DTypeOf(array.dtype());
    Device device;
    if (array.device_type() == nb::device::cuda::value) {
        device = {DeviceKind::Cuda, array.device_id()};
    } else if (array.device_type() != nb::device::cpu::value &&
               array.device_type() != nb::device::cuda_host::value) {
        throw std::invalid_argument("Arrays must live on the CPU or a CUDA device");
    }
    std::vector<std::int64_t> shape(array.ndim());
    std::vector<std::int64_t> strides(array.ndim());
    for (std::size_t i = 0; i < array.ndim(); ++i) {
        shape[i] = static_cast<std::int64_t>(array.shape(i));
        strides[i] = array.stride(i);
    }
    auto owner = std::make_shared<ArrayOwner>();
    owner->array = array;
    return Tensor(const_cast<void*>(array.data()), type, std::move(shape), std::move(strides),
                  device, std::move(owner));
}

nb::object ArrayFromTensor(const Tensor& tensor) {
    auto* kept = new Tensor(tensor);
    nb::capsule owner(kept, [](void* p) noexcept { delete static_cast<Tensor*>(p); });
    std::vector<std::size_t> shape(tensor.Shape().begin(), tensor.Shape().end());
    const std::vector<std::int64_t>& strides = tensor.Strides();
    const bool cuda = tensor.Location().kind == DeviceKind::Cuda;
    const int deviceType = cuda ? nb::device::cuda::value : nb::device::cpu::value;
    if (!cuda) {
        nb::ndarray<nb::numpy, nb::ro> array(tensor.Data(), shape.size(), shape.data(), owner,
                                             strides.data(), DlpackOf(tensor.Type()), deviceType,
                                             0);
        return nb::cast(array, nb::rv_policy::reference);
    }
    nb::ndarray<nb::array_api, nb::ro> array(tensor.Data(), shape.size(), shape.data(), owner,
                                             strides.data(), DlpackOf(tensor.Type()), deviceType,
                                             tensor.Location().id);
    return nb::cast(array, nb::rv_policy::reference);
}

void BindTensor(nb::module_& m) {
    m.def(
        "_roundtrip", [](const AnyArray& array) { return ArrayFromTensor(TensorFromArray(array)); },
        nb::arg("array"),
        "Adopts `array` as an Ernie tensor and returns a read-only view of it; for testing "
        "interop.");
}

}  // namespace ernie::python
