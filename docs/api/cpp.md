# C++ API

The public headers are under `terraingen/<module>/`, included as `"terraingen/<module>/<file>.h"` (or all at once with `"terraingen/terraingen.h"`), installed with TerrainGen and found with `find_package(terraingen)`. The classes most programs start from:

| Area | Classes |
| --- | --- |
| Terrains | [Terrain](../cpp/structterraingen_1_1Terrain.md), [EvaluatedTerrain](../cpp/structterraingen_1_1EvaluatedTerrain.md), [Surface](../cpp/classterraingen_1_1Surface.md), [Raster](../cpp/classterraingen_1_1Raster.md), [GeoTransform](../cpp/structterraingen_1_1GeoTransform.md) |
| Sources and layers | [Source](../cpp/classterraingen_1_1Source.md), [FlatSource](../cpp/classterraingen_1_1FlatSource.md), [GeoTiffSource](../cpp/classterraingen_1_1GeoTiffSource.md), [Layer](../cpp/classterraingen_1_1Layer.md), [NoiseRelief](../cpp/classterraingen_1_1NoiseRelief.md), [Filter](../cpp/classterraingen_1_1Filter.md), [RimSmoothingFilter](../cpp/classterraingen_1_1RimSmoothingFilter.md) |
| Craters | [CraterLayer](../cpp/classterraingen_1_1CraterLayer.md), [CraterLayerOptions](../cpp/structterraingen_1_1CraterLayerOptions.md), [CraterModels](../cpp/structterraingen_1_1CraterModels.md), [CraterList](../cpp/classterraingen_1_1CraterList.md), [SurfaceModel](../cpp/structterraingen_1_1SurfaceModel.md) |
| Extending | [SizeFrequencyDistribution](../cpp/classterraingen_1_1SizeFrequencyDistribution.md), [CraterAgeModel](../cpp/classterraingen_1_1CraterAgeModel.md), [DegradationModel](../cpp/classterraingen_1_1DegradationModel.md), [CraterProfile](../cpp/classterraingen_1_1CraterProfile.md), [AlbedoSource](../cpp/classterraingen_1_1AlbedoSource.md), [AlbedoLayer](../cpp/classterraingen_1_1AlbedoLayer.md) |
| Rocks | [RockLayer](../cpp/classterraingen_1_1RockLayer.md) |
| Configs | [TerrainConfig](../cpp/structterraingen_1_1TerrainConfig.md), [GeneratedTerrain](../cpp/structterraingen_1_1GeneratedTerrain.md) |
| Tiles | [TileScheme](../cpp/structterraingen_1_1TileScheme.md), [TileCache](../cpp/classterraingen_1_1TileCache.md) |

Every class is under [Classes](../cpp/annotated.md), by header under [Files](../cpp/files.md), and the free functions under [Functions](../cpp/namespace_member_functions.md).
