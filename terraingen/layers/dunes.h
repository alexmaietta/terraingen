// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `DuneLayer`, dune fields of barchans, domes or crest-line dunes, with ripples on them.
#pragma once

#include "terraingen/layers/anchored_plane.h"
#include "terraingen/layers/fade.h"
#include "terraingen/layers/layer.h"
#include "terraingen/layers/material.h"
#include "terraingen/random/stream.h"
#include "terraingen/sources/field.h"
#include "terraingen/sources/source.h"
#include "terraingen/util/geometry.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace terraingen {

/// The kind of dune a `DuneLayer` builds.
enum class DuneShape {
    Barchan,     ///< Isolated crescents with a slip face, horns downwind; domes below `minWidth`.
    Dome,        ///< Isolated rounded heaps without a slip face.
    Transverse,  ///< Continuous crests across the wind, each with a slip face.
    Barchanoid,  ///< Transverse crests bent into linked crescents.
    Linear,      ///< Symmetric crests along the wind, as under a bimodal wind on scarce sand.
};

/// Ripples superposed on dunes, their crests bent by the dune's slope: each rises along a straight
/// stoss to a sharp crest and falls along a straight lee.
struct DuneRipples {
    double wavelength = 0.0;  ///< Crest to crest on flat sand (m); 0 for no ripples.
    double height = 0.0;      ///< Trough to crest on flat sand (m).
    /// Share of a wavelength the lee face takes, in (0, 1): 0.5 is symmetric, less is steeper.
    double leeFraction = 0.5;
    double sinuosity = 0.3;  ///< How far a crest wanders along its length, in wavelengths.
    /// How strongly the dune's slope turns and tightens the ripples: their wave vector is the wind
    /// minus this times the dune's gradient, so they shorten and lower on steep lee faces and turn
    /// on flanks; they never grow above `height`.
    double slopeCoupling = 1.0;
    double interduneCover = 0.0;  ///< Share of the ripples' height kept off the dunes, in [0, 1].
    /// Defect density: pairs of crest ends (forked crests, Y-junctions) per meter of crest, as
    /// Ewing et al. (2010) measure it; 0 for none.
    double defectDensity = 0.0;
    /// How far crests straighten off the stoss, in [0, 1]: 0 keeps `sinuosity` everywhere, 1
    /// keeps it on flat sand and stoss slopes and takes it away on flanks and lee slopes as steep
    /// as the stoss.
    double flankStraightening = 0.0;
    ResolutionFade fade{};  ///< How ripples too fine for the spacing fade out.

    bool operator==(const DuneRipples& other) const {
        return wavelength == other.wavelength && height == other.height &&
               leeFraction == other.leeFraction && sinuosity == other.sinuosity &&
               slopeCoupling == other.slopeCoupling && interduneCover == other.interduneCover &&
               defectDensity == other.defectDensity &&
               flankStraightening == other.flankStraightening && fade == other.fade;
    }
};

/// Grainflows on slip faces: avalanches that leave a theater-headed scarp below the brink and a
/// tongue-shaped lobe of sand below it. Each is drawn on a lattice aligned with the wind, its sizes
/// uniform between the bounds, and shown only where the ground faces downwind near the slip face
/// angle. Lengths are in meters, along the slope; widths across it.
struct DuneGrainflows {
    double cover = 0.0;  ///< Chance, in [0, 1], that each lattice cell holds one; 0 for none.
    double minScarpWidth = 0.5;
    double maxScarpWidth = 0.5;
    double minScarpLength = 1.0;
    double maxScarpLength = 1.0;
    double minLobeWidth = 0.5;
    double maxLobeWidth = 0.5;
    double minLobeLength = 3.0;
    double maxLobeLength = 3.0;
    double thickness = 0.015;  ///< Depth of a scarp and height of a lobe (m).
    ResolutionFade fade{};     ///< How grainflows too narrow for the spacing fade out.

