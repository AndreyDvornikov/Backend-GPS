/// @file PayloadParser.h
/// @brief Разбор входящего JSON-пакета в доменные типы телеметрии.

#pragma once

#include <string>
#include <vector>

#include "TelemetryTypes.h"

/// @brief Преобразует строку JSON в набор телеметрических записей.
/// @return true, если пакет соответствует ожидаемой схеме.
bool ParsePayload(const std::string& line,
                  std::vector<DbRow>& rows,
                  LocationData& location,
                  long long& timestampMs);
