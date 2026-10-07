#pragma once

#include "terraingen/layers/bedforms.h"

namespace terraingen {

/// Martian large wind ripples, metre-scale bedforms with sinuous crests and lee faces at the angle
/// of repose (Lapotre et al. 2016): a 2 m wavelength, a height of a tenth of it (an assumption: no
/// measured height was found), a lee share giving a 30 degree lee slope, and sinuous crests. The
/// cover (15%) and field size (80 m) are not from a source; set them for a site.
///
/// Reference: M. G. A. Lapotre et al., "Large wind ripples on Mars: a record of atmospheric
/// evolution", Science 353(6294):55-58, 2016. https://doi.org/10.1126/science.aaf3206
BedformOptions MarsLargeRipples();

/// Martian transverse aeolian ridges, symmetric ridges of the typical size Hugenholtz et al. (2017)
/// measured on 2295 TARs in HiRISE terrain models: a 25.8 m wavelength and 1.3 m height. The cover
/// (7%) and field size (250 m) are not from a source; set them for a site.
///
/// Reference: C. H. Hugenholtz, T. E. Barchyn and E. A. Boulding, "Morphology of transverse aeolian
/// ridges (TARs) on Mars from a large sample", Icarus 286, 2017.
BedformOptions MarsTransverseAeolianRidges();

}  // namespace terraingen
