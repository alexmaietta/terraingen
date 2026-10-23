#include "terraingen/layers/craters/degradation/diffusion.h"

#include "terraingen/noise/hash.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
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

/// `TableAges` in single precision, where the crater kernel reads them.
CraterData KernelAges() {
    static const std::vector<float> ages = [] {
        std::vector<float> values;
        for (const double age : TableAges()) {
            values.push_back(static_cast<float>(age));
        }
        return values;
    }();
    CraterData data;
    data.values = ages.data();
    return data;
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

/// Implicit steps of radial diffusion on `n` cells `dx` apart with the outer edge held at 0.
class RadialDiffusion {
  public:
    RadialDiffusion(std::size_t n, double dx)
        : n_(n), below_(n + 1, 0.0), above_(n + 1, 0.0),
          system_{std::vector<double>(n), std::vector<double>(n), std::vector<double>(n)}, c_(n),
          d_(n) {
        for (std::size_t i = 1; i < n; ++i) {
            const double x = static_cast<double>(i) * dx;
            below_[i] = (x - 0.5 * dx) / (x * dx * dx);
            above_[i] = (x + 0.5 * dx) / (x * dx * dx);
        }
        above_[0] = 4.0 / (dx * dx);
    }

    /// Diffuses heights `h` (n + 1 values) over `dt`.
    void Step(std::vector<double>& h, double dt) {
        for (std::size_t i = 0; i < n_; ++i) {
            system_.lower[i] = -dt * below_[i];
            system_.diag[i] = 1.0 + dt * (below_[i] + above_[i]);
            system_.upper[i] = -dt * above_[i];
        }
        h[n_] = 0.0;
        SolveTridiagonal(system_, h, c_, d_);
    }

  private:
    std::size_t n_;
    std::vector<double> below_;
    std::vector<double> above_;
    Tridiagonal system_;
    std::vector<double> c_;
    std::vector<double> d_;
};

/// Appends heights `h` (cells `dx` diameters apart) to `heights` and returns their morphometry:
/// the rim the highest from 0.25 diameters out, the floor the lowest within 0.5.
CraterMorphometry AppendHeights(const std::vector<double>& h, double dx,
                                std::vector<float>& heights) {
    double rim = -std::numeric_limits<double>::infinity();
    double floor = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < h.size(); ++i) {
        const double x = static_cast<double>(i) * dx;
        if (x >= 0.25) {
            rim = std::fmax(rim, h[i]);
        }
        if (x <= 0.5) {
            floor = std::fmin(floor, h[i]);
        }
        heights.push_back(static_cast<float>(h[i]));
    }
    return {rim - floor, rim};
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
    return HashWords({BitCast<std::uint64_t>(key.depthRatio),
                      BitCast<std::uint64_t>(key.rimHeightRatio),
                      BitCast<std::uint64_t>(key.diameter)});
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
    kernel_.diffused = 1u;
    kernel_.tableExtent = static_cast<float>(0.5 * reachRadii_);
    kernel_.tableCells = static_cast<std::uint32_t>(cells_);
    kernel_.agesOffset = 0u;
    kernel_.ageCount = static_cast<std::uint32_t>(TableAges().size());
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
    RadialDiffusion diffusion(n, dx);
    Table table;
    const std::vector<double>& ages = TableAges();
    table.heights.reserve(ages.size() * (n + 1));
    double tau = 0.0;
    for (const double target : ages) {
        while (tau < target) {
            const double dt = std::fmin(target - tau, std::fmax(dx * dx, 0.002 * tau));
            diffusion.Step(h, dt);
            tau += dt;
        }
        table.morphometry.push_back(AppendHeights(h, dx, table.heights));
    }
    return table;
}

std::shared_ptr<const DiffusedProfile::Table>
DiffusedProfile::CachedTable(const CraterMorphometry& fresh, double diameter) const {
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
    const std::shared_ptr<const Table> table = CachedTable(fresh, diameter);
    return {table, &table->heights};
}

double DiffusedProfile::Height(double distance, double diameter, const CraterState& state) const {
    if (!(state.diffusion > 0.0)) {
        return fresh_->Height(distance, diameter, {state.fresh, state.fresh, 0.0});
    }
    const float x = static_cast<float>(distance) / static_cast<float>(diameter);
    if (x >= kernel_.tableExtent) {
        return 0.0;
    }
    const auto table = CachedTable(state.fresh, diameter);
    CraterData heights;
    heights.values = table->heights.data();
    return diameter * static_cast<double>(KernelDiffusedValue(kernel_, KernelAges(), heights, 0u, x,
                                                              static_cast<float>(state.diffusion)));
}

CraterMorphometry DiffusedProfile::Morphometry(const CraterMorphometry& fresh, double diameter,
                                               double tau) const {
    if (!(tau > 0.0)) {
        return fresh;
    }
    const auto table = CachedTable(fresh, diameter);
    const KernelAgeRow age = KernelAgeRowAt(kernel_, KernelAges(), static_cast<float>(tau));
    CraterMorphometry result = table->morphometry[age.row];
    if (age.weight > 0.0f) {
        const auto weight = static_cast<double>(age.weight);
        const CraterMorphometry& next = table->morphometry[age.row + 1];
        result.depthRatio += weight * (next.depthRatio - result.depthRatio);
        result.rimHeightRatio += weight * (next.rimHeightRatio - result.rimHeightRatio);
    }
    return result;
}

double DiffusedProfile::AgeAtDepthRatio(const CraterMorphometry& fresh, double diameter,
                                        double depthRatio) const {
    const std::vector<CraterMorphometry>& rows = CachedTable(fresh, diameter)->morphometry;
    const std::vector<double>& ages = TableAges();
    if (rows.front().depthRatio <= depthRatio) {
        return 0.0;
    }
    for (std::size_t r = 1; r < rows.size(); ++r) {
        const double above = rows[r - 1].depthRatio;
        const double below = rows[r].depthRatio;
        if (below <= depthRatio) {
            const double weight = (above - depthRatio) / (above - below);
            return r == 1 ? weight * ages[1]
                          : ages[r - 1] * std::pow(ages[r] / ages[r - 1], weight);
        }
    }
    return ages.back();
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
    return WornState(crater, 1.0);
}

CraterState DiffusionDegradation::WornState(const Crater& crater, double rate) const {
    RequireDiameter(crater.diameter);
    if (!IsNonNegative(crater.ageGyr)) {
        throw std::domain_error("Crater age must be finite and not negative");
    }
    const double kappaT = rate * ages_(crater.diameter, crater.ageGyr);
    if (!IsNonNegative(kappaT)) {
        throw std::domain_error("A crater's diffusion age must be finite and not negative");
    }
    const CraterMorphometry fresh = fresh_(crater.diameter);
    const double tau = kappaT / (crater.diameter * crater.diameter);
    return {fresh, profile_->Morphometry(fresh, crater.diameter, tau), tau};
}

}  // namespace terraingen