    bool operator==(const DuneGrainflows& other) const {
        return cover == other.cover && minScarpWidth == other.minScarpWidth &&
               maxScarpWidth == other.maxScarpWidth && minScarpLength == other.minScarpLength &&
               maxScarpLength == other.maxScarpLength && minLobeWidth == other.minLobeWidth &&
               maxLobeWidth == other.maxLobeWidth && minLobeLength == other.minLobeLength &&
               maxLobeLength == other.maxLobeLength && thickness == other.thickness &&
               fade == other.fade;
    }
};

/// The dunes a `DuneLayer` builds, their sizes and slopes. Lengths are in meters, angles in
/// radians.
struct DuneOptions {
    DuneShape shape = DuneShape::Barchan;
    /// Where the wind blows toward, radians counter-clockwise from +x; slip faces face it.
    double windAzimuth = 0.0;
    double azimuthSpread = 0.0;  ///< Each barchan or dome's wind differs by up to this.
    double slipFaceAngle = 0.5585053606381855;  ///< Lee slip face slope; 32 deg.
    double stossSlope = 0.17453292519943295;    ///< Mean windward slope; 10 deg.
    double stossExponent = 3.0;  ///< Exponent of the cos^alpha windward profile of a dune.
    double domeExponent = 1.8;   ///< Exponent of the cos^alpha profile of a dome.

    /// Barchans and domes: one candidate per lattice cell of `spacing`. By default barchans 50 m
    /// wide in a lattice of 129 m cells half of which hold a dune (Elbelrhiti et al. 2008,
    /// https://arxiv.org/abs/cond-mat/0609120).
    double spacing = 129.0;
    double density = 0.5;      ///< Chance a cell holds a dune, in [0, 1].
    double width = 50.0;       ///< Mean width, horn to horn (m).
    double widthSpread = 0.3;  ///< Each width varies from (1 - it) to (1 + it) times `width`.
    double minWidth = 15.0;    ///< Width below which a barchan is a dome without a slip face.
    double heightRatio = 1.0 / 15.0;  ///< Height over width.
    double hornLength = 9.1;  ///< How far the horns reach downwind of the crest, in heights.
    double domeLength = 2.0;  ///< Length of a dome over its width.

    /// Transverse, barchanoid and linear dunes: crest-to-crest spacing (m).
    double wavelength = 550.0;
    double height = 20.0;    ///< Crest height (m).
    double sinuosity = 0.3;  ///< How far a crest wanders along its length, in wavelengths.
    /// Share of the height that varies along crests, ending some, in [0, 1].
    double defects = 0.3;
    /// Crest-line dunes: pairs of crest ends per meter of crest, as Ewing et al. (2010) measure it;
    /// crests break into segments that stand above half their height for 1 / `defectDensity`
    /// meters on average, falling to nothing over a wavelength either side of each break. 0 for
    /// none.
    double defectDensity = 0.0;
    double flankSlope = 0.3490658503988659;  ///< Mean flank slope of linear dunes; 20 deg.

    std::uint32_t seed = 1;  ///< Varies the dunes and ripples.
    /// On a body, the surface point whose tangent frame the dunes are laid out in; true to scale
    /// near it, and no dunes on the far hemisphere.
    Vec2d anchor{};
    ResolutionFade
        fade{};  ///< How dunes too fine for the spacing fade out, by samples across them.
    DuneRipples ripples{};
    DuneGrainflows grainflows{};  ///< Avalanches on the slip faces; none by default.
    /// Source cells across from which the source's measured data already holds a dune: with a
    /// source given to the layer, the dunes are left out where their size (`width` of barchans
    /// and domes, `wavelength` of crests) is at least that many of its cells, as are ripples by
    /// their wavelength and grainflows by their narrowest lobe (`SourceResolvedLimit`). Unset, or
    /// without a source, adds them everywhere.
    std::optional<double> sourceResolvesPixels = 4.0;

