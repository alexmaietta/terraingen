#include "ernie/interop/tensor.h"

#include <gtest/gtest.h>

#include <array>
#include <vector>

using ernie::Device;
using ernie::DeviceKind;
using ernie::DType;
using ernie::Tensor;

namespace {

/// A producer-side DLPack tensor that records whether its deleter ran.
struct Producer {
    std::vector<float> values;
    std::vector<std::int64_t> shape;
    std::vector<std::int64_t> strides;
    bool* deleted = nullptr;
    DLManagedTensorVersioned managed{};

    static DLManagedTensorVersioned* Make(std::vector<float> values,
                                          std::vector<std::int64_t> shape,
                                          std::vector<std::int64_t> strides, bool* deleted,
                                          DLDataType type = {kDLFloat, 32, 1},
                                          DLDevice device = {kDLCPU, 0}) {
        auto* producer =
            new Producer{std::move(values), std::move(shape), std::move(strides), deleted};
        DLManagedTensorVersioned& m = producer->managed;
        m.version = {DLPACK_MAJOR_VERSION, DLPACK_MINOR_VERSION};
        m.manager_ctx = producer;
        m.deleter = [](DLManagedTensorVersioned* self) {
            auto* owner = static_cast<Producer*>(self->manager_ctx);
            *owner->deleted = true;
            delete owner;
        };
        m.dl_tensor.data = producer->values.data();
        m.dl_tensor.device = device;
        m.dl_tensor.ndim = static_cast<std::int32_t>(producer->shape.size());
        m.dl_tensor.dtype = type;
        m.dl_tensor.shape = producer->shape.data();
        m.dl_tensor.strides = producer->strides.empty() ? nullptr : producer->strides.data();
        return &m;
    }
};

}  // namespace

TEST(Tensor, ZerosIsContiguousOwnedAndZeroed) {
    const Tensor tensor = Tensor::Zeros(DType::Float32, {2, 3});
    EXPECT_EQ(tensor.Elements(), 6);
    EXPECT_EQ(tensor.Strides(), (std::vector<std::int64_t>{3, 1}));
    EXPECT_TRUE(tensor.IsContiguous());
    EXPECT_EQ(tensor.Location(), Device{});
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(tensor.As<float>()[i], 0.0f);
    }
    EXPECT_THROW(tensor.As<double>(), std::invalid_argument);
    EXPECT_THROW(Tensor::Zeros(DType::Float32, {-1}), std::invalid_argument);
}

TEST(Tensor, SizesMatchDLPackBits) {
    EXPECT_EQ(ernie::SizeOf(DType::UInt8), 1u);
    EXPECT_EQ(ernie::SizeOf(DType::UInt16), 2u);
    EXPECT_EQ(ernie::SizeOf(DType::Float16), 2u);
    EXPECT_EQ(ernie::SizeOf(DType::UInt32), 4u);
    EXPECT_EQ(ernie::SizeOf(DType::Int32), 4u);
    EXPECT_EQ(ernie::SizeOf(DType::Int64), 8u);
    EXPECT_EQ(ernie::SizeOf(DType::Float32), 4u);
    EXPECT_EQ(ernie::SizeOf(DType::Float64), 8u);
}

TEST(Tensor, CopyChecksTheShape) {
    const std::array<std::int32_t, 4> values{1, 2, 3, 4};
    const Tensor tensor = Tensor::Copy<std::int32_t>(values, {2, 2});
    EXPECT_EQ(tensor.As<std::int32_t>()[3], 4);
    EXPECT_THROW(Tensor::Copy<std::int32_t>(values, {3}), std::invalid_argument);
}

TEST(Tensor, ViewsRejectMismatchedStrides) {
    float value = 0.0f;
    EXPECT_THROW(Tensor(&value, DType::Float32, {1, 1}, {1}), std::invalid_argument);
    EXPECT_THROW(Tensor(&value, DType::Float32, {-2}), std::invalid_argument);
}

