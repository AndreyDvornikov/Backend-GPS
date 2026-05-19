/// @file TelemetryPanel.h
/// @brief Отрисовка основного окна телеметрии.

#pragma once

#include "Database.h"
#include "TelemetryModel.h"

/// @brief Рисует окно телеметрии и элементы управления загрузкой истории.
void RenderTelemetryPanel(Database& database, TelemetryModel& model, const TelemetryState& state, int& loadSampleCount);
