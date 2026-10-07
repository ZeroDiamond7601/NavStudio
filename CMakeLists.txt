cmake_minimum_required(VERSION 3.15)
project(NavMeshCore VERSION 1.4.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_AMXX_MODULE "Build AMX Mod X module library" ON)
option(BUILD_CLI "Build standalone command-line verification tool" ON)
option(BUILD_EDITOR "Build NavStudio standalone GUI editor" OFF)

# Force 32-bit target for Linux when building AMXX module
if(UNIX AND NOT APPLE)
    if(BUILD_AMXX_MODULE)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -m32 -static-libgcc -static-libstdc++")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -m32 -static-libgcc -static-libstdc++")
    endif()
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
    # Statically link MSVC runtime (/MT) for standalone portability on game servers
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Include directories for SDK
set(SDK_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/amxmodx/public
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/amxmodx/public/sdk
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/metamod
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/hlsdk/common
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/hlsdk/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/hlsdk/dlls
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/hlsdk/pm_shared
)

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
)

add_library(nav_core STATIC ${CORE_SOURCES})
target_include_directories(nav_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(nav_core PUBLIC Threads::Threads)

# AMXX Module Target
if(BUILD_AMXX_MODULE)
    set(MODULE_SOURCES
        src/nav/async_pathfinder.cpp
        src/amxx/amxx_api.cpp
        src/amxx/amxx_bsp_natives.cpp
        src/amxx/amxx_nav_natives.cpp
        sdk/amxmodx/public/sdk/amxxmodule.cpp
    )

    add_library(navmesh_amxx SHARED ${MODULE_SOURCES})
    target_include_directories(navmesh_amxx PRIVATE ${SDK_INCLUDES})
    target_link_libraries(navmesh_amxx PRIVATE nav_core)

    if(MSVC)
        target_compile_definitions(navmesh_amxx PRIVATE _USRDLL)
        set_target_properties(navmesh_amxx PROPERTIES
            OUTPUT_NAME "navmesh_amxx"
            PREFIX ""
            SUFFIX ".dll"
        )
    else()
        set_target_properties(navmesh_amxx PROPERTIES
            OUTPUT_NAME "navmesh_amxx_i386"
            PREFIX ""
            SUFFIX ".so"
        )
    endif()
endif()

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
