# Makefile for Linux 32-bit AMX Mod X module

CXX = g++
CXXFLAGS = -m32 -fPIC -shared -O3 -Wall -Wno-narrowing -Wno-write-strings -std=c++17 -pthread

SDK_AMXX = sdk/amxmodx/public
SDK_METAMOD = sdk/metamod
SDK_HLSDK = sdk/hlsdk

INCLUDES = -Isrc \
           -I$(SDK_AMXX) \
           -I$(SDK_AMXX)/sdk \
           -I$(SDK_METAMOD) \
           -I$(SDK_HLSDK)/common \
           -I$(SDK_HLSDK)/engine \
           -I$(SDK_HLSDK)/dlls \
           -I$(SDK_HLSDK)/pm_shared

SRC = src/bsp/bsp_entity.cpp \
      src/bsp/bsp_file.cpp \
      src/nav/nav_area.cpp \
      src/nav/nav_grid.cpp \
      src/nav/nav_path.cpp \
      src/nav/nav_file.cpp \
      src/nav/async_pathfinder.cpp \
      src/amxx/amxx_api.cpp \
      src/amxx/amxx_bsp_natives.cpp \
      src/amxx/amxx_nav_natives.cpp \
      $(SDK_AMXX)/sdk/amxxmodule.cpp

TARGET = navmesh_amxx_i386.so

all: $(TARGET) cli

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRC) -o $(TARGET)

cli:
	$(CXX) -O3 -std=c++17 -pthread -Isrc \
		src/bsp/bsp_entity.cpp \
		src/bsp/bsp_file.cpp \
		src/nav/nav_area.cpp \
		src/nav/nav_grid.cpp \
		src/nav/nav_path.cpp \
		src/nav/nav_file.cpp \
		src/nav/async_pathfinder.cpp \
		src/cli/main.cpp \
		-o nav_cli

clean:
	rm -f $(TARGET) nav_cli
