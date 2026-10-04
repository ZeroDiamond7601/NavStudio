#if defined _navmesh_included
  #endinput
#endif
#define _navmesh_included

#pragma reqlib navmesh
#if !defined AMXMODX_NOAUTOLOAD
  #pragma loadlib navmesh
#endif

/**
 * Collision Hull Types for BSP tracing
 */
#define HULL_POINT              0  // Ray / Bullet / Zero size point
#define HULL_HUMAN              1  // Standing Player (32x32x72)
#define HULL_LARGE              2  // Large Hull (64x64x64)
#define HULL_HEAD               3  // Crouching Player (32x32x36)

/**
 * BSP Contents codes
 */
#define CONTENTS_EMPTY         -1
#define CONTENTS_SOLID         -2
#define CONTENTS_WATER         -3
#define CONTENTS_SLIME         -4
#define CONTENTS_LAVA          -5
#define CONTENTS_SKY           -6
#define CONTENTS_ORIGIN        -7
#define CONTENTS_CLIP          -8

/**
 * Navigation Area Attribute Flags
 */
#define NAV_ATTR_NONE           0x00
#define NAV_ATTR_CROUCH         0x01  // Player must crouch in this area
#define NAV_ATTR_JUMP           0x02  // Player must jump to enter/traverse
#define NAV_ATTR_PRECISE        0x04  // Inhibit obstacle avoidance, move precisely
#define NAV_ATTR_NO_JUMP        0x08  // Inhibit discontinuity jumps

/**
 * Cardinal Directions
 */
enum NavDirType
{
    NAV_DIR_NORTH = 0,
    NAV_DIR_EAST,
    NAV_DIR_SOUTH,
    NAV_DIR_WEST,
    NUM_NAV_DIRECTIONS
};

/**
 * Traversal Types
 */
enum NavTraverseType
{
    NAV_TRAVERSE_NORTH = 0,
    NAV_TRAVERSE_EAST,
    NAV_TRAVERSE_SOUTH,
    NAV_TRAVERSE_WEST,
    NAV_TRAVERSE_LADDER_UP,
    NAV_TRAVERSE_LADDER_DOWN,
    NAV_TRAVERSE_JUMP,
    NUM_NAV_TRAVERSE_TYPES
};

/**
 * Pathfinding Options Flags
 */
#define NAV_PATH_DEFAULT        0x00
#define NAV_PATH_AVOID_CROUCH   0x01  // Heavily penalize crouching areas
#define NAV_PATH_AVOID_JUMP     0x02  // Penalize jumping areas
#define NAV_PATH_PREFER_SAFE    0x04  // Prefer areas with cover / hiding spots
#define NAV_PATH_SMOOTH         0x08  // Line-of-sight path smoothing across BSP

/* ========================================================================== */
/*                                BSP NATIVES                                 */
/* ========================================================================== */

/**
 * Loads a BSP map into memory.
 *
 * @param mapname   Map name (e.g., "de_dust2" or full path)
 * @return          1 on success, 0 on failure
 */
native bsp_load_map(const mapname[]);

/**
 * Checks if a BSP map is currently loaded.
 *
 * @return          1 if loaded, 0 otherwise
 */
native bsp_is_loaded();

/**
 * Gets the BSP leaf index for a 3D coordinate.
 *
 * @param origin    Float array containing [x, y, z]
 * @return          Leaf index, or -1 if outside the map
 */
native bsp_get_leaf(const Float:origin[3]);

/**
 * Checks if leaf_b is visible from leaf_a using precomputed PVS VisData.
 *
 * @param leaf_a    Source leaf index
 * @param leaf_b    Destination leaf index
 * @return          1 if visible, 0 if occluded
 */
native bsp_check_vis(leaf_a, leaf_b);

/**
 * Checks if leaf_b is audible from leaf_a using precomputed PAS VisData.
 *
 * @param leaf_a    Source leaf index
 * @param leaf_b    Destination leaf index
 * @return          1 if audible, 0 otherwise
 */
native bsp_check_pas(leaf_a, leaf_b);

/**
 * Traces a line (Hull 0) against world geometry.
 *
 * @param start     Starting [x, y, z] coordinate
 * @param end       Ending [x, y, z] coordinate
 * @param hitPos    Optional output array for hit coordinate
 * @param hitNormal Optional output array for surface normal vector
 * @return          1 if hit occurred, 0 if line is clear
 */
native bsp_trace_line(const Float:start[3], const Float:end[3], Float:hitPos[3] = Float:{0.0, 0.0, 0.0}, Float:hitNormal[3] = Float:{0.0, 0.0, 0.0});

