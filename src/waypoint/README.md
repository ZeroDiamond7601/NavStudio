# Waypoint System Architecture & Specification

## Overview

The Waypoint module in NavStudio provides an end-to-end framework for loading, generating, analyzing, editing, and exporting tactical bot navigation graphs across popular Counter-Strike bot engines.

The module supports full round-trip conversion between Valve Navigation Meshes (`.nav`) and waypoint files, automated whole-map generation from GoldSrc BSP architecture, distance-sorted auto-linking with BSP collision raycasting, and in-editor visual editing.

---

## File Structure & Responsibilities

| File | Responsibility |
| :--- | :--- |
| [`waypoint_types.h`](file:///c:/Users/ziyad/OneDrive/Bureau/nav_module/src/waypoint/waypoint_types.h) | Core data models, enumeration definitions (`BotType`, `GameMod`), tactical bitflags, and connection flags. |
| [`waypoint_graph.h`](file:///c:/Users/ziyad/OneDrive/Bureau/nav_module/src/waypoint/waypoint_graph.h) | In-memory graph structure, node queries, connection management, auto-linking, and validation. |
| [`waypoint_graph.cpp`](file:///c:/Users/ziyad/OneDrive/Bureau/nav_module/src/waypoint/waypoint_graph.cpp) | Implementation of topological node mutations, spatial searches, A* graph validation, and wayzone calculations. |
| [`waypoint_codecs.cpp`](file:///c:/Users/ziyad/OneDrive/Bureau/nav_module/src/waypoint/waypoint_codecs.cpp) | Binary serializers and deserializers for `.ewp` (CS-EBOT), `.spt` (SyPB), `.pwf` (YaPB), and `.wpt` (POD-Bot mm). |
| [`ebot_generator.h`](file:///c:/Users/ziyad/OneDrive/Bureau/nav_module/src/waypoint/ebot_generator.h) | Asynchronous whole-map waypoint generator interface and configuration structures. |
| [`ebot_generator.cpp`](file:///c:/Users/ziyad/OneDrive/Bureau/nav_module/src/waypoint/ebot_generator.cpp) | Generator engine ported from CS-EBOT: multi-seed BFS flood fill, hull drop traces, ladder extraction, and sightlines. |
| [`waypoint_nav_converter.h`](file:///c:/Users/ziyad/OneDrive/Bureau/nav_module/src/waypoint/waypoint_nav_converter.h) | Bidirectional conversion declarations between Valve NavMesh and WaypointGraph. |
| [`waypoint_nav_converter.cpp`](file:///c:/Users/ziyad/OneDrive/Bureau/nav_module/src/waypoint/waypoint_nav_converter.cpp) | Bidirectional conversion algorithms mapping NavAreas and hiding spots to waypoint nodes and vice versa. |
| [`compressor.h`](file:///c:/Users/ziyad/OneDrive/Bureau/nav_module/src/waypoint/compressor.h) | High-performance LZSS compression and decompression routines for CS-EBOT and YaPB formats. |

---

## Supported Formats

### 1. CS-EBOT (`.ewp`)
- **Magic**: `EBOTWP\0` (8 bytes) or `EWP\0` (4 bytes)
- **File Version**: 127
- **Compression**: LZSS compressed or raw uncompressed binary stream
- **Header**: Contains point count, map name (32 chars), and author (32 chars)
- **Node Size**: 56 bytes (`EBotDiskPath` aligned to CS-EBOT `struct Path`)
- **Capabilities**: Up to 8 outgoing links with jump/crouch flags, camp orientation pitch and yaw, mesh clusters, fall check, and Zombie Plague specific flags.

### 2. SyPB (`.spt` / `.pwf`)
- **Magic**: `SyPB\0` (4 bytes) or `PWF\0`
- **File Version**: 125
- **Node Size**: Packed binary format with team flags, connection flags, and radius.

### 3. YaPB (`.pwf`)
- **Magic**: `YaPB\0` (4 bytes)
- **File Version**: 7
- **Compression**: LZSS compressed data stream.

### 4. POD-Bot mm (`.wpt`)
- **Magic**: `PODWPT!`
- **File Version**: 6
- **Compression**: Raw uncompressed binary format with 10 outgoing connections per node.

---

## Bitflags & Mod Rules

The waypoint module uses unified 32-bit flag masks (`waypoint_types.h`) that are automatically mapped to and from engine-specific formats during file I/O:

```cpp
// Standard Tactical & Objective Flags
WPT_FLAG_CROUCH          = (1 << 0)   // Node requires crouching / ducking
WPT_FLAG_CAMP            = (1 << 1)   // Camping perch or ambush spot
WPT_FLAG_LADDER          = (1 << 2)   // Ladder climb node
WPT_FLAG_LIFT            = (1 << 3)   // Elevator / lift platform
WPT_FLAG_DOOR            = (1 << 4)   // Door passage
WPT_FLAG_JUMP            = (1 << 5)   // Jump maneuver required
WPT_FLAG_DOUBLEJUMP      = (1 << 6)   // Requires teammate boost / double jump
WPT_FLAG_GOAL            = (1 << 7)   // Mission objective (bomb site, hostage)
WPT_FLAG_RESCUE          = (1 << 8)   // Hostage rescue zone
WPT_FLAG_SNIPER          = (1 << 9)   // Sniper nest with tactical sightlines
WPT_FLAG_TERRORIST       = (1 << 10)  // Terrorist team exclusive
WPT_FLAG_COUNTER         = (1 << 11)  // Counter-Terrorist team exclusive
WPT_FLAG_AVOID           = (1 << 12)  // Avoidance node (hazard or danger)
WPT_FLAG_USEBUTTON       = (1 << 13)  // Trigger / button activation

// CS-EBOT & Zombie Plague Specific Flags
WPT_FLAG_FALLCHECK       = (1 << 14)  // Check for floor beneath before step
WPT_FLAG_SPECIFICGRAVITY = (1 << 15)  // Custom low/high gravity jump
WPT_FLAG_WAITUNTIL       = (1 << 16)  // Wait for condition / trigger
WPT_FLAG_ZMHMCAMP        = (1 << 17)  // Zombie Plague barricade camp
WPT_FLAG_HMCAMPMESH      = (1 << 18)  // Human camp mesh cluster node
WPT_FLAG_ZOMBIEONLY      = (1 << 19)  // Restricted exclusively to zombies
WPT_FLAG_HUMANONLY       = (1 << 20)  // Restricted exclusively to humans
WPT_FLAG_ZOMBIEPUSH      = (1 << 21)  // Directional zombie rush
WPT_FLAG_FALLRISK        = (1 << 22)  // High ledge (prevents evasive strafe)
WPT_FLAG_HELICOPTER      = (1 << 23)  // Helicopter escape extraction
WPT_FLAG_ONLYONE         = (1 << 24)  // Single bot at a time passage
```

---

## Generator Pipeline

The whole-map waypoint generator (`ebot_generator.cpp`) operates in an asynchronous background worker thread:

1. **Seed Gathering**:
   - Gathers seed positions from all player spawns (`info_player_start`, `info_player_deathmatch`), bomb zones (`func_bomb_target`, `info_bomb_target`), hostage points (`hostage_entity`, `info_hostage_rescue`), and VIP extraction entities.
   - If zero spawn entities are present, the generator falls back to scanning map bounds for walkable floor surfaces.

2. **Floor Flood Fill**:
   - Executes 8-directional breadth-first search (BFS) on an exploration grid with step height tracing (`maxStepHeight = 18.0`) and jump height tracing (`maxJumpHeight = 45.0`).
   - Uses `HULL_POINT` downward traces (`HULL_POINT = 0`) to trace true BSP floor geometry.

3. **Ladder Linking**:
   - Extracts all `func_ladder` brush entities and parses ladder bounds to place vertical ladder nodes and connect them to top and bottom floors.

4. **Distance-Sorted Auto-Linking**:
   - Sorts candidate neighbor nodes by Euclidean distance.
   - Validates horizontal and vertical step limits and verifies line-of-sight clearance via `BSPFile::TraceWorld`.

5. **Tactical Sightline Analysis**:
   - Analyzes open areas, chokepoints, and vantage perches to assign camping pitch/yaw angles and sniper flags.
