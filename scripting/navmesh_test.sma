#include <amxmodx>
#include <amxmisc>
#include <fakemeta>
#include <navmesh>

#define PLUGIN  "NavMesh Core Demo"
#define VERSION "1.0.0"
#define AUTHOR  "Ziyad"

#define MAX_ASYNC_TRACKING 256

new g_BeamSprite;

// Track which player requested an async path
new g_TaskToPlayer[MAX_ASYNC_TRACKING];

public plugin_init()
{
    register_plugin(PLUGIN, VERSION, AUTHOR);

    register_clcmd("say /navinfo",   "Cmd_NavInfo",   ADMIN_ALL, "Display current nav and bsp info");
    register_clcmd("say /navpath",   "Cmd_NavPath",   ADMIN_ALL, "Build synchronous path (green laser)");
    register_clcmd("say /navasync",  "Cmd_NavAsync",  ADMIN_ALL, "Build asynchronous path (blue laser)");
    register_clcmd("say /navground", "Cmd_NavGround", ADMIN_ALL, "Test ground detection");

    // Clear task tracking table
    for (new i = 0; i < MAX_ASYNC_TRACKING; i++)
    {
        g_TaskToPlayer[i] = 0;
    }

    // Auto-load map navigation data
    new szMap[64];
    get_mapname(szMap, charsmax(szMap));

    if (bsp_load_map(szMap))
    {
        server_print("[NavMesh Demo] BSP '%s' loaded successfully.", szMap);
    }
    else
    {
        server_print("[NavMesh Demo] Warning: Failed to load BSP '%s'.", szMap);
    }

    if (nav_load(szMap))
    {
        server_print("[NavMesh Demo] NAV '%s' loaded successfully (%d areas).",
                     szMap, nav_get_area_count());
    }
    else
    {
        server_print("[NavMesh Demo] Warning: Failed to load NAV '%s'.", szMap);
    }
}

public plugin_precache()
{
    g_BeamSprite = precache_model("sprites/laserbeam.spr");
}

public server_frame()
{
    // If not compiled as Metamod plugin, poll completed async tasks each frame.
    // In Metamod mode, StartFrame_Post handles this automatically.
    nav_process_async();
}

/**
 * Forward: Called when BSP or NAV data finishes loading.
 */
public nav_on_map_loaded(bsp_loaded, nav_loaded, area_count)
{
    server_print("[NavMesh Demo] nav_on_map_loaded forward: BSP=%d, NAV=%d, Areas=%d",
                 bsp_loaded, nav_loaded, area_count);
}

/**
 * Forward: Called on the main server thread when a background worker finishes computing an A* path.
 */
public nav_on_path_computed(task_id, path_id, Float:length, success)
{
    new playerId = 0;
    if (task_id > 0 && task_id < MAX_ASYNC_TRACKING)
    {
        playerId = g_TaskToPlayer[task_id];
        g_TaskToPlayer[task_id] = 0;
    }

    if (!success || path_id == 0)
    {
        if (playerId && is_user_connected(playerId))
        {
            client_print(playerId, print_chat, "[NavMesh Async] Path calculation failed (no route found)!");
        }
        return;
    }

    new count = nav_path_get_segment_count(path_id);

    if (playerId && is_user_connected(playerId))
    {
        client_print(playerId, print_chat,
                     "[NavMesh Async] Task #%d complete: %d segments, %.1f units (Blue laser).",
                     task_id, count, length);

        // Draw blue laser beams between path waypoints
        for (new i = 1; i < count; i++)
        {
            new Float:p1[3], Float:p2[3];
            nav_path_get_point(path_id, i - 1, p1);
            nav_path_get_point(path_id, i, p2);

            p1[2] += 12.0;
            p2[2] += 12.0;

            DrawBeam(p1, p2, 100, 0, 150, 255); // Cyan/Blue beam
        }
    }

    // Always release path handle after use
    nav_path_destroy(path_id);
}

public Cmd_NavInfo(id)
{
    if (!is_user_alive(id))
        return PLUGIN_HANDLED;

    new Float:origin[3];
    pev(id, pev_origin, origin);

    new leaf = bsp_get_leaf(origin);
    new area = nav_get_nearest_area(origin, 500.0);

    new place[64] = "Unknown";
    new areaId = -1;
    new flags = 0;

    if (area != -1)
    {
        areaId = nav_get_area_id(area);
        flags = nav_get_area_flags(area);
        nav_get_place_name(area, place, charsmax(place));
    }

    client_print(id, print_chat, "[NavMesh] Origin: (%.1f, %.1f, %.1f) | Leaf: %d",
                 origin[0], origin[1], origin[2], leaf);
    client_print(id, print_chat, "[NavMesh] Area: #%d (ID: %d) | Place: '%s' | Flags: 0x%02X",
                 area, areaId, place, flags);

    return PLUGIN_HANDLED;
}

