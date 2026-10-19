/// @file
/// @brief Every random stream tag: the word that keys `RandomStream` and `SubstreamSeed` apart for
/// one purpose. Tags are distinct (checked below) and frozen: changing one changes every output
/// drawn from it.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace terraingen {

inline constexpr std::uint32_t kCraterStream = 0x43524154u;       ///< Crater sizes and centres.
inline constexpr std::uint32_t kCraterAgeStream = 0x43524147u;    ///< Crater ages.
inline constexpr std::uint32_t kCraterIdStream = 0x43524944u;     ///< Crater IDs.
inline constexpr std::uint32_t kCraterKeepStream = 0x43524B50u;   ///< Keep-share thinning by ID.
inline constexpr std::uint32_t kCraterRayStream = 0x52415953u;    ///< A crater's ray layout.
inline constexpr std::uint32_t kCraterSlopeStream = 0x534C4F50u;  ///< Wear on slopes per crater.
inline constexpr std::uint32_t kCraterIrregularityStream = 0x49525247u;  ///< Outline harmonics.
inline constexpr std::uint32_t kCraterMarkStream = 0x4D41524Bu;          ///< Outline marks.
inline constexpr std::uint32_t kCraterTextureStream = 0x54455854u;       ///< Wall texture seed.
inline constexpr std::uint32_t kSecondaryStream = 0x53454344u;       ///< Secondaries per sector.
inline constexpr std::uint32_t kSecondaryIdStream = 0x53434944u;     ///< Secondary crater IDs.
inline constexpr std::uint32_t kSecondaryClumpStream = 0x434C554Du;  ///< Mean clump noise.

inline constexpr std::uint32_t kBoulderCandidateStream = 0x524F434Bu;  ///< Boulder candidates.
inline constexpr std::uint32_t kBoulderAttributeStream = 0x41545452u;  ///< Per-boulder draws.
inline constexpr std::uint32_t kBoulderPieceStream = 0x50494543u;      ///< Broken boulder pieces.
inline constexpr std::uint32_t kBoulderShapeStream = 0x424F554Cu;      ///< Boulder model shapes.
inline constexpr std::uint32_t kBoulderFractureStream = 0x43555453u;   ///< Shape fracture noise.
inline constexpr std::uint32_t kBoulderPrototypeStream = 0x50524F54u;  ///< Shape prototype pick.
inline constexpr std::uint32_t kBoulderFragmentStream = 0x46524147u;   ///< Shape fragments.
inline constexpr std::uint32_t kBoulderClusterStream = 0x434C5553u;    ///< Cluster centres.
inline constexpr std::uint32_t kBoulderPatchStream = 0x50415443u;      ///< Abundance patches.
inline constexpr std::uint32_t kThomasPlaneStream = 0x54484D32u;  ///< Thomas centres on a plane.
inline constexpr std::uint32_t kThomasSpaceStream = 0x54484D33u;  ///< Thomas centres on a body.

inline constexpr std::uint32_t kCraterAlbedoStream = 0x414C4244u;  ///< Crater albedo per crater.
inline constexpr std::uint32_t kObliqueRayStream = 0x4F424C51u;    ///< Oblique impact rays.
inline constexpr std::uint32_t kAlbedoZoneStream = 0x5A4F4E45u;    ///< Crater albedo zones.
inline constexpr std::uint32_t kEjectaLobeStream = 0x4C4F4245u;    ///< Ejecta lobe noise key.
inline constexpr std::uint32_t kBlastZoneStream = 0x424C5354u;     ///< Blast zone edge noise key.
inline constexpr std::uint32_t kSplotchStream = 0x53504C54u;       ///< Splotch sides.
inline constexpr std::uint32_t kSplotchSwarmStream = 0x5357524Du;  ///< Splotches of a swarm.
inline constexpr std::uint32_t kSwarmCraterStream = 0x50415253u;   ///< Craters that throw swarms.
inline constexpr std::uint32_t kRockAlbedoStream = 0x524B414Cu;    ///< Unresolved rock cover.
inline constexpr std::uint32_t kAlbedoDetailRmsStream = 0x5EED1234u;  ///< Detail RMS estimate.

