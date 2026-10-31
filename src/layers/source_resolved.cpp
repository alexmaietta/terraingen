// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/source_resolved.h"

#include "util/checks.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// `pixelsAcross` cells of a source's data at each point, in meters.
class SourceResolvedField final : public SurfaceField {
  public:
    SourceResolvedField(std::shared_ptr<const Source> source, double metersPerCell)
        : m_source(std::move(source)), m_metersPerCell(metersPerCell) {}

    double ValueAt(double x, double y) const override {
        return m_metersPerCell * m_source->ResolutionAt(x, y);
    }

    double Max() const override {
        return std::numeric_limits<double>::infinity();
    }

  private:
    std::shared_ptr<const Source> m_source;
    double m_metersPerCell;
};

}  // namespace

std::shared_ptr<const SurfaceField> SourceResolvedLimit(std::shared_ptr<const Source> source,
                                                        double pixelsAcross,
                                                        const Surface& surface) {
    if (!source) {
        throw std::invalid_argument("A source resolved limit needs a source");
    }
    RequirePositive(pixelsAcross, "Pixels across");
    return std::make_shared<const SourceResolvedField>(std::move(source),
                                                       pixelsAcross * surface.UnitLength());
}

std::shared_ptr<const SurfaceField> LayerResolvedLimit(const std::shared_ptr<const Source>& source,
                                                       const std::optional<double>& pixelsAcross,
                                                       const Surface& surface) {
    if (!source || !pixelsAcross) {
        return nullptr;
    }
    return SourceResolvedLimit(source, *pixelsAcross, surface);
}

double SourceResolvedSize(const Source* source, const std::optional<double>& pixelsAcross,
                          const Surface& surface, double x, double y) {
    if (source == nullptr || !pixelsAcross) {
        return std::numeric_limits<double>::infinity();
    }
    return *pixelsAcross * surface.UnitLength() * source->ResolutionAt(x, y);
}

void ValidateSourceResolvesPixels(const std::optional<double>& pixelsAcross) {
    if (pixelsAcross) {
        RequirePositive(*pixelsAcross, "Source resolves pixels");
    }
}

}  // namespace terraingen
