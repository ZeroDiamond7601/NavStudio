#ifndef WAYPOINT_NAV_CONVERTER_H
#define WAYPOINT_NAV_CONVERTER_H

#include "waypoint/waypoint_graph.h"
#include "nav/nav_file.h"

class WaypointNavConverter {
public:
    struct ConvertStats {
        size_t waypointsCreated{0};
        size_t connectionsCreated{0};
        size_t laddersConverted{0};
        size_t sniperPointsMapped{0};
        size_t campPointsMapped{0};
        size_t zombieCampsMapped{0};
    };

    // Converts Valve Navigation Mesh (.nav) into Waypoint Graph (.ewp, .spt, .pwf, .wpt)
    static ConvertStats NavToWaypoints(
        const NavMesh& nav,
        WaypointGraph& outGraph,
        BotType botType = BotType::EBot,
        GameMod mod = GameMod::Standard
    );

    // Converts Waypoint Graph into Valve Navigation Mesh (.nav)
    static size_t WaypointsToNav(
        const WaypointGraph& graph,
        NavMesh& outNav
    );
};

#endif // WAYPOINT_NAV_CONVERTER_H
