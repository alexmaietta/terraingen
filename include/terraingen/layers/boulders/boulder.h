// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `Boulder`, the record of one placed boulder, `BoulderList` of many, and `ManualBoulder`,
/// one placed by hand.
#pragma once

#include "terraingen/util/geometry.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <vector>

namespace terraingen {

/// Which of its axes a boulder stands on end along, the one turned to the vertical.
enum class BoulderRest : std::uint8_t {
    Face,  ///< On its broadest face: its short axis up.
    Side,  ///< On a side: its intermediate axis up.
    End,   ///< On an end: its long axis up.
};

/// A plane that sand-laden wind has cut across a boulder's windward side: a ventifact facet.
/// Rock above the plane is gone.
struct BoulderFacet {
    /// Direction the facet faces (upwind), radians counter-clockwise from east.
    double azimuth = 0.0;
    double angle = 0.0;  ///< Slope of the facet from horizontal, radians, in (0, 90) deg.
    /// Distance (m) toward `azimuth` from the boulder's center at which the facet meets the surface
    /// the boulder stands on.
    double reach = 0.0;

    /// The facet's height (m) above that surface at east and north `offset` meters from the
    /// boulder's center, negative beyond `reach`.
    double HeightAt(const Vec2d& offset) const {
        const double along = offset.x * std::cos(azimuth) + offset.y * std::sin(azimuth);
        return (reach - along) * std::tan(angle);
    }

    bool operator==(const BoulderFacet& other) const {
        return azimuth == other.azimuth && angle == other.angle && reach == other.reach;
    }
};

/// One boulder: where it stands, how large it is and how it lies, in surface coordinates
/// and meters.
struct Boulder {
    double x = 0.0;         ///< Surface x of its center.
    double y = 0.0;         ///< Surface y of its center.
    double diameter = 0.0;  ///< Length of its long axis: its largest horizontal extent on its face.
    /// Length of its short axis, buried part included: its full height resting on its face.
    double height = 0.0;
    double burial = 0.0;   ///< Share of its vertical extent below the surface, in [0, 1).
    double yaw = 0.0;      ///< Rotation about the vertical, radians counter-clockwise from east.
    double lean = 0.0;     ///< Tilt of the boulder's vertical axis from the vertical, radians.
    std::uint64_t id = 0;  ///< Stable ID, keyed like crater IDs.
    BoulderRest rest = BoulderRest::Face;  ///< Which axis it stands along.
    double tilt = 0.0;         ///< Angle of its up axis from the vertical, after lean, radians.
    double tiltAzimuth = 0.0;  ///< Direction its up axis tips toward, radians from east.
    /// Height of the ground beneath its center before any boulders, when the layer knows it.
    std::optional<double> ground{};
    /// Placed as a 3D model of its own rather than drawn into the heights
    /// (`BoulderModelThreshold`).
    bool model = false;
    /// How worn it is, from 0 (fresh) to 1: its crater's wear for a crater block, uniform between
    /// craters.
    double wear = 0.0;
    /// For a fragment, the ID of the boulder it broke from; 0 for a whole boulder.
    std::uint64_t parent = 0;
    std::uint32_t piece = 0;  ///< For a fragment, which piece of its parent it is.
    /// Its ventifact facets, the first `facetCount` of them (`BoulderVentifacts`).
    std::array<BoulderFacet, 2> facets{};
    std::uint8_t facetCount = 0;  ///< How many facets it has, 0 to 2.
    /// The seed of its shape; unset derives it from its ID, or its parent's for a fragment
    /// (`BoulderShapeSeed`).
    std::optional<std::uint64_t> shapeSeed{};

    /// Height of the top above the surface it stands on, `height` (1 - `burial`), in meters.
    double ExposedHeight() const {
        return height * (1.0 - burial);
    }

    /// `top`, a height (m) of the boulder above the surface at east and north `offset` meters from
    /// its center, lowered to its facets and not below 0.
    double CutByFacets(const Vec2d& offset, double top) const {
        for (std::uint8_t k = 0; k < facetCount; ++k) {
            top = std::fmin(top, facets[k].HeightAt(offset));
        }
        return std::fmax(top, 0.0);
    }

    bool operator==(const Boulder& other) const {
        return x == other.x && y == other.y && diameter == other.diameter &&
               height == other.height && burial == other.burial && yaw == other.yaw &&
               lean == other.lean && id == other.id && rest == other.rest && tilt == other.tilt &&
               tiltAzimuth == other.tiltAzimuth && ground == other.ground && model == other.model &&
               wear == other.wear && parent == other.parent && piece == other.piece &&
               facets == other.facets && facetCount == other.facetCount &&
               shapeSeed == other.shapeSeed;
    }
};

/// Boulders stored as one array per field, as `CraterList` stores craters, so each column can be
/// shared with NumPy or a GPU without copying.
class BoulderList {
  public:
    /// An empty list.
    BoulderList() = default;

