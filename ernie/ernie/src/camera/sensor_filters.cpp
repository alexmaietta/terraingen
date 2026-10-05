#include "ernie/camera/sensor_filters.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace ernie {

std::string_view NameOf(SensorFilter filter) {
    switch (filter) {
    case SensorFilter::Lens:
        return "lens";
    case SensorFilter::Qe:
        return "qe";
    case SensorFilter::Noise:
        return "noise";
    case SensorFilter::Adc:
        return "adc";
    case SensorFilter::Cie:
        return "cie";
    case SensorFilter::Exposure:
        return "exposure";
    }
    return "";
}

std::optional<SensorFilter> SensorFilterNamed(std::string_view name) {
    for (const SensorFilter filter : kSensorFilters) {
        if (NameOf(filter) == name) {
            return filter;
        }
    }
    return std::nullopt;
}

std::vector<SensorFilter> DefaultFilterChain() {
    return {kSensorFilters.begin(), kSensorFilters.end()};
}

std::vector<SensorFilter> CheckFilterChain(std::span<const SensorFilter> chain) {
    std::vector<SensorFilter> checked;
    if (chain.empty() || chain.front() != SensorFilter::Lens) {
        checked.push_back(SensorFilter::Lens);
    }
    const auto seen = [&checked](SensorFilter filter) {
        return std::find(checked.begin(), checked.end(), filter) != checked.end();
    };
    for (const SensorFilter filter : chain) {
        if (seen(filter)) {
            throw std::invalid_argument("Sensor filter '" + std::string(NameOf(filter)) +
                                        "' appears twice");
        }
        if ((filter == SensorFilter::Noise || filter == SensorFilter::Adc) &&
            !seen(SensorFilter::Qe)) {
            throw std::invalid_argument("Sensor filter '" + std::string(NameOf(filter)) +
                                        "' needs 'qe' before it");
        }
        if (filter == SensorFilter::Exposure && !seen(SensorFilter::Cie)) {
            throw std::invalid_argument("Sensor filter 'exposure' needs 'cie' before it");
        }
        checked.push_back(filter);
    }
    return checked;
}

std::vector<std::string_view> VarsWrittenBy(std::span<const SensorFilter> chain) {
    std::vector<std::string_view> vars;
    for (const SensorFilter filter : chain) {
        switch (filter) {
        case SensorFilter::Lens:
            vars.insert(vars.end(),
                        {render_vars::kSpectralRadiance, render_vars::kDistanceToImagePlane,
                         render_vars::kDistanceToCamera, render_vars::kNormal, render_vars::kPrimId,
                         render_vars::kInstanceId});
            break;
        case SensorFilter::Qe:
            vars.push_back(render_vars::kElectrons);
            break;
        case SensorFilter::Noise:
            break;
        case SensorFilter::Adc:
            vars.push_back(render_vars::kRawDn);
            break;
        case SensorFilter::Cie:
            vars.push_back(render_vars::kHdrColor);
            break;
        case SensorFilter::Exposure:
            vars.push_back(render_vars::kLdrColor);
            break;
        }
    }
    return vars;
}

}  // namespace ernie
