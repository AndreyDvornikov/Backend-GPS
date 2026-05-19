#include "TelemetryPanel.h"

#include "PlotPanel.h"
#include "imgui.h"

void RenderTelemetryPanel(Database& database, TelemetryModel& model, const TelemetryState& state, int& loadSampleCount) {
    ImGui::Begin("Radar Telemetry");
    ImGui::Text("Packets: %lld (malformed: %lld)", state.receivedPackets, state.malformedPackets);
    ImGui::Text("DB ready: %s", state.dbReady ? "yes" : "no");
    ImGui::Text("Last timestamp_ms: %lld", state.lastTimestampMs);
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("Load N samples", &loadSampleCount);
    if (loadSampleCount < 1) {
        loadSampleCount = 1;
    }
    if (ImGui::Button("Load from DB")) {
        model.LoadFromDatabase(database, loadSampleCount);
    }

    if (state.lastLocation.latitude && state.lastLocation.longitude) {
        ImGui::Text("Location: %.6f, %.6f", *state.lastLocation.latitude, *state.lastLocation.longitude);
    }
    if (state.lastLocation.altitude) {
        ImGui::Text("Altitude: %.2f", *state.lastLocation.altitude);
    }
    if (state.lastLocation.accuracy) {
        ImGui::Text("Accuracy: %.2f", *state.lastLocation.accuracy);
    }

    if (ImGui::CollapsingHeader("Current samples", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("samples", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("PCI");
            ImGui::TableSetupColumn("Cell ID");
            ImGui::TableSetupColumn("RSRP");
            ImGui::TableSetupColumn("RSSI");
            ImGui::TableSetupColumn("SINR");
            ImGui::TableHeadersRow();

            for (const auto& sample : state.latestSamples) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(sample.networkType.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", sample.pci.value_or(-1));
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%lld", sample.cellIdentity.value_or(-1));
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%d", sample.rsrp.value_or(-999));
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%d", sample.rssi.value_or(-999));
                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%d", sample.sinr.value_or(-999));
            }

            ImGui::EndTable();
        }
    }

    RenderMetricPlot("RSRP", state.rsrpTimeByPci, state.rsrpValueByPci);
    RenderMetricPlot("RSSI", state.rssiTimeByPci, state.rssiValueByPci);
    RenderMetricPlot("SINR", state.sinrTimeByPci, state.sinrValueByPci);
    ImGui::End();
}
