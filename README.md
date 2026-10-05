# NavMesh Core - AMX Mod X Navigation System

[![CI & Build](https://github.com/ZeroDiamond7601/amxmodx-navmesh/actions/workflows/build.yml/badge.svg)](https://github.com/ZeroDiamond7601/amxmodx-navmesh/actions)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Platform](https://img.shields.io/badge/Platform-Debian%20%7C%20Linux%20%7C%20Windows%20(x86)-brightgreen.svg)]()
[![Compatibility](https://img.shields.io/badge/AMXX-1.8.x%20--%201.10.x%20%7C%20ReHLDS-orange.svg)]()

**NavMesh Core** is a high-performance C++ module for **AMX Mod X** (GoldSrc / Counter-Strike 1.6 / Counter-Strike: Condition Zero). It provides direct, memory-mapped access to GoldSrc **`.bsp`** maps (Version 30) and Counter-Strike **`.nav`** navigation meshes (Versions 4 & 5), featuring ultra-fast collision tracing, spatial partitioning, full **A\* pathfinding** with portal smoothing, a standalone verification CLI, and **NavStudio**, a hardware-accelerated 3D desktop visualizer and mesh editor.

---

## Features

### GoldSrc BSP Engine (`.bsp`)
* **Direct Lump Parsing:** Reads and caches GoldSrc BSP lumps (Planes, Nodes, Clipnodes, Leaves, Models, Visibility, Textures, Texinfo, Faces, Vertices, Edges, Surfedges, Marksurfaces, Entities, Lighting) directly in memory with strict bounds safety.
* **Exact Ray & Hull Tracing:** Supports exact Hull 0 (Point / Bullets) and Hulls 1-3 (Player standing, crouch, large hull) raycasting against world geometry and brush models (`*1`, `*2`, doors, breakables).
* **Surface Lightmap & Illumination Sampling:** Samples exact luxel lighting along arbitrary rays or at 3D face coordinates with bilinear interpolation across the 16-unit lightmap grid, returning both scalar brightness and full RGB channels.
* **Material Classification & Texture Flags:** Automatically parses and classifies surfaces into physical engine material types (`MAT_CONCRETE`, `MAT_METAL`, `MAT_WOOD`, `MAT_VENT`, `MAT_GRATE`, `MAT_TILE`, `MAT_SLOSH`, `MAT_GLASS`, `MAT_FLESH`, etc.) and detects transparency, fluid, sky, and animated texture flags.
* **Face Polygon Vertices Extraction:** Extracts ordered 3D world polygon vertices for any face in the map via surfedge/edge index chains.
* **Worldspawn Metadata:** Direct extraction of environment skybox prefix (`skyname`), map title, and referenced WAD files.
* **Leaf Marksurfaces & Spatial Partitions:** Queries the faces touching any leaf via marksurfaces indices.
* **Entity Target Graphs:** High-speed lookup of entity linkages by `target` and `targetname` attributes.
* **Visibility & Audibility Engine:** PVS (Potentially Visible Set) and PAS (Potentially Audible Set) decompressed bitmasks, direct point-to-point visibility and audibility checks, and visible leaf count queries.
* **Map Geometry & Bounds:** Fast access to world bounds, leaf bounding boxes, contents codes, ambient audio levels, planes, and node/leaf counts.
* **Entity & Texture Metadata:** Key-value property lookups on BSP entities and texture dimension queries.

### Navigation Mesh System (`.nav`)
* **Format Compatibility:** Fully parses Counter-Strike 1.6 / Condition Zero `.nav` files (Magic `0xFEEDFACE`, Versions 4 and 5).
* **Bilinear Quad Elevation:** Calculates exact ground elevation at any `(x, y)` coordinate inside an area quad using 4-corner bilinear interpolation.
* **Spatial Grid:** Uniform 2D hash grid (cell size 300 units) providing O(1) spatial queries for nearest area lookups.
* **Tactical Data:** Access to hiding spots (cover, sniper spots), approach areas, encounter paths, and named map places (e.g. `"BombsiteA"`, `"TSpawn"`).
* **Ladder Support:** Automatically extracts and links `func_ladder` entities from the BSP into the navigation graph.

### A* Pathfinding Engine
* **Portal Waypoints:** Uses portal boundaries between adjacent areas so calculated paths route cleanly through doorways instead of blindly aiming for area centers.
* **Custom Traversal Costs:** Supports options to avoid crouching, avoid jumping, or prefer paths with cover.
* **Line-of-Sight Smoothing:** Optional string-pulling optimization that checks line-of-sight against BSP geometry to remove redundant waypoints.
* **Active Path Handles:** High-performance path instance management exposed directly to AMXX Pawn scripting.
* **Asynchronous Offloaded Pathfinding:** Non-blocking worker threads handle multi-route A* calculations and dispatch results via event forwards (`nav_on_path_computed`).

### NavStudio: 3D BSP Visualizer & NavMesh Editor
* **Hardware-Accelerated 3D Viewport:** OpenGL 3.3 Core rendering with Dear ImGui docking interface.
* **Multiple Shading Modes:** Solid clay shading, wireframe edge display, and Ghost / X-Ray mode (translucent BSP walls to view navigation meshes through floors and ceilings).
* **NavMesh Inspection:** Color-coded area quads (Normal, Crouch, Jump, Blocked), directional connection lines (cyan for two-way, magenta for one-way), and ladder bounding boxes.
* **Interactive Editing Tools:**
  * **Floor Snapping:** Casts downward rays to the BSP collision hull to ground floating area corner vertices.
  * **Attribute Flag Editor:** Toggle `NAV_ATTR_CROUCH`, `NAV_ATTR_JUMP`, `NAV_ATTR_PRECISE`, and `NAV_ATTR_NO_JUMP`.
  * **Place Name Manager:** Search, filter, and assign designated map locations.
  * **Area Connections:** View, link, and toggle area traversability.
* **Undo / Redo System:** Command-pattern history stack (`Ctrl+Z` / `Ctrl+Y`) for non-destructive mesh authoring.
* **Area Hierarchy Search:** Filterable tree view of all areas with instant camera focus (`F` key).

---

## Directory Structure

```text
nav_module/
├── .github/
│   └── workflows/
│       └── build.yml               # Debian container & Windows CI workflow
├── sdk/                            # Self-contained AMXX / Metamod / HLSDK headers
│   ├── amxmodx/
│   ├── metamod/
│   └── hlsdk/
├── src/
│   ├── math/
│   │   └── vector3.h               # Vector math library
│   ├── bsp/
│   │   ├── bsp_types.h             # GoldSrc BSP v30 lump definitions
│   │   ├── bsp_file.h / .cpp       # BSP loader and ray casting engine
│   │   └── bsp_entity.h / .cpp     # Map entity parsing
│   ├── nav/
│   │   ├── nav_types.h             # Navigation mesh constants and structs
│   │   ├── nav_area.h / .cpp       # Area quad elevation and connections
│   │   ├── nav_grid.h / .cpp       # Spatial uniform 2D grid
│   │   ├── nav_path.h / .cpp       # A* Pathfinding engine
│   │   └── nav_file.h / .cpp       # .nav file parser and serializer
│   ├── amxx/
│   │   ├── amxx_api.h / .cpp       # Module lifecycle and exports
│   │   ├── amxx_bsp_natives.cpp    # BSP Pawn natives
│   │   └── amxx_nav_natives.cpp    # NAV Pawn natives
│   ├── cli/
│   │   └── main.cpp                # Standalone verification & benchmark CLI
│   └── editor/                     # NavStudio 3D desktop visualizer & editor
│       ├── camera/                 # FPS Flycam, Orbit, and 2D cameras
│       ├── commands/               # Command pattern undo/redo engine
│       ├── glad/                   # Embedded OpenGL 3.3 Core loader
│       ├── math/                   # Matrix4 MVP and unprojection math
│       ├── render/                 # BSP and NavMesh OpenGL renderers and shaders
│       ├── scene/                  # Scene manager and raycast picker
│       ├── ui/                     # Dear ImGui dockspace, inspector, and hierarchy
│       └── main.cpp                # GLFW window and application loop
├── scripting/
│   ├── include/
│   │   └── navmesh.inc             # Pawn include file with documentation
│   └── navmesh_test.sma            # Example test plugin
├── CMakeLists.txt                  # Modular CMake configuration (nav_core, amxx, cli, editor)
├── Makefile                        # Linux direct Makefile
├── .gitignore
├── LICENSE
└── README.md
```

---

## Installation

1. Download the latest release from the [GitHub Releases](https://github.com/ZeroDiamond7601/amxmodx-navmesh/releases) page:
   * **Linux Module:** `navmesh_amxx_i386.so` (Compiled in Debian Bookworm with static libstdc++)
   * **Windows Module:** `navmesh_amxx.dll`
   * **Windows GUI Editor:** `nav_editor.exe`
   * **Command-Line Tool:** `nav_cli.exe` / `nav_cli`
2. Copy the module binary to your server:
   * `cstrike/addons/amxmodx/modules/navmesh_amxx_i386.so` (Linux)
   * `cstrike/addons/amxmodx/modules/navmesh_amxx.dll` (Windows)
3. Open `cstrike/addons/amxmodx/configs/modules.ini` and add:
   ```ini
   navmesh
   ```
4. Copy `scripting/include/navmesh.inc` to your compiler's `include/` directory.
5. Restart your server.

---

## Building from Source

### CMake Build Options

| Option | Default | Description |
| :--- | :--- | :--- |
| `BUILD_AMXX_MODULE` | `ON` | Compiles the AMX Mod X server binary (`navmesh_amxx`) |
| `BUILD_CLI` | `ON` | Compiles the headless map verification tool (`nav_cli`) |
| `BUILD_EDITOR` | `OFF` | Compiles the NavStudio 3D GUI editor (`nav_editor`) |

### Linux (Debian / Ubuntu x86)

```bash
# 1. Install 32-bit compilation dependencies
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install -y gcc-multilib g++-multilib cmake make

# 2. Build the AMXX module and CLI tool
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j$(nproc)

# Output binaries:
# build/navmesh_amxx_i386.so
# build/nav_cli
```

To build NavStudio on Linux, install `libgl1-mesa-dev` and `libx11-dev`, then pass `-DBUILD_EDITOR=ON`:
```bash
sudo apt-get install -y libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_EDITOR=ON
cmake --build build --config Release -j$(nproc)

# Output binary: build/nav_editor
```

### Windows (MSVC)

Open **Developer Command Prompt for Visual Studio** or PowerShell:

```powershell
# 1. Configure with the GUI editor enabled
cmake -B build -A Win32 -DBUILD_EDITOR=ON

# 2. Compile the Release build
cmake --build build --config Release

# Output binaries:
# build\Release\navmesh_amxx.dll
# build\Release\nav_cli.exe
# build\Release\nav_editor.exe
```

---

## NavStudio Usage Guide

Launch NavStudio from the terminal or by double-clicking `nav_editor.exe`:

```cmd
nav_editor.exe cstrike/maps/de_dust2.bsp
```

If a `.nav` file exists in the same directory, it will automatically load alongside the BSP geometry.

### Controls and Shortcuts

| Action | Shortcut |
| :--- | :--- |
| **Move Camera (Flycam)** | `W / A / S / D` |
| **Elevate Camera Up / Down** | `E / Q` |
| **Look Around** | Right-Click + Drag |
| **Adjust Camera Speed** | Mouse Wheel (while holding Right-Click) |
| **Orbit Selected Area** | `Alt` + Left-Click + Drag |
| **Focus Camera on Selection** | `F` |
| **Reset Camera** | `Home` |
| **Select NavArea** | Left-Click (in 3D Viewport or Hierarchy) |
| **Snap Area to Floor** | `S` |
| **Undo** | `Ctrl + Z` |
| **Redo** | `Ctrl + Y` |
| **Save Navigation Mesh** | `Ctrl + S` |
| **Open Map / NavMesh** | `Ctrl + O` |

---

## Standalone CLI Tool (`nav_cli`)

The project includes a headless CLI tool to analyze maps, verify geometry, and benchmark A* pathfinding without running a game server:

```bash
./build/nav_cli cstrike/maps/de_dust2.bsp czero/maps/de_dust2.nav
```

**Sample Output:**
```text
=========================================================
 NavMesh Core - CS 1.6 BSP & NAV Verification CLI
=========================================================

[BSP] Loading: de_dust2.bsp...
  -> BSP loaded successfully!
  -> Entities: 147
  -> Models: 43
  -> CT Spawns: 32
  -> T Spawns: 32

[NAV] Loading: de_dust2.nav...
  -> NAV loaded successfully!
  -> Format version: 5
  -> Total Navigation Areas: 718
  -> Places: TSpawn, BombsiteB, CTSpawn, Side, Middle, BombsiteA...

[PATHFINDING] Running A* Benchmark...
  -> Start Position: (-1680, -840, 128)
  -> Goal Position:  (280, 2240, 32)
  -> Path found!
  -> Waypoints count: 18
  -> Total Path length: 3412.50 units
```

---

## Pawn API Overview (`navmesh.inc`)

### BSP Functions
```pawn
// Map loading & status
native bsp_load_map(const mapname[]);
native bsp_is_loaded();

// Visibility & Audibility (PVS & PAS)
native bsp_get_leaf(const Float:origin[3]);
native bsp_check_vis(leaf_a, leaf_b);
native bsp_check_pas(leaf_a, leaf_b);
native bsp_is_point_visible(const Float:ptA[3], const Float:ptB[3]);
native bsp_is_point_audible(const Float:ptA[3], const Float:ptB[3]);
native bsp_get_pvs_size();
native bsp_get_leaf_pvs(leaf_index, buffer[], maxlen);
native bsp_get_leaf_pas(leaf_index, buffer[], maxlen);
native bsp_get_visible_leaf_count(leaf_index);

// Collision & Tracing
native bsp_trace_line(const Float:start[3], const Float:end[3], Float:hitPos[3] = Float:{0.0,0.0,0.0}, Float:hitNormal[3] = Float:{0.0,0.0,0.0});
native bsp_trace_line_ex(const Float:start[3], const Float:end[3], Float:hitPos[3], Float:hitNormal[3], texture[], maxlen);
native bsp_trace_hull(const Float:start[3], const Float:end[3], hull_type, Float:hitPos[3] = Float:{0.0,0.0,0.0}, Float:hitNormal[3] = Float:{0.0,0.0,0.0});
native bsp_trace_hull_ex(const Float:start[3], const Float:end[3], hull_type, Float:hitPos[3], Float:hitNormal[3], texture[], maxlen);
native bsp_trace_texture(const Float:start[3], const Float:end[3], texture[], maxlen);
native bsp_trace_wall(const Float:start[3], const Float:end[3], hull_type);
native bsp_trace_model(model_idx, const Float:start[3], const Float:end[3], hull_type);
native bsp_get_ground(const Float:start[3], Float:out[3], Float:max_drop = 2000.0);
native bsp_get_contents(const Float:origin[3]);

// World, Leaves & Geometry
native bsp_get_world_bounds(Float:mins[3], Float:maxs[3]);
native bsp_get_leaf_bounds(leaf_index, Float:mins[3], Float:maxs[3]);
native bsp_get_leaf_contents(leaf_index);
native bsp_get_leaf_ambient(leaf_index, channel);
native bsp_get_leaf_count();
native bsp_get_node_count();
native bsp_get_plane_count();
native bsp_get_face_count();
native bsp_get_plane(plane_index, Float:normal[3], &Float:dist, &type);

// Textures & Surfaces
native bsp_get_texture_count();
native bsp_get_texture_name(texture_index, name[], maxlen);
native bsp_get_texture_size(texture_index, &width, &height);
native bsp_find_texture(const name[]);
native BSPMaterialType:bsp_get_surface_material(const texture_name[]);
native BSPMaterialType:bsp_trace_material(const Float:start[3], const Float:end[3], texture[] = "", maxlen = 0);
native bsp_get_texture_flags(texture_index, &flags = 0, &is_transparent = 0, &is_fluid = 0, &is_sky = 0, &is_animated = 0);

// Illumination & Lightmaps
native bsp_get_point_light(const Float:start[3], const Float:end[3], &Float:brightness, Float:color[3] = Float:{0.0,0.0,0.0});
native bsp_get_face_light(face_index, const Float:point[3], &Float:brightness, Float:color[3] = Float:{0.0,0.0,0.0});

// Geometry & Polygons
native bsp_get_face_vertex_count(face_index);
native bsp_get_face_polygon(face_index, Float:output[][3], max_vertices);
native bsp_get_leaf_face_count(leaf_index);
native bsp_get_leaf_faces(leaf_index, faces[], max_faces);

// Worldspawn Metadata
native bsp_get_skyname(output[], maxlen);
native bsp_get_map_title(output[], maxlen);
native bsp_get_wad_list(output[], maxlen);

// Entities & Submodels
native bsp_get_entity_count(const classname[] = "");
native bsp_get_entity_origin(const classname[], target_index, Float:output[3]);
native bsp_get_entity_key(entity_index, const key[], value[], maxlen);
native bsp_find_entity_by_key(const key[], const value[], start_index = 0);
native bsp_get_brush_model(const classname[], target_index, Float:out_mins[3], Float:out_maxs[3]);
native bsp_find_entities_by_target(const target[], output[], max_found);
native bsp_find_entities_by_targetname(const targetname[], output[], max_found);
native bsp_get_entity_target(entity_index, output[], maxlen);
native bsp_get_entity_targetname(entity_index, output[], maxlen);
native bsp_get_model_count();
native bsp_get_model_bounds(model_index, Float:mins[3], Float:maxs[3]);
native bsp_get_model_origin(model_index, Float:origin[3]);
native bsp_get_entities(const classname[], Float:output[], max_found);
```

### NavMesh & Pathfinding Functions
```pawn
native nav_load(const mapname[] = "");
native nav_unload();
native nav_is_loaded();
native nav_get_area_count();
native nav_get_area_by_id(area_id);
native nav_get_area_id(area_index);
native nav_get_nearest_area(const Float:pos[3], Float:max_dist = 1000.0);
native nav_get_area_at_point(const Float:pos[3], Float:max_z_delta = 40.0);
native nav_get_area_center(area_index, Float:center[3]);
native nav_get_area_extent(area_index, Float:mins[3], Float:maxs[3]);
native Float:nav_get_area_z(area_index, Float:x, Float:y);
native nav_get_area_flags(area_index);
native nav_is_point_in_area(area_index, const Float:pos[3], Float:max_z_delta = 40.0);
native nav_get_closest_point(area_index, const Float:pos[3], Float:closest[3]);
native Float:nav_get_distance_to_area(area_index, const Float:pos[3]);
native nav_get_adjacent_count(area_index, NavDirType:direction);
native nav_get_adjacent_area(area_index, NavDirType:direction, adj_index);
native nav_is_connected(area_a, area_b, direction = -1);
native nav_get_place_name(area_index, output[], maxlen);
native nav_get_hiding_spot_count(area_index);
native nav_get_hiding_spot(area_index, spot_index, Float:pos[3], &flags);
native nav_get_ladder_count();
native nav_get_ladder_info(ladder_index, Float:top[3], Float:bottom[3], &Float:length, &Float:width, &direction);

// Pathfinding
native nav_build_path(const Float:start[3], const Float:goal[3], &path_id, flags = NAV_PATH_DEFAULT);
native nav_build_path_async(const Float:start[3], const Float:goal[3], flags = NAV_PATH_DEFAULT);
native nav_process_async();
native nav_path_get_segment_count(path_id);
native nav_path_get_point(path_id, segment_index, Float:pos[3]);
native nav_path_get_area(path_id, segment_index);
native NavTraverseType:nav_path_get_how(path_id, segment_index);
native Float:nav_path_get_length(path_id);
native nav_path_get_point_along(path_id, Float:dist, Float:pos[3]);
native nav_path_destroy(path_id);
native nav_path_clear_all();
```

### Forwards
```pawn
forward nav_on_map_loaded(bsp_loaded, nav_loaded, area_count);
forward nav_on_path_computed(task_id, path_id, Float:length, success);
```

---

## Example Usage

### Synchronous Pathfinding
```pawn
#include <amxmodx>
#include <fakemeta>
#include <navmesh>

public plugin_init()
{
    register_plugin("Nav Example", "1.0", "Author");
    register_clcmd("say /path", "Cmd_Path");
}

public Cmd_Path(id)
{
    new Float:start[3], Float:goal[3];
    pev(id, pev_origin, start);

    // Set goal 500 units away
    goal[0] = start[0] + 500.0;
    goal[1] = start[1] + 500.0;
    goal[2] = start[2];

    new pathId = 0;
    if (nav_build_path(start, goal, pathId, NAV_PATH_SMOOTH))
    {
        new count = nav_path_get_segment_count(pathId);
        new Float:length = nav_path_get_length(pathId);
        client_print(id, print_chat, "Path found! Waypoints: %d, Length: %.1f units", count, length);

        for (new i = 0; i < count; i++)
        {
            new Float:wp[3];
            nav_path_get_point(pathId, i, wp);
            // Process waypoint...
        }

        nav_path_destroy(pathId);
    }
    return PLUGIN_HANDLED;
}
```

### Asynchronous Offloaded Pathfinding
```pawn
// Queue non-blocking pathfinding job on background worker thread
new taskId = nav_build_path_async(start, goal, NAV_PATH_SMOOTH);

// Fired on the main server thread when worker completes:
public nav_on_path_computed(task_id, path_id, Float:length, success)
{
    if (!success || !path_id)
        return;

    new count = nav_path_get_segment_count(path_id);
    server_print("Async path computed: %d waypoints, %.1f units", count, length);

    // Process waypoints and destroy handle
    nav_path_destroy(path_id);
}
```

---

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).
Portions based on AMX Mod X SDK, Metamod, and ReGameDLL.
