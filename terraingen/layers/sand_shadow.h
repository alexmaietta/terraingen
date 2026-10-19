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

/// The sand shadows a `SandShadowLayer` lays behind boulders. Angles are in radians.
struct SandShadowOptions {
    /// Where the wind blows toward, radians counter-clockwise from +x (local east on a body).
    double windAzimuth = 0.0;
    /// Length of a shadow beyond the boulder over the boulder's width. 1.1 is the reattachment
    /// length behind a dome (Savory and Toy 1986, after Hesp and Smyth 2017); their modelled
    /// shadow dunes run 1.3 to 5.6 widths, longer in lighter winds.
    double lengthRatio = 1.1;
    /// Slope of a shadow's flanks, below 90 deg; at most the angle of repose, about 32 deg for dry
    /// medium sand (Hesp 1981).
    double flankAngle = 0.5585053606381855;
    double minDiameter = 0.0;  ///< Smallest boulder (m) with a shadow.
    /// Sand depth (m) that fully covers the ground in the material map.
    double coverThickness = 0.02;
    ResolutionFade fade{};  ///< Fade of shadows behind boulders too small for the spacing.

    bool operator==(const SandShadowOptions& other) const {
        return windAzimuth == other.windAzimuth && lengthRatio == other.lengthRatio &&
               flankAngle == other.flankAngle && minDiameter == other.minDiameter &&
               coverThickness == other.coverThickness && fade == other.fade;
    }
    bool operator!=(const SandShadowOptions& other) const {
        return !(*this == other);
    }
};

/// Checks sand shadow options.
/// @throws std::invalid_argument for a non-finite wind azimuth, a length ratio or cover thickness
/// that is not positive and finite, a flank angle outside (0, 90) deg, a negative minimum
/// diameter, or an invalid fade.
void ValidateSandShadowOptions(const SandShadowOptions& options);

/// Sand shadows: a sand tail in the lee of each boulder of a boulder layer, after Hesp (1981).
///
/// A tail is a pyramid starting at the boulder's downwind edge as wide as the boulder (its
/// diameter), its flanks at `flankAngle`, so its crest is (width / 2) tan(flankAngle) high; where
/// that is higher than the boulder stands, the crest is lowered to the boulder's top and the flanks
/// eased. It narrows and lowers to a point `lengthRatio` widths downwind. Overlapping tails take
/// the deepest sand. The tails are added to the height beneath, so place the layer after the
/// boulder layer; it lays sand over the material map.
///
/// The boulders are those the boulder layer places, read with this layer's ground; with the
/// boulder layer's bedrock exposure field that ground includes the boulders' relief.
class SandShadowLayer final : public Layer, public MaterialSource {
  public:
    /// @param boulders The boulder layer whose boulders cast shadows; on `surface`.
    /// @throws std::invalid_argument for invalid options or a null boulder layer.
    SandShadowLayer(std::shared_ptr<const BoulderLayer> boulders, SandShadowOptions options,
                    Surface surface = {});

    /// The sand depth (m) the shadows lay at world (x, y).
    /// @param spacing Sample spacing in metres, which picks the boulders as the boulder layer does.
    /// @param ground Needed with the boulder layer's exposure field.
    double ThicknessAt(double x, double y, double spacing = 0.0,
                       const Ground* ground = nullptr) const;

    /// The options the layer was made with.
    const SandShadowOptions& Options() const {
        return options_;
    }

    MaterialClass Material() const override {
        return MaterialClass::Sand;
    }
    /// The sand depth over `coverThickness`, smoothed, in [0, 1].
    double MaterialCoverAt(double x, double y, double spacing, const Ground* ground) const override;
    /// `MaterialCoverAt` over `grid`, the boulders found once for the whole grid.
    Raster<double> MaterialCoverOver(const RasterGrid& grid, double spacing, const Ground* ground,
                                     unsigned threads) const override;

  protected:
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    /// One boulder's shadow, ready to draw.
    struct Shadow {
        Vec2d centre{};
        double width = 0.0;
        double cap = 0.0;
        double weight = 1.0;
        Bounds2d box{};
    };
    std::vector<Shadow> ShadowsOver(const Bounds2d& region, double spacing,
                                    const Ground* ground) const;
    double ShadowDepth(const Shadow& shadow, double x, double y) const;

    std::shared_ptr<const BoulderLayer> boulders_;
    SandShadowOptions options_;
    Surface surface_;
    Vec2d downwind_{1.0, 0.0};
    double flankSlope_ = 0.0;
};

}  // namespace terraingen
