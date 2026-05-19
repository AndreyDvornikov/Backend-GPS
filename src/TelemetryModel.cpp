/// @file TelemetryModel.cpp
/// @brief Реализация класса TelemetryModel.

#include "TelemetryModel.h"

#include <mutex>
#include <optional>
#include <vector>

#include "Database.h"
#include "TelemetryHelpers.h"

namespace {
constexpr size_t kMaxPlotPoints = 400;
}

TelemetryModel::TelemetryModel() = default;

void TelemetryModel::AddSamples(const std::vector<DbRow>& rows,
                                const LocationData& loc,
                                long long ts) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.lastTimestampMs = ts;
    state_.lastLocation = loc;
    state_.receivedPackets++;

    for (const auto& row : rows) {
        if (!row.pci.has_value()) {
            continue;
        }

        const float sec = nextPlotTime(row.timestampMs);
        const std::optional<int> rsrp = row.rsrp.has_value() ? row.rsrp : row.ssRsrp;
        if (IsValidMetricValue(rsrp)) {
            pushPoint(state_.rsrpTimeByPci[*row.pci], state_.rsrpValueByPci[*row.pci], sec, static_cast<float>(*rsrp));
        }

        if (IsValidMetricValue(row.rssi)) {
            pushPoint(state_.rssiTimeByPci[*row.pci], state_.rssiValueByPci[*row.pci], sec, static_cast<float>(*row.rssi));
        }

        const auto sinr = SelectPreferredSinr(row);
        if (IsValidMetricValue(sinr)) {
            pushPoint(state_.sinrTimeByPci[*row.pci], state_.sinrValueByPci[*row.pci], sec, static_cast<float>(*sinr));
        }
    }

    state_.latestSamples.clear();
    state_.latestSamples.reserve(rows.size());
    for (const auto& row : rows) {
        state_.latestSamples.push_back(BuildLiveSample(row));
    }
}

bool TelemetryModel::LoadFromDatabase(Database& db, int sampleCount) {
    std::vector<DbRow> rows;
    if (!db.LoadLastRows(sampleCount, rows)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    clearState();

    for (const auto& row : rows) {
        if (!row.pci.has_value()) {
            continue;
        }

        const float sec = nextPlotTime(row.timestampMs);
        const std::optional<int> rsrp = row.rsrp.has_value() ? row.rsrp : row.ssRsrp;
        if (IsValidMetricValue(rsrp)) {
            pushPoint(state_.rsrpTimeByPci[*row.pci], state_.rsrpValueByPci[*row.pci], sec, static_cast<float>(*rsrp));
        }

        if (IsValidMetricValue(row.rssi)) {
            pushPoint(state_.rssiTimeByPci[*row.pci], state_.rssiValueByPci[*row.pci], sec, static_cast<float>(*row.rssi));
        }

        const auto sinr = SelectPreferredSinr(row);
        if (IsValidMetricValue(sinr)) {
            pushPoint(state_.sinrTimeByPci[*row.pci], state_.sinrValueByPci[*row.pci], sec, static_cast<float>(*sinr));
        }
    }

    if (!rows.empty()) {
        const auto& last = rows.back();
        state_.lastTimestampMs = last.timestampMs;
        state_.lastLocation.latitude = last.latitude;
        state_.lastLocation.longitude = last.longitude;
        state_.lastLocation.altitude = last.altitude;
        state_.lastLocation.accuracy = last.accuracy;
    }

    state_.latestSamples.clear();
    state_.latestSamples.reserve(rows.size());
    for (const auto& row : rows) {
        state_.latestSamples.push_back(BuildLiveSample(row));
    }

    return true;
}

TelemetryState TelemetryModel::GetSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void TelemetryModel::SetDatabaseReady(bool ready) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.dbReady = ready;
}

void TelemetryModel::IncrementMalformedPackets() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.malformedPackets++;
}

void TelemetryModel::clearState() {
    const bool dbReady = state_.dbReady;
    const long long malformedPackets = state_.malformedPackets;
    state_ = TelemetryState{};
    state_.dbReady = dbReady;
    state_.malformedPackets = malformedPackets;
    lastPlotTimestampMs_ = 0;
    monotonicPlotTimeSec_ = 0.0f;
}

float TelemetryModel::nextPlotTime(long long currentMs) {
    if (lastPlotTimestampMs_ == 0) {
        lastPlotTimestampMs_ = currentMs;
        monotonicPlotTimeSec_ = 0.0f;
        return 0.0f;
    }

    float delta = static_cast<float>(currentMs - lastPlotTimestampMs_) / 1000.0f;
    if (delta <= 0.0f || delta > 60.0f) {
        delta = 1.0f;
    }
    lastPlotTimestampMs_ = currentMs;
    monotonicPlotTimeSec_ += delta;
    return monotonicPlotTimeSec_;
}

void TelemetryModel::pushPoint(std::vector<float>& xs,
                               std::vector<float>& ys,
                               float x,
                               float y) {
    xs.push_back(x);
    ys.push_back(y);
    if (xs.size() > kMaxPlotPoints) {
        xs.erase(xs.begin());
        ys.erase(ys.begin());
    }
}
