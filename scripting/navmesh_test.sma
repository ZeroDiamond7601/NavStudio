#include <amxmodx>
#include <amxmisc>
#include <fakemeta>
#include <navmesh>

#define PLUGIN  "NavMesh Core Demo"
#define VERSION "1.0.0"
#define AUTHOR  "Ziyad"

new g_BeamSprite;

public plugin_init()
{
    register_plugin(PLUGIN, VERSION, AUTHOR);

    register_clcmd("say /navinfo", "Cmd_NavInfo", ADMIN_ALL, "Display current nav and bsp info");
    register_clcmd("say /navpath", "Cmd_NavPath", ADMIN_ALL, "Build path to crosshair target");
    register_clcmd("say /navground", "Cmd_NavGround", ADMIN_ALL, "Test ground detection");

    // Load navigation data on map start
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

    // Aim trace forward 2000 units
    new Float:viewOfs[3], Float:angles[3], Float:forwardVec[3];
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

    // Use our module's BSP trace
    bsp_trace_line(eyes, traceEnd, end);

    // Drop goal to ground
    new Float:goalGround[3];
    if (bsp_get_ground(end, goalGround, 500.0))
    {
        end = goalGround;
    }

    new pathId = 0;
    if (!nav_build_path(start, end, pathId, NAV_PATH_SMOOTH))
    {
        client_print(id, print_chat, "[NavMesh] Could not find a path to the target!");
        return PLUGIN_HANDLED;
    }

    new count = nav_path_get_segment_count(pathId);
    new Float:len = nav_path_get_length(pathId);

    client_print(id, print_chat, "[NavMesh] Path found! Segments: %d | Length: %.1f units", count, len);

    // Draw beams between waypoints
    for (new i = 1; i < count; i++)
    {
        new Float:p1[3], Float:p2[3];
        nav_path_get_point(pathId, i - 1, p1);
        nav_path_get_point(pathId, i, p2);

        // Raise slightly off ground for visibility
        p1[2] += 10.0;
        p2[2] += 10.0;

        DrawBeam(p1, p2, 100);
    }

    nav_path_destroy(pathId);
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

stock DrawBeam(const Float:p1[3], const Float:p2[3], lifeTimeTenths)
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
    write_byte(0);           // starting frame
    write_byte(0);           // frame rate
    write_byte(lifeTimeTenths); // life in 0.1s
    write_byte(10);          // line width
    write_byte(0);           // noise
    write_byte(0);           // red
    write_byte(255);         // green
    write_byte(100);         // blue
    write_byte(200);         // brightness
    write_byte(0);           // scroll speed
    message_end();
}