    /// A list of `boulders`, in order.
    BoulderList(std::initializer_list<Boulder> boulders);

    /// The number of boulders.
    std::size_t Size() const {
        return id_.size();
    }

    /// Whether the list holds no boulder.
    bool Empty() const {
        return id_.empty();
    }

    /// Reserves room for `count` boulders in every column.
    void Reserve(std::size_t count);

    /// Appends a boulder.
    void Append(const Boulder& boulder);

    /// Appends every boulder of `other`, in its order.
    void Append(const BoulderList& other);

    /// The boulder at `index`.
    /// @throws std::out_of_range if `index` >= `Size()`.
    Boulder At(std::size_t index) const;

    /// Memberwise equality, column by column.
    bool operator==(const BoulderList& other) const;
    bool operator!=(const BoulderList& other) const {
        return !(*this == other);
    }

    /// Every east coordinate, in surface coordinates.
    const std::vector<double>& X() const {
        return x_;
    }
    /// Every north coordinate, in surface coordinates.
    const std::vector<double>& Y() const {
        return y_;
    }
    /// Every diameter, in meters.
    const std::vector<double>& Diameters() const {
        return diameter_;
    }
    /// Every height, in meters.
    const std::vector<double>& Heights() const {
        return height_;
    }
    /// Every buried share of the vertical extent, in [0, 1).
    const std::vector<double>& Burials() const {
        return burial_;
    }
    /// Every yaw, in radians.
    const std::vector<double>& Yaws() const {
        return yaw_;
    }
    /// Every lean, in radians.
    const std::vector<double>& Leans() const {
        return lean_;
    }
    /// Every stable ID.
    const std::vector<std::uint64_t>& Ids() const {
        return id_;
    }
    /// Every `BoulderRest` as a byte.
    const std::vector<std::uint8_t>& Rests() const {
        return rest_;
    }
    /// Every tilt, in radians.
    const std::vector<double>& Tilts() const {
        return tilt_;
    }
    /// Every tilt azimuth, in radians.
    const std::vector<double>& TiltAzimuths() const {
        return tiltAzimuth_;
    }
    /// Every ground height in meters, NaN where unknown.
    const std::vector<double>& Grounds() const {
        return ground_;
    }
    /// 1 for boulders placed as 3D models, 0 for the rest.
    const std::vector<std::uint8_t>& Models() const {
        return model_;
    }
    /// Every wear, from 0 (fresh) to 1.
    const std::vector<double>& Wears() const {
        return wear_;
    }
    /// Every parent ID, 0 for a whole boulder.
    const std::vector<std::uint64_t>& Parents() const {
        return parent_;
    }
    /// Every piece index among its parent's pieces.
    const std::vector<std::uint32_t>& Pieces() const {
        return piece_;
    }
    /// Every boulder's facets; the first `FacetCounts()` of each are used.
    const std::vector<std::array<BoulderFacet, 2>>& Facets() const {
        return facets_;
    }
    /// Every boulder's number of facets in use.
    const std::vector<std::uint8_t>& FacetCounts() const {
        return facetCount_;
    }
    /// Every shape seed, 0 where unset (see `HasShapeSeeds`).
    const std::vector<std::uint64_t>& ShapeSeeds() const {
        return shapeSeed_;
    }
    /// 1 where a boulder has a shape seed, 0 where it derives one from its ID.
    const std::vector<std::uint8_t>& HasShapeSeeds() const {
        return hasShapeSeed_;
    }

