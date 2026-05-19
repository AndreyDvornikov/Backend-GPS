/// @file GuiApp.h
/// @brief Точка входа в графическое приложение мониторинга.

#pragma once

#include <atomic>

#include "Database.h"
#include "TelemetryModel.h"

/// @brief Запускает GUI и обрабатывает пользовательский цикл приложения.
void RunGui(std::atomic<bool>& running, Database& database, TelemetryModel& model);
