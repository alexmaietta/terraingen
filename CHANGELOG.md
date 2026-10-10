# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- Rocks are now boulders everywhere, with no aliases for the old names: `Rock` is `Boulder`, `RockLayer` and `RockLayerOptions` are `BoulderLayer` and `BoulderLayerOptions` (and the other `Rock*` types and `rock_*` functions likewise), the `"rocks"` layer and `"rock_list"` output of configurations are `"boulders"` and `"boulder_list"`, and the headers are under `terraingen/layers/boulders/`. Boulders placed as 3D models are "boulder models": `TerrainFeatures::boulderModels`, `PlaceBoulderModel`, `boulder_model_count`. `RockModels` is `BoulderModelThreshold` and `RockModelCurve` is `SpecificationCurve`.