    bool operator==(const DuneOptions& other) const {
        return shape == other.shape && windAzimuth == other.windAzimuth &&
               azimuthSpread == other.azimuthSpread && slipFaceAngle == other.slipFaceAngle &&
               stossSlope == other.stossSlope && stossExponent == other.stossExponent &&
               domeExponent == other.domeExponent && spacing == other.spacing &&
               density == other.density && width == other.width &&
               widthSpread == other.widthSpread && minWidth == other.minWidth &&
               heightRatio == other.heightRatio && hornLength == other.hornLength &&
               domeLength == other.domeLength && wavelength == other.wavelength &&
               height == other.height && sinuosity == other.sinuosity && defects == other.defects &&
               defectDensity == other.defectDensity && flankSlope == other.flankSlope &&
               seed == other.seed && anchor == other.anchor && fade == other.fade &&
               ripples == other.ripples && grainflows == other.grainflows &&
               sourceResolvesPixels == other.sourceResolvesPixels;
    }
};

/// Checks dune options.
/// @throws std::invalid_argument for non-positive sizes or slopes, a slip face or flank of 90 deg
/// or more, a stoss slope not below the slip face, a density, defects or interdune cover outside
/// [0, 1], a width spread outside [0, 1), negative spreads or sinuosity, or invalid ripples or
/// fade.
void ValidateDuneOptions(const DuneOptions& options);

/// Martian large wind ripples to lay on dunes: 2.1 m wavelength, 0.2 m height and a 30 deg lee;
/// 0.099 pairs of crest ends per meter, as Olympia Undae's 2.1 m wind ripples (Ewing et al. 2010);
/// sinuous on stoss slopes and straighter on flanks and lee slopes (Lapotre et al. 2016; Ewing et
/// al. 2017).
DuneRipples MarsDuneRipples();

/// The grainflows of Namib Dune's lee slope (Ewing et al. 2017, Table 1): scarps 0.54 to 0.67 m
/// wide and 1.0 to 1.6 m long, lobes 0.57 to 0.66 m wide and 2.9 to 6 m long. Their thickness is
/// not measured on Mars; 0.015 m is that of a Namibian barchan's grainflows (Nield et al. 2017, in
/// Ewing et al. 2017). Every cell holds one, as palimpsest grainflows are abundant on that slope.
DuneGrainflows MarsDuneGrainflows();

/// Martian barchans with large ripples: 29 deg slip faces, 8 deg stoss slopes, 370 m wide, domes
/// below 200 m wide and twice as long as wide; spacing and density are placeholders to set per
/// site.
DuneOptions MarsBarchans();

/// The transverse dunes of Mars's north polar erg with large ripples: 248 m apart and 28 m high,
/// with 29 deg slip faces and 8 deg stoss slopes, and the defect density of Olympia Undae's primary
/// crests, 0.0027 pairs of ends per meter (Ewing et al. 2010).
DuneOptions MarsTransverseDunes();

/// Wind-blown dunes added to the terrain.
///
/// Barchans are crescents: cross-wind slices of height H(1 - (y/Y)^2), a cos^alpha windward
/// profile up to a brink bent downwind toward the horns, and a slip face falling away from the
/// brink at `slipFaceAngle`; overlapping dunes join as their upper envelope. Crest-line dunes
/// follow a warped crest phase whose lee width keeps the slope at the slip face angle, to within a
/// few degrees where crests bend. A function
/// of position only, so grids, tiles and points agree.
class DuneLayer final : public FormulaLayer, public MaterialSource {
  public:
    /// On a body, dunes are laid out in the tangent frame of `options.anchor`, with the azimuth
    /// counter-clockwise from local east.
    /// @param source The source the terrain's heights come from, which with
    /// `options.sourceResolvesPixels` holds the dunes it resolves; null for none.
    /// @throws std::invalid_argument as `ValidateDuneOptions`.
    explicit DuneLayer(DuneOptions options, Surface surface = {},
                       std::shared_ptr<const Source> source = nullptr);

