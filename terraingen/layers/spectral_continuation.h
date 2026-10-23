/// @file
/// @brief `SpectralContinuationLayer`, detail finer than a base DEM that continues the base's
/// measured power-law spectrum, and the radially averaged power spectrum it is fitted from.
#pragma once

#include "terraingen/layers/fade.h"
#include "terraingen/layers/layer.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terraingen {

/// One annulus of a radially averaged power spectrum.
struct SpectrumBin {
    double frequency = 0.0;  ///< Mean radial frequency of the bin, in cycles per raster unit.
    double power = 0.0;      ///< Mean power spectral density, in height^2 raster units^2.
    std::size_t count = 0;   ///< Frequency samples averaged.
};

/// The radially averaged power spectral density of `heights` on its own cells.
///
/// The centered window of the largest power-of-2 width and height that fit is detrended by its
/// least-squares plane, cells without data set to the plane, and tapered by a Hann window in each
/// direction against edge leakage; the periodogram is normalized by the window's energy so that
/// the PSD integrates to the height variance. Bins are a quarter octave wide in |f|, from the
/// window's fundamental to the Nyquist frequency 1 / (2 pixel size), finest last; empty bins are
/// left out. Frequencies are in cycles per unit of the raster's transform.
/// @throws std::invalid_argument for a rotated raster, a window under 8 cells a side, or one with
/// no data.
std::vector<SpectrumBin> RadialPowerSpectrum(const Raster<float>& heights);

/// The wavelengths, in pixels of the base, over which its spectrum is fitted.
///
/// The defaults leave out the scales a DEM rarely holds whole: those under 4 pixels, smoothed by
/// the DEM's own interpolation and resampling, and those over a quarter of the window, few in
/// number and widened by the Hann window.
struct SpectrumFitBand {
    double minPixels = 4.0;  ///< Shortest wavelength fitted, in pixels; at least 2.
    double maxPixels = 0.0;  ///< Longest wavelength fitted, in pixels; 0 for a quarter window.

    bool operator==(const SpectrumFitBand& other) const {
        return minPixels == other.minPixels && maxPixels == other.maxPixels;
    }
    bool operator!=(const SpectrumFitBand& other) const {
        return !(*this == other);
    }
};

/// Checks a fit band.
/// @throws std::invalid_argument for a `minPixels` under 2 or not finite, or a `maxPixels` that is
/// neither 0 nor above `minPixels`.
void ValidateSpectrumFitBand(const SpectrumFitBand& band);

/// A power-law spectrum P(f) = C f^-beta, by its slope and the RMS height it holds in the octave
/// of wavelengths just finer than the base's Nyquist wavelength.
struct PowerLawSpectrum {
    /// Spectral slope beta of the radially averaged PSD, in [2, 4]: beta = 2 H + 2 for a
    /// self-affine surface of Hurst exponent H (Rosenburg et al. 2011).
    double slope = 3.0;
    /// RMS height in meters the power law holds at wavelengths from one to two base pixels.
    double octaveRms = 0.0;
};

/// The power law fitted to the radially averaged spectrum of `heights` (`RadialPowerSpectrum`)
/// over `band`, by least squares of log power on log frequency, each bin weighted by its samples.
/// The slope is clamped to [2, 4]; a raster without relief gives a slope of 3 and an RMS of 0.
/// @throws std::invalid_argument as `RadialPowerSpectrum` and `ValidateSpectrumFitBand`, or for a
/// band holding fewer than 3 bins.
PowerLawSpectrum EstimatePowerLawSpectrum(const Raster<float>& heights,
                                          const SpectrumFitBand& band = {});

/// The spectrum a `SpectralContinuationLayer` continues and the wavelengths it adds.
struct SpectralContinuationOptions {
    double pixelSize = 1.0;       ///< Cell size of the base in meters; detail starts at twice it.
    double slope = 3.0;           ///< Spectral slope beta, in [2, 4] (`PowerLawSpectrum::slope`).
    double octaveRms = 0.1;       ///< Meters (`PowerLawSpectrum::octaveRms`).
    double minWavelength = 0.05;  ///< Shortest wavelength added, in meters.
    /// Octaves above the base's Nyquist frequency over which the detail tapers in, from none at
    /// the Nyquist frequency to the whole power law; positive.
    double taperOctaves = 1.0;
    std::uint32_t seed = 1;  ///< Varies the detail.
    /// How wavelengths too fine to resolve fade out, by samples per wavelength.
    ResolutionFade fade{};

