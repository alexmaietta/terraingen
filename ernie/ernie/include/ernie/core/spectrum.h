#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

/// Spectral quantities for Ernie's spectral rendering: wavelengths in nanometres, spectral
/// irradiance in W m^-2 nm^-1 and spectral radiance in W m^-2 sr^-1 nm^-1.
namespace ernie {

/// Speed of light (m/s), Planck's constant (J s), Boltzmann's constant (J/K), the
/// Stefan-Boltzmann constant (W m^-2 K^-4) and the maximum luminous efficacy (lm/W), as defined
/// by the SI (CODATA 2018) and the CIE.
inline constexpr double kSpeedOfLight = 299792458.0;
inline constexpr double kPlanck = 6.62607015e-34;
inline constexpr double kBoltzmann = 1.380649e-23;
inline constexpr double kStefanBoltzmann = 5.670374419e-8;
inline constexpr double kLuminousEfficacy = 683.002;

/// A wavelength range split into `count` bands of equal width, the spectral sampling of one
/// camera. Band `i` covers [`Lower(i)`, `Lower(i) + Width()`).
struct SpectralBands {
    double minNm = 380.0;
    double maxNm = 780.0;
    int count = 16;

    double Width() const {
        return (maxNm - minNm) / count;
    }

    double Lower(int band) const {
        return minNm + band * Width();
    }

    double Centre(int band) const {
        return Lower(band) + 0.5 * Width();
    }

    /// @throws std::invalid_argument unless 0 < minNm < maxNm and 1 <= count <= kMaxBands.
    void Check() const;

    static constexpr int kMaxBands = 64;

    bool operator==(const SpectralBands&) const = default;
};

/// A spectrum tabulated at increasing wavelengths and linearly interpolated between them; constant
/// beyond the first and last samples. An empty spectrum is zero everywhere.
class Spectrum {
  public:
    Spectrum() = default;

    /// @throws std::invalid_argument if the wavelengths are not strictly increasing or the sizes
    /// differ.
    Spectrum(std::vector<double> wavelengthsNm, std::vector<double> values);

    /// A spectrum equal to `value` everywhere.
    static Spectrum Constant(double value);

    /// From interleaved (wavelength, value) pairs, the form of the `ernie:` spectrum attributes.
    /// @throws std::invalid_argument for an odd count or as the constructor.
    static Spectrum FromPairs(std::span<const float> pairs);

    double operator()(double wavelengthNm) const;

    /// The mean of the spectrum over [`lowNm`, `highNm`], integrating the interpolant exactly.
    double Average(double lowNm, double highNm) const;

    /// The band averages over `bands`.
    std::vector<float> Bin(const SpectralBands& bands) const;

    bool Empty() const {
        return wavelengths_.empty();
    }

    const std::vector<double>& Wavelengths() const {
        return wavelengths_;
    }

    const std::vector<double>& Values() const {
        return values_;
    }

  private:
    double Integral(double lowNm, double highNm) const;

    std::vector<double> wavelengths_;
    std::vector<double> values_;
};

/// Planck's law: the spectral radiance of a black body at `kelvin`, in W m^-2 sr^-1 nm^-1.
double PlanckRadiance(double wavelengthNm, double kelvin);

/// The spectral irradiance of a black body at `kelvin` scaled so that its integral over all
/// wavelengths is `totalIrradiance` (W/m^2), averaged over each of `bands`.
std::vector<float> BlackbodyIrradiance(const SpectralBands& bands, double kelvin,
                                       double totalIrradiance);

/// The CIE 1931 2-degree colour matching functions x, y and z at a wavelength, from the
/// multi-lobe Gaussian fit of Wyman, Sloan and Shirley (2013); y peaks at 1 near 555 nm.
///
/// Reference: C. Wyman, P.-P. Sloan and P. Shirley, "Simple Analytic Approximations to the CIE XYZ
/// Color Matching Functions", Journal of Computer Graphics Techniques 2(2):1-11, 2013.
glm::dvec3 CieXyz(double wavelengthNm);

/// The colour matching functions averaged over each band, scaled by the band width, so that
/// summing them times band values integrates a spectrum: XYZ = sum_i value_i * weights_i.
std::vector<glm::dvec3> CieWeights(const SpectralBands& bands);

/// CIE XYZ of band values (sum of value times `CieWeights`), without the luminous efficacy.
glm::dvec3 ToXyz(const SpectralBands& bands, std::span<const float> values);

/// Linear sRGB (D65 white, IEC 61966-2-1 primaries) from CIE XYZ.
glm::dvec3 XyzToLinearSrgb(glm::dvec3 xyz);

/// Linear sRGB of a colour given in CIE XYZ relative to the equal-energy illuminant E (white at
/// the XYZ of a flat spectrum under the colour matching functions), adapted to sRGB's D65 white
/// with the Bradford transform, so flat spectra map to greys.
///
/// Reference: K. M. Lam, "Metamerism and Colour Constancy", PhD thesis, University of Bradford,
/// 1985; matrices as given by B. Lindbloom, http://www.brucelindbloom.com/Eqn_ChromAdapt.html
glm::dvec3 EqualEnergyXyzToLinearSrgb(glm::dvec3 xyz);

/// A smooth reflectance spectrum in Jakob and Hanika's sigmoid-polynomial form:
/// s(lambda) = 1/2 + x / (2 sqrt(1 + x^2)) with x = c0 t^2 + c1 t + c2 and t the wavelength mapped
/// from 360-830 nm to 0-1. It lies in [0, 1] at every wavelength and is flat for c0 = c1 = 0.
///
/// Reference: W. Jakob and J. Hanika, "A Low-Dimensional Function Space for Efficient Spectral
/// Upsampling", Computer Graphics Forum (Proc. Eurographics) 38(2):147-155, 2019.
/// https://doi.org/10.1111/cgf.13626
struct SigmoidSpectrum {
    glm::dvec3 coefficients{0.0};

    double operator()(double wavelengthNm) const;

    /// The spectrum tabulated every 5 nm over 360-830 nm.
    Spectrum Tabulate() const;
};

/// The sigmoid spectrum whose colour under the equal-energy illuminant, adapted to D65, is the
/// linear sRGB colour `rgb` (each component clamped to [0, 1]), found by Gauss-Newton iteration as
/// Jakob and Hanika do, but minimising the residual in linear sRGB instead of CIELAB. Greys give
/// flat spectra exactly. Saturated colours outside what reflectances can reach end at the nearest
/// spectrum found.
SigmoidSpectrum RgbToSigmoid(glm::dvec3 rgb);

/// The linear sRGB colour of a reflectance spectrum under the equal-energy illuminant, adapted
/// to D65 (`EqualEnergyXyzToLinearSrgb`).
glm::dvec3 ReflectanceToLinearSrgb(const Spectrum& reflectance);

/// `RgbToSigmoid(rgb)` tabulated as a `Spectrum`.
Spectrum RgbToSpectrum(glm::dvec3 rgb);

}  // namespace ernie