TEST(Tensor, RowsShareMemoryAndKeepTheOwnerAlive) {
    const std::array<float, 6> values{0, 1, 2, 3, 4, 5};
    Tensor row;
    {
        const Tensor tensor = Tensor::Copy<float>(values, {3, 2});
        row = tensor.Row(2);
        EXPECT_EQ(row.Shape(), (std::vector<std::int64_t>{2}));
        EXPECT_EQ(static_cast<float*>(row.Data()), tensor.As<float>() + 4);
        EXPECT_THROW(tensor.Row(3), std::out_of_range);
        EXPECT_THROW(tensor.Row(-1), std::out_of_range);
    }
    EXPECT_EQ(row.As<float>()[1], 5.0f);
    EXPECT_THROW(Tensor().Row(0), std::out_of_range);
}

TEST(Tensor, ContiguousGathersAStridedView) {
    const std::array<float, 6> values{0, 1, 2, 3, 4, 5};
    const Tensor tensor = Tensor::Copy<float>(values, {2, 3});
    const Tensor transposed(tensor.Data(), DType::Float32, {3, 2}, {1, 3}, {}, nullptr);
    EXPECT_FALSE(transposed.IsContiguous());
    const Tensor packed = transposed.Contiguous();
    EXPECT_TRUE(packed.IsContiguous());
    EXPECT_EQ(std::vector<float>(packed.As<float>(), packed.As<float>() + 6),
              (std::vector<float>{0, 3, 1, 4, 2, 5}));
    EXPECT_EQ(tensor.Contiguous().Data(), tensor.Data()) << "already contiguous: no copy";

    const Tensor cuda(tensor.Data(), DType::Float32, {6}, {}, {DeviceKind::Cuda, 0});
    EXPECT_THROW(cuda.Contiguous(), std::invalid_argument);
}

TEST(Tensor, FromDlpackAdoptsWithoutCopyingAndDeletesOnce) {
    bool deleted = false;
    DLManagedTensorVersioned* managed = Producer::Make({1, 2, 3, 4, 5, 6}, {2, 3}, {}, &deleted);
    const void* data = managed->dl_tensor.data;
    {
        const Tensor tensor = Tensor::FromDlpack(managed);
        const Tensor copy = tensor;
        EXPECT_EQ(tensor.Data(), data);
        EXPECT_EQ(tensor.Type(), DType::Float32);
        EXPECT_EQ(tensor.Shape(), (std::vector<std::int64_t>{2, 3}));
        EXPECT_EQ(tensor.Strides(), (std::vector<std::int64_t>{3, 1}));
        EXPECT_FALSE(deleted);
    }
    EXPECT_TRUE(deleted);
}

TEST(Tensor, FromDlpackKeepsStridesAndByteOffset) {
    bool deleted = false;
    DLManagedTensorVersioned* managed =
        Producer::Make({9, 1, 2, 3, 4, 5, 6}, {3, 2}, {1, 3}, &deleted);
    managed->dl_tensor.byte_offset = sizeof(float);
    const Tensor tensor = Tensor::FromDlpack(managed);
    EXPECT_EQ(tensor.Strides(), (std::vector<std::int64_t>{1, 3}));
    EXPECT_EQ(tensor.As<float>()[0], 1.0f);
    const Tensor packed = tensor.Contiguous();
    EXPECT_EQ(packed.As<float>()[1], 4.0f);
}

