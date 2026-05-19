/// @file TelemetryTypes.h
/// @brief Общие типы телеметрии, используемые во всех слоях приложения.

#pragma once

#include <optional>
#include <string>
#include <vector>

/// @brief Координаты и точность позиционирования устройства.
struct LocationData {
    std::optional<double> latitude;
    std::optional<double> longitude;
    std::optional<double> altitude;
    std::optional<double> accuracy;
};

/// @brief Упрощённый сэмпл для отображения в текущей таблице GUI.
struct LiveSample {
    std::string networkType;
    std::optional<int> pci;
    std::optional<long long> cellIdentity;
    std::optional<int> rsrp;
    std::optional<int> rssi;
    std::optional<int> sinr;
};

/// @brief Полная запись телеметрии для БД и внутренней обработки.
struct DbRow {
    long long timestampMs = 0;
    std::optional<double> latitude;
    std::optional<double> longitude;
    std::optional<double> altitude;
    std::optional<double> accuracy;
    std::string networkType;
    bool registered = false;
    std::optional<int> band;
    std::optional<long long> cellIdentity;
    std::optional<long long> nci;
    std::optional<int> earfcn;
    std::optional<int> arfcn;
    std::optional<int> nrarfcn;
    std::optional<std::string> mcc;
    std::optional<std::string> mnc;
    std::optional<int> pci;
    std::optional<int> tac;
    std::optional<int> lac;
    std::optional<int> bsic;
    std::optional<int> psc;
    std::optional<int> asuLevel;
    std::optional<int> cqi;
    std::optional<int> rsrp;
    std::optional<int> rsrq;
    std::optional<int> rssi;
    std::optional<int> rssnr;
    std::optional<long long> timingAdvance;
    std::optional<int> ssRsrp;
    std::optional<int> ssRsrq;
    std::optional<int> ssSinr;
    std::string rawJson;
};