/**
 * Traces a collision hull (0-3) against world geometry.
 *
 * @param start     Starting [x, y, z] coordinate
 * @param end       Ending [x, y, z] coordinate
 * @param hull_type Collision hull type (HULL_POINT, HULL_HUMAN, HULL_LARGE, HULL_HEAD)
 * @param hitPos    Optional output array for hit coordinate
 * @param hitNormal Optional output array for surface normal vector
 * @return          1 if hit occurred, 0 if line is clear
 */
native bsp_trace_hull(const Float:start[3], const Float:end[3], hull_type, Float:hitPos[3] = Float:{0.0, 0.0, 0.0}, Float:hitNormal[3] = Float:{0.0, 0.0, 0.0});

/**
 * Traces a line against world geometry (backward compatibility with bsp-core).
 *
 * @param start     Starting [x, y, z] coordinate
 * @param end       Ending [x, y, z] coordinate
 * @param hull_type Collision hull (0-3)
 * @return          1 if hit occurred, 0 if line is clear
 */
native bsp_trace_wall(const Float:start[3], const Float:end[3], hull_type);

/**
 * Traces a line against a specific brush model (bmodel).
 *
 * @param model_idx Model index (e.g. 1 for *1)
 * @param start     Starting [x, y, z]
 * @param end       Ending [x, y, z]
 * @param hull_type Collision hull (0-3)
 * @return          1 if hit occurred, 0 if clear
 */
native bsp_trace_model(model_idx, const Float:start[3], const Float:end[3], hull_type);

/**
 * Drops downwards to find the ground height below a coordinate.
 *
 * @param start     Starting [x, y, z]
 * @param out       Output array to store ground [x, y, z]
 * @param max_drop  Maximum distance to drop downwards (default 2000.0)
 * @return          1 if ground was found, 0 otherwise
 */
native bsp_get_ground(const Float:start[3], Float:out[3], Float:max_drop = 2000.0);

/**
 * Retrieves the raw BSP contents (CONTENTS_SOLID, CONTENTS_WATER, etc.) at point.
 *
 * @param origin    [x, y, z] point
 * @return          Contents integer code
 */
native bsp_get_contents(const Float:origin[3]);

/**
 * Gets the number of map entities parsed from the BSP.
 *
 * @param classname Optional classname filter (e.g. "info_player_start")
 * @return          Number of matching entities
 */
native bsp_get_entity_count(const classname[] = "");

/**
 * Gets the origin of an entity by classname and index.
 *
 * @param classname     Entity classname
 * @param target_index  Index among entities of this class (0-based)
 * @param output        Float array to store [x, y, z]
 * @return              1 on success, 0 on failure
 */
native bsp_get_entity_origin(const classname[], target_index, Float:output[3]);

/**
 * Gets bounding box (mins and maxs) of a brush model entity.
 *
 * @param classname     Entity classname (e.g., "func_wall")
 * @param target_index  Index among entities of this class
 * @param out_mins      Output mins [x, y, z]
 * @param out_maxs      Output maxs [x, y, z]
 * @return              1 on success, 0 on failure
 */
native bsp_get_brush_model(const classname[], target_index, Float:out_mins[3], Float:out_maxs[3]);

/**
 * Fills an array with origins of matching entities (bsp-core compatibility).
 *
 * @param classname Entity classname to search for
 * @param output    Float array to store packed origins (size max_found * 3)
 * @param max_found Maximum number of entity origins to retrieve
 * @return          Number of entities found
 */
native bsp_get_entities(const classname[], Float:output[], max_found);

// Backward compatibility aliases
native nav_get_entities(const classname[], Float:output[], max_found) = bsp_get_entities;
native nav_get_ground(const Float:start[3], Float:out[3], Float:max_drop = 2000.0) = bsp_get_ground;
native nav_trace_wall(const Float:start[3], const Float:end[3], hull_type) = bsp_trace_wall;
native nav_trace_model(model_idx, const Float:start[3], const Float:end[3], hull_type) = bsp_trace_model;
native nav_get_contents(const Float:origin[3]) = bsp_get_contents;

/* ========================================================================== */
/*                              NAV MESH NATIVES                              */
/* ========================================================================== */

/**
 * Loads a .nav navigation mesh for the map.
 *
 * @param mapname   Map name (or empty string to auto-detect current map)
 * @return          1 on success, 0 on failure
 */
native nav_load(const mapname[] = "");

/**
 * Unloads current navigation mesh and frees memory.
 *
 * @return          1 on success
 */
native nav_unload();

/**
 * Checks if a navigation mesh is loaded.
 *
 * @return          1 if loaded, 0 otherwise
 */
native nav_is_loaded();

/**
 * Gets total count of navigation areas in loaded mesh.
 *
 * @return          Area count
 */
native nav_get_area_count();

/**
 * Finds area index by its unique NavArea ID.
 *
 * @param area_id   Unique area ID
 * @return          Area index (0 to count-1), or -1 if not found
 */
native nav_get_area_by_id(area_id);

