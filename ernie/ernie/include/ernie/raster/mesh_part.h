#pragma once

#include "ernie/gpu/gpu_buffer.h"
#include "ernie/gpu/pipeline.h"
#include "ernie/interop/cuda_interop.h"
#include "ernie/interop/tensor.h"
#include "ernie/raster/raster_part.h"
#include "ernie/raster/raster_renderer.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ernie {

/// Triangle mesh geometry in a mesh's own frame.
struct MeshData {
    std::vector<float> points;           ///< x, y, z per vertex.
    std::vector<std::uint32_t> indices;  ///< Three vertex indices per triangle.
    std::vector<float> normals;          ///< Empty, or x, y, z per vertex.
    std::vector<float> colors;           ///< Empty, or linear r, g, b per vertex.
};

/// Triangulates polygons given as USD `faceVertexCounts` and `faceVertexIndices` into fans.
/// @throws std::invalid_argument if a face has fewer than 3 vertices, the counts do not add up to
/// the number of indices, or an index is negative or not below `pointCount`.
std::vector<std::uint32_t> TriangulateFaces(std::span<const std::int64_t> faceVertexCounts,
                                            std::span<const std::int64_t> faceVertexIndices,
                                            std::size_t pointCount);

/// Per-instance arrays of a point instancer, as UsdGeomPointInstancer defines them.
struct InstanceData {
    std::vector<float> positions;  ///< x, y, z per instance.
    std::vector<float>
        orientations;           ///< Empty, or a quaternion x, y, z, w (real last) per instance.
    std::vector<float> scales;  ///< Empty, or x, y, z per instance.
};

/// Per-instance arrays on a CUDA device, as contiguous float32 tensors: positions (n, 3), and
/// orientations (n, 4, imaginary part first) and scales (n, 3), each empty when absent.
struct DeviceInstanceData {
    Tensor positions;
    Tensor orientations;
    Tensor scales;
    /// Bounds of the instances in the instancer's frame, prototypes included, as USD's `extent`;
    /// without them the positions and scales are copied to the host to compute bounds.
    std::optional<std::pair<glm::vec3, glm::vec3>> extent;
};

/// One draw of a mesh: either the mesh itself, or the given instances of an instancer with the
/// mesh as their prototype.
struct MeshItem {
    std::uint32_t mesh = 0;
    glm::mat4 transform{1.0f};  ///< World transform of the mesh, or of the instancer.
    glm::mat4 prototype{1.0f};  ///< Instanced: the prototype's own transform, applied first.
    std::optional<std::uint32_t> instancer;
    std::vector<std::uint32_t> instances;  ///< Instanced: indices into the instancer's arrays.
    std::uint32_t material = RasterRenderer::kDefaultMaterial;  ///< Id in the renderer's table.
    glm::vec3 tint{1.0f};      ///< Multiplies the material's albedo where colours are not used.
    bool vertexColors = true;  ///< Use the mesh's vertex colours, if it has any, as the tint.
};

/// Triangle meshes and point-instanced copies of them: a `RasterPart`, lit with the renderer's
/// sun and shadows and shaded with the renderer's materials, like the terrain.
///
/// Meshes and instancers are added once and their arrays replaced as they change; the caller
/// lists the draws (`SetItems`), each with a material, and the renderer prepares and records them
/// every frame. Detail textures are projected along the three world axes (triplanar). Geometry of
/// all meshes lives in shared
/// pools of storage buffers that the vertex shader reads (no vertex buffers), rebuilt when meshes
/// are added or removed and rewritten in place when only points move. Instances are composed on
/// the GPU from the raw position, orientation and scale arrays, so updating thousands of
/// instances from a simulation costs a copy of those arrays and nothing per instance on the CPU.
/// Meshes are two-sided; without normals each triangle is lit flat.
///
/// Reference: Pixar, "UsdGeomPointInstancer",
/// https://openusd.org/release/api/class_usd_geom_point_instancer.html
class MeshPart : public RasterPart {
  public:
    /// Meshes drawn by `renderer`, which must outlive the part.
    /// @throws std::runtime_error if the shadow pipeline cannot be created.
    explicit MeshPart(RasterRenderer& renderer);
    ~MeshPart() override;
    MeshPart(const MeshPart&) = delete;
    MeshPart& operator=(const MeshPart&) = delete;

    /// Adds a mesh and returns its handle.
    /// @throws std::invalid_argument if an array's size is not a multiple of 3, normals or colours
    /// do not match the points, or an index is out of range.
    std::uint32_t AddMesh(MeshData data);

    /// Replaces a mesh's points, keeping their number.
    /// @throws std::invalid_argument for an unknown mesh or a different number of points.
    void SetPoints(std::uint32_t mesh, std::span<const float> points);

    /// Replaces a mesh's points with a contiguous float32 CUDA tensor of the same number of
    /// points, copied device to device at the next `Prepare`. `bounds` are the points' bounds;
    /// without them the points are copied to the host to compute them.
    /// @throws std::invalid_argument for an unknown mesh, a different number of points, a tensor
    /// that is not float32 on the CUDA device, or without `SetCudaInterop`.
    void SetPoints(std::uint32_t mesh, const Tensor& points,
                   std::optional<std::pair<glm::vec3, glm::vec3>> bounds = std::nullopt);

