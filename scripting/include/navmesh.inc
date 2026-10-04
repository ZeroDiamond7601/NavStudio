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
 * BSP Material Classification Types
 */
enum BSPMaterialType
{
    MAT_CONCRETE = 0,
    MAT_METAL,
    MAT_DIRT,
    MAT_VENT,
    MAT_GRATE,
    MAT_TILE,
    MAT_SLOSH,
    MAT_WOOD,
    MAT_COMPUTER,
    MAT_GLASS,
    MAT_FLESH,
    MAT_FOLIAGE
};

/**
 * BSP Texture Surface Flags
 */
#define TEX_FLAG_NONE           0x00
#define TEX_FLAG_TRANSPARENT    0x01
#define TEX_FLAG_FLUID          0x02
#define TEX_FLAG_SKY            0x04
#define TEX_FLAG_ANIMATED       0x08
#define TEX_FLAG_LIGHT          0x10

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
 * Checks if a 3D point ptB is potentially visible from ptA using PVS.
 *
 * @param ptA       Source [x, y, z] coordinate
 * @param ptB       Target [x, y, z] coordinate
 * @return          1 if potentially visible, 0 if occluded
 */
native bsp_is_point_visible(const Float:ptA[3], const Float:ptB[3]);

/**
 * Checks if a 3D point ptB is potentially audible from ptA using PAS.
 *
 * @param ptA       Source [x, y, z] coordinate
 * @param ptB       Target [x, y, z] coordinate
 * @return          1 if potentially audible, 0 otherwise
 */
native bsp_is_point_audible(const Float:ptA[3], const Float:ptB[3]);

/**
 * Gets the size of a decompressed PVS/PAS row in bytes ((num_leaves + 7) / 8).
 *
 * @return          Size in bytes
 */
native bsp_get_pvs_size();

/**
 * Decompresses the Potentially Visible Set (PVS) bitmask for a leaf.
 * Each cell in buffer receives 1 byte (8 leaves per byte).
 *
 * @param leaf_index    Leaf index (1 to num_leaves - 1)
 * @param buffer        Cell array to store decompressed bytes
 * @param maxlen        Maximum array length
 * @return              Number of bytes written
 */
native bsp_get_leaf_pvs(leaf_index, buffer[], maxlen);

/**
 * Computes and decompresses the Potentially Audible Set (PAS) bitmask for a leaf.
 *
 * @param leaf_index    Leaf index
 * @param buffer        Cell array to store decompressed bytes
 * @param maxlen        Maximum array length
 * @return              Number of bytes written
 */
native bsp_get_leaf_pas(leaf_index, buffer[], maxlen);

/**
 * Gets the total number of leaves visible from a specific leaf.
 *
 * @param leaf_index    Leaf index
 * @return              Count of visible leaves
 */
native bsp_get_visible_leaf_count(leaf_index);

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
 * Traces a line (Hull 0) against world geometry and retrieves the impacted surface texture.
 *
 * @param start     Starting [x, y, z] coordinate
 * @param end       Ending [x, y, z] coordinate
 * @param hitPos    Output array for hit coordinate
 * @param hitNormal Output array for surface normal vector
 * @param texture   Output string buffer to receive texture name (e.g. "dirt", "wood")
 * @param maxlen    Maximum texture string buffer length
 * @return          1 if hit occurred, 0 if line is clear
 */
native bsp_trace_line_ex(const Float:start[3], const Float:end[3], Float:hitPos[3], Float:hitNormal[3], texture[], maxlen);

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
 * Traces a collision hull (0-3) against world geometry and retrieves the impacted surface texture.
 *
 * @param start     Starting [x, y, z] coordinate
 * @param end       Ending [x, y, z] coordinate
 * @param hull_type Collision hull type (0-3)
 * @param hitPos    Output array for hit coordinate
 * @param hitNormal Output array for surface normal vector
 * @param texture   Output string buffer to receive texture name
 * @param maxlen    Maximum texture string buffer length
 * @return          1 if hit occurred, 0 if line is clear
 */
