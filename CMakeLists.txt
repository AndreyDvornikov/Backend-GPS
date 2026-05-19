find_package(CURL REQUIRED)
cmake_minimum_required(VERSION 3.14)
project(MyProjectName)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/external)
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(Threads REQUIRED)
find_package(PostgreSQL REQUIRED)
include_directories(${GLEW_INCLUDE_DIRS} ${INCLUDE_DIR} ${PostgreSQL_INCLUDE_DIRS})

add_library(imgui STATIC
    ${THIRD_PARTY_DIR}/imgui/imgui.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_demo.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl2.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${THIRD_PARTY_DIR}/imgui
    ${THIRD_PARTY_DIR}/imgui/backends
)
target_compile_options(imgui PRIVATE -fPIC)

add_library(implot STATIC
    ${THIRD_PARTY_DIR}/implot/implot.cpp
    ${THIRD_PARTY_DIR}/implot/implot_demo.cpp
    ${THIRD_PARTY_DIR}/implot/implot_items.cpp
)

target_include_directories(implot PUBLIC ${THIRD_PARTY_DIR}/implot)
target_link_libraries(implot PRIVATE imgui)
target_compile_options(implot PRIVATE -fPIC)

add_executable(main
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/CurlUtils.cpp
    ${SRC_DIR}/Database.cpp
    ${SRC_DIR}/GuiApp.cpp
    ${SRC_DIR}/MapWidget.cpp
    ${SRC_DIR}/PayloadParser.cpp
    ${SRC_DIR}/PlotPanel.cpp
    ${SRC_DIR}/TelemetryHelpers.cpp
    ${SRC_DIR}/TelemetryModel.cpp
    ${SRC_DIR}/TelemetryPanel.cpp
    ${SRC_DIR}/TelemetryServer.cpp
    ${SRC_DIR}/TileStorage.cpp
)

target_link_libraries(main PRIVATE CURL::libcurl
    imgui
    implot
    ${PostgreSQL_LIBRARIES}
    ${SDL2_LIBRARIES}
    ${OPENGL_LIBRARIES}
    ${GLEW_LIBRARIES}
    Threads::Threads
)
