// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/degradation/diffusion.h"

#include "terraingen/layers/craters/crater_fields.h"
#include "terraingen/noise/hash.h"
#include "terraingen/util/bits.h"
#include "util/checks.h"

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
        : m_n(n), m_below(n + 1, 0.0), m_above(n + 1, 0.0),
          m_system{std::vector<double>(n), std::vector<double>(n), std::vector<double>(n)}, m_c(n),
          m_d(n) {
        for (std::size_t i = 1; i < n; ++i) {
            const double x = static_cast<double>(i) * dx;
            m_below[i] = (x - 0.5 * dx) / (x * dx * dx);
            m_above[i] = (x + 0.5 * dx) / (x * dx * dx);
        }
        m_above[0] = 4.0 / (dx * dx);
    }

    /// Diffuses heights `h` (n + 1 values) over `dt`.
    void Step(std::vector<double>& h, double dt) {
        for (std::size_t i = 0; i < m_n; ++i) {
            m_system.lower[i] = -dt * m_below[i];
            m_system.diag[i] = 1.0 + dt * (m_below[i] + m_above[i]);
            m_system.upper[i] = -dt * m_above[i];
        }
        h[m_n] = 0.0;
        SolveTridiagonal(m_system, h, m_c, m_d);
    }

  private:
    std::size_t m_n;
    std::vector<double> m_below;
    std::vector<double> m_above;
    Tridiagonal m_system;
    std::vector<double> m_c;
    std::vector<double> m_d;
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
    CheckRanges(law);
}

std::size_t DiffusedProfile::KeyHash::operator()(const Key& key) const {
    return HashWords({BitCast<std::uint64_t>(key.depthRatio),
                      BitCast<std::uint64_t>(key.rimHeightRatio),
                      BitCast<std::uint64_t>(key.diameter)});
}

DiffusedProfile::DiffusedProfile(std::shared_ptr<const CraterProfile> fresh, double reachRadii,
                                 std::size_t cells, std::size_t cacheTables)
    : m_fresh(std::move(fresh)), m_reachRadii(reachRadii), m_cells(cells),
      m_instance(NextInstance()) {
    if (!m_fresh) {
        throw std::invalid_argument("A diffused profile needs a fresh profile");
    }
    if (!(std::isfinite(m_reachRadii) && m_reachRadii >= m_fresh->ReachRadii())) {
        throw std::invalid_argument(
            "A diffused profile must reach at least as far as the fresh one");
    }
    if (m_cells < 100 || cacheTables == 0) {
        throw std::invalid_argument("A diffused profile needs at least 100 cells and a cache");
    }
    m_tables = std::make_shared<LruCache<Key, Table, KeyHash>>(
        cacheTables, [](const Table&) { return std::size_t{1}; });
    m_kernel.diffused = 1u;
    m_kernel.tableExtent = static_cast<float>(0.5 * m_reachRadii);
    m_kernel.tableCells = static_cast<std::uint32_t>(m_cells);
    m_kernel.agesOffset = 0u;
    m_kernel.ageCount = static_cast<std::uint32_t>(TableAges().size());
}

const std::vector<double>& DiffusedProfile::Ages() const {
    return TableAges();
}

DiffusedProfile::Table DiffusedProfile::Solve(const CraterMorphometry& fresh,
                                              double diameter) const {
    const std::size_t n = m_cells;
    const double extent = 0.5 * m_reachRadii;
    const double dx = extent / static_cast<double>(n);
    const CraterState initial{fresh, fresh, 0.0};
    std::vector<double> h(n + 1, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        h[i] =
            m_fresh->Height(static_cast<double>(i) * dx * diameter, diameter, initial) / diameter;
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
    const double reference = m_fresh->IsComplex(diameter) ? diameter : kReferenceDiameter;
    const Key key{fresh.depthRatio, fresh.rimHeightRatio, reference};
    struct Last {
        std::uint64_t instance = 0;
        Key key{};
        std::shared_ptr<const Table> table;
    };
    thread_local Last last;
    if (last.instance == m_instance && last.key == key) {
        return last.table;
    }
    last.table = m_tables->Get(key, [&] { return Solve(fresh, reference); });
    last.instance = m_instance;
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
        return m_fresh->Height(distance, diameter, {state.fresh, state.fresh, 0.0});
    }
    const float x = static_cast<float>(distance) / static_cast<float>(diameter);
    if (x >= m_kernel.tableExtent) {
        return 0.0;
    }
    const auto table = CachedTable(state.fresh, diameter);
    CraterData heights;
    heights.values = table->heights.data();
    return diameter *
           static_cast<double>(KernelDiffusedValue(m_kernel, KernelAges(), heights, 0u, x,
                                                   static_cast<float>(state.diffusion)));
}

CraterMorphometry DiffusedProfile::Morphometry(const CraterMorphometry& fresh, double diameter,
                                               double tau) const {
    if (!(tau > 0.0)) {
        return fresh;
    }
    const auto table = CachedTable(fresh, diameter);
    const KernelAgeRow age = KernelAgeRowAt(m_kernel, KernelAges(), static_cast<float>(tau));
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
    : m_fresh(std::move(fresh)), m_ages(std::move(ages)), m_profile(std::move(profile)) {
    if (!m_fresh || !m_ages || !m_profile) {
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
    const double kappaT = rate * m_ages(crater.diameter, crater.ageGyr);
    if (!IsNonNegative(kappaT)) {
        throw std::domain_error("A crater's diffusion age must be finite and not negative");
    }
    const CraterMorphometry fresh = m_fresh(crater.diameter);
    const double tau = kappaT / (crater.diameter * crater.diameter);
    return {fresh, m_profile->Morphometry(fresh, crater.diameter, tau), tau};
}

}  // namespace terraingen