native bsp_trace_hull_ex(const Float:start[3], const Float:end[3], hull_type, Float:hitPos[3], Float:hitNormal[3], texture[], maxlen);

/**
 * Traces a zero-width ray against world geometry and returns the hit surface texture name.
 *
 * @param start     Starting [x, y, z]
 * @param end       Ending [x, y, z]
 * @param texture   Output string buffer for texture name
 * @param maxlen    Maximum buffer length
 * @return          1 if hit occurred and texture found, 0 otherwise
 */
native bsp_trace_texture(const Float:start[3], const Float:end[3], texture[], maxlen);

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
 * Gets the world bounding box (minimum and maximum coordinates) of the map.
 *
 * @param mins      Output array for minimum [x, y, z]
 * @param maxs      Output array for maximum [x, y, z]
 * @return          1 on success, 0 on failure
 */
native bsp_get_world_bounds(Float:mins[3], Float:maxs[3]);

/**
 * Gets the bounding box of a specific BSP leaf.
 *
 * @param leaf_index    Leaf index
 * @param mins          Output array for minimum [x, y, z]
 * @param maxs          Output array for maximum [x, y, z]
 * @return              1 on success, 0 on failure
 */
native bsp_get_leaf_bounds(leaf_index, Float:mins[3], Float:maxs[3]);

/**
 * Gets the contents flags of a specific BSP leaf.
 *
 * @param leaf_index    Leaf index
 * @return              Contents integer (e.g. CONTENTS_SOLID, CONTENTS_WATER, CONTENTS_EMPTY)
 */
native bsp_get_leaf_contents(leaf_index);

/**
 * Gets the ambient lighting/audio level of a leaf.
 *
 * @param leaf_index    Leaf index
 * @param channel       Ambient channel (0-3)
 * @return              Ambient level (0-255)
 */
native bsp_get_leaf_ambient(leaf_index, channel);

/**
 * Gets total count of leaves in the BSP tree.
 *
 * @return              Leaf count
 */
native bsp_get_leaf_count();

/**
 * Gets total count of BSP tree nodes.
 *
 * @return              Node count
 */
native bsp_get_node_count();

/**
 * Gets total count of geometric planes in the BSP.
 *
 * @return              Plane count
 */
native bsp_get_plane_count();

/**
 * Gets total count of surface faces in the BSP.
 *
 * @return              Face count
 */
native bsp_get_face_count();

/**
 * Gets geometric plane parameters by plane index.
 *
 * @param plane_index   Plane index
 * @param normal        Output array for plane normal vector [x, y, z]
 * @param dist          Output float for plane distance from origin
 * @param type          Output integer for plane type (0=X, 1=Y, 2=Z, 3=ANYX, 4=ANYY, 5=ANYZ)
 * @return              1 on success, 0 on failure
 */
native bsp_get_plane(plane_index, Float:normal[3], &Float:dist, &type);

/**
 * Gets total number of textures referenced in the BSP.
 *
 * @return              Texture count
 */
native bsp_get_texture_count();

/**
 * Gets the name of a texture by texture index.
 *
 * @param texture_index Texture index (0 to count-1)
 * @param name          Output string buffer
 * @param maxlen        Maximum buffer length
 * @return              Number of characters written
 */
native bsp_get_texture_name(texture_index, name[], maxlen);

/**
 * Gets the pixel dimensions of a texture.
 *
 * @param texture_index Texture index
 * @param width         Output integer for texture width
 * @param height        Output integer for texture height
 * @return              1 on success, 0 on failure
 */
native bsp_get_texture_size(texture_index, &width, &height);

/**
 * Finds texture index by texture name (case-insensitive).
 *
 * @param name          Texture name
 * @return              Texture index, or -1 if not found
 */