    /// @throws std::invalid_argument for an unknown mesh.
    void RemoveMesh(std::uint32_t mesh);

    /// Adds an instancer and returns its handle.
    /// @throws std::invalid_argument as `SetInstances`.
    std::uint32_t AddInstancer(InstanceData data);

    /// Replaces an instancer's arrays.
    /// @throws std::invalid_argument for an unknown instancer, positions whose size is not a
    /// multiple of 3, or orientations or scales that do not match the positions.
    void SetInstances(std::uint32_t instancer, InstanceData data);

    /// @throws std::invalid_argument for an unknown instancer.
    void RemoveInstancer(std::uint32_t instancer);

    /// Replaces an instancer's arrays with CUDA tensors, copied device to device at the next
    /// `Prepare`; only the arrays of instancers that changed are copied.
    /// @throws std::invalid_argument for an unknown instancer, tensors that are not float32 on
    /// the CUDA device or do not match the positions, or without `SetCudaInterop`.
    void SetInstances(std::uint32_t instancer, const DeviceInstanceData& data);

    /// Enables CUDA inputs: geometry and instance pools move to memory shared with `cuda`, which
    /// must outlive the part. Null turns CUDA inputs off again.
    void SetCudaInterop(CudaInterop* cuda);

    /// Sets the draws of the next frames; nothing is drawn until the next `Prepare`. Draws whose
    /// material is removed later use the default material.
    /// @throws std::invalid_argument for an unknown mesh, instancer or material, or an instance
    /// index out of range.
    void SetItems(std::vector<MeshItem> items);

    /// World bounds of the current draws, or none without any.
    std::optional<Bounds3> Bounds() const override;

    /// Uploads changed geometry and the draws of the next frame.
    /// @throws std::invalid_argument if a material's colour pipeline fails to compile.
    void Prepare(const FrameParams& params, std::span<const ShadowCascade> cascades) override;

    std::size_t MeshCount() const {
        return meshes_.size();
    }

    std::size_t InstancerCount() const {
        return instancers_.size();
    }

    /// Triangles drawn per frame by the current draws, instances included.
    std::uint64_t TriangleCount() const;

    void RecordShadow(VkCommandBuffer commands, const RasterFrame& frame, std::size_t cascade,
                      const glm::mat4& lightViewProjection) const override;
    void RecordColor(VkCommandBuffer commands, const RasterFrame& frame) const override;

  private:
    struct Mesh {
        MeshData data;
        glm::vec3 low{0.0f};
        glm::vec3 high{0.0f};
        std::uint32_t firstIndex = 0;
        std::uint32_t firstVertex = 0;
        bool pointsDirty = false;
        Tensor devicePoints;  ///< CUDA points replacing `data.points` on the GPU, if any.
    };

    struct Instancer {
        InstanceData data;  ///< Host arrays; for device instancers, host copies for bounds.
        DeviceInstanceData device;
        bool onDevice = false;
        std::size_t count = 0;
        std::uint32_t first = 0;
        bool dirty = false;
    };

    const GraphicsPipeline& ColorPipeline(const BrdfModel& model);
    void RebuildGeometry();
    void RebuildInstances();
    void WriteInstancer(const Instancer& instancer);
    void CopyDevicePoints(Mesh& mesh);
    std::unique_ptr<GpuBuffer> Pool(std::span<const std::byte> bytes) const;
    void CheckDeviceTensor(const Tensor& tensor, std::size_t components, const char* what) const;
    std::vector<DescriptorResource> Resources(const RasterFrame& frame,
                                              std::uint32_t material) const;
    void Draw(VkCommandBuffer commands, std::size_t item, const glm::mat4& light) const;

    RasterRenderer& renderer_;
    const VulkanContext& context_;
    PipelineLayout layout_;
    std::unique_ptr<GraphicsPipeline> shadowPipeline_;
    std::map<std::string, std::unique_ptr<GraphicsPipeline>, std::less<>> colorPipelines_;

    std::map<std::uint32_t, Mesh> meshes_;
    std::map<std::uint32_t, Instancer> instancers_;
    std::uint32_t nextHandle_ = 1;
    bool geometryDirty_ = true;
    bool instancesDirty_ = true;
    CudaInterop* cuda_ = nullptr;
    std::vector<MeshItem> items_;
    std::vector<const GraphicsPipeline*> itemPipelines_;

    std::unique_ptr<GpuBuffer> points_;
    std::unique_ptr<GpuBuffer> normals_;
    std::unique_ptr<GpuBuffer> colors_;
    std::unique_ptr<GpuBuffer> indices_;
    std::unique_ptr<GpuBuffer> positions_;
    std::unique_ptr<GpuBuffer> orientations_;
    std::unique_ptr<GpuBuffer> scales_;
    std::unique_ptr<GpuBuffer> instanceIds_;
    std::unique_ptr<GpuBuffer> draws_;
};

}  // namespace ernie
