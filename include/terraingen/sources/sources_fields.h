// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The field lists of diamond-square options, chain seams and GeoTIFF band reads, and the
/// names of their enums.
#pragma once

#include "terraingen/geometry/geometry_fields.h"
#include "terraingen/sources/diamond_square.h"
#include "terraingen/sources/geotiff_source.h"
#include "terraingen/sources/source_chain.h"
#include "terraingen/util/option_fields.h"

#include <array>

namespace terraingen {

template <> struct EnumNames<DiamondSquareInterp> {
    static constexpr std::array<EnumEntry<DiamondSquareInterp>, 2> kValues{{
        {"bilinear", DiamondSquareInterp::Bilinear,
         "Square nodes the mean of their four corners, diamond nodes the mean of their four "
         "neighbors, samples bilinear between nodes."},
        {"catmull_rom", DiamondSquareInterp::CatmullRom,
         "Catmull-Rom splines over four nodes along each axis, as the VIPER terrain expansion "
         "does: smoother, with fewer lattice creases."},
    }};
};

template <> struct EnumNames<ChainSeamMode> {
    static constexpr std::array<EnumEntry<ChainSeamMode>, 4> kValues{{
        {"none", ChainSeamMode::None,
         "Each cell takes the first source with data, unchanged: a step where a gap begins."},
        {"feather", ChainSeamMode::Feather,
         "Adds the height difference at the nearest edge post to the later source, fading it to "
         "0 across width_m of the gap: the cheapest, without a solve."},
        {"height", ChainSeamMode::Height,
         "Shifts the later source by the edge's height difference spread smoothly into the gap, "
         "fading out over decay_length_m beyond an open edge: heights meet (C0), keeping the "
         "later source's relief."},
        {"slope", ChainSeamMode::Slope,
         "As height, with a biharmonic spread that matches slopes at the edge as well (C1)."},
    }};
};

/// The fields of diamond-square upsampling, beside its "type".
template <typename IO> void Fields(IO& io, DiamondSquareOptions& options) {
    io("fractal_dimension", options.fractalDimension,
       "Fractal dimension, 2 smooth and 3 rough: each square or diamond step scales the "
       "displacement by 2^-((3 - F) / 2). Unset estimates it from the median height differences of "
       "the posts at lags of 1 to 8 posts.",
       "", Between(2.0, 3.0));
    io("scale_m", options.scale,
       "Displacement standard deviation in meters at the posts' last step; unset estimates it "
       "per fit_window_px window.",
       "m", AtLeast(0.0));
    io("fit_window_px", options.fitWindow,
       "Side in posts of the overlapping windows, centered every half side, that an unset "
       "scale_m is fitted over; each node's scale is bilinear between the windows around it. 0 "
       "fits one scale to a sample of the posts; else at least 4.",
       "px", Between(0.0, static_cast<double>(kMaxDiamondSquareFitWindow)));
    io("interp", options.interp,
       "How new nodes are filled from the coarser ones and samples read between the finest "
       "nodes.");
    io("min_pixel_size_m", options.minPixelSize,
       "Finest node spacing in meters the posts are refined to; 0 refines down to the sample "
       "spacing.",
       "m", AtLeast(0.0));
    io("seed", options.seed, "Seed of the displacements.");
}

/// The fields of how a chain joins each source to the gaps of those before it.
template <typename IO> void Fields(IO& io, ChainSeams& seams) {
    io("mode", seams.mode,
       "How each source meets the gaps of those before it: none switches outright, feather "
       "fades the edge's height difference out across the gap, height shifts the later source "
       "to meet the earlier one's heights (C0) and slope to meet its heights and slopes (C1). "
       "The match is solved on the posts of the source before each gap.");
    io("width_m", seams.width,
       "Width in meters of the band inside a gap over which feather fades the height difference "
       "at the edge to 0.",
       "m", Above(0.0));
    io("decay_length_m", seams.decayLength,
       "Length in meters over which height and slope fade the shift out where a gap is open, "
       "beyond a source's edge; it is 0 from three decay lengths on. A hole much narrower than "
       "it takes the whole shift.",
       "m", Above(0.0));
}

/// The fields of how a GeoTIFF band is read; the upsampler and cache are not configured here.
template <typename IO> void GeoTiffReadFields(IO& io, GeoTiffSourceOptions& read) {
    io("band", read.band, "Band of the file to read, counting from 0.");
    io("scale", read.scale,
       "Factor each sample is multiplied by, replacing the band's scale from the file's GDAL "
       "metadata. Unset keeps the file's.");
    io("offset_m", read.offset,
       "Added to each scaled sample, replacing the band's offset from the file's GDAL metadata. "
       "Unset keeps the file's.",
       "m");
    io("crop", read.crop, "Box in the raster's CRS units to use; unset uses the whole file.");
    io("effective_pixel_size_m", read.effectivePixelSize,
       "Cell size in meters the heights truly resolve, for a DEM that was upsampled or smoothed "
       "before it was saved; it replaces the cell size as the scale below which the source "
       "resolves no features. Unset uses the cell size.",
       "m", Above(0.0));
}

}  // namespace terraingen
