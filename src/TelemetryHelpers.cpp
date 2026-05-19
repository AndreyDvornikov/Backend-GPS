#include "TelemetryHelpers.h"

namespace {
constexpr int kInvalidMetricIntMax = 2147483647;
}

bool IsValidMetricValue(const std::optional<int>& value) {
    return value.has_value() && *value != -999 && *value != 0 && *value != kInvalidMetricIntMax;
}

std::optional<int> SelectPreferredSinr(const DbRow& row) {
    if (IsValidMetricValue(row.ssSinr)) {
        return row.ssSinr;
    }
    if (IsValidMetricValue(row.rssnr)) {
        return row.rssnr;
    }
    return std::nullopt;
}

LiveSample BuildLiveSample(const DbRow& row) {
    LiveSample sample;
    sample.networkType = row.networkType;
    sample.pci = row.pci;
    sample.cellIdentity = row.cellIdentity;
    sample.rsrp = row.rsrp.has_value() ? row.rsrp : row.ssRsrp;
    sample.rssi = row.rssi;
    sample.sinr = SelectPreferredSinr(row);
    return sample;
}
