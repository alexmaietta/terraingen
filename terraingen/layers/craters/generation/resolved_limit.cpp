#include "terraingen/layers/craters/generation/resolved_limit.h"

#include "terraingen/util/checks.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// `pixelsAcross` cells of a source's data at each point, in metres.
class UnresolvedLimitField final : public SurfaceField {
  public:
    UnresolvedLimitField(std::shared_ptr<const Source> source, double metresPerCell)
        : source_(std::move(source)), metresPerCell_(metresPerCell) {}

    double ValueAt(double x, double y) const override {
        return metresPerCell_ * source_->ResolutionAt(x, y);
    }

    double Max() const override {
        return std::numeric_limits<double>::infinity();
    }

  private:
    std::shared_ptr<const Source> source_;
    double metresPerCell_;
};

}  // namespace

std::shared_ptr<const SurfaceField> UnresolvedCraterLimit(std::shared_ptr<const Source> source,
                                                          double pixelsAcross,
                                                          const Surface& surface) {
    if (!source) {
        throw std::invalid_argument("An unresolved crater limit needs a source");
    }
    RequirePositive(pixelsAcross, "Pixels across");
    return std::make_shared<const UnresolvedLimitField>(std::move(source),
                                                        pixelsAcross * surface.UnitLength());
}

}  // namespace terraingen
