#pragma once

#include <dlpack/dlpack.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace ernie {

/// Element types Ernie exchanges.
enum class DType : std::uint8_t { UInt8, UInt16, UInt32, Int32, Float16, Float32, Float64 };

/// Bytes per element of `type`.
std::size_t SizeOf(DType type);

/// Where a tensor's memory lives.
enum class DeviceKind : std::uint8_t { Cpu, Cuda };

struct Device {
    DeviceKind kind = DeviceKind::Cpu;
    int id = 0;

    bool operator==(const Device&) const = default;
};

/// A strided n-dimensional view of typed memory with an optional owner that keeps it alive: Ernie's
/// end of DLPack.
///
/// Shapes and strides are in elements, as in DLPack; empty strides mean row-major contiguous.
/// Copies share the memory and its owner. `FromDlpack` adopts a producer's tensor without copying
/// and calls its deleter when the last `Tensor` referencing it is gone; `ToDlpack` hands memory to
/// a consumer the same way.
///
/// Reference: DLPack, https://dmlc.github.io/dlpack/latest/
class Tensor {
  public:
    Tensor() = default;

    /// A view of `data`; `owner`, if given, is kept alive as long as any copy of the view.
    /// @throws std::invalid_argument if a dimension is negative or strides do not match the
    /// shape's rank.
    Tensor(void* data, DType type, std::vector<std::int64_t> shape,
           std::vector<std::int64_t> strides = {}, Device device = {},
           std::shared_ptr<const void> owner = {});

    /// An owning, contiguous CPU tensor filled with zeros.
    static Tensor Zeros(DType type, std::vector<std::int64_t> shape);

    /// An owning, contiguous CPU copy of `values` with `shape`.
    /// @throws std::invalid_argument if the shape's element count differs from `values.size()`.
    template <typename T>
    static Tensor Copy(std::span<const T> values, std::vector<std::int64_t> shape);

    /// Adopts a DLPack tensor; its deleter runs when the last `Tensor` referencing it is gone.
    /// @throws std::invalid_argument for an unsupported data type, lanes or device; the producer's
    /// deleter still runs.
    static Tensor FromDlpack(DLManagedTensorVersioned* managed);
    static Tensor FromDlpack(DLManagedTensor* managed);

    /// Exports this tensor; the result keeps the memory alive until the consumer calls its deleter.
    DLManagedTensorVersioned* ToDlpack() const;

    bool Empty() const {
        return data_ == nullptr;
    }

    void* Data() const {
        return data_;
    }

    DType Type() const {
        return type_;
    }

    Device Location() const {
        return device_;
    }

    const std::vector<std::int64_t>& Shape() const {
        return shape_;
    }

    /// Strides in elements, filled in for contiguous tensors.
    const std::vector<std::int64_t>& Strides() const {
        return strides_;
    }

    std::size_t Rank() const {
        return shape_.size();
    }

    std::int64_t Elements() const;

    /// A view of entry `index` along the first dimension, sharing this tensor's memory and owner.
    /// @throws std::out_of_range if the tensor has no dimensions or `index` is out of range.
    Tensor Row(std::int64_t index) const;

    bool IsContiguous() const;

    /// A contiguous, owning CPU copy, or this tensor itself when it already is contiguous on the
    /// CPU.
    /// @throws std::invalid_argument for a tensor that is not on the CPU.
    Tensor Contiguous() const;

    /// Typed pointer to the first element.
    /// @throws std::invalid_argument if `T` does not match the element type.
    template <typename T> T* As() const;

  private:
    void* data_ = nullptr;
    DType type_ = DType::Float32;
    std::vector<std::int64_t> shape_;
    std::vector<std::int64_t> strides_;
    Device device_;
    std::shared_ptr<const void> owner_;
};

/// The `DType` of a C++ element type.
template <typename T> constexpr DType DTypeOf();

template <> constexpr DType DTypeOf<std::uint8_t>() {
    return DType::UInt8;
}
template <> constexpr DType DTypeOf<std::uint16_t>() {
    return DType::UInt16;
}
template <> constexpr DType DTypeOf<std::uint32_t>() {
    return DType::UInt32;
}
template <> constexpr DType DTypeOf<std::int32_t>() {
    return DType::Int32;
}
template <> constexpr DType DTypeOf<float>() {
    return DType::Float32;
}
template <> constexpr DType DTypeOf<double>() {
    return DType::Float64;
}

template <typename T>
Tensor Tensor::Copy(std::span<const T> values, std::vector<std::int64_t> shape) {
    Tensor tensor = Zeros(DTypeOf<T>(), std::move(shape));
    if (static_cast<std::size_t>(tensor.Elements()) != values.size()) {
        throw std::invalid_argument("Tensor::Copy shape does not match the number of values");
    }
    std::copy(values.begin(), values.end(), tensor.As<T>());
    return tensor;
}

template <typename T> T* Tensor::As() const {
    if (DTypeOf<T>() != type_) {
        throw std::invalid_argument("Tensor::As with the wrong element type");
    }
    return static_cast<T*>(data_);
}

}  // namespace ernie
