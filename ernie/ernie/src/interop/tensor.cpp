#include "ernie/interop/tensor.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <numeric>
#include <string>
#include <utility>

namespace ernie {

namespace {

DLDataType ToDl(DType type) {
    switch (type) {
    case DType::UInt8:
        return {kDLUInt, 8, 1};
    case DType::UInt16:
        return {kDLUInt, 16, 1};
    case DType::UInt32:
        return {kDLUInt, 32, 1};
    case DType::Int32:
        return {kDLInt, 32, 1};
    case DType::Int64:
        return {kDLInt, 64, 1};
    case DType::Float16:
        return {kDLFloat, 16, 1};
    case DType::Float32:
        return {kDLFloat, 32, 1};
    case DType::Float64:
        return {kDLFloat, 64, 1};
    }
    throw std::invalid_argument("Unknown element type");
}

DType FromDl(DLDataType type) {
    if (type.lanes != 1) {
        throw std::invalid_argument("DLPack tensors with vector lanes are not supported");
    }
    if (type.code == kDLUInt && type.bits == 8) {
        return DType::UInt8;
    }
    if (type.code == kDLUInt && type.bits == 16) {
        return DType::UInt16;
    }
    if (type.code == kDLUInt && type.bits == 32) {
        return DType::UInt32;
    }
    if (type.code == kDLInt && type.bits == 32) {
        return DType::Int32;
    }
    if (type.code == kDLInt && type.bits == 64) {
        return DType::Int64;
    }
    if (type.code == kDLFloat && type.bits == 16) {
        return DType::Float16;
    }
    if (type.code == kDLFloat && type.bits == 32) {
        return DType::Float32;
    }
    if (type.code == kDLFloat && type.bits == 64) {
        return DType::Float64;
    }
    throw std::invalid_argument("Unsupported DLPack data type (code " + std::to_string(type.code) +
                                ", " + std::to_string(type.bits) + " bits)");
}

Device FromDl(DLDevice device) {
    switch (device.device_type) {
    case kDLCPU:
    case kDLCUDAHost:
        return {DeviceKind::Cpu, 0};
    case kDLCUDA:
        return {DeviceKind::Cuda, device.device_id};
    default:
        throw std::invalid_argument("Unsupported DLPack device type " +
                                    std::to_string(static_cast<int>(device.device_type)));
    }
}

std::vector<std::int64_t> RowMajor(const std::vector<std::int64_t>& shape) {
    std::vector<std::int64_t> strides(shape.size(), 1);
    for (std::size_t i = shape.size(); i > 1; --i) {
        strides[i - 2] = strides[i - 1] * shape[i - 1];
    }
    return strides;
}

/// Wraps a DLPack tensor's view, calling `release` once when the last reference goes.
template <typename Managed>
Tensor Adopt(Managed* managed, const DLTensor& dl, void (*release)(Managed*)) {
    std::shared_ptr<const void> owner(managed, [release](Managed* m) { release(m); });
    std::vector<std::int64_t> shape(dl.shape, dl.shape + dl.ndim);
    std::vector<std::int64_t> strides;
    if (dl.strides != nullptr) {
        strides.assign(dl.strides, dl.strides + dl.ndim);
    }
    void* data = static_cast<std::byte*>(dl.data) + dl.byte_offset;
    return Tensor(data, FromDl(dl.dtype), std::move(shape), std::move(strides), FromDl(dl.device),
                  std::move(owner));
}

void ReleaseVersioned(DLManagedTensorVersioned* managed) {
    if (managed->deleter != nullptr) {
        managed->deleter(managed);
    }
}

void ReleaseLegacy(DLManagedTensor* managed) {
    if (managed->deleter != nullptr) {
        managed->deleter(managed);
    }
}

/// What an exported tensor keeps alive, freed by its DLPack deleter.
struct Export {
    DLManagedTensorVersioned managed{};
    Tensor tensor;
    std::vector<std::int64_t> shape;
    std::vector<std::int64_t> strides;
};

}  // namespace

std::size_t SizeOf(DType type) {
    return ToDl(type).bits / 8u;
}

Tensor::Tensor(void* data, DType type, std::vector<std::int64_t> shape,
               std::vector<std::int64_t> strides, Device device, std::shared_ptr<const void> owner)
    : data_(data), type_(type), shape_(std::move(shape)), strides_(std::move(strides)),
      device_(device), owner_(std::move(owner)) {
    for (const std::int64_t extent : shape_) {
        if (extent < 0) {
            throw std::invalid_argument("Tensor dimensions must not be negative");
        }
    }
    if (strides_.empty()) {
        strides_ = RowMajor(shape_);
    } else if (strides_.size() != shape_.size()) {
        throw std::invalid_argument("Tensor strides must have one entry per dimension");
    }
}

Tensor Tensor::Zeros(DType type, std::vector<std::int64_t> shape) {
    const std::int64_t elements =
        std::accumulate(shape.begin(), shape.end(), std::int64_t{1}, std::multiplies<>());
    if (elements < 0) {
        throw std::invalid_argument("Tensor dimensions must not be negative");
    }
    const std::size_t bytes = static_cast<std::size_t>(elements) * SizeOf(type);
    std::shared_ptr<std::byte[]> memory(new std::byte[std::max<std::size_t>(bytes, 1)]());
    void* data = memory.get();
    return Tensor(data, type, std::move(shape), {}, {}, std::move(memory));
}

Tensor Tensor::FromDlpack(DLManagedTensorVersioned* managed) {
    if (managed == nullptr) {
        throw std::invalid_argument("Null DLPack tensor");
    }
    if (managed->version.major != DLPACK_MAJOR_VERSION) {
        ReleaseVersioned(managed);
        throw std::invalid_argument("Unsupported DLPack major version");
    }
    return Adopt(managed, managed->dl_tensor, ReleaseVersioned);
}

Tensor Tensor::FromDlpack(DLManagedTensor* managed) {
    if (managed == nullptr) {
        throw std::invalid_argument("Null DLPack tensor");
    }
    return Adopt(managed, managed->dl_tensor, ReleaseLegacy);
}

DLManagedTensorVersioned* Tensor::ToDlpack() const {
    auto* exported = new Export();
    exported->tensor = *this;
    exported->shape = shape_;
    exported->strides = strides_;
    DLManagedTensorVersioned& m = exported->managed;
    m.version = {DLPACK_MAJOR_VERSION, DLPACK_MINOR_VERSION};
    m.manager_ctx = exported;
    m.deleter = [](DLManagedTensorVersioned* self) {
        delete static_cast<Export*>(self->manager_ctx);
    };
    DLTensor& dl = m.dl_tensor;
    dl.data = data_;
    dl.device =
        device_.kind == DeviceKind::Cuda ? DLDevice{kDLCUDA, device_.id} : DLDevice{kDLCPU, 0};
    dl.ndim = static_cast<std::int32_t>(shape_.size());
    dl.dtype = ToDl(type_);
    dl.shape = exported->shape.data();
    dl.strides = exported->strides.data();
    dl.byte_offset = 0;
    return &m;
}

std::int64_t Tensor::Elements() const {
    return std::accumulate(shape_.begin(), shape_.end(), std::int64_t{1}, std::multiplies<>());
}

Tensor Tensor::Row(std::int64_t index) const {
    if (shape_.empty() || index < 0 || index >= shape_[0]) {
        throw std::out_of_range("Tensor::Row index out of range");
    }
    void* data = static_cast<std::byte*>(data_) +
                 index * strides_[0] * static_cast<std::int64_t>(SizeOf(type_));
    return Tensor(data, type_, {shape_.begin() + 1, shape_.end()},
                  {strides_.begin() + 1, strides_.end()}, device_, owner_);
}

bool Tensor::IsContiguous() const {
    return strides_ == RowMajor(shape_) || Elements() <= 1;
}

Tensor Tensor::Contiguous() const {
    if (device_.kind != DeviceKind::Cpu) {
        throw std::invalid_argument("Tensor::Contiguous needs a CPU tensor");
    }
    if (IsContiguous()) {
        return *this;
    }
    Tensor out = Zeros(type_, shape_);
    const std::size_t element = SizeOf(type_);
    std::vector<std::int64_t> index(shape_.size(), 0);
    auto* destination = static_cast<std::byte*>(out.Data());
    const auto* source = static_cast<const std::byte*>(data_);
    for (std::int64_t n = 0; n < Elements(); ++n) {
        std::int64_t offset = 0;
        for (std::size_t d = 0; d < shape_.size(); ++d) {
            offset += index[d] * strides_[d];
        }
        std::memcpy(destination + static_cast<std::size_t>(n) * element,
                    source + offset * static_cast<std::int64_t>(element), element);
        for (std::size_t d = shape_.size(); d > 0; --d) {
            if (++index[d - 1] < shape_[d - 1]) {
                break;
            }
            index[d - 1] = 0;
        }
    }
    return out;
}

}  // namespace ernie
