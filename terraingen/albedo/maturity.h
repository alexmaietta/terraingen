#pragma once

namespace terraingen {

/// The origin of Lucey et al.'s (2000) optical maturity parameter in the plane of 750 nm
/// reflectance and 950/750 nm reflectance ratio: the hypothetical hypermature end-member, very dark
/// and very red. The default is their origin for Clementine data, 0.08 and 1.19; they note other
/// calibrations need their own (`kLaboratoryMaturityOrigin`).
struct OpticalMaturityOrigin {
    double reflectance = 0.08;
    double ratio = 1.19;

    bool operator==(const OpticalMaturityOrigin&) const = default;
};

/// Lucey et al.'s (2000) origin for John Adams' directional-hemispherical laboratory spectra,
/// 0.01 and 1.26, which their Table 1 of returned soils uses.
inline constexpr OpticalMaturityOrigin kLaboratoryMaturityOrigin{0.01, 1.26};

/// Lucey et al.'s (2000, eq. 1) optical maturity parameter (OMAT) of a surface with 750 nm
/// reflectance `r750` and 950 nm reflectance `r950`: its distance from `origin` in the plane of
/// reflectance and ratio, sqrt((r750 - x0)^2 + (r950/r750 - y0)^2). Higher is less mature: very
/// mature soils score about 0.09 to 0.12, and immature ejecta near crater rims 0.29 to 0.42 (Grier
/// et al. 2001).
///
/// Reference: P. G. Lucey, D. T. Blewett, G. J. Taylor and B. R. Hawke, "Imaging of lunar surface
/// maturity", JGR 105(E8):20377-20386, 2000. https://doi.org/10.1029/1999JE001110
/// @throws std::invalid_argument unless `r750` is positive and both are finite.
double OpticalMaturity(double r750, double r950, const OpticalMaturityOrigin& origin = {});

/// The reflectance of the same material as a surface of reflectance `reflectance` and optical
/// maturity `reference`, at optical maturity `maturity`.
///
/// In Lucey et al.'s (2000, Fig. 1) plane, iron content is the angle about `origin` and maturity
/// the distance from it, orthogonal trends, so at fixed iron content the 750 nm reflectance is
/// x0 + OMAT cos(theta_Fe): it moves from x0 in proportion to the maturity parameter. The result is
/// x0 + (`reflectance` - x0) `maturity` / `reference`. Below x0, which the relation does not cover,
/// x0 is taken as `reflectance` and the reflectance is kept. The relation is for 750 nm; it is
/// applied to other bands as an approximation.
/// @throws std::invalid_argument unless `reference` is positive, `maturity` is not negative and
/// all are finite.
double ReflectanceAtMaturity(double reflectance, double reference, double maturity,
                             double originReflectance = OpticalMaturityOrigin{}.reflectance);

}  // namespace terraingen
