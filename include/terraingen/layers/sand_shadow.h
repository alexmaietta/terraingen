// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `SandShadowLayer`, sand tails in the lee of a boulder layer's boulders.
#pragma once

#include "terraingen/layers/boulders/boulder_layer.h"
#include "terraingen/layers/fade.h"
#include "terraingen/layers/layer.h"
#include "terraingen/layers/material.h"

#include <memory>
#include <vector>

namespace terraingen {

/// The sand shadows a `SandShadowLayer` lays behind boulders. Angles are in degrees.
struct SandShadowOptions {
    /// Where the wind blows toward, degrees counter-clockwise from +x (local east on a body).
    double windAzimuthDegrees = 0.0;
    /// Length of a shadow beyond the boulder over the boulder's width. 1.1 is the reattachment
    /// length behind a dome (Savory and Toy 1986, after Hesp and Smyth 2017); their modeled
    /// shadow dunes run 1.3 to 5.6 widths, longer in lighter winds.
    double lengthRatio = 1.1;
    /// Slope of a shadow's flanks, below 90 deg; at most the angle of repose, about 32 deg for dry
    /// medium sand (Hesp 1981).
    double flankAngleDegrees = 32.0;
    double minDiameter = 0.0;  ///< Smallest boulder (m) with a shadow.
    /// Sand depth (m) that fully covers the ground in the material map.
    double coverThickness = 0.02;
    ResolutionFade fade{};  ///< Fade of shadows behind boulders too small for the spacing.

    bool operator==(const SandShadowOptions& other) const {
        return windAzimuthDegrees == other.windAzimuthDegrees && lengthRatio == other.lengthRatio &&
               flankAngleDegrees == other.flankAngleDegrees && minDiameter == other.minDiameter &&
               coverThickness == other.coverThickness && fade == other.fade;
    }
};

/// Checks sand shadow options.
/// @throws std::invalid_argument for a non-finite wind azimuth, a length ratio or cover thickness
/// that is not positive and finite, a flank angle outside (0, 90) deg, a negative minimum
/// diameter, or an invalid fade.
void ValidateSandShadowOptions(const SandShadowOptions& options);

/// Sand shadows: a pyramidal sand tail in the lee of each boulder of a boulder layer, as wide as
/// the boulder and `lengthRatio` widths long (Hesp 1981); place it after the boulder layer.
class SandShadowLayer final : public Layer, public MaterialSource {
  public:
    /// @param boulders The boulder layer whose boulders cast shadows.
    /// @throws std::invalid_argument for invalid options or a null boulder layer.
    SandShadowLayer(std::shared_ptr<const BoulderLayer> boulders, SandShadowOptions options);

    /// The sand depth (m) the shadows lay at world (x, y), evaluated as in `context`: its spacing
    /// picks the boulders as the boulder layer does (0 keeps every one), and its ground is needed
    /// with the boulder layer's exposure field.
    double ThicknessAt(double x, double y, const LayerContext& context = {}) const;

    /// The options the layer was made with.
    const SandShadowOptions& Options() const {
        return options_;
    }

    /// The boulder layer the shadows trail, which must come before this layer.
    std::vector<const Layer*> Dependencies() const override {
        return {boulders_.get()};
    }

    /// A copy trailing the replacement of its boulder layer; null when it is not replaced.
    LayerPtr Rebound(const LayerReplacements& replaced) const override;

    MaterialClass Material() const override {
        return MaterialClass::Sand;
    }
    /// The sand depth over `coverThickness`, smoothed, in [0, 1].
    double MaterialCoverAt(double x, double y, const LayerContext& context) const override;
    /// `MaterialCoverAt` over `grid`, the boulders found once for the whole grid.
    Raster<double> MaterialCoverOver(const RasterGrid& grid,
                                     const LayerContext& context) const override;

  protected:
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    /// One boulder's shadow, ready to draw.
    struct Shadow {
        Vec2d center{};
        double width = 0.0;
        double cap = 0.0;
        double weight = 1.0;
        Bounds2d box{};
    };
    std::vector<Shadow> ShadowsOver(const Bounds2d& region, double spacing, const Ground* ground,
                                    const Surface& surface) const;
    double ShadowDepth(const Shadow& shadow, double x, double y, const Surface& surface) const;
    static std::vector<Bounds2d> Boxes(const std::vector<Shadow>& shadows);

    std::shared_ptr<const BoulderLayer> boulders_;
    SandShadowOptions options_;
    Vec2d downwind_{1.0, 0.0};
    double flankSlope_ = 0.0;
};

}  // namespace terraingen