native bsp_find_texture(const name[]);

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
 * Reads any key-value property string from a BSP entity by its global entity index.
 *
 * @param entity_index  Entity index (0 to bsp_get_entity_count() - 1)
 * @param key           Key name (e.g. "targetname", "angles", "speed")
 * @param value         Output string buffer
 * @param maxlen        Maximum string buffer length
 * @return              Number of characters written
 */
native bsp_get_entity_key(entity_index, const key[], value[], maxlen);

/**
 * Finds the first or next entity matching a specific key-value pair.
 *
 * @param key           Key name to search for (e.g. "targetname")
 * @param value         Value to match
 * @param start_index   Start searching from this entity index
 * @return              Entity index, or -1 if not found
 */
native bsp_find_entity_by_key(const key[], const value[], start_index = 0);

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
 * Gets total count of submodels (brush models like doors, triggers, platforms) in the BSP.
 *
 * @return              Model count
 */
native bsp_get_model_count();

/**
 * Gets the bounding box of a submodel.
 *
 * @param model_index   Model index (0 is world model, 1..N are submodels)
 * @param mins          Output array for mins [x, y, z]
 * @param maxs          Output array for maxs [x, y, z]
 * @return              1 on success, 0 on failure
 */
native bsp_get_model_bounds(model_index, Float:mins[3], Float:maxs[3]);

/**
 * Gets the origin of a submodel.
 *
 * @param model_index   Model index
 * @param origin        Output array for origin [x, y, z]
 * @return              1 on success, 0 on failure
 */
native bsp_get_model_origin(model_index, Float:origin[3]);

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

/**
 * Samples illumination (lightmap luxels) along a ray intersecting the world.
 *
 * @param start         Ray origin [x, y, z]
 * @param end           Ray terminus [x, y, z]
 * @param brightness    Output scalar brightness (0.0 to 255.0)
 * @param color         Optional output RGB vector (0.0 to 255.0 per channel)
 * @return              1 if hit a valid lightmapped surface, 0 otherwise
 */
native bsp_get_point_light(const Float:start[3], const Float:end[3], &Float:brightness, Float:color[3] = Float:{0.0, 0.0, 0.0});

/**
 * Samples lightmap illumination on a specific face at a 3D surface point.
 *
 * @param face_index    Face index (0 to bsp_get_face_count() - 1)
 * @param point         3D coordinate on or near face surface [x, y, z]
 * @param brightness    Output scalar brightness (0.0 to 255.0)
 * @param color         Optional output RGB vector (0.0 to 255.0 per channel)
 * @return              1 on success, 0 on failure
 */
native bsp_get_face_light(face_index, const Float:point[3], &Float:brightness, Float:color[3] = Float:{0.0, 0.0, 0.0});

/**
 * Classifies a texture name into an engine material type.
 *
 * @param texture_name  Texture name string (e.g. "c1a0_flr1", "{fence", "!water")
 * @return              BSPMaterialType enum value
 */
native BSPMaterialType:bsp_get_surface_material(const texture_name[]);

/**
 * Traces a line against world geometry and returns the surface material type.
 *
 * @param start         Start vector [x, y, z]
 * @param end           End vector [x, y, z]
 * @param texture       Optional output string buffer to receive texture name
 * @param maxlen        Maximum length of texture buffer
 * @return              BSPMaterialType enum value
 */
native BSPMaterialType:bsp_trace_material(const Float:start[3], const Float:end[3], texture[] = "", maxlen = 0);

/**
 * Retrieves special texture properties and classification flags.
 *
 * @param texture_index  Texture index (0 to bsp_get_texture_count() - 1)
 * @param flags          Optional output bitmask of TEX_FLAG_* flags
 * @param is_transparent Optional output 1 if texture starts with '{' (grate/fence)
 * @param is_fluid       Optional output 1 if texture starts with '!' (water/liquid)
 * @param is_sky         Optional output 1 if texture starts with 'sky'
 * @param is_animated    Optional output 1 if texture starts with '+' (animated frame)
 * @return               Bitmask of TEX_FLAG_* flags
 */
