/// A rover's wheel contacts from point queries, as a physics engine such as Chrono asks for them
/// every step.
///
/// `EvaluateAt` gives the height at one point, equal to the grid cell centred there, and is safe
/// to call from several threads at once. This example drives a four-wheeled rover across cratered,
/// rocky ground, queries each wheel's contact height every step on one thread and then four, and
/// reports the body's height, pitch and roll and the query rate.
///
///     example_tiles_rover_contacts [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);

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
    terraingen::RockLayerOptions rockOptions;
    rockOptions.seed = 6;
    rockOptions.coverage = 0.06;
    rockOptions.minDiameter = 0.1;
    rockOptions.maxDiameter = 1.0;
    terraingen::NoiseLayer fbm;
    fbm.fractal.seed = 6;
    fbm.fractal.wavelength = 50.0f;
    fbm.fractal.amplitude = 1.0f;
    terraingen::Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(64, 64, 1.0);
    terrain.layers = {
        std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}),
        craters, std::make_shared<terraingen::RockLayer>(rockOptions, craters)};

    constexpr double kSpacing = 0.05;
    constexpr std::array<std::array<double, 2>, 4> kWheels{
        {{1.0, 0.8}, {1.0, -0.8}, {-1.0, 0.8}, {-1.0, -0.8}}};
    constexpr int kSteps = 4000;
    std::vector<std::array<float, 4>> contacts(kSteps);
    const auto query = [&](int first, int last) {
        for (int step = first; step < last; ++step) {
            const double x = -200.0 + 0.1 * step;
            const double y = 2.0 * std::sin(0.02 * step);
            const double heading = std::atan(0.04 * std::cos(0.02 * step));
            for (std::size_t w = 0; w < kWheels.size(); ++w) {
                const double px =
                    x + std::cos(heading) * kWheels[w][0] - std::sin(heading) * kWheels[w][1];
                const double py =
                    y + std::sin(heading) * kWheels[w][0] + std::cos(heading) * kWheels[w][1];
                contacts[static_cast<std::size_t>(step)][w] =
                    terraingen::EvaluateAt(terrain, px, py, kSpacing);
            }
        }
    };
    for (const unsigned threads : {1u, 4u}) {
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
        std::printf("%2u thread(s): %d wheel queries in %.0f ms (%.0f per second)\n", threads,
                    4 * kSteps, 1000.0 * elapsed.count(), 4 * kSteps / elapsed.count());
    }

    double maxPitch = 0.0;
    double maxRoll = 0.0;
    for (const auto& h : contacts) {
        const double front = 0.5 * static_cast<double>(h[0] + h[1]);
        const double back = 0.5 * static_cast<double>(h[2] + h[3]);
        const double left = 0.5 * static_cast<double>(h[0] + h[2]);
        const double right = 0.5 * static_cast<double>(h[1] + h[3]);
        maxPitch = std::max(maxPitch, std::fabs(std::atan2(front - back, 2.0)));
        maxRoll = std::max(maxRoll, std::fabs(std::atan2(left - right, 1.6)));
    }
    constexpr double kDegrees = 180.0 / 3.14159265358979323846;
    std::printf("pitch up to %.1f deg, roll up to %.1f deg over %.0f m\n", maxPitch * kDegrees,
                maxRoll * kDegrees, 0.1 * kSteps);
    return 0;
}
