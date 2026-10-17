#include "terraingen/layers/craters/degradation/diffusion.h"

#include "terraingen/util/checks.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// A number no other diffused profile has, so a thread's last table is never another's.
std::uint64_t NextInstance() {
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1);
}

constexpr double kFirstAge = 1e-5;
constexpr double kLastAge = 0.5;
constexpr std::size_t kAgeSteps = 160;
constexpr double kReferenceDiameter = 1000.0;

/// The tabulated diffusion ages: 0, then geometric from `kFirstAge` to `kLastAge`.
const std::vector<double>& TableAges() {
    static const std::vector<double> ages = [] {
        std::vector<double> values{0.0};
        const double ratio =
            std::pow(kLastAge / kFirstAge, 1.0 / static_cast<double>(kAgeSteps - 1));
        for (std::size_t k = 0; k < kAgeSteps; ++k) {
            values.push_back(k + 1 == kAgeSteps
                                 ? kLastAge
                                 : kFirstAge * std::pow(ratio, static_cast<double>(k)));
        }
        return values;
    }();
    return ages;
}

/// The row below `tau` and the weight of the row above it, interpolating in log tau (linearly in
/// tau before the first nonzero age).
std::pair<std::size_t, double> RowOf(double tau) {
    const std::vector<double>& ages = TableAges();
    if (tau <= 0.0) {
        return {0, 0.0};
    }
    if (tau >= ages.back()) {
        return {ages.size() - 1, 0.0};
    }
    if (tau < ages[1]) {
        return {0, tau / ages[1]};
    }
    const auto above =
        static_cast<std::size_t>(std::upper_bound(ages.begin(), ages.end(), tau) - ages.begin());
    const std::size_t below = above - 1;
    return {below, std::log(tau / ages[below]) / std::log(ages[above] / ages[below])};
}

/// A tridiagonal system lower[i] x[i-1] + diag[i] x[i] + upper[i] x[i+1] = rhs[i] for rows i < n,
/// with x[n] given.
struct Tridiagonal {
    std::vector<double> lower;
    std::vector<double> diag;
    std::vector<double> upper;
};

/// Solves `system` by the Thomas algorithm: `x` holds the right-hand side of rows 0 to n - 1 and
/// the fixed x[n], and is replaced by the solution. `c` and `d` are scratch of n values.
void SolveTridiagonal(const Tridiagonal& system, std::vector<double>& x, std::vector<double>& c,
                      std::vector<double>& d) {
    const std::size_t n = system.diag.size();
    c[0] = system.upper[0] / system.diag[0];
    d[0] = x[0] / system.diag[0];
    for (std::size_t i = 1; i < n; ++i) {
        const double m = system.diag[i] - system.lower[i] * c[i - 1];
        c[i] = system.upper[i] / m;
        d[i] = (x[i] - system.lower[i] * d[i - 1]) / m;
    }
    for (std::size_t i = n; i-- > 0;) {
        x[i] = d[i] - c[i] * x[i + 1];
    }
}

}  // namespace

double PowerLawDiffusivity::Diffusivity(double diameter) const {
    return coefficient * std::pow(std::fmax(diameter, minDiameter) / 1000.0, exponent);
}

double PowerLawDiffusivity::operator()(double diameter, double ageGyr) const {
    return Diffusivity(diameter) * ageGyr * 1000.0;
}

void ValidatePowerLawDiffusivity(const PowerLawDiffusivity& law) {
    RequirePositive(law.coefficient, "A diffusivity coefficient");
    if (!(std::isfinite(law.exponent) && IsNonNegative(law.minDiameter))) {
        throw std::invalid_argument(
            "A diffusivity exponent must be finite and its minimum diameter not negative");
    }
}

std::size_t DiffusedProfile::KeyHash::operator()(const Key& key) const {
    std::uint64_t hash = 1469598103934665603ull;
    for (const double part : {key.depthRatio, key.rimHeightRatio, key.diameter}) {
        hash = (hash ^ std::bit_cast<std::uint64_t>(part)) * 1099511628211ull;
    }
    return static_cast<std::size_t>(hash);
}

DiffusedProfile::DiffusedProfile(std::shared_ptr<const CraterProfile> fresh, double reachRadii,
                                 std::size_t cells, std::size_t cacheTables)
    : fresh_(std::move(fresh)), reachRadii_(reachRadii), cells_(cells), instance_(NextInstance()) {
    if (!fresh_) {
        throw std::invalid_argument("A diffused profile needs a fresh profile");
    }
    if (!(std::isfinite(reachRadii_) && reachRadii_ >= fresh_->ReachRadii())) {
        throw std::invalid_argument(
            "A diffused profile must reach at least as far as the fresh one");
    }
    if (cells_ < 100 || cacheTables == 0) {
        throw std::invalid_argument("A diffused profile needs at least 100 cells and a cache");
    }
    tables_ = std::make_shared<LruCache<Key, Table, KeyHash>>(
        cacheTables, [](const Table&) { return std::size_t{1}; });
}

const std::vector<double>& DiffusedProfile::Ages() const {
    return TableAges();
}

