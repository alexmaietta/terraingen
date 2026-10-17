#include "terraingen/gpu/metal.h"

// On Apple platforms CMake compiles this file as Objective-C++ (with ARC) against the system Metal
// framework; elsewhere it is C++ and only the stub below, which reports Metal unavailable, builds.
#if defined(__APPLE__)

#include "terraingen/util/log.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

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
    id<MTLComputePipelineState> PipelineFor(std::span<const char> source) {
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
};

namespace {

std::once_flag gOnce;
MetalCompute* gCompute = nullptr;
std::string gReason;

}  // namespace

MetalCompute::MetalCompute(std::unique_ptr<State> state) : state_(std::move(state)) {}

MetalCompute::~MetalCompute() = default;

MetalCompute* MetalCompute::Get() {
    std::call_once(gOnce, [] {
        try {
            auto state = std::make_unique<State>();
            state->Open();
            const std::string name = state->name;
            gCompute = new MetalCompute(std::move(state));
            LogInfo("gpu", "Metal compute on ", name);
        } catch (const std::exception& error) {
            gReason = error.what();
            LogInfo("gpu", "Metal unavailable: ", gReason);
        }
    });
    return gCompute;
}

std::string MetalCompute::Reason() {
    Get();
    return gReason;
}

const std::string& MetalCompute::DeviceName() const {
    return state_->name;
}

void MetalCompute::Run(std::span<const char> source,
                       const std::vector<std::span<std::byte>>& buffers,
                       std::span<const std::byte> pushConstants,
                       std::array<std::uint32_t, 3> groups,
                       std::array<std::uint32_t, 3> groupSize) {
    State& s = *state_;
    const std::lock_guard lock(s.mutex);
    @autoreleasepool {
        id<MTLComputePipelineState> pipeline = s.PipelineFor(source);
        std::vector<id<MTLBuffer>> deviceBuffers;
        for (const std::span<std::byte> buffer : buffers) {
            id<MTLBuffer> deviceBuffer =
                [s.device newBufferWithBytes:buffer.data()
                                      length:std::max<std::size_t>(buffer.size(), 4)
                                     options:MTLResourceStorageModeShared];
            if (deviceBuffer == nil) {
                throw std::runtime_error("Metal buffer allocation failed");
            }
            deviceBuffers.push_back(deviceBuffer);
        }
        id<MTLCommandBuffer> command = [s.queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:pipeline];
        for (std::size_t i = 0; i < deviceBuffers.size(); ++i) {
            [encoder setBuffer:deviceBuffers[i] offset:0 atIndex:i];
        }
        [encoder setBytes:pushConstants.data()
                   length:pushConstants.size()
                  atIndex:deviceBuffers.size()];
        [encoder dispatchThreadgroups:MTLSizeMake(groups[0], groups[1], groups[2])
                threadsPerThreadgroup:MTLSizeMake(groupSize[0], groupSize[1], groupSize[2])];
        [encoder endEncoding];
        [command commit];
        [command waitUntilCompleted];
        if ([command status] != MTLCommandBufferStatusCompleted) {
            throw std::runtime_error("The Metal kernel failed");
        }
        for (std::size_t i = 0; i < buffers.size(); ++i) {
            std::memcpy(buffers[i].data(), [deviceBuffers[i] contents], buffers[i].size());
        }
    }
}

}  // namespace terraingen

#else

#include <stdexcept>
#include <utility>

namespace terraingen {

struct MetalCompute::State {};

MetalCompute::MetalCompute(std::unique_ptr<State> state) : state_(std::move(state)) {}

MetalCompute::~MetalCompute() = default;

MetalCompute* MetalCompute::Get() {
    return nullptr;
}

std::string MetalCompute::Reason() {
    return "Metal runs only on Apple platforms";
}

const std::string& MetalCompute::DeviceName() const {
    throw std::logic_error("Metal is unavailable on this platform");
}

void MetalCompute::Run(std::span<const char> /*source*/,
                       const std::vector<std::span<std::byte>>& /*buffers*/,
                       std::span<const std::byte> /*pushConstants*/,
                       std::array<std::uint32_t, 3> /*groups*/,
                       std::array<std::uint32_t, 3> /*groupSize*/) {
    throw std::logic_error("Metal is unavailable on this platform");
}

}  // namespace terraingen

#endif