  private:
    std::vector<double> x_;
    std::vector<double> y_;
    std::vector<double> diameter_;
    std::vector<double> height_;
    std::vector<double> burial_;
    std::vector<double> yaw_;
    std::vector<double> lean_;
    std::vector<std::uint64_t> id_;
    std::vector<std::uint8_t> rest_;
    std::vector<double> tilt_;
    std::vector<double> tiltAzimuth_;
    std::vector<double> ground_;
    std::vector<std::uint8_t> model_;
    std::vector<double> wear_;
    std::vector<std::uint64_t> parent_;
    std::vector<std::uint32_t> piece_;
    std::vector<std::array<BoulderFacet, 2>> facets_;
    std::vector<std::uint8_t> facetCount_;
    std::vector<std::uint64_t> shapeSeed_;
    std::vector<std::uint8_t> hasShapeSeed_;
};

/// A boulder placed by hand: its center and size, and any of its pose and shape fixed; unset
/// fields are drawn as for generated boulders. Set fields by name with the chainable setters:
///
///     ManualBoulder(0.5, 0.5, 1.8).Height(1.1).Burial(0.2).YawDegrees(30.0).Model(true)
struct ManualBoulder {
    double x = 0.0;         ///< Surface x of its center.
    double y = 0.0;         ///< Surface y of its center.
    double diameter = 0.0;  ///< Length of its long axis (m).
    /// Stable ID; unset derives one from position and size (`ManualCraterId`).
    std::optional<std::uint64_t> id{};
    /// Vertical extent (m), buried part included; unset draws it from the pose options.
    std::optional<double> height{};
    std::optional<double> burial{};  ///< Buried share of the height, in [0, 1); unset draws it.
    /// Rotation about the vertical, degrees counter-clockwise from east; unset draws it.
    std::optional<double> yawDegrees{};
    /// Tilt of its vertical axis from the vertical, degrees; unset draws it.
    std::optional<double> leanDegrees{};
    /// Angle of its up axis from the vertical after lean, degrees; unset draws it.
    std::optional<double> tiltDegrees{};
    /// Direction its up axis tips toward, degrees counter-clockwise from east; unset draws it.
    std::optional<double> tiltAzimuthDegrees{};
    std::optional<double> wear{};       ///< How worn it is, from 0 (fresh) to 1; unset draws it.
    std::optional<BoulderRest> rest{};  ///< Which axis it stands along; unset draws it.
    /// The seed of its shape (`GenerateBoulderShape`); unset derives it from its ID.
    std::optional<std::uint64_t> shapeSeed{};
    /// Whether it is a 3D model rather than relief in the heights; unset follows the layer's
    /// `BoulderRendering::models` threshold.
    std::optional<bool> model{};

    ManualBoulder() = default;
    /// A boulder `length` meters long centered at surface (`centerX`, `centerY`), the rest drawn.
    ManualBoulder(double centerX, double centerY, double length)
        : x(centerX), y(centerY), diameter(length) {}

    /// Sets `id`; this and the setters below return the boulder for chaining.
    ManualBoulder& Id(std::uint64_t value) {
        id = value;
        return *this;
    }
    /// Sets `height`.
    ManualBoulder& Height(double meters) {
        height = meters;
        return *this;
    }
    /// Sets `burial`.
    ManualBoulder& Burial(double share) {
        burial = share;
        return *this;
    }
    /// Sets `yawDegrees`.
    ManualBoulder& YawDegrees(double degrees) {
        yawDegrees = degrees;
        return *this;
    }
    /// Sets `leanDegrees`.
    ManualBoulder& LeanDegrees(double degrees) {
        leanDegrees = degrees;
        return *this;
    }
    /// Sets `tiltDegrees`.
    ManualBoulder& TiltDegrees(double degrees) {
        tiltDegrees = degrees;
        return *this;
    }
    /// Sets `tiltAzimuthDegrees`.
    ManualBoulder& TiltAzimuthDegrees(double degrees) {
        tiltAzimuthDegrees = degrees;
        return *this;
    }
    /// Sets `wear`.
    ManualBoulder& Wear(double value) {
        wear = value;
        return *this;
    }
    /// Sets `rest`.
    ManualBoulder& Rest(BoulderRest value) {
        rest = value;
        return *this;
    }
    /// Sets `shapeSeed`.
    ManualBoulder& ShapeSeed(std::uint64_t value) {
        shapeSeed = value;
        return *this;
    }
    /// Sets `model`.
    ManualBoulder& Model(bool value) {
        model = value;
        return *this;
    }

    /// Whether any of its lean, tilt, tilt azimuth or rest is fixed.
    bool FixesOrientation() const {
        return leanDegrees || tiltDegrees || tiltAzimuthDegrees || rest;
    }

    /// Whether any of its pose, shape or model choice is fixed.
    bool FixesAny() const {
        return FixesOrientation() || height || burial || yawDegrees || wear || shapeSeed || model;
    }

    bool operator==(const ManualBoulder& other) const {
        return x == other.x && y == other.y && diameter == other.diameter && id == other.id &&
               height == other.height && burial == other.burial && yawDegrees == other.yawDegrees &&
               leanDegrees == other.leanDegrees && tiltDegrees == other.tiltDegrees &&
               tiltAzimuthDegrees == other.tiltAzimuthDegrees && wear == other.wear &&
               rest == other.rest && shapeSeed == other.shapeSeed && model == other.model;
    }
    bool operator!=(const ManualBoulder& other) const {
        return !(*this == other);
    }
};

}  // namespace terraingen