    /// The dunes' height at world (x, y), without ripples (m).
    double DuneHeightAt(double x, double y) const;

    /// The options the layer was made with.
    const DuneOptions& Options() const {
        return options_;
    }

    MaterialClass Material() const override {
        return MaterialClass::Sand;
    }
    /// How fully dune sand covers (x, y): the dunes, and with ripples their `interduneCover`.
    double MaterialCoverAt(double x, double y, double spacing, const Ground* ground) const override;

  protected:
    float HeightAt(float height, double x, double y, double spacing,
                   const Ground* ground) const override;

  private:
    double Height(const Vec2d& q) const;
    double Barchans(const Vec2d& q) const;
    /// The height at planar point `q` of the barchan or dome of lattice cell (`gx`, `gy`), drawn
    /// from `random` after its chance.
    double BarchanCell(RandomStream& random, std::int64_t gx, std::int64_t gy,
                       const Vec2d& q) const;
    double Crests(const Vec2d& q) const;
    /// How far planar point `p` lies from a barchanoid crest's links, in [0, 1].
    double CrestSegment(const Vec2d& p) const;
    /// The warped crest phase at planar point `p`, in wavelengths.
    double CrestPhase(const Vec2d& p) const;
    /// The share of the crest height standing at planar point `q` on crest phase `psi`.
    double CrestAmplitude(const Vec2d& q, double psi) const;
    /// The crest profile at phase `u` in [0, 1) for a crest `height` meters high whose phase is
    /// stretched by `stretch`.
    double CrestProfile(double u, double height, double stretch) const;
    double Ripples(const Vec2d& q, double dune) const;
    /// How fully a dune `dune` meters high covers the ground with sand, in [0, 1].
    double SandDepth(double dune) const;
    /// How much of crest `crest` stands at `along` meters along it, in [0, 1]: 0 at its breaks.
    double CrestBreaks(std::int64_t crest, double along) const;
    /// The phase that forked crests `wavelength` meters apart, `density` pairs of ends per meter of
    /// crest, add near planar point `q`, in wavelengths; `core` becomes the weight in [0, 1] that
    /// fades the crests out at the forks' ends.
    double Forks(const Vec2d& q, double wavelength, double density, double& core) const;
    /// Adds to `phase` and `core` the fork centered at `center`, its shape drawn from `random`.
    static void AddFork(double& phase, double& core, RandomStream& random, const Vec2d& q,
                        const Vec2d& center, double wavelength);
    /// The dunes' gradient at planar point `q`, by central differences `step` meters apart.
    Vec2d Gradient(const Vec2d& q, double step) const;
    /// How far the ground with gradient `g` is a slip face, in [0, 1]: facing downwind near the
    /// slip face angle.
    double SlipFaceWeight(const Vec2d& g) const;
    /// The grainflows' relief at planar point `q` (m), on the slip face share `face`.
    double Grainflows(const Vec2d& q, double face) const;
    /// Adds to `relief` the scarp and lobe of one grainflow at `s` meters downslope of its head
    /// and `n` across, its sizes drawn from `random`.
    void AddGrainflow(double& relief, RandomStream& random, double s, double n) const;

    DuneOptions options_;
    std::shared_ptr<const SurfaceField> limit_;  ///< `SourceResolvedLimit`; null for none.
    AnchoredPlane plane_;
    Vec2d wind_{1.0, 0.0};
    std::int64_t rings_ = 1;
    std::uint32_t defectSeed_ = 0;  ///< Seed of the noise of defects along crests.
    std::uint32_t wanderSeed_ = 0;  ///< Seed of the ripple crest wander.
    std::uint32_t kinkSeed_ = 0;    ///< Seed of the ripple crest kinks.
    std::uint32_t fadeSeed_ = 0;    ///< Seed of the ripple fading along crests.
};

}  // namespace terraingen