inline constexpr std::uint32_t kRoughnessOctaveStream = 0x524F5547u;    ///< Roughness octaves.
inline constexpr std::uint32_t kRoughnessRotationStream = 0x524F5433u;  ///< Octave rotations.
inline constexpr std::uint32_t kStrataStream = 0x53545241u;             ///< Stratum thicknesses.
inline constexpr std::uint32_t kStrataHardnessStream = 0x48415244u;     ///< Stratum hardness.
inline constexpr std::uint32_t kDuneStream = 0x44554E45u;               ///< Dune lattice cells.
inline constexpr std::uint32_t kDuneForkStream = 0x464F524Bu;           ///< Ripple forks.
inline constexpr std::uint32_t kDuneCrestBreakStream = 0x42524B53u;     ///< Crest breaks.
inline constexpr std::uint32_t kDuneGrainflowStream = 0x47464C57u;      ///< Grainflow tongues.
inline constexpr std::uint32_t kDuneDefectStream = 0x44464354u;         ///< Crest defect noise.
inline constexpr std::uint32_t kRippleWanderStream = 0x52574E44u;       ///< Ripple wander noise.
inline constexpr std::uint32_t kRippleKinkStream = 0x524B4E4Bu;         ///< Ripple kink noise.
inline constexpr std::uint32_t kRippleFadeStream = 0x52464144u;         ///< Ripple fade noise.
inline constexpr std::uint32_t kBedformStream = 0x42454446u;            ///< Bedform lattice cells.
inline constexpr std::uint32_t kBedformSphereStream = 0x42454453u;      ///< Bedforms on a body.
inline constexpr std::uint32_t kBedformWanderStream = 0x42574E44u;      ///< Bedform wander noise.
inline constexpr std::uint32_t kPavementStream = 0x50415645u;           ///< Plate sites.
inline constexpr std::uint32_t kPavementOctaveStream = 0x50564F43u;     ///< Crack octave seeds.
inline constexpr std::uint32_t kPavementParentStream = 0x50565052u;     ///< Plates within a plate.
inline constexpr std::uint32_t kPavementWarpStream = 0x50565750u;       ///< Plate edge warp noise.
inline constexpr std::uint32_t kPatchFineStream = 0x5046494Eu;          ///< Fine patch noise.
inline constexpr std::uint32_t kMidpointDisplacementStream = 0x524D4450u;  ///< RMD displacements.
inline constexpr std::uint32_t kSpectralContinuationStream = 0x5350434Eu;  ///< Gabor impulses.

namespace detail {

/// Every tag above.
inline constexpr std::array kStreamTags = {kCraterStream,
                                           kCraterAgeStream,
                                           kCraterIdStream,
                                           kCraterKeepStream,
                                           kCraterRayStream,
                                           kCraterSlopeStream,
                                           kCraterIrregularityStream,
                                           kCraterMarkStream,
                                           kCraterTextureStream,
                                           kSecondaryStream,
                                           kSecondaryIdStream,
                                           kSecondaryClumpStream,
                                           kBoulderCandidateStream,
                                           kBoulderAttributeStream,
                                           kBoulderPieceStream,
                                           kBoulderShapeStream,
                                           kBoulderFractureStream,
                                           kBoulderPrototypeStream,
                                           kBoulderFragmentStream,
                                           kBoulderClusterStream,
                                           kBoulderPatchStream,
                                           kThomasPlaneStream,
                                           kThomasSpaceStream,
                                           kCraterAlbedoStream,
                                           kObliqueRayStream,
                                           kAlbedoZoneStream,
                                           kEjectaLobeStream,
                                           kBlastZoneStream,
                                           kSplotchStream,
                                           kSplotchSwarmStream,
                                           kSwarmCraterStream,
                                           kRockAlbedoStream,
                                           kAlbedoDetailRmsStream,
                                           kRoughnessOctaveStream,
                                           kRoughnessRotationStream,
                                           kStrataStream,
                                           kStrataHardnessStream,
                                           kDuneStream,
                                           kDuneForkStream,
                                           kDuneCrestBreakStream,
                                           kDuneGrainflowStream,
                                           kDuneDefectStream,
                                           kRippleWanderStream,
                                           kRippleKinkStream,
                                           kRippleFadeStream,
                                           kBedformStream,
                                           kBedformSphereStream,
                                           kBedformWanderStream,
                                           kPavementStream,
                                           kPavementOctaveStream,
                                           kPavementParentStream,
                                           kPavementWarpStream,
                                           kPatchFineStream,
                                           kMidpointDisplacementStream,
                                           kSpectralContinuationStream};

/// Whether no two of `tags` are equal.
template <std::size_t N> constexpr bool AllDistinct(const std::array<std::uint32_t, N>& tags) {
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (tags[i] == tags[j]) {
                return false;
            }
        }
    }
    return true;
}

static_assert(AllDistinct(kStreamTags), "Random stream tags must be distinct");

}  // namespace detail

}  // namespace terraingen
