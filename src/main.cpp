#include <GL/glew.h>
#include <SDL.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"

using json = nlohmann::json;
using namespace std;

struct TopApp {
    int uid;
    long long bytes_used;
};

struct CellTower {
    string type;
    string operator_name;
    int rsrp, rsrq, rssi, cqi, rssnr, ta, asu;
    int pci, tac, earfcn, mcc, mnc, band;     
    long long cid;                            
};

struct RadarData {
    double lat = 0.0, lon = 0.0, alt = 0.0, accuracy = 0.0;
    long long time = 0;
    vector<CellTower> towers;
    long long total_rx = 0, total_tx = 0;
    double mean_bytes = 0.0, std_dev = 0.0;
    vector<TopApp> top_apps;
};

void SetupCoffeeStyle() {
    ImVec4* colors = ImGui::GetStyle().Colors;
    ImVec4 color_espresso = ImVec4(0.16f, 0.13f, 0.10f, 1.00f); 
    ImVec4 color_mocha    = ImVec4(0.29f, 0.23f, 0.18f, 1.00f); 
    ImVec4 color_cream    = ImVec4(0.95f, 0.90f, 0.85f, 1.00f); 
    ImVec4 color_foam     = ImVec4(0.76f, 0.60f, 0.42f, 1.00f); 

    colors[ImGuiCol_Text] = color_espresso;
    colors[ImGuiCol_WindowBg] = color_cream;
    colors[ImGuiCol_PopupBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.98f);
    colors[ImGuiCol_TitleBg] = color_mocha;
    colors[ImGuiCol_TitleBgActive] = color_mocha;
    colors[ImGuiCol_TitleBgCollapsed] = color_mocha;
    colors[ImGuiCol_Button] = color_foam;         
    colors[ImGuiCol_ButtonHovered] = color_mocha; 
    colors[ImGuiCol_ButtonActive] = color_espresso;
    colors[ImGuiCol_CheckMark] = color_mocha;
    colors[ImGuiCol_SliderGrab] = color_mocha;
    colors[ImGuiCol_SliderGrabActive] = color_espresso;
    colors[ImGuiCol_Header] = color_foam;
    colors[ImGuiCol_HeaderHovered] = color_mocha; 
    colors[ImGuiCol_HeaderActive] = color_mocha;
    colors[ImGuiCol_PlotLines] = color_espresso;  
    colors[ImGuiCol_PlotLinesHovered] = color_mocha;
    ImPlotStyle& plotStyle = ImPlot::GetStyle();
    plotStyle.Colors[ImPlotCol_PlotBg] = color_cream; 
    plotStyle.Colors[ImPlotCol_FrameBg] = color_cream; 
    plotStyle.Colors[ImPlotCol_PlotBorder] = color_mocha; 
    plotStyle.Colors[ImPlotCol_AxisText] = color_espresso; 
}

RadarData g_radarData;
mutex g_dataMutex;
atomic<bool> g_appRunning{true};

void zmq_listener_thread() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::pull);
    socket.bind("tcp://*:5555");
    socket.set(zmq::sockopt::rcvtimeo, 1000); 

    while (g_appRunning) {
        zmq::message_t request;
        auto res = socket.recv(request, zmq::recv_flags::none);
        if (res) {
            string msg_str(static_cast<char*>(request.data()), request.size());
            try {
                auto j = json::parse(msg_str);
                lock_guard<mutex> lock(g_dataMutex);
                g_radarData.lat = j.value("latitude", 0.0);
                g_radarData.lon = j.value("longitude", 0.0);
                g_radarData.alt = j.value("altitude", 0.0);
                g_radarData.time = j.value("time", 0LL);

                g_radarData.towers.clear();
                if (j.contains("network") && j["network"].contains("LTE")) {
                    for (auto& item : j["network"]["LTE"]) {
                        CellTower tower;
                        tower.type = "LTE";
                        tower.operator_name = item.value("operator", "Unknown");
                        tower.rsrp = item.value("rsrp", -140);
                        tower.rsrq = item.value("rsrq", 0);
                        tower.rssi = item.value("rssi", 0);
                        tower.cqi = item.value("cqi", 0);
                        tower.rssnr = item.value("rssnr", 0);
                        tower.ta = item.value("timing_advance", 0);
                        tower.asu = item.value("asu_level", 0);
                        tower.pci = item.value("pci", 0);
                        tower.tac = item.value("tac", 0);
                        tower.earfcn = item.value("earfcn", 0);
                        tower.cid = item.value("ci", 0LL);
                        g_radarData.towers.push_back(tower);
                    }
                }
                g_radarData.accuracy = j.value("accuracy", 0.0);

                if (j.contains("traffic")) {
                    auto& t = j["traffic"];
                    g_radarData.total_rx = t.value("total_rx", 0LL);
                    g_radarData.total_tx = t.value("total_tx", 0LL);
                    g_radarData.mean_bytes = t.value("mean", 0.0);
                    g_radarData.std_dev = t.value("std_dev", 0.0);
                    g_radarData.top_apps.clear();
                    if (t.contains("top_apps_2sigma")) {
                        for (auto& item : t["top_apps_2sigma"]) {
                            TopApp app;
                            app.uid = item.value("uid", 0);
                            app.bytes_used = item.value("bytes", 0LL);
                            g_radarData.top_apps.push_back(app);
                        }
                    }
                }
            } catch (const exception& e) {
                cerr << "err: " << e.what() << endl;
            }
        }
    }
}

