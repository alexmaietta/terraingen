/// A rover's wheel contacts from point queries, as a physics engine such as Chrono asks for them
/// every step.
///
/// `EvaluateAt` gives the height at one point, equal to the grid cell centred there, and is safe
/// to call from several threads at once. This example drives a four-wheeled rover across cratered,
/// rocky ground, queries each wheel's contact height every step on one thread and then four, and
/// reports the body's height, pitch and roll and the query rate.
///
///     example_tiles_rover_contacts [--output-dir DIR] [--test]
///     terraingen-examples tiles_rover_contacts

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr double kSpacing = 0.05;
constexpr std::array<std::array<double, 2>, 4> kWheels{
    {{1.0, 0.8}, {1.0, -0.8}, {-1.0, 0.8}, {-1.0, -0.8}}};
constexpr int kSteps = 4000;
constexpr double kDegrees = 180.0 / 3.14159265358979323846;

class TilesRoverContacts : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        const terraingen::SurfaceModel surface = terraingen::LunarSurface(3.5);
        terraingen::CraterLayerOptions craterOptions;
        craterOptions.seed = 6;
        craterOptions.generation.dMin = 2.5;
        craterOptions.generation.dMax = 40.0;
        const auto craters = std::make_shared<terraingen::CraterLayer>(
            terraingen::CraterModels{surface.density, surface.ages,
                                     std::make_shared<terraingen::FreshnessDegradation>(
                                         surface.ages, terraingen::LunarFreshCraterMorphometry),
                                     std::make_shared<terraingen::FassettThomsonProfile>()},
            craterOptions);
        terraingen::BoulderLayerOptions boulderOptions;
        boulderOptions.seed = 6;
        boulderOptions.coverage = 0.06;
        boulderOptions.minDiameter = 0.1;
        boulderOptions.maxDiameter = 1.0;
        terraingen::NoiseLayer fbm;
        fbm.fractal.seed = 6;
        fbm.fractal.wavelength = 50.0f;
        fbm.fractal.amplitude = 1.0f;
        terraingen::Terrain terrain;
        terrain.source = std::make_shared<terraingen::FlatSource>(64, 64, 1.0);
        terrain.layers = {
            std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}),
            craters, std::make_shared<terraingen::BoulderLayer>(boulderOptions, craters)};

        char line[160];
        runs_.clear();
        for (const unsigned threads : {1u, 4u}) {
            std::vector<std::array<float, 4>> contacts(kSteps);
            const auto query = [&](int first, int last) {
                for (int step = first; step < last; ++step) {
                    const double x = -200.0 + 0.1 * step;
                    const double y = 2.0 * std::sin(0.02 * step);
                    const double heading = std::atan(0.04 * std::cos(0.02 * step));
                    for (std::size_t w = 0; w < kWheels.size(); ++w) {
                        const double px = x + std::cos(heading) * kWheels[w][0] -
                                          std::sin(heading) * kWheels[w][1];
                        const double py = y + std::sin(heading) * kWheels[w][0] +
                                          std::cos(heading) * kWheels[w][1];
                        contacts[static_cast<std::size_t>(step)][w] =
                            terraingen::EvaluateAt(terrain, px, py, kSpacing);
                    }
                }
            };
            const auto start = std::chrono::steady_clock::now();
            std::vector<std::thread> pool;
            for (unsigned t = 0; t < threads; ++t) {
                pool.emplace_back(query, static_cast<int>(kSteps * t / threads),
                                  static_cast<int>(kSteps * (t + 1) / threads));
            }
            for (std::thread& thread : pool) {
                thread.join();
            }
            const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
            std::snprintf(line, sizeof(line),
                          "%2u thread(s): %d wheel queries in %.0f ms (%.0f per second)\n", threads,
                          4 * kSteps, 1000.0 * elapsed.count(), 4 * kSteps / elapsed.count());
            Out() << line;
            runs_.push_back(std::move(contacts));
        }

        maxPitch_ = 0.0;
        maxRoll_ = 0.0;
        for (const auto& h : runs_.back()) {
            const double front = 0.5 * static_cast<double>(h[0] + h[1]);
            const double back = 0.5 * static_cast<double>(h[2] + h[3]);
            const double left = 0.5 * static_cast<double>(h[0] + h[2]);
            const double right = 0.5 * static_cast<double>(h[1] + h[3]);
            maxPitch_ = std::max(maxPitch_, std::fabs(std::atan2(front - back, 2.0)));
            maxRoll_ = std::max(maxRoll_, std::fabs(std::atan2(left - right, 1.6)));
        }
        std::snprintf(line, sizeof(line), "pitch up to %.1f deg, roll up to %.1f deg over %.0f m\n",
                      maxPitch_ * kDegrees, maxRoll_ * kDegrees, 0.1 * kSteps);
        Out() << line;

        terraingen::RasterGrid strip{1600, 48,
                                     terraingen::GeoTransform::NorthUp(-200.0, 6.0, 0.25)};
        ground_ = terraingen::Evaluate(terrain, strip).heights;
        Show("ground along the path", ground_);
    }

    void Test() override {
        Check(runs_.size() == 2 && runs_[0] == runs_[1],
              "four threads give the same contacts as one, to the bit");
        bool finite = true;
        float lowest = runs_[0][0][0];
        float highest = lowest;
        for (const auto& wheels : runs_[0]) {
            for (const float h : wheels) {
                finite = finite && std::isfinite(h);
                lowest = std::min(lowest, h);
                highest = std::max(highest, h);
            }
        }
        Check(finite, "every contact height is finite");
        Check(lowest > -20.0f && highest < 20.0f && highest > lowest,
              "contact heights vary within 20 m of the datum");
        Check(maxPitch_ > 0.0 && maxPitch_ * kDegrees < 60.0 && maxRoll_ > 0.0 &&
                  maxRoll_ * kDegrees < 60.0,
              "the body pitches and rolls, by less than 60 degrees");
        Check(ground_.Width() == 1600 && ground_.Height() == 48, "the ground strip is 1600 x 48");
    }

  private:
    std::vector<std::vector<std::array<float, 4>>> runs_;
    double maxPitch_ = 0.0;
    double maxRoll_ = 0.0;
    terraingen::Raster<float> ground_;
};

}  // namespace

TERRAINGEN_EXAMPLE(TilesRoverContacts, "A rover's wheel contacts from point queries")