public Cmd_NavPath(id)
{
    if (!is_user_alive(id))
        return PLUGIN_HANDLED;

    new Float:start[3], Float:end[3];
    pev(id, pev_origin, start);

    if (!GetCrosshairTarget(id, end))
    {
        client_print(id, print_chat, "[NavMesh] Failed to determine target point!");
        return PLUGIN_HANDLED;
    }

    new pathId = 0;
    if (!nav_build_path(start, end, pathId, NAV_PATH_SMOOTH))
    {
        client_print(id, print_chat, "[NavMesh] Could not find a path to the target!");
        return PLUGIN_HANDLED;
    }

    new count = nav_path_get_segment_count(pathId);
    new Float:len = nav_path_get_length(pathId);

    client_print(id, print_chat, "[NavMesh] Sync Path: Segments: %d | Length: %.1f units (Green laser)", count, len);

    // Draw green laser beams
    for (new i = 1; i < count; i++)
    {
        new Float:p1[3], Float:p2[3];
        nav_path_get_point(pathId, i - 1, p1);
        nav_path_get_point(pathId, i, p2);

        p1[2] += 10.0;
        p2[2] += 10.0;

        DrawBeam(p1, p2, 100, 0, 255, 100); // Green
    }

    nav_path_destroy(pathId);
    return PLUGIN_HANDLED;
}

public Cmd_NavAsync(id)
{
    if (!is_user_alive(id))
        return PLUGIN_HANDLED;

    new Float:start[3], Float:end[3];
    pev(id, pev_origin, start);

    if (!GetCrosshairTarget(id, end))
    {
        client_print(id, print_chat, "[NavMesh Async] Failed to determine target point!");
        return PLUGIN_HANDLED;
    }

    new taskId = nav_build_path_async(start, end, NAV_PATH_SMOOTH);
    if (!taskId)
    {
        client_print(id, print_chat, "[NavMesh Async] Failed to dispatch async path task!");
        return PLUGIN_HANDLED;
    }

    if (taskId < MAX_ASYNC_TRACKING)
    {
        g_TaskToPlayer[taskId] = id;
    }

    client_print(id, print_chat, "[NavMesh Async] Request #%d queued on worker thread...", taskId);
    return PLUGIN_HANDLED;
}

public Cmd_NavGround(id)
{
    if (!is_user_alive(id))
        return PLUGIN_HANDLED;

    new Float:origin[3], Float:ground[3];
    pev(id, pev_origin, origin);

    if (bsp_get_ground(origin, ground))
    {
        client_print(id, print_chat, "[NavMesh] Ground detected at Z: %.2f (Delta: %.2f)",
                     ground[2], origin[2] - ground[2]);
    }
    else
    {
        client_print(id, print_chat, "[NavMesh] No ground found within 2000 units!");
    }

    return PLUGIN_HANDLED;
}

static bool:GetCrosshairTarget(id, Float:outGround[3])
{
    new Float:start[3], Float:viewOfs[3], Float:angles[3], Float:forwardVec[3];
    pev(id, pev_origin, start);
    pev(id, pev_view_ofs, viewOfs);
    pev(id, pev_v_angle, angles);

    new Float:eyes[3];
    eyes[0] = start[0] + viewOfs[0];
    eyes[1] = start[1] + viewOfs[1];
    eyes[2] = start[2] + viewOfs[2];

    engfunc(EngFunc_AngleVectors, angles, forwardVec, Float:{0.0,0.0,0.0}, Float:{0.0,0.0,0.0});

    new Float:traceEnd[3];
    traceEnd[0] = eyes[0] + forwardVec[0] * 2000.0;
    traceEnd[1] = eyes[1] + forwardVec[1] * 2000.0;
    traceEnd[2] = eyes[2] + forwardVec[2] * 2000.0;

    new Float:end[3];
    bsp_trace_line(eyes, traceEnd, end);

    if (!bsp_get_ground(end, outGround, 500.0))
    {
        outGround = end;
    }

    return true;
}

stock DrawBeam(const Float:p1[3], const Float:p2[3], lifeTimeTenths, r, g, b)
{
    message_begin(MSG_BROADCAST, SVC_TEMPENTITY);
    write_byte(TE_BEAMPOINTS);
    write_coord(floatround(p1[0]));
    write_coord(floatround(p1[1]));
    write_coord(floatround(p1[2]));
    write_coord(floatround(p2[0]));
    write_coord(floatround(p2[1]));
    write_coord(floatround(p2[2]));
    write_short(g_BeamSprite);
    write_byte(0);              // starting frame
    write_byte(0);              // frame rate
    write_byte(lifeTimeTenths); // life in 0.1s
    write_byte(10);             // line width
    write_byte(0);              // noise
    write_byte(r);              // red
    write_byte(g);              // green
    write_byte(b);              // blue
    write_byte(200);            // brightness
    write_byte(0);              // scroll speed
    message_end();
}