void run_gui() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return;
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    SDL_Window* window = SDL_CreateWindow("Radar Backend", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 768, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

    if (glewInit() != GLEW_OK) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;        

    SetupCoffeeStyle();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    while (g_appRunning) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) g_appRunning = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_None);

        {
            RadarData localData;
            {
                lock_guard<mutex> lock(g_dataMutex);
                localData = g_radarData;
            }

            ImGui::Begin("Radar Telemetry");

            if (ImGui::CollapsingHeader("GPS Data", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("Lat: %.6f | Lon: %.6f", localData.lat, localData.lon);
                ImGui::Text("Alt: %.2f m", localData.alt);
                ImGui::Text("Time: %lld", localData.time);
            }

            if (ImGui::CollapsingHeader("Cells", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (localData.towers.empty()) {
                    ImGui::Text("Searching...");
                } else {
                    if (ImGui::BeginTable("Towers", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY, ImVec2(0, 300))) {
                        ImGui::TableSetupColumn("Operator/CID");
                        ImGui::TableSetupColumn("PCI/TAC");
                        ImGui::TableSetupColumn("EARFCN");
                        ImGui::TableSetupColumn("RSRP");
                        ImGui::TableSetupColumn("RSRQ");
                        ImGui::TableSetupColumn("RSSI/SNR");
                        ImGui::TableSetupColumn("CQI");
                        ImGui::TableSetupColumn("TA");
                        ImGui::TableHeadersRow();

                        for (const auto& t : localData.towers) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0); ImGui::Text("%s\n%lld", t.operator_name.c_str(), t.cid);
                            ImGui::TableSetColumnIndex(1); ImGui::Text("%d/%d", t.pci, t.tac);
                            ImGui::TableSetColumnIndex(2); ImGui::Text("%d", t.earfcn);
                            ImGui::TableSetColumnIndex(3); 
                            if (t.rsrp > -90) ImGui::TextColored(ImVec4(0,1,0,1), "%d", t.rsrp);
                            else ImGui::TextColored(ImVec4(1,0,0,1), "%d", t.rsrp);
                            ImGui::TableSetColumnIndex(4); ImGui::Text("%d", t.rsrq);
                            ImGui::TableSetColumnIndex(5); ImGui::Text("%d/%d", t.rssi, t.rssnr);
                            ImGui::TableSetColumnIndex(6); ImGui::Text("%d", t.cqi);
                            ImGui::TableSetColumnIndex(7); ImGui::Text("%d", t.ta);
                        }
                        ImGui::EndTable();
                    }
                }
            }

            if (ImGui::CollapsingHeader("Traffic")) {
                ImGui::Text("RX: %lld | TX: %lld", localData.total_rx, localData.total_tx);
                ImGui::Text("Mean: %.1f | StdDev: %.1f", localData.mean_bytes, localData.std_dev);
                if (!localData.top_apps.empty()) {
                    for (auto& a : localData.top_apps) ImGui::BulletText("UID %d: %lld bytes", a.uid, a.bytes_used);
                }
            }
            ImGui::End();
        }

        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

int main(int argc, char** argv) {
    thread net(zmq_listener_thread);
    run_gui();
    g_appRunning = false;
    if (net.joinable()) net.join();
    return 0;
}