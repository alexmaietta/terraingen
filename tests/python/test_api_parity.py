"""Every public class, struct, enum and function of the C++ headers is bound in Python, apart from
the internals listed in INTERNAL with the reason they are left out."""

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
    "HeightmapOutput", "CraterListOutput", "RockListOutput", "BoulderModelsOutput", "AlbedoOutput",
    "BodyMeshOutput", "BuildCraterModels", "BuildSurface", "DescribeSource", "TileKeyHash", "Scheme",
    # The GPU's fixed-size layer block and device internals; Python selects backends.
    "NoiseLayers", "ComputeKernel", "VulkanCompute", "DeviceName", "Reason", "Run",
    # Internal steps of crater, rock and albedo evaluation, reached through their layers.
    "Entries", "ReliefBefore", "DatumGround", "DatumMargin", "CraterInfluence", "CraterProfileDistance",
    "ApplyCraterTo", "GenerateCellCraters", "SpacingConflicts", "Solve", "TableFor", "Morphometry",
    "AddField", "EjectaCraters", "Abundance", "AddRelief", "PlacedOnLevel", "Roughness", "Smoothing",
    "ApplyParallel", "Gather", "Prepare", "ForEachAlbedoCell", "MaxStretch", "NoiseAt", "SegmentAt",
    "PieceAt", "IntervalAt", "LowestAt", "Derivative", "Polynomial", "BoulderMesh", "SceneMesh",
    "IsModel", "MinCraterDiameter",
    # Vector arithmetic and geometry helpers; Python uses NumPy.
    "Cross", "Dot", "Length", "Normalized", "Box3d", "Extents", "ShadedMesh", "After", "Expanded",
    "SphereLocate", "SphereOffset", "UnitVector", "Bucket", "GetKind",
    # Python's perlin_noise3 is the double-precision form (PerlinNoise3Precise), and
    # SecondaryCraterLayer takes the profile and degradation of SecondaryCraterModels as arguments.
    "PerlinNoise3", "SecondaryCraterModels",
    # Small accessors and checks reached through properties or constructors.
    "Empty", "Row", "Contains", "CheckBounds", "CheckDiameter", "PositiveFinite", "Reserve", "Enabled",
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
            for name in sorted(public_names(header) - INTERNAL):
                if not re.search(r"\b" + name + r"\b", bound):
                    missing.append(f"{header.relative_to(ROOT)}: {name}")
    assert not missing, "C++ names without Python bindings:\n" + "\n".join(missing)


def test_the_python_api_page_documents_every_exported_name():
    import terraingen

    page = (ROOT / "docs/api/python.md").read_text()
    documented = set(re.findall(r"^\.\. auto\w+:: terraingen\.(\w+)$", page, re.M))
    missing = sorted(set(terraingen.__all__) - documented)
    assert not missing, "Exported but not on docs/api/python.md: " + ", ".join(missing)
