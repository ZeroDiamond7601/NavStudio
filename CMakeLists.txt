cmake_minimum_required(VERSION 3.15)
project(NavMeshCore VERSION 1.0.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_AMXX_MODULE "Build AMX Mod X module library" ON)
option(BUILD_CLI "Build standalone command-line verification tool" ON)

# Force 32-bit target for Linux when building AMXX module
if(UNIX AND NOT APPLE)
    if(BUILD_AMXX_MODULE)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -m32")
    endif()
    add_compile_options(-Wall -Wno-narrowing -Wno-write-strings -fPIC)
endif()

# Windows MSVC configuration
if(MSVC)
    add_compile_definitions(
        WIN32
        _WINDOWS
        _USRDLL
        _CRT_SECURE_NO_WARNINGS
        HAVE_STDINT_H
        NOMINMAX
        NO_MSVC8_AUTO_COMPAT
    )
    # Statically link MSVC runtime (/MT) for standalone portability on game servers
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Include directories
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

# Core sources for BSP and NAV engines (pure algorithms & geometry, no AMXX dependencies)
set(CORE_SOURCES
    src/bsp/bsp_entity.cpp
    src/bsp/bsp_file.cpp
    src/nav/nav_area.cpp
    src/nav/nav_grid.cpp
    src/nav/nav_path.cpp
    src/nav/nav_file.cpp
)

find_package(Threads REQUIRED)

# AMXX Module Target
if(BUILD_AMXX_MODULE)
    set(MODULE_SOURCES
        ${CORE_SOURCES}
        src/nav/async_pathfinder.cpp
        src/amxx/amxx_api.cpp
        src/amxx/amxx_bsp_natives.cpp
        src/amxx/amxx_nav_natives.cpp
        sdk/amxmodx/public/sdk/amxxmodule.cpp
    )

    add_library(navmesh_amxx SHARED ${MODULE_SOURCES})

    target_include_directories(navmesh_amxx PRIVATE ${SDK_INCLUDES})
    target_link_libraries(navmesh_amxx PRIVATE Threads::Threads)

    if(WIN32)
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
    set(CLI_SOURCES
        ${CORE_SOURCES}
        src/cli/main.cpp
    )

    add_executable(nav_cli ${CLI_SOURCES})
    target_include_directories(nav_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    set_target_properties(nav_cli PROPERTIES
        OUTPUT_NAME "nav_cli"
    )
endif()
