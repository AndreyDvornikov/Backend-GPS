/// @file PlotPanel.h
/// @brief Отрисовка графиков телеметрии по PCI.

#pragma once

#include <unordered_map>
#include <vector>

/// @brief Рисует один график метрики для нескольких PCI.
void RenderMetricPlot(const char* title,
                      const std::unordered_map<int, std::vector<float>>& timeByPci,
                      const std::unordered_map<int, std::vector<float>>& valueByPci);
