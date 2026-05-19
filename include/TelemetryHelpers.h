/// @file TelemetryHelpers.h
/// @brief Вспомогательные функции для нормализации и чтения метрик телеметрии.

#pragma once

#include <optional>

#include "TelemetryTypes.h"

/// @brief Проверяет, что значение метрики пригодно для графика.
bool IsValidMetricValue(const std::optional<int>& value);

/// @brief Выбирает предпочтительное значение SINR из доступных полей.
std::optional<int> SelectPreferredSinr(const DbRow& row);

/// @brief Преобразует запись БД в компактный GUI-сэмпл.
LiveSample BuildLiveSample(const DbRow& row);
