cmake_minimum_required(VERSION 3.15)
project(NavStudio VERSION 1.6.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_EDITOR "Build NavStudio standalone GUI editor" ON)
option(BUILD_CLI "Build standalone command-line verification tool" ON)

# Linux compiler options
if(UNIX AND NOT APPLE)
    add_compile_options(-Wall -Wno-narrowing -Wno-write-strings -fPIC)
endif()

# Windows MSVC configuration
if(MSVC)
    add_compile_definitions(
        WIN32
        _WINDOWS
        _CRT_SECURE_NO_WARNINGS
        HAVE_STDINT_H
        NOMINMAX
        NO_MSVC8_AUTO_COMPAT
    )
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(Threads REQUIRED)

# Core sources for BSP and NAV engines (pure algorithms & geometry, reusable by all targets)
set(CORE_SOURCES
    src/bsp/bsp_entity.cpp
    src/bsp/bsp_file.cpp
    src/bsp/wad_file.cpp
    src/nav/nav_area.cpp
    src/nav/nav_grid.cpp
    src/nav/nav_path.cpp
    src/nav/nav_file.cpp
    src/nav/nav_generator.cpp
    src/waypoint/waypoint_graph.cpp
    src/waypoint/waypoint_nav_converter.cpp
    src/waypoint/ebot_generator.cpp
)

add_library(nav_core STATIC ${CORE_SOURCES})
target_include_directories(nav_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(nav_core PUBLIC Threads::Threads)

# Standalone CLI / Verification Target
if(BUILD_CLI)
    add_executable(nav_cli src/cli/main.cpp)
    target_link_libraries(nav_cli PRIVATE nav_core)
    set_target_properties(nav_cli PROPERTIES
        OUTPUT_NAME "nav_cli"
    )
endif()

# Standalone GUI Editor (NavStudio) Target
if(BUILD_EDITOR)
    find_package(OpenGL REQUIRED)

    # GLFW windowing
    if(NOT TARGET glfw)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/CMakeLists.txt)
            add_subdirectory(third_party/glfw)
        else()
            include(FetchContent)
            FetchContent_Declare(
                glfw
                GIT_REPOSITORY https://github.com/glfw/glfw.git
                GIT_TAG 3.4
            )
            FetchContent_MakeAvailable(glfw)
        endif()
    endif()

    # Dear ImGui (Docking branch)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.cpp)
        include(FetchContent)
        FetchContent_Declare(
            imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG docking
        )
        FetchContent_MakeAvailable(imgui)
        set(IMGUI_DIR ${imgui_SOURCE_DIR})
    else()
        set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
    endif()

    set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )

    set(EDITOR_SOURCES
        src/editor/main.cpp
        src/editor/camera/camera.cpp
        src/editor/render/shader.cpp
        src/editor/render/bsp_renderer.cpp
        src/editor/render/texture_manager.cpp
        src/editor/render/image_loader.cpp
        src/editor/render/skybox_renderer.cpp
        src/editor/render/nav_renderer.cpp
        src/editor/render/waypoint_renderer.cpp
        src/editor/render/gizmo_renderer.cpp
        src/editor/render/entity_renderer.cpp
        src/editor/scene/editor_scene.cpp
        src/editor/scene/scene_picker.cpp
        src/editor/ui/editor_ui.cpp
        src/editor/ui/file_dialog.cpp
        src/editor/glad/src/glad.c
        ${IMGUI_SOURCES}
    )

    if(WIN32)
        list(APPEND EDITOR_SOURCES src/editor/res/nav_editor.rc)
    endif()

    add_executable(nav_editor ${EDITOR_SOURCES})
    target_include_directories(nav_editor PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/editor
        ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/res
        ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/glad/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
    )
    target_link_libraries(nav_editor PRIVATE
        nav_core
        glfw
        OpenGL::GL
        Threads::Threads
    )

    if(WIN32)
        target_link_libraries(nav_editor PRIVATE comdlg32 shell32 ole32)
        set_target_properties(nav_editor PROPERTIES OUTPUT_NAME "nav_editor")
    endif()
endif()
