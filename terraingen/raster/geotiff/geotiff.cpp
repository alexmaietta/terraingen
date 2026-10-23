#include "terraingen/raster/geotiff/geotiff.h"

#include "terraingen/raster/geotiff/tiff_format.h"

#include "terraingen/util/checks.h"

#include <cmath>
#include <cstdlib>
#include <initializer_list>

namespace terraingen {

namespace {

/// One key of a GeoKeyDirectory: its value inline (`location` 0) or at index `value` of the tag
/// `location`.
struct GeoKeyEntry {
    std::uint16_t key;
    std::uint16_t location;
    std::uint16_t value;
};

/// A GeoKeyDirectory of version 1.1.0 holding `entries`, each of count 1.
std::vector<std::uint16_t> GeoKeyDirectory(std::initializer_list<GeoKeyEntry> entries) {
    std::vector<std::uint16_t> directory = {1, 1, 0, static_cast<std::uint16_t>(entries.size())};
    for (const GeoKeyEntry& entry : entries) {
        directory.insert(directory.end(), {entry.key, entry.location, 1, entry.value});
    }
    return directory;
}

/// The value of attribute `name` in the XML start tag `tag`, if present.
std::optional<std::string> XmlAttribute(const std::string& tag, const std::string& name) {
    const std::string key = " " + name + "=\"";
    const std::size_t start = tag.find(key);
    if (start == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t first = start + key.size();
    const std::size_t last = tag.find('"', first);
    if (last == std::string::npos) {
        return std::nullopt;
    }
    return tag.substr(first, last - first);
}

}  // namespace

std::optional<std::uint16_t> detail::ShortGeoKey(const GeoKeys& keys, std::uint16_t key) {
    const std::vector<std::uint16_t>& directory = keys.directory;
    if (directory.size() < 4) {
        return std::nullopt;
    }
    const std::size_t count = directory[3];
    for (std::size_t k = 0; k < count && 4 + 4 * k + 3 < directory.size(); ++k) {
        const std::size_t at = 4 + 4 * k;
        if (directory[at] == key && directory[at + 1] == 0) {
            return directory[at + 3];
        }
    }
    return std::nullopt;
}

bool GeoKeys::PixelIsPoint() const {
    return detail::ShortGeoKey(*this, detail::kGtRasterTypeGeoKey) == detail::kRasterPixelIsPoint;
}

bool GeoKeys::IsGeographic() const {
    return detail::ShortGeoKey(*this, detail::kGtModelTypeGeoKey) == detail::kModelTypeGeographic;
}

std::vector<BandScaling> detail::ParseGdalBandScaling(const std::string& xml, std::size_t bands) {
    std::vector<BandScaling> scaling(bands);
    std::size_t position = 0;
    while ((position = xml.find("<Item", position)) != std::string::npos) {
        const std::size_t close = xml.find('>', position);
        const std::size_t end = xml.find("</Item>", close);
        if (close == std::string::npos || end == std::string::npos) {
            break;
        }
        const std::string tag = xml.substr(position, close - position);
        const std::string text = xml.substr(close + 1, end - close - 1);
        position = end;
        const auto sample = XmlAttribute(tag, "sample");
        const auto role = XmlAttribute(tag, "role");
        if (!sample || !role || (*role != "scale" && *role != "offset")) {
            continue;
        }
        char* parsed = nullptr;
        const unsigned long long band = std::strtoull(sample->c_str(), &parsed, 10);
        if (parsed == sample->c_str() || band >= bands) {
            continue;
        }
        const double value = std::strtod(text.c_str(), &parsed);
        if (parsed == text.c_str() || !std::isfinite(value)) {
            continue;
        }
        (*role == "scale" ? scaling[band].scale : scaling[band].offset) = value;
    }
    return scaling;
}

GeoKeys GeographicSphereKeys(double radius) {
    using namespace detail;
    RequirePositive(radius, "A sphere's radius");
    GeoKeys keys;
    keys.directory = GeoKeyDirectory({
        {kGtModelTypeGeoKey, 0, kModelTypeGeographic},
        {kGtRasterTypeGeoKey, 0, kRasterPixelIsArea},
        {kGeographicTypeGeoKey, 0, kUserDefined},
        {kGeogGeodeticDatumGeoKey, 0, kUserDefined},
        {kGeogAngularUnitsGeoKey, 0, kAngularDegree},
        {kGeogEllipsoidGeoKey, 0, kUserDefined},
        {kGeogSemiMajorAxisGeoKey, kGeoDoubleParams, 0},
        {kGeogSemiMinorAxisGeoKey, kGeoDoubleParams, 1},
    });
    keys.doubleParams = {radius, radius};
    return keys;
}

}  // namespace terraingen