native bsp_get_texture_flags(texture_index, &flags = 0, &is_transparent = 0, &is_fluid = 0, &is_sky = 0, &is_animated = 0);

/**
 * Gets the number of vertices composing a face polygon.
 *
 * @param face_index    Face index (0 to bsp_get_face_count() - 1)
 * @return              Vertex count, or 0 on error
 */
native bsp_get_face_vertex_count(face_index);

/**
 * Extracts 3D world coordinates for all vertices of a face polygon.
 *
 * @param face_index    Face index (0 to bsp_get_face_count() - 1)
 * @param output        2D array to receive vertex points [max_vertices][3]
 * @param max_vertices  Maximum number of vertices to write
 * @return              Number of vertices written
 */
native bsp_get_face_polygon(face_index, Float:output[][3], max_vertices);

/**
 * Retrieves the skybox texture prefix defined in the map's worldspawn entity.
 *
 * @param output        Output string buffer
 * @param maxlen        Maximum string buffer length
 * @return              1 if skyname found, 0 otherwise
 */
native bsp_get_skyname(output[], maxlen);

/**
 * Retrieves the map title defined in the map's worldspawn entity (message key).
 *
 * @param output        Output string buffer
 * @param maxlen        Maximum string buffer length
 * @return              1 if title found, 0 otherwise
 */
native bsp_get_map_title(output[], maxlen);

/**
 * Retrieves the semicolon-separated list of WAD texture files referenced in worldspawn.
 *
 * @param output        Output string buffer
 * @param maxlen        Maximum string buffer length
 * @return              Number of characters written, or 0 if not found
 */
native bsp_get_wad_list(output[], maxlen);

/**
 * Gets the number of surface faces referencing a specific BSP leaf (marksurfaces).
 *
 * @param leaf_index    Leaf index (0 to bsp_get_leaf_count() - 1)
 * @return              Number of faces touching this leaf
 */
native bsp_get_leaf_face_count(leaf_index);

/**
 * Retrieves the face indices associated with a BSP leaf (marksurfaces).
 *
 * @param leaf_index    Leaf index (0 to bsp_get_leaf_count() - 1)
 * @param faces         Array to receive face indices
 * @param max_faces     Maximum number of face indices to write
 * @return              Number of face indices written
 */
native bsp_get_leaf_faces(leaf_index, faces[], max_faces);

/**
 * Finds all entity indices whose 'target' property matches the given target string.
 *
 * @param target        Target string to search for
 * @param output        Integer array to receive entity indices
 * @param max_found     Maximum elements to write
 * @return              Number of matching entities found
 */
native bsp_find_entities_by_target(const target[], output[], max_found);

/**
 * Finds all entity indices whose 'targetname' property matches the given targetname string.
 *
 * @param targetname    Targetname string to search for
 * @param output        Integer array to receive entity indices
 * @param max_found     Maximum elements to write
 * @return              Number of matching entities found
 */
native bsp_find_entities_by_targetname(const targetname[], output[], max_found);

/**
 * Reads the 'target' property string of an entity.
 *
 * @param entity_index  Entity index (0 to bsp_get_entity_count() - 1)
 * @param output        Output string buffer
 * @param maxlen        Maximum string buffer length
 * @return              Number of characters written
 */
native bsp_get_entity_target(entity_index, output[], maxlen);

/**
 * Reads the 'targetname' property string of an entity.
 *
 * @param entity_index  Entity index (0 to bsp_get_entity_count() - 1)
 * @param output        Output string buffer
 * @param maxlen        Maximum string buffer length
 * @return              Number of characters written
 */
native bsp_get_entity_targetname(entity_index, output[], maxlen);

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