TEST(Tensor, FromDlpackRejectsUnsupportedInputsButStillReleasesThem) {
    bool deleted = false;
    EXPECT_THROW(Tensor::FromDlpack(Producer::Make({1}, {1}, {}, &deleted, {kDLFloat, 32, 4})),
                 std::invalid_argument);
    EXPECT_TRUE(deleted);

    deleted = false;
    EXPECT_THROW(Tensor::FromDlpack(Producer::Make({1}, {1}, {}, &deleted, {kDLBfloat, 16, 1})),
                 std::invalid_argument);
    EXPECT_TRUE(deleted);

    deleted = false;
    EXPECT_THROW(Tensor::FromDlpack(
                     Producer::Make({1}, {1}, {}, &deleted, {kDLFloat, 32, 1}, {kDLMetal, 0})),
                 std::invalid_argument);
    EXPECT_TRUE(deleted);

    deleted = false;
    DLManagedTensorVersioned* future = Producer::Make({1}, {1}, {}, &deleted);
    future->version.major = DLPACK_MAJOR_VERSION + 1;
    EXPECT_THROW(Tensor::FromDlpack(future), std::invalid_argument);
    EXPECT_TRUE(deleted);

    EXPECT_THROW(Tensor::FromDlpack(static_cast<DLManagedTensorVersioned*>(nullptr)),
                 std::invalid_argument);
    EXPECT_THROW(Tensor::FromDlpack(static_cast<DLManagedTensor*>(nullptr)), std::invalid_argument);
}

TEST(Tensor, FromDlpackAcceptsCudaDevicesAndLegacyTensors) {
    bool deleted = false;
    const Tensor cuda =
        Tensor::FromDlpack(Producer::Make({1}, {1}, {}, &deleted, {kDLFloat, 32, 1}, {kDLCUDA, 1}));
    EXPECT_EQ(cuda.Location(), (Device{DeviceKind::Cuda, 1}));

    static bool legacyDeleted = false;
    static std::int64_t shape = 3;
    static std::array<std::uint16_t, 3> values{7, 8, 9};
    auto* legacy = new DLManagedTensor{};
    legacy->dl_tensor.data = values.data();
    legacy->dl_tensor.device = {kDLCPU, 0};
    legacy->dl_tensor.ndim = 1;
    legacy->dl_tensor.dtype = {kDLUInt, 16, 1};
    legacy->dl_tensor.shape = &shape;
    legacy->deleter = [](DLManagedTensor* self) {
        legacyDeleted = true;
        delete self;
    };
    {
        const Tensor tensor = Tensor::FromDlpack(legacy);
        EXPECT_EQ(tensor.As<std::uint16_t>()[2], 9);
    }
    EXPECT_TRUE(legacyDeleted);
}

TEST(Tensor, ToDlpackRoundTripsAndKeepsTheMemoryAliveUntilDeleted) {
    const std::array<double, 4> values{1, 2, 3, 4};
    DLManagedTensorVersioned* exported = nullptr;
    const void* data = nullptr;
    {
        const Tensor tensor = Tensor::Copy<double>(values, {4});
        exported = tensor.ToDlpack();
        data = tensor.Data();
    }
    EXPECT_EQ(exported->version.major, static_cast<std::uint32_t>(DLPACK_MAJOR_VERSION));
    EXPECT_EQ(exported->dl_tensor.data, data);
    EXPECT_EQ(exported->dl_tensor.dtype.code, kDLFloat);
    EXPECT_EQ(exported->dl_tensor.dtype.bits, 64);
    EXPECT_EQ(exported->dl_tensor.ndim, 1);
    EXPECT_EQ(exported->dl_tensor.shape[0], 4);
    EXPECT_EQ(static_cast<double*>(exported->dl_tensor.data)[3], 4.0);

    const Tensor back = Tensor::FromDlpack(exported);
    EXPECT_EQ(back.Data(), data);
    EXPECT_EQ(back.As<double>()[0], 1.0);
}

TEST(Tensor, ToDlpackExportsEveryTypeAndCudaDevices) {
    float value = 0.0f;
    for (const DType type : {DType::UInt8, DType::UInt16, DType::UInt32, DType::Int32, DType::Int64,
                             DType::Float16, DType::Float32, DType::Float64}) {
        const Tensor tensor(&value, type, {1}, {}, {DeviceKind::Cuda, 2});
        DLManagedTensorVersioned* exported = tensor.ToDlpack();
        EXPECT_EQ(exported->dl_tensor.device.device_type, kDLCUDA);
        EXPECT_EQ(exported->dl_tensor.device.device_id, 2);
        const Tensor back = Tensor::FromDlpack(exported);
        EXPECT_EQ(back.Type(), type);
    }
}
