#include "GuiApp.h"

#include <GL/glew.h>
#include <SDL.h>

#include "MapWidget.h"
#include "TelemetryPanel.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "implot.h"

namespace {
void SetupStyle() {
    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.94f, 0.97f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.17f, 0.22f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.19f, 0.22f, 0.29f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.19f, 0.24f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.22f, 0.26f, 0.34f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.35f, 0.45f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.22f, 0.26f, 0.36f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.16f, 0.18f, 0.23f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.35f, 0.40f, 0.52f, 0.70f);
    colors[ImGuiCol_Button] = ImVec4(0.23f, 0.40f, 0.75f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.32f, 0.50f, 0.85f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.34f, 0.66f, 1.00f);

    ImPlotStyle& plotStyle = ImPlot::GetStyle();
    plotStyle.Colors[ImPlotCol_PlotBg] = ImVec4(0.13f, 0.15f, 0.20f, 1.00f);
    plotStyle.Colors[ImPlotCol_FrameBg] = ImVec4(0.16f, 0.18f, 0.24f, 1.00f);
    plotStyle.Colors[ImPlotCol_PlotBorder] = ImVec4(0.42f, 0.47f, 0.60f, 0.85f);
    plotStyle.Colors[ImPlotCol_AxisText] = ImVec4(0.90f, 0.92f, 0.96f, 1.00f);
    plotStyle.Colors[ImPlotCol_AxisGrid] = ImVec4(0.65f, 0.70f, 0.82f, 0.20f);
    plotStyle.Colors[ImPlotCol_AxisTick] = ImVec4(0.80f, 0.84f, 0.92f, 0.80f);
    plotStyle.LineWeight = 2.0f;
}
}

void RunGui(std::atomic<bool>& running, Database& database, TelemetryModel& model) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        return;
    }

    const char* glslVersion = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    SDL_Window* window = SDL_CreateWindow("Radar Backend",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          1200,
                                          840,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                              SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1);

    if (glewInit() != GLEW_OK) {
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

    SetupStyle();
    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init(glslVersion);

    int loadSampleCount = 500;
    MapWidget mapWidget(running);

    while (running.load()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        const TelemetryState state = model.GetSnapshot();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        RenderTelemetryPanel(database, model, state, loadSampleCount);
        mapWidget.Render(state.lastLocation);

        ImGui::Render();
        glViewport(0, 0, static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y));
        glClearColor(0.10f, 0.10f, 0.10f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
