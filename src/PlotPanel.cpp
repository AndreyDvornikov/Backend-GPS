#include "PlotPanel.h"

#include <algorithm>
#include <string>

#include "implot.h"

void RenderMetricPlot(const char* title,
                      const std::unordered_map<int, std::vector<float>>& timeByPci,
                      const std::unordered_map<int, std::vector<float>>& valueByPci) {
    if (!ImPlot::BeginPlot(title, ImVec2(-1, 220))) {
        return;
    }

    ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_None);
    ImPlot::SetupAxes("Time (s)", title);
    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);

    int colorIndex = 0;
    for (const auto& entry : valueByPci) {
        const int pci = entry.first;
        const auto itTime = timeByPci.find(pci);
        if (itTime == timeByPci.end()) {
            continue;
        }

        const auto& ys = entry.second;
        const auto& xs = itTime->second;
        const size_t count = std::min(xs.size(), ys.size());
        if (count == 0) {
            continue;
        }

        ImPlot::SetNextLineStyle(ImPlot::GetColormapColor(colorIndex++), 2.0f);
        ImPlot::PlotLine(("PCI " + std::to_string(pci)).c_str(), xs.data(), ys.data(), static_cast<int>(count));
    }

    ImPlot::PopStyleVar();
    ImPlot::EndPlot();
}
