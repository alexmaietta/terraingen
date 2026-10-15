"""Every public class, struct, enum and function of the C++ headers is bound in Python, apart from
the internals listed in INTERNAL with the reason they are left out and the private headers in
detail/ folders."""

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
MODULES = ["util", "raster", "noise", "random", "meshes", "distribution", "sources", "layers", "filters", "albedo", "bodies", "pipeline", "gpu"]

INTERNAL = {
    # Threading, caching and logging plumbing used inside the library.
    "ParallelFor", "ResolveThreads", "LruCache", "Sum", "Capacity", "Stopwatch", "Milliseconds",
    "FormatDuration", "LogEnabled", "StandardErrorLogSink", "CraterCellCache", "MaxCraters", "Cache",
    # TIFF codecs and header details behind read_geotiff / write_geotiff.
    "DecodeDeflate", "DecodeLzw", "DecodePackBits", "EncodeDeflate", "EncodeLzw", "ParseGdalBandScaling",
    "GeoTiff", "ShortKey", "Identity", "Load", "Native", "Scaling", "Keys",
    # Config spec structs: Python builds configs as dicts (Config.from_dict) and library objects.
    "Boxed", "Effective", "CraterBody", "DiffusivityModel", "EquilibriumModel", "NoiseLayerConfig",
    "HeightmapOutput", "CraterListOutput", "BoulderListOutput", "BoulderModelsOutput", "AlbedoOutput",
    "BodyMeshOutput", "ListOutput", "GeoTiffOutput", "BuildCraterModels", "BuildSurface",
    "DescribeSource", "TileKeyHash", "Scheme",
    # What the CLI's validate command prints; Python reads Config.grid() and validate() raises.
    "ConfigSummary",
    # The GPU's fixed-size layer block and device internals; Python selects backends.
    "NoiseLayers", "ComputeKernel", "VulkanCompute", "DeviceName", "Reason", "Run",
    # Internal steps of crater, boulder and albedo evaluation, reached through their layers.
    "Craters", "ReliefBefore", "AddBucket", "DatumGround", "DatumMargin", "CraterReliefIndex",
    "PlaneFit", "Mean", "PreparedCrater", "PrepareCraters", "CraterSampling", "CraterProfileDistance",
    "ApplyCraterTo", "GenerateCellCraters", "CachedCellCraters", "FilterCraters", "ResolveDMax",
    "SpacingConflicts", "Solve", "TableFor", "Morphometry", "AddField", "EjectaCraters", "Abundance",
    "BackgroundAbundance", "EjectaAbundance", "ScatterBoulderCandidates", "DrawBoulder", "DrawRelief",
    "InLocalFrame", "AddRelief", "PlacedOnLevel", "Roughness", "MakeOctave", "OctaveNoise",
    "KeepFeaturesInside", "Smoothing", "ApplyParallel", "Gather", "Prepare", "Eligible", "DrawRay", "AddRays",
    "ForEachAlbedoCell", "ForEachAlbedoBlock", "MeasureRoughness", "DetailSum", "LogBrightnessGrid",
    "StructureFunction", "FitSlope", "MaxStretch", "NoiseAt", "SegmentAt", "PieceAt", "IntervalAt",
    "LowestAt", "Derivative", "Polynomial", "BoulderMesh", "SceneMesh", "IsModel",
    "MinCraterDiameter", "AppendManualCraters", "AddCraters", "ValidatePointContext", "RequireGround",
    # Noise, random and raster-loop helpers shared by the layers; Python works on whole rasters.
    "PatchNoise", "NoiseLayersAt", "PerlinNoise2Precise", "IdWords", "SplitId", "StandardNormal",
    "Fft", "PixelWindow", "WindowOf", "WindowsAround", "FeatureCell", "ForEachFeatureCell",
    "MapCells", "ResolvedSpacing",
    # The groups of BoulderLayerOptions and their checks; Python keeps the flat config attributes
    # and checks them through validate_boulder_layer_options.
    "ValidateBoulderSizes", "ValidateBoulderPatches", "ValidateBoulderEjecta",
    "ValidateBoulderBedrock", "ValidateBoulderPose", "ValidateBoulderRendering",
    "MaxBackgroundAbundance", "EjectaSizes",
    # Argument checks and the JSON paths of configuration errors; Python sees the exceptions.
    "IsPositive", "IsNonNegative", "IsInUnitInterval", "RequirePositive", "RequireNonNegative",
    "RequireFinite", "RequireUnitInterval", "RequireNonEmpty", "At", "Child", "Element",
    # Vector arithmetic and geometry helpers; Python uses NumPy.
    "Cross", "Dot", "Length", "Normalized", "Box3d", "Extents", "ShadedMesh", "After", "Expanded",
    "SphereLocate", "SphereOffset", "UnitVector", "Bucket", "GetKind", "PointBounds",
    # Python's perlin_noise3 is the double-precision form (PerlinNoise3Precise).
    "PerlinNoise3",
    # Small accessors and checks reached through properties or constructors.
    "Empty", "Row", "Contains", "CheckBounds", "CheckDiameter", "Reserve", "Enabled",
    "Lowest", "Highest", "ExposedHeight", "Sampler", "Ground", "Production", "Inner", "GetSurface",
    "Params", "Sources", "Scale", "IsNoData", "PixelIsPoint", "IsGeographic", "Area", "PixelSize",
    "SurfaceArea", "WrapBodyTile", "ExponentialIntegral", "ValidateSyntheticAlbedo", "ValidateConfig",
}


def public_names(header):
    text = re.sub(r"namespace detail \{.*?\n\}  // namespace detail", "", header.read_text(), flags=re.S)
    names = {m.group(1) for m in re.finditer(r"^(?:class|struct|enum class)\s+([A-Z]\w+)", text, re.M)}
    names |= {
        m.group(1) for m in re.finditer(r"^(?:inline |constexpr |static )*[\w:<>,\s\*&]+?\s+([A-Z]\w+)\(", text, re.M)
    }
    return {n for n in names if not n.endswith("Spec")}


def test_every_public_cpp_name_is_bound_or_listed_as_internal():
    bound = "\n".join(p.read_text() for p in (ROOT / "terraingen/python/src").glob("*.[ch]*"))
    missing = []
    for module in MODULES:
        for header in sorted((ROOT / "terraingen" / module).rglob("*.h")):
            if "detail" in header.parts:  # A module's private headers, not installed.
                continue
            for name in sorted(public_names(header) - INTERNAL):
                if not re.search(r"\b" + name + r"\b", bound):
                    missing.append(f"{header.relative_to(ROOT)}: {name}")
    assert not missing, "C++ names without Python bindings:\n" + "\n".join(missing)
