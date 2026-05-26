#include "HeatMap.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr double kEarthRadiusMeters = 6371000.0;
constexpr double kRadiusMeters = 80.0;
constexpr double kPower = 1.0;

constexpr double kPi = 3.14159265358979323846;

double DegreesToRadians(double degrees) {
    return degrees * kPi / 180.0;
}

double RadiansToDegrees(double radians) {
    return radians * 180.0 / kPi;
}

void TileXYToLatLon(double tileX, double tileY, int zoom, double& lat, double& lon) {
    const double scale = std::pow(2.0, zoom);
    lon = tileX / scale * 360.0 - 180.0;
    const double latRad = std::atan(std::sinh(kPi * (1.0 - 2.0 * tileY / scale)));
    lat = RadiansToDegrees(latRad);
}

double HaversineDistance(
    double lat1,
    double lon1,
    double lat2,
    double lon2
) {
    const double dLat = DegreesToRadians(lat2 - lat1);
    const double dLon = DegreesToRadians(lon2 - lon1);

    const double a =
        std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
        std::cos(DegreesToRadians(lat1)) *
        std::cos(DegreesToRadians(lat2)) *
        std::sin(dLon / 2.0) *
        std::sin(dLon / 2.0);

    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));

    return kEarthRadiusMeters * c;
}

double LatitudePaddingDegrees(double meters) {
    return RadiansToDegrees(meters / kEarthRadiusMeters);
}

double LongitudePaddingDegrees(double meters, double latitude) {
    const double cosLat = std::max(0.1, std::cos(DegreesToRadians(latitude)));
    return RadiansToDegrees(meters / (kEarthRadiusMeters * cosLat));
}

std::vector<HeatPoint> FilterPointsForBounds(
    const std::vector<HeatPoint>& points,
    const GeoBounds& bounds,
    double radiusMeters
) {
    std::vector<HeatPoint> filtered;
    filtered.reserve(points.size());

    const double centerLat = (bounds.minLat + bounds.maxLat) * 0.5;
    const double latPadding = LatitudePaddingDegrees(radiusMeters);
    const double lonPadding = LongitudePaddingDegrees(radiusMeters, centerLat);

    const double minLat = bounds.minLat - latPadding;
    const double maxLat = bounds.maxLat + latPadding;
    const double minLon = bounds.minLon - lonPadding;
    const double maxLon = bounds.maxLon + lonPadding;

    for (const HeatPoint& point : points) {
        if (point.latitude < minLat || point.latitude > maxLat) {
            continue;
        }
        if (point.longitude < minLon || point.longitude > maxLon) {
            continue;
        }
        filtered.push_back(point);
    }

    return filtered;
}

RGBA GetColor(double value, HeatMapMetric metric) {
    if (std::isnan(value)) {
        return {0, 0, 0, 0};
    }

    double normalized = 0.0;

    switch (metric) {
        case HeatMapMetric::RSRP:
            normalized = (value + 100.0) / 20.0;
            break;

        case HeatMapMetric::RSRQ:
            normalized = (value + 20.0) / 17.0;
            break;

        case HeatMapMetric::RSSI:
            normalized = (value + 110.0) / 60.0;
            break;

        case HeatMapMetric::Altitude:
            normalized = value / 300.0;
            break;
    }

    normalized = std::clamp(normalized, 0.0, 1.0);

    const uint8_t r =
        static_cast<uint8_t>(255.0 * normalized);

    const uint8_t g =
        static_cast<uint8_t>(255.0 * (1.0 - std::abs(normalized - 0.5) * 2.0));

    const uint8_t b =
        static_cast<uint8_t>(255.0 * (1.0 - normalized));

    return {r, g, b, 110};
}

} // namespace

double HeatMap::IDWInterpolate(
    double lat,
    double lon,
    const std::vector<HeatPoint>& points,
    double radiusMeters,
    double power
) {
    double numerator = 0.0;
    double denominator = 0.0;

    for (const HeatPoint& point : points) {
        const double distance = HaversineDistance(
            lat,
            lon,
            point.latitude,
            point.longitude
        );

        if (distance > radiusMeters) {
            continue;
        }

        const double safeDistance =
            std::max(distance, 1.0);

        const double weight =
            1.0 / std::pow(safeDistance, power);

        numerator += weight * point.value;
        denominator += weight;
    }

    if (denominator <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    return numerator / denominator;
}

GeoBounds HeatMap::GetTileBounds(int zoom, int x, int y) {
    GeoBounds bounds;

    TileXYToLatLon(static_cast<double>(x), static_cast<double>(y), zoom, bounds.maxLat, bounds.minLon);
    TileXYToLatLon(static_cast<double>(x + 1), static_cast<double>(y + 1), zoom, bounds.minLat, bounds.maxLon);

    return bounds;
}

void HeatMap::GenerateTileForMapTile(
    const std::vector<HeatPoint>& points,
    int zoom,
    int x,
    int y,
    HeatMapMetric metric,
    std::vector<uint8_t>& outRGBA
) {
    const GeoBounds bounds = GetTileBounds(zoom, x, y);
    const std::vector<HeatPoint> filteredPoints = FilterPointsForBounds(points, bounds, kRadiusMeters);
    if (filteredPoints.empty()) {
        outRGBA.clear();
        return;
    }

    if (filteredPoints.empty()) {
        outRGBA.assign(kTileResolution * kTileResolution * 4, 0);
        return;
    }

    GenerateTile(
        filteredPoints,
        kTileResolution,
        kTileResolution,
        bounds.minLat,
        bounds.maxLat,
        bounds.minLon,
        bounds.maxLon,
        metric,
        outRGBA
    );
}

void HeatMap::GenerateTile(
    const std::vector<HeatPoint>& points,
    int width,
    int height,
    double minLat,
    double maxLat,
    double minLon,
    double maxLon,
    HeatMapMetric metric,
    std::vector<uint8_t>& outRGBA
) {
    outRGBA.resize(width * height * 4);

    if (points.empty()) {
        std::fill(outRGBA.begin(), outRGBA.end(), 0);
        return;
    }

    for (int py = 0; py < height; ++py) {
        const double latitude =
            maxLat -
            (static_cast<double>(py) / static_cast<double>(height)) *
            (maxLat - minLat);

        for (int px = 0; px < width; ++px) {
            const double longitude =
                minLon +
                (static_cast<double>(px) / static_cast<double>(width)) *
                (maxLon - minLon);

            const double value = IDWInterpolate(
                latitude,
                longitude,
                points,
                kRadiusMeters,
                kPower
            );

            const RGBA color = GetColor(value, metric);
            const int index = (py * width + px) * 4;

            outRGBA[index + 0] = color.r;
            outRGBA[index + 1] = color.g;
            outRGBA[index + 2] = color.b;
            outRGBA[index + 3] = color.a;
        }
    }
}
