#include "terraingen/gpu/metal.h"

#include <stdexcept>
#include <utility>

// On Apple platforms CMake compiles this file as Objective-C++ (with ARC) against the system Metal
// framework; elsewhere it is C++ and only the stub below, which reports Metal unavailable, builds.
#if defined(__APPLE__)

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

namespace terraingen {

/// The device, its command queue and the pipelines of compiled sources. Compiled with ARC, so the
/// Objective-C objects are released with the state.
struct MetalCompute::State {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    std::string name;
    std::mutex mutex;
    /// Pipelines keyed by their source, which lives for the whole process.
    std::map<const char*, id<MTLComputePipelineState>> pipelines;

    /// Opens the system's default device and a command queue.
    void Open() {
        device = MTLCreateSystemDefaultDevice();
        if (device == nil) {
            throw std::runtime_error("no Metal device");
        }
        queue = [device newCommandQueue];
        if (queue == nil) {
            throw std::runtime_error("Metal command queue creation failed");
        }
        name = [[device name] UTF8String];
    }

    /// The pipeline of `source`, compiled on first use.
    id<MTLComputePipelineState> CachedPipeline(Span<const char> source) {
        const auto found = pipelines.find(source.data());
        if (found != pipelines.end()) {
            return found->second;
        }
        NSString* text = [[NSString alloc] initWithBytes:source.data()
                                                  length:source.size()
                                                encoding:NSUTF8StringEncoding];
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:text options:nil error:&error];
        if (library == nil) {
            throw std::runtime_error(std::string("Metal kernel compilation failed: ") +
                                     [[error localizedDescription] UTF8String]);
        }
        id<MTLFunction> function = [library newFunctionWithName:@"main_0"];
        if (function == nil) {
            throw std::runtime_error("The Metal kernel has no main_0");
        }
        id<MTLComputePipelineState> pipeline =
            [device newComputePipelineStateWithFunction:function error:&error];
        if (pipeline == nil) {
            throw std::runtime_error(std::string("Metal pipeline creation failed: ") +
                                     [[error localizedDescription] UTF8String]);
        }
        return pipelines.emplace(source.data(), pipeline).first->second;
    }

    /// A shared device buffer holding a copy of each host buffer.
    std::vector<id<MTLBuffer>> Upload(Span<const DeviceBuffer> buffers) const {
        std::vector<id<MTLBuffer>> deviceBuffers;
        for (const DeviceBuffer& buffer : buffers) {
            id<MTLBuffer> deviceBuffer =
                [device newBufferWithBytes:buffer.bytes.data()
                                    length:std::max<std::size_t>(buffer.bytes.size(), 4)
                                   options:MTLResourceStorageModeShared];
            if (deviceBuffer == nil) {
                throw std::runtime_error("Metal buffer allocation failed");
            }
            deviceBuffers.push_back(deviceBuffer);
        }
        return deviceBuffers;
    }

    /// Encodes the dispatch, binding the buffers in order and the push constants after them,
    /// and waits for it to complete.
    void DispatchAndWait(id<MTLComputePipelineState> pipeline,
                         const std::vector<id<MTLBuffer>>& deviceBuffers,
                         Span<const std::byte> pushConstants, const Dispatch& dispatch) const {
        const auto& [groups, size] = dispatch;
        id<MTLCommandBuffer> command = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:pipeline];
        for (std::size_t i = 0; i < deviceBuffers.size(); ++i) {
            [encoder setBuffer:deviceBuffers[i] offset:0 atIndex:i];
        }
        [encoder setBytes:pushConstants.data()
                   length:pushConstants.size()
                  atIndex:deviceBuffers.size()];
        [encoder dispatchThreadgroups:MTLSizeMake(groups[0], groups[1], groups[2])
                threadsPerThreadgroup:MTLSizeMake(size[0], size[1], size[2])];
        [encoder endEncoding];
        [command commit];
        [command waitUntilCompleted];
        if ([command status] != MTLCommandBufferStatusCompleted) {
            throw std::runtime_error("The Metal kernel failed");
        }
    }
};

std::unique_ptr<MetalCompute> MetalCompute::Open() {
    auto state = std::make_unique<State>();
    state->Open();
    return std::unique_ptr<MetalCompute>(new MetalCompute(std::move(state)));
}

const std::string& MetalCompute::DeviceName() const {
    return state_->name;
}

void MetalCompute::Run(const KernelProgram& program, Span<const DeviceBuffer> buffers,
                       Span<const std::byte> pushConstants, const Dispatch& dispatch) {
    if (!CanRun(program)) {
        throw std::runtime_error("The kernel has no Metal source");
    }
    State& s = *state_;
    const std::lock_guard lock(s.mutex);
    @autoreleasepool {
        id<MTLComputePipelineState> pipeline = s.CachedPipeline(program.metal);
        const std::vector<id<MTLBuffer>> deviceBuffers = s.Upload(buffers);
        s.DispatchAndWait(pipeline, deviceBuffers, pushConstants, dispatch);
        for (std::size_t i = 0; i < buffers.size(); ++i) {
            if (buffers[i].output != nullptr) {
                std::memcpy(buffers[i].output, [deviceBuffers[i] contents],
                            buffers[i].bytes.size());
            }
        }
    }
}

}  // namespace terraingen

#else

namespace terraingen {

struct MetalCompute::State {};

std::unique_ptr<MetalCompute> MetalCompute::Open() {
    throw std::runtime_error("Metal runs only on Apple platforms");
}

const std::string& MetalCompute::DeviceName() const {
    throw std::runtime_error("Metal is unavailable on this platform");
}

void MetalCompute::Run(const KernelProgram& /*program*/, Span<const DeviceBuffer> /*buffers*/,
                       Span<const std::byte> /*pushConstants*/, const Dispatch& /*dispatch*/) {
    throw std::runtime_error("Metal is unavailable on this platform");
}

}  // namespace terraingen

#endif

namespace terraingen {

MetalCompute::MetalCompute(std::unique_ptr<State> state) : state_(std::move(state)) {}

MetalCompute::~MetalCompute() = default;

bool MetalCompute::CanRun(const KernelProgram& program) const {
    return !program.metal.empty();
}

}  // namespace terraingen
