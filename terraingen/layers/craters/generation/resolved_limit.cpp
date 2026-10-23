#include "terraingen/layers/craters/generation/resolved_limit.h"

#include "terraingen/layers/source_resolved.h"

#include <stdexcept>
#include <utility>

namespace terraingen {

std::shared_ptr<const SurfaceField> UnresolvedCraterLimit(std::shared_ptr<const Source> source,
                                                          double pixelsAcross,
                                                          const Surface& surface) {
    if (!source) {
        throw std::invalid_argument("An unresolved crater limit needs a source");
    }
    return SourceResolvedLimit(std::move(source), pixelsAcross, surface);
}

}  // namespace terraingen