    bool operator==(const SpectralContinuationOptions& other) const {
        return pixelSize == other.pixelSize && slope == other.slope &&
               octaveRms == other.octaveRms && minWavelength == other.minWavelength &&
               taperOctaves == other.taperOctaves && seed == other.seed && fade == other.fade;
    }
    bool operator!=(const SpectralContinuationOptions& other) const {
        return !(*this == other);
    }
};

/// Checks spectral continuation options.
/// @throws std::invalid_argument for a `pixelSize`, `minWavelength` or `taperOctaves` that is not
/// positive and finite, a `slope` outside [2, 4], a negative or non-finite `octaveRms`, or an
/// invalid `fade`.
void ValidateSpectralContinuationOptions(const SpectralContinuationOptions& options);

/// Detail finer than a base DEM that continues its power-law spectrum to shorter wavelengths with
/// random phases: spectral synthesis of a fractional Brownian surface (Saupe 1988, in Peitgen and
/// Saupe, The Science of Fractal Images) restricted to the wavelengths the base cannot hold.
///
/// The target PSD is C f^-beta between the base's Nyquist frequency f_N = 1 / (2 `pixelSize`)
/// and 1 / `minWavelength`, times a smoothstep in log2(f / f_N) / `taperOctaves`, so the detail
/// adds nothing at wavelengths the base resolves. Instead of an FFT over the evaluated grid, which
/// would tie the detail to that grid, it is sparse Gabor convolution noise (Lagae et al. 2009,
/// doi:10.1145/1531326.1531360): octave bands from f_N up, each a fixed number of Gaussian-windowed
/// cosine impulses per cell of a band lattice, every impulse keyed by its cell and drawing its
/// position, direction, phase and log-uniform frequency within the band. Impulse amplitudes are
/// set from the target PSD at their frequency, so the noise's spectrum is the target smoothed by
/// the impulses' Gaussian bandwidth (a seventh of the band's lowest frequency), isotropic and
/// free of lattice artefacts.
///
/// Heights are a function of position, seed and sample spacing alone, so grids, tiles and points
/// agree. Each impulse fades out by its wavelength over the sample spacing (`fade`), so at the
/// base's own spacing the layer adds nothing. On a body, surface coordinates are scaled by
/// `Surface::UnitLength` to a local plane.
class SpectralContinuationLayer final : public Layer {
  public:
    /// @throws std::invalid_argument as `ValidateSpectralContinuationOptions`.
    explicit SpectralContinuationLayer(SpectralContinuationOptions options, Surface surface = {});

    /// The target power spectral density at `frequency` cycles per meter, in m^4.
    double TargetPsd(double frequency) const;

    /// The lowest frequencies of the bands, in cycles per meter, coarsest first.
    std::vector<double> BandFrequencies() const;

    /// The options the layer was made with.
    const SpectralContinuationOptions& Options() const {
        return options_;
    }

  protected:
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    /// One octave band of impulses.
    struct Band {
        double frequency = 0.0;  ///< Lowest frequency, in cycles per meter.
        double bandwidth = 0.0;  ///< Gaussian envelope parameter a, in 1 / m.
        double reach = 0.0;      ///< Impulse radius and lattice cell side, in meters.
        std::uint32_t index = 0;
    };

    struct ImpulseTable;

    /// The impulses of `band` over the lattice cells that reach the box [`minX`, `maxX`] x
    /// [`minY`, `maxY`] meters, faded for `spacing` meters.
    ImpulseTable Table(const Band& band, double minX, double minY, double maxX, double maxY,
                       double spacing) const;
    /// The detail at `p` meters from the impulses of `tables`.
    static double DetailAt(const std::vector<ImpulseTable>& tables, const Vec2d& p);
    /// Adds the detail at `positions` (meters) to `heights` at `spacing` meters.
    void AddDetail(const std::vector<Vec2d>& positions, const std::vector<float*>& heights,
                   double spacing) const;

    SpectralContinuationOptions options_;
    Surface surface_;
    double coefficient_ = 0.0;  ///< C in m^(4 - beta), P(f) = C f^-beta.
    std::vector<Band> bands_;
};

}  // namespace terraingen