/**
 * Gets the unique Area ID for a given area index.
 *
 * @param area_index Area index
 * @return           Unique Area ID
 */
native nav_get_area_id(area_index);

/**
 * Finds the nearest navigation area to a given world coordinate.
 *
 * @param pos       World coordinate [x, y, z]
 * @param max_dist  Maximum search distance (default 1000.0)
 * @return          Area index, or -1 if none found
 */
native nav_get_nearest_area(const Float:pos[3], Float:max_dist = 1000.0);

/**
 * Finds the navigation area directly containing a coordinate.
 *
 * @param pos           World coordinate [x, y, z]
 * @param max_z_delta   Maximum allowed Z difference (default 40.0)
 * @return              Area index, or -1 if point is outside all areas
 */
native nav_get_area_at_point(const Float:pos[3], Float:max_z_delta = 40.0);

/**
 * Gets the center coordinate of a navigation area.
 *
 * @param area_index    Area index
 * @param center        Float array to store [x, y, z]
 * @return              1 on success, 0 on failure
 */
native nav_get_area_center(area_index, Float:center[3]);

/**
 * Gets the bounding box extent of a navigation area.
 *
 * @param area_index    Area index
 * @param mins          Float array to store mins [x, y, z]
 * @param maxs          Float array to store maxs [x, y, z]
 * @return              1 on success, 0 on failure
 */
native nav_get_area_extent(area_index, Float:mins[3], Float:maxs[3]);

/**
 * Calculates exact bilinear interpolated ground height on the area quad.
 *
 * @param area_index    Area index
 * @param x             World X coordinate
 * @param y             World Y coordinate
 * @return              Exact Z coordinate on the nav area floor
 */
native Float:nav_get_area_z(area_index, Float:x, Float:y);

/**
 * Gets attribute flags of an area (crouch, jump, precise, no-jump).
 *
 * @param area_index    Area index
 * @return              Bitmask of NAV_ATTR_* flags
 */
native nav_get_area_flags(area_index);

/**
 * Checks if a 3D point is inside an area.
 *
 * @param area_index    Area index
 * @param pos           Coordinate [x, y, z]
 * @param max_z_delta   Maximum Z difference (default 40.0)
 * @return              1 if inside, 0 otherwise
 */
native nav_is_point_in_area(area_index, const Float:pos[3], Float:max_z_delta = 40.0);

/**
 * Computes the closest point on a navigation area to a given position.
 *
 * @param area_index    Area index
 * @param pos           Input coordinate [x, y, z]
 * @param closest       Output array to store closest [x, y, z]
 * @return              1 on success, 0 on failure
 */
native nav_get_closest_point(area_index, const Float:pos[3], Float:closest[3]);

/**
 * Calculates Euclidean distance from a position to an area.
 *
 * @param area_index    Area index
 * @param pos           Input coordinate [x, y, z]
 * @return              Distance in world units
 */
native Float:nav_get_distance_to_area(area_index, const Float:pos[3]);

/**
 * Gets the number of connected areas in a cardinal direction.
 *
 * @param area_index    Area index
 * @param direction     Cardinal direction (NAV_DIR_NORTH to NAV_DIR_WEST)
 * @return              Number of connected areas
 */
native nav_get_adjacent_count(area_index, NavDirType:direction);

/**
 * Gets the area index of an adjacent connected area.
 *
 * @param area_index    Area index
 * @param direction     Cardinal direction
 * @param adj_index     Adjacent connection index (0 to count-1)
 * @return              Adjacent area index, or -1 if invalid
 */
native nav_get_adjacent_area(area_index, NavDirType:direction, adj_index);

/**
 * Checks if area_a is connected to area_b.
 *
 * @param area_a        Source area index
 * @param area_b        Destination area index
 * @param direction     Direction to check (-1 for any direction)
 * @return              1 if connected, 0 otherwise
 */
native nav_is_connected(area_a, area_b, direction = -1);

/**
 * Gets the named Place string for an area (e.g. "BombsiteA", "TSpawn", "Tunnel").
 *
 * @param area_index    Area index
 * @param output        Buffer to receive place name
 * @param maxlen        Maximum buffer length
 * @return              Number of characters written
 */
native nav_get_place_name(area_index, output[], maxlen);

/**
 * Gets count of tactical hiding spots inside an area.
 *
 * @param area_index    Area index
 * @return              Hiding spot count
 */
native nav_get_hiding_spot_count(area_index);

/**
 * Retrieves details for a specific hiding spot.
 *
 * @param area_index    Area index
 * @param spot_index    Spot index (0 to count-1)
 * @param pos           Output array for spot position [x, y, z]
 * @param flags         Output integer for spot flags (cover, sniper, etc.)
 * @return              1 on success, 0 on failure
 */
native nav_get_hiding_spot(area_index, spot_index, Float:pos[3], &flags);