DiffusedProfile::Table DiffusedProfile::Solve(const CraterMorphometry& fresh,
                                              double diameter) const {
    const std::size_t n = cells_;
    const double extent = 0.5 * reachRadii_;
    const double dx = extent / static_cast<double>(n);
    const CraterState initial{fresh, fresh, 0.0};
    std::vector<double> h(n + 1, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        h[i] = fresh_->Height(static_cast<double>(i) * dx * diameter, diameter, initial) / diameter;
    }
    std::vector<double> below(n + 1, 0.0);
    std::vector<double> above(n + 1, 0.0);
    for (std::size_t i = 1; i < n; ++i) {
        const double x = static_cast<double>(i) * dx;
        below[i] = (x - 0.5 * dx) / (x * dx * dx);
        above[i] = (x + 0.5 * dx) / (x * dx * dx);
    }
    above[0] = 4.0 / (dx * dx);

    Table table;
    const std::vector<double>& ages = TableAges();
    table.heights.reserve(ages.size() * (n + 1));
    Tridiagonal system{std::vector<double>(n), std::vector<double>(n), std::vector<double>(n)};
    std::vector<double> c(n);
    std::vector<double> d(n);
    const auto step = [&](double dt) {
        for (std::size_t i = 0; i < n; ++i) {
            system.lower[i] = -dt * below[i];
            system.diag[i] = 1.0 + dt * (below[i] + above[i]);
            system.upper[i] = -dt * above[i];
        }
        h[n] = 0.0;
        SolveTridiagonal(system, h, c, d);
    };
    double tau = 0.0;
    for (const double target : ages) {
        while (tau < target) {
            const double dt = std::fmin(target - tau, std::fmax(dx * dx, 0.002 * tau));
            step(dt);
            tau += dt;
        }
        double rim = -INFINITY;
        double floor = INFINITY;
        for (std::size_t i = 0; i <= n; ++i) {
            const double x = static_cast<double>(i) * dx;
            if (x >= 0.25) {
                rim = std::fmax(rim, h[i]);
            }
            if (x <= 0.5) {
                floor = std::fmin(floor, h[i]);
            }
            table.heights.push_back(static_cast<float>(h[i]));
        }
        table.morphometry.push_back({rim - floor, rim});
    }
    return table;
}

std::shared_ptr<const DiffusedProfile::Table>
DiffusedProfile::TableFor(const CraterMorphometry& fresh, double diameter) const {
    const double reference = fresh_->IsComplex(diameter) ? diameter : kReferenceDiameter;
    const Key key{fresh.depthRatio, fresh.rimHeightRatio, reference};
    struct Last {
        std::uint64_t instance = 0;
        Key key{};
        std::shared_ptr<const Table> table;
    };
    thread_local Last last;
    if (last.instance == instance_ && last.key == key) {
        return last.table;
    }
    last.table = tables_->Get(key, [&] { return Solve(fresh, reference); });
    last.instance = instance_;
    last.key = key;
    return last.table;
}

std::shared_ptr<const std::vector<float>>
DiffusedProfile::HeightTable(const CraterMorphometry& fresh, double diameter) const {
    const std::shared_ptr<const Table> table = TableFor(fresh, diameter);
    return {table, &table->heights};
}

double DiffusedProfile::Height(double distance, double diameter, const CraterState& state) const {
    if (!(state.diffusion > 0.0)) {
        return fresh_->Height(distance, diameter, {state.fresh, state.fresh, 0.0});
    }
    const double x = distance / diameter;
    const double extent = 0.5 * reachRadii_;
    if (x >= extent) {
        return 0.0;
    }
    const auto table = TableFor(state.fresh, diameter);
    const double cell = x / extent * static_cast<double>(cells_);
    const auto i = std::min(static_cast<std::size_t>(cell), cells_ - 1);
    const double t = cell - static_cast<double>(i);
    const auto [row, weight] = RowOf(state.diffusion);
    const std::size_t width = cells_ + 1;
    const auto at = [&](std::size_t r) {
        const float* values = table->heights.data() + r * width;
        return (1.0 - t) * static_cast<double>(values[i]) + t * static_cast<double>(values[i + 1]);
    };
    double value = at(row);
    if (weight > 0.0) {
        value += weight * (at(row + 1) - value);
    }
    return diameter * value;
}

CraterMorphometry DiffusedProfile::Morphometry(const CraterMorphometry& fresh, double diameter,
                                               double tau) const {
    if (!(tau > 0.0)) {
        return fresh;
    }
    const auto table = TableFor(fresh, diameter);
    const auto [row, weight] = RowOf(tau);
    CraterMorphometry result = table->morphometry[row];
    if (weight > 0.0) {
        const CraterMorphometry& next = table->morphometry[row + 1];
        result.depthRatio += weight * (next.depthRatio - result.depthRatio);
        result.rimHeightRatio += weight * (next.rimHeightRatio - result.rimHeightRatio);
    }
    return result;
}

DiffusionDegradation::DiffusionDegradation(FreshMorphometry fresh, DiffusionAgeModel ages,
                                           std::shared_ptr<const DiffusedProfile> profile)
    : fresh_(std::move(fresh)), ages_(std::move(ages)), profile_(std::move(profile)) {
    if (!fresh_ || !ages_ || !profile_) {
        throw std::invalid_argument(
            "Diffusion degradation needs fresh morphometry, diffusion ages and a diffused profile");
    }
}

CraterState DiffusionDegradation::State(const Crater& crater) const {
    if (!IsPositive(crater.diameter)) {
        throw std::domain_error("Crater diameter must be positive and finite");
    }
    if (!IsNonNegative(crater.ageGyr)) {
        throw std::domain_error("Crater age must be finite and not negative");
    }
    const double kappaT = ages_(crater.diameter, crater.ageGyr);
    if (!IsNonNegative(kappaT)) {
        throw std::domain_error("A crater's diffusion age must be finite and not negative");
    }
    const CraterMorphometry fresh = fresh_(crater.diameter);
    const double tau = kappaT / (crater.diameter * crater.diameter);
    return {fresh, profile_->Morphometry(fresh, crater.diameter, tau), tau};
}

}  // namespace terraingen
