#pragma once

#include <string_view>

/// Prim type and attribute names, following UsdGeom, UsdLux, UsdShade and UsdRender where those
/// schemas have the concept, with Ernie's own under `ernie:`.
///
/// References:
/// - Pixar, OpenUSD schema domains, https://openusd.org/release/api/index.html
/// - NVIDIA, Omniverse lighting, photometric units,
///   https://docs.omniverse.nvidia.com/materials-and-rendering/latest/lighting.html
namespace ernie::tokens {

// Prim types.
inline constexpr std::string_view kXform = "Xform";
inline constexpr std::string_view kScope = "Scope";
inline constexpr std::string_view kMesh = "Mesh";
inline constexpr std::string_view kPointInstancer = "PointInstancer";
inline constexpr std::string_view kVolume = "Volume";
inline constexpr std::string_view kCamera = "Camera";
inline constexpr std::string_view kDistantLight = "DistantLight";
inline constexpr std::string_view kSphereLight = "SphereLight";
inline constexpr std::string_view kRectLight = "RectLight";
inline constexpr std::string_view kDiskLight = "DiskLight";
inline constexpr std::string_view kDomeLight = "DomeLight";
inline constexpr std::string_view kMaterial = "Material";
inline constexpr std::string_view kTerrain = "Terrain";
inline constexpr std::string_view kRenderSettings = "RenderSettings";
inline constexpr std::string_view kRenderProduct = "RenderProduct";
inline constexpr std::string_view kRenderVar = "RenderVar";

// Imageable and Xformable (UsdGeom).
inline constexpr std::string_view kTransform = "xformOp:transform";  ///< dmat4, column vectors.
inline constexpr std::string_view kVisibility = "visibility";        ///< kInherited or kInvisible.
inline constexpr std::string_view kInherited = "inherited";
inline constexpr std::string_view kInvisible = "invisible";

// Geometry (UsdGeom Mesh, PointInstancer).
inline constexpr std::string_view kPoints = "points";
inline constexpr std::string_view kNormals = "normals";
inline constexpr std::string_view kFaceVertexCounts = "faceVertexCounts";
inline constexpr std::string_view kFaceVertexIndices = "faceVertexIndices";
inline constexpr std::string_view kDisplayColor = "primvars:displayColor";
inline constexpr std::string_view kPositions = "positions";
inline constexpr std::string_view kOrientations = "orientations";
inline constexpr std::string_view kScales = "scales";
inline constexpr std::string_view kVelocities = "velocities";
inline constexpr std::string_view kProtoIndices = "protoIndices";
inline constexpr std::string_view kPrototypes = "prototypes";

// Material binding (UsdShade MaterialBindingAPI): a Path.
inline constexpr std::string_view kMaterialBinding = "material:binding";

// Camera (UsdGeom Camera). Focal length and apertures share a unit, conventionally millimetres;
// only their ratio sets the field of view.
inline constexpr std::string_view kFocalLength = "focalLength";
inline constexpr std::string_view kHorizontalAperture = "horizontalAperture";
inline constexpr std::string_view kVerticalAperture = "verticalAperture";
inline constexpr std::string_view kClippingRange = "clippingRange";  ///< vec2 near, far.
inline constexpr std::string_view kProjection = "projection";  ///< kPerspective or kOrthographic.
inline constexpr std::string_view kPerspective = "perspective";
inline constexpr std::string_view kOrthographic = "orthographic";
inline constexpr std::string_view kCameraExposure = "exposure";  ///< Stops; scales by 2^-exposure.

// Lights (UsdLux). A DistantLight shines along the -z axis of its transform.
inline constexpr std::string_view kIntensity = "inputs:intensity";
inline constexpr std::string_view kExposure = "inputs:exposure";  ///< Stops; scales by 2^exposure.
inline constexpr std::string_view kColor = "inputs:color";
inline constexpr std::string_view kAngle = "inputs:angle";  ///< Degrees subtended.
inline constexpr std::string_view kRadius = "inputs:radius";
inline constexpr std::string_view kWidth = "inputs:width";
inline constexpr std::string_view kHeight = "inputs:height";
inline constexpr std::string_view kTextureFile = "inputs:texture:file";
inline constexpr std::string_view kShadowEnable = "inputs:shadow:enable";

// Terrain (Ernie): a north-up heightfield, x east, y north, z up.
inline constexpr std::string_view kHeights = "heights";          ///< float32 tensor (rows, cols).
inline constexpr std::string_view kOrigin = "ernie:origin";      ///< dvec2 north-west corner.
inline constexpr std::string_view kCellSize = "ernie:cellSize";  ///< double, world units.
inline constexpr std::string_view kNoData = "ernie:noData";      ///< float marking missing cells.
inline constexpr std::string_view kExaggeration = "ernie:exaggeration";  ///< float.

// Material (Ernie surface). BRDF parameters are `inputs:<parameter id>`.
inline constexpr std::string_view kBrdf = "ernie:brdf";             ///< BRDF model id.
inline constexpr std::string_view kAlbedo = "inputs:albedo";        ///< vec3 linear albedo.
inline constexpr std::string_view kAlbedoMap = "inputs:albedoMap";  ///< float32 (rows, cols).
inline constexpr std::string_view kAlbedoMapOrigin = "inputs:albedoMap:origin";
inline constexpr std::string_view kAlbedoMapCellSize = "inputs:albedoMap:cellSize";
inline constexpr std::string_view kAlbedoMapNoData = "inputs:albedoMap:noData";
inline constexpr std::string_view kAlbedoMapEnable = "inputs:albedoMap:enable";  ///< bool.
/// Detail textures: float32 (rows, cols, channels) linear, or uint8 with sRGB albedo and linear
/// normals; 1, 3 or 4 channels.
inline constexpr std::string_view kDetailAlbedo = "inputs:detailAlbedo";
inline constexpr std::string_view kDetailNormal = "inputs:detailNormal";
inline constexpr std::string_view kDetailTileSize = "inputs:detail:tileSize";
inline constexpr std::string_view kDetailAlbedoStrength = "inputs:detail:albedoStrength";
inline constexpr std::string_view kDetailNormalStrength = "inputs:detail:normalStrength";
inline constexpr std::string_view kDetailDirectXNormals = "inputs:detail:directXNormals";
inline constexpr std::string_view kDetailHexTiling = "inputs:detail:hexTiling";
inline constexpr std::string_view kDetailHexRotation = "inputs:detail:hexRotation";
inline constexpr std::string_view kDetailHexContrast = "inputs:detail:hexContrast";
inline constexpr std::string_view kDetailHexSharpness = "inputs:detail:hexSharpness";

// Render settings (UsdRender), with Ernie's rasterizer options under `ernie:`.
inline constexpr std::string_view kCameraRel = "camera";       ///< Path of the camera.
inline constexpr std::string_view kResolution = "resolution";  ///< ivec2 pixels.
inline constexpr std::string_view kSourceName = "sourceName";  ///< RenderVar name, e.g. "LdrColor".
inline constexpr std::string_view kAmbient = "ernie:ambient";  ///< float fraction of sunlight.
inline constexpr std::string_view kColorMode = "ernie:colorMode";
inline constexpr std::string_view kHillshade = "hillshade";
inline constexpr std::string_view kColormap = "colormap";
inline constexpr std::string_view kShadedColormap = "shadedColormap";
inline constexpr std::string_view kWireframe = "ernie:wireframe";                  ///< bool.
inline constexpr std::string_view kClearColor = "ernie:clearColor";                ///< vec3 linear.
inline constexpr std::string_view kShadowCascades = "ernie:shadows:cascades";      ///< int.
inline constexpr std::string_view kShadowResolution = "ernie:shadows:resolution";  ///< int.
inline constexpr std::string_view kShadowSoftness = "ernie:shadows:softness";
inline constexpr std::string_view kShadowNormalOffset = "ernie:shadows:normalOffset";
inline constexpr std::string_view kShadowSplitLambda = "ernie:shadows:splitLambda";
inline constexpr std::string_view kShadowShowCascades = "ernie:shadows:showCascades";
inline constexpr std::string_view kLodEnable = "ernie:lod:enable";
inline constexpr std::string_view kLodDetail = "ernie:lod:detail";
inline constexpr std::string_view kLodMorphStart = "ernie:lod:morphStart";
inline constexpr std::string_view kLodFreeze = "ernie:lod:freeze";
inline constexpr std::string_view kLodShowLevels = "ernie:lod:showLevels";

/// RenderVar source names.
inline constexpr std::string_view kLdrColor = "LdrColor";

}  // namespace ernie::tokens
