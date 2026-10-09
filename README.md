# NavStudio - 3D BSP Visualizer & Navigation Mesh Editor

[![CI & Build](https://github.com/ZeroDiamond7601/NavStudio/actions/workflows/build.yml/badge.svg)](https://github.com/ZeroDiamond7601/NavStudio/actions)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Platform](https://img.shields.io/badge/Platform-Windows%20(x86)%20%7C%20Linux-brightgreen.svg)]()
[![Release](https://img.shields.io/badge/Version-v1.5.3-orange.svg)](https://github.com/ZeroDiamond7601/NavStudio/releases)

**NavStudio** is a standalone, hardware-accelerated 3D desktop visualizer, navigation mesh editor, and verification suite for **GoldSrc** (`.bsp` v30) maps and **Counter-Strike 1.6 / Condition Zero** (`.nav` v4 & v5) navigation meshes.

It provides real-time OpenGL 3.3 Core rendering with Dear ImGui docking, Valve Hammer Editor-style 3D textured rendering, WAD3 archive texture resolution, interactive mesh modeling tools, collision tracing, and Wavefront OBJ export.

> [!TIP]
> **Game Server Module:** Looking for the server-side AMX Mod X module with runtime pathfinding and Pawn natives? Visit **[amxmodx-navmesh](https://github.com/ZeroDiamond7601/amxmodx-navmesh)**!

---

## Key Features

### 3D Hardware-Accelerated Viewport & Shading Modes (`F4`)
* **Hammer 3D Textured View:** Renders full GoldSrc textures mapped onto BSP brush faces with directional sun lighting, ambient hemisphere fill, and specular highlights matching Valve Hammer Editor.
* **WAD3 Archive Loader & Texture Management:** Parses GoldSrc `WAD3` and `WAD2` archives (`cstrike.wad`, `halflife.wad`, etc.), decoding 4-level miptex lumps (`TYP_MIPTEX`) and 256-color RGB palettes into high-resolution 32-bit RGBA OpenGL textures with automatic mipmap generation and repeat wrapping.
* **Embedded & External Texture Resolution:** Automatically resolves external WAD files referenced by `worldspawn` entity `"wad"` key strings, scans map and game mod directories (`cstrike/`, `valve/`), and extracts embedded textures directly from BSP `LUMP_TEXTURES`.
* **Masked Alpha Cutouts:** Transparent alpha-test masking for `{`-prefixed textures (grates, chainlink fences, ladders, vents) discarding masked pixels for clean silhouettes without sorting artifacts.
* **Solid Clay Shading:** Clean neutral studio clay view with GoldSrc Z-Up hemisphere lighting and brush edge outlines.
* **Wireframe & Ghost / X-Ray:** Wireframe edge view and translucent Ghost view to inspect navigation meshes through complex walls and multi-story geometry.

### Interactive Hammer-Style Mesh Editing Tools
* **3D Position Gizmo & Arrows:** Interactive Red (+X East), Green (+Y North), and Blue (+Z Up) axis arrows and center box handle for precise dragging and elevation control.
* **Move / Grab (`G`):** Areas smoothly glide and track the 3D mouse cursor position on the ground plane, with optional `X`, `Y`, `Z` axis constraints.
* **Radial Scale Tool (`S`):** Screen-space radial scaling tool to resize area dimensions smoothly, with optional `X` or `Y` width/length constraints.
* **Individual Edge Manipulation:** Click and drag any of the 4 perimeter edges (North, East, South, West) to resize area bounds, just like Hammer brush faces.
* **Camera-Facing Edge Extrude (`E` / Shift+Drag Edge):** Extrude edges in the camera-facing direction to spawn new adjacent areas automatically stitched with bidirectional connections.
* **Magnetic Edge Snapping on Move:** Seamlessly snaps moved areas flush against neighboring edges and automatically welds lateral connections.
* **Split Knife Tool (`K` / `Shift+X`):** Interactive cutter with cycling angles (`0°`, `45°`, `90°`, `135°`) to slice areas into connected sub-areas while preserving external connections.
* **Bridge Tool (`B`):** 2-click interactive tool to create intermediate connecting areas between any two nav area edges.
* **Draw Area Marquee Tool (`N`):** 2-click rectangular area creation directly on floor and sloped geometry.
* **Flood Fill Area Tool (`F`):** Click any room floor or brush surface to auto-generate connected nav meshes spanning the room.
* **Coplanar Mesh Optimizer:** 1-click simplification that merges contiguous coplanar areas into unified bounding quads.
* **Sloped Corner Elevation Rotation (`R`):** Rotates area 90 degrees clockwise around center while cycling corner elevations to preserve ramp grade and sloped stairs.
* **Duplicate (`Shift+D`):** Clones selected areas with unique IDs and immediately enters Move mode.
* **Delete (`Delete` / `Backspace` / `X`):** Deletes areas or connections with non-destructive Undo/Redo stack support (`Ctrl+Z` / `Ctrl+Y`).
* **Connection Link Management:** Dedicated connection inspection, two-way toggling (`2`), reverse direction (`R`), and midpoint focus (`F`).
* **Place Names Manager & Dropdown:** Assign designated map locations (e.g. `BombsiteA`, `TSpawn`) from an auto-suggested dropdown of existing places.
* **Marquee Box Selection (`Shift+B`):** 2D screen-space rectangle selection for multi-area manipulation.
* **Wavefront OBJ Export:** Export complete navigation meshes with quad geometry and ladder meshes to `.obj` format for Blender, 3ds Max, or game engines.

### GoldSrc Entity System & Hammer-Style 3D Archetypes (`F3`)
* **Player Spawns:** Humanoid 3D hulls (32x32x72) with head indicators and forward-facing yaw orientation arrows:
  * Counter-Terrorist spawns (`info_player_start`) in CT Blue.
  * Terrorist spawns (`info_player_deathmatch`) in T Red.
  * VIP spawns (`info_vip_start`) in Cyan.
* **Objectives & Hostages:**
  * Bomb targets (`info_bomb_target`, `func_bomb_target`) with hazard markers and red objective diamonds.
  * Hostages (`hostage_entity`) with humanoid green hulls and rescue zones (`info_hostage_rescue`, `func_hostage_rescue`).
  * Buy zones (`func_buyzone`) and escape zones (`func_escapezone`).
* **Light Sources:** 3D yellow diamonds/octahedrons for point lights (`light`), spotlight cones and direction vectors for `light_spot`, and ambient sun icons for `light_environment`.
* **Weapons & Armoury:** 3D item crates and diamonds for `armoury_entity` with automatic weapon name resolution (AK-47, M4A1, AWP, Deagle, etc.) and item count.
* **Ambient Audio:** Magenta emitter nodes for `ambient_generic` with sound file and volume inspect.
* **Brush Entities & Triggers:** Bounding volumes with wireframe edges for `func_door`, `func_button`, `func_breakable`, `func_ladder`, and `trigger_*`.
* **Entity Target Connections:** Visualizes cause-and-effect wiring in 3D by rendering cyan-amber linkage lines with directional mid-point arrows between triggers (`target`) and destination entities (`targetname`).

---

## Directory Structure

```text
NavStudio/
├── .github/
│   └── workflows/
│       └── build.yml               # CI build & release workflow
├── src/
│   ├── math/                       # Vector3, Ray, Matrix4 math library
│   ├── bsp/                        # GoldSrc BSP v30 lump parsing & collision tracing
│   │   ├── bsp_file.h / .cpp
│   │   ├── bsp_entity.h / .cpp
│   │   └── wad_file.h / .cpp
│   ├── nav/                        # Counter-Strike .nav mesh parser, grid & generator
│   │   ├── nav_area.h / .cpp
│   │   ├── nav_file.h / .cpp
│   │   ├── nav_grid.h / .cpp
│   │   ├── nav_path.h / .cpp
│   │   └── nav_generator.h / .cpp
│   ├── cli/                        # Standalone verification & generator CLI tool
│   │   └── main.cpp
│   └── editor/                     # NavStudio 3D desktop visualizer & editor
│       ├── camera/                 # FPS Flycam, Orbit, and 2D cameras
│       ├── commands/               # Command pattern undo/redo engine
│       ├── glad/                   # Embedded OpenGL 3.3 Core loader
│       ├── render/                 # OpenGL shaders, BSP/NAV/Entity/Skybox renderers
│       ├── scene/                  # Editor scene graph, handles, and ray picker
│       ├── ui/                     # Dear ImGui dockspace, inspector, and hierarchy
│       └── main.cpp                # GLFW window and application loop
├── third_party/                    # GLFW & Dear ImGui
├── CMakeLists.txt                  # Standalone CMake build file
├── LICENSE
└── README.md
```

---

## Keyboard Shortcuts

| Shortcut | Action |
| :--- | :--- |
| **Right-Click Drag** | FPS Freelook / Flycam rotation |
| **W, A, S, D** | Move camera (Flycam) |
| **Shift** | Fast camera move speed multiplier |
| **F4** | Cycle Shading Mode (Textured, Clay, Wireframe, Translucent) |
| **F3** | Toggle Entity Archetypes display |
| **G** | Move / Grab selected areas (tracks ground plane) |
| **S** | Radial Scale tool (when area selected) |
| **E** | Extrude camera-facing edge |
| **K / Shift+X** | Knife cut tool (press **R** to cycle angles: 0°, 45°, 90°, 135°) |
| **B** | Bridge tool (2-click edge bridge) |
| **N** | Draw area marquee tool (2-click floor rect) |
| **F** | Flood fill area tool (when nothing selected) / Focus camera (when area/connection selected) |
| **R** | Rotate selected area 90° / Reverse selected connection direction |
| **2** | Toggle selected connection bidirectional / Set Gizmo to Rotate |
| **Shift+D** | Duplicate selected area |
| **Delete / Backspace / X** | Delete selected connection or area |
| **Ctrl+Z / Ctrl+Y** | Undo / Redo |
| **Ctrl+A** | Select All areas |
| **Shift+B** | Toggle Marquee Box Select tool |
| **[ / ]** | Decrease / Increase reference grid size |
| **Shift+W** | Toggle grid snapping |
| **Ctrl+,** | Preferences window |
| **Esc** | Cancel current tool mode / Deselect connection / Clear area selection |

---

## Building from Source

### Requirements
* CMake 3.15+
* C++17 compatible compiler (MSVC 2019/2022 on Windows, GCC/Clang on Linux)

### Windows (Visual Studio)
```powershell
cmake -B build -A Win32 -DBUILD_EDITOR=ON -DBUILD_CLI=ON
cmake --build build --config Release
```
Output binaries:
* `build/Release/nav_editor.exe`
* `build/Release/nav_cli.exe`

### Linux
```bash
sudo apt-get install -y cmake make g++ libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_CLI=ON
cmake --build build --config Release -j$(nproc)
```
Output binary:
* `build/nav_cli`

---

## License
NavStudio is licensed under the [GNU General Public License v3.0](LICENSE).
