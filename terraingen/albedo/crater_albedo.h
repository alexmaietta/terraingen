/// @file
/// @brief Bright, immature ejecta, interiors and rays around fresh craters.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/albedo/crater_albedo_options.h"
#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/rays.h"
#include "terraingen/random/stream.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace terraingen {

/// The bright ejecta, interiors and rays of the craters of a `CraterLayer`, as
/// `CraterAlbedoOptions` describes, shaped by the craters' irregular outlines where the layer has
/// them. The craters come from the layer for the region being evaluated, widened by the longest
/// ray, so every grid, tile and point agrees; excesses are summed in crater ID order.
class CraterAlbedo final : public AlbedoLayer {
  public:
    /// The bright material of the craters of `craters`.
    /// @throws std::invalid_argument for a null layer or options invalid as
    /// `ValidateCraterAlbedoOptions`.
    CraterAlbedo(std::shared_ptr<const CraterLayer> craters, CraterAlbedoOptions options = {});

    /// Brightens the albedo at one point by the craters' summed excess; NaN stays NaN.
    void ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                 const AlbedoContext& context) const override;

    /// `ApplyAt` at every cell center, gathering craters once per block of rows.
    void Apply(std::vector<Raster<float>>& bands, const AlbedoContext& context) const override;

    /// The excess optical maturity (OMAT) `crater` gives surface point (`x`, `y`) at a sample
    /// spacing of `spacing` meters.
    double Excess(const Crater& crater, double x, double y, double spacing) const;

    /// The relative reflectance change the zones of `crater` give surface point (`x`, `y`) at a
    /// sample spacing of `spacing` meters; 0 without zones. Shadows need `ground`.
    double ReflectanceChange(const Crater& crater, double x, double y, double spacing,
                             const Ground* ground = nullptr) const;

    /// The impact `crater` came from, drawn from its ID; vertical without oblique impacts.
    CraterImpact Impact(const Crater& crater) const;

    /// How far from its center `crater` can brighten the ground: the farthest of its ejecta and
    /// its rays, in meters; 0 without bright material.
    double Reach(const Crater& crater) const;

    /// How bright, and for how long.
    const CraterAlbedoOptions& Options() const {
        return options_;
    }

  private:
    struct Ray;
    struct Prepared;
    Prepared Prepare(const Crater& crater, double spacing) const;
    /// Whether `crater` is selected and drawn to have bright material, from the next uniform of
    /// `random`.
    bool Eligible(const Crater& crater, RandomStream& random) const;
    /// The next ray of `random`, for a crater of median ray length `median` formed by `impact`.
    Ray DrawRay(RandomStream& random, double median, const CraterImpact& impact,
                const RayDirection* direction) const;
    /// Draws the rays of `prepared`, widening its reach to the longest.
    void AddRays(Prepared& prepared, RandomStream& random) const;
    /// Draws the reflectance zones of `prepared`, widening its reach to the farthest, for a crater
    /// with `left` of its bright material's life and a resolution weight of `resolution`.
    void AddZones(Prepared& prepared, double left, double resolution) const;
    /// Samples the ground around `prepared` for the shadows of its distal zones over `region`.
    void AddShadows(Prepared& prepared, const Ground& ground, const Bounds2d& region) const;
    /// How far beyond a region craters are gathered for their ejecta and rays, in meters.
    double GatherMargin() const;
    /// Appends the craters of `list` with bright material resolved at `spacing` that reach
    /// `region` to `prepared`, prepared.
    void TakePrepared(const CraterList& list, const Bounds2d& region, double spacing,
                      std::vector<Prepared>& prepared) const;
    /// The craters with bright material reaching `region` at `spacing` meters, in ID order, with
    /// their zone shadows on `ground` when it is set.
    std::vector<Prepared> Gather(const Bounds2d& region, double spacing,
                                 const Ground* ground) const;

    std::shared_ptr<const CraterLayer> craters_;
    CraterAlbedoOptions options_;
};

}  // namespace terraingen