/**
 * Gets total count of ladders registered in the navigation system.
 *
 * @return              Ladder count
 */
native nav_get_ladder_count();

/**
 * Retrieves information about a navigation ladder.
 *
 * @param ladder_index  Ladder index
 * @param top           Output top coordinate [x, y, z]
 * @param bottom        Output bottom coordinate [x, y, z]
 * @param length        Output ladder height/length
 * @param width         Output ladder width
 * @param direction     Output facing direction
 * @return              1 on success, 0 on failure
 */
native nav_get_ladder_info(ladder_index, Float:top[3], Float:bottom[3], &Float:length, &Float:width, &direction);

/* ========================================================================== */
/*                             PATHFINDING NATIVES                            */
/* ========================================================================== */

/**
 * Computes an optimal A* path between two world positions.
 *
 * @param start         Starting [x, y, z]
 * @param goal          Goal [x, y, z]
 * @param path_id       Output integer handle for the created path
 * @param flags         Pathfinding flags (NAV_PATH_AVOID_CROUCH, NAV_PATH_SMOOTH, etc.)
 * @return              1 if path was successfully found, 0 otherwise
 */
native nav_build_path(const Float:start[3], const Float:goal[3], &path_id, flags = NAV_PATH_DEFAULT);

/**
 * Computes an optimal A* path between two world positions asynchronously on a background worker thread.
 * When path calculation completes, the 'nav_on_path_computed' forward is invoked on the main server thread.
 *
 * @param start         Starting [x, y, z]
 * @param goal          Goal [x, y, z]
 * @param flags         Pathfinding flags (NAV_PATH_AVOID_CROUCH, NAV_PATH_SMOOTH, etc.)
 * @return              Unique task ID (> 0) on success, or 0 on failure
 */
native nav_build_path_async(const Float:start[3], const Float:goal[3], flags = NAV_PATH_DEFAULT);

/**
 * Manually polls and processes completed background worker path tasks.
 * If Metamod is active, this is called automatically in StartFrame_Post.
 * In standalone AMXX without Metamod hooks, call this inside server_frame().
 *
 * @return              1 on success
 */
native nav_process_async();

/**
 * Gets the number of waypoints (segments) in a calculated path.
 *
 * @param path_id       Path handle
 * @return              Segment count
 */
native nav_path_get_segment_count(path_id);

/**
 * Gets the 3D position of a waypoint along the path.
 *
 * @param path_id       Path handle
 * @param segment_index Waypoint index (0 to segment_count-1)
 * @param pos           Output array to store [x, y, z]
 * @return              1 on success, 0 on failure
 */
native nav_path_get_point(path_id, segment_index, Float:pos[3]);

/**
 * Gets the navigation area index for a waypoint.
 *
 * @param path_id       Path handle
 * @param segment_index Waypoint index
 * @return              Area index
 */
native nav_path_get_area(path_id, segment_index);

/**
 * Gets how to traverse to this waypoint (walk, jump, crouch, ladder).
 *
 * @param path_id       Path handle
 * @param segment_index Waypoint index
 * @return              NavTraverseType enum value
 */
native NavTraverseType:nav_path_get_how(path_id, segment_index);

/**
 * Gets the total Euclidean length of the path in world units.
 *
 * @param path_id       Path handle
 * @return              Path length
 */
native Float:nav_path_get_length(path_id);

/**
 * Computes a point a specific distance along the path.
 *
 * @param path_id       Path handle
 * @param dist          Distance along path from start
 * @param pos           Output array to store [x, y, z]
 * @return              1 on success, 0 on failure
 */
native nav_path_get_point_along(path_id, Float:dist, Float:pos[3]);

/**
 * Destroys a path handle and frees its memory.
 *
 * @param path_id       Path handle
 * @return              1 on success, 0 on failure
 */
native nav_path_destroy(path_id);

/**
 * Clears all active path handles.
 *
 * @return              1 on success
 */
native nav_path_clear_all();

/* ========================================================================== */
/*                                  FORWARDS                                  */
/* ========================================================================== */

/**
 * Called when a BSP or NAV map is loaded or reloaded.
 *
 * @param bsp_loaded    1 if BSP world geometry is loaded, 0 otherwise
 * @param nav_loaded    1 if NAV navigation mesh is loaded, 0 otherwise
 * @param area_count    Total number of navigation areas loaded
 */
forward nav_on_map_loaded(bsp_loaded, nav_loaded, area_count);

/**
 * Called on the main server thread when an asynchronous path request completes.
 *
 * @param task_id       The unique request ID returned by nav_build_path_async
 * @param path_id       The created path handle (valid if success == 1), or 0
 * @param length        Total path length in world units
 * @param success       1 if a path was successfully found, 0 if unreachable/failed
 */
forward nav_on_path_computed(task_id, path_id, Float:length, success);
