#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include "../bsp/bsp_file.h"
#include "../nav/nav_file.h"
#include "../nav/nav_path.h"

int main(int argc, char* argv[]) {
    std::cout << "=========================================================\n";
    std::cout << " NavMesh Core - CS 1.6 BSP & NAV Verification CLI\n";
    std::cout << "=========================================================\n\n";

    if (argc < 2) {
        std::cout << "Usage:\n";
        std::cout << "  nav_cli <path_to_bsp_or_nav> [optional_second_file]\n\n";
        std::cout << "Examples:\n";
        std::cout << "  nav_cli de_dust2.bsp de_dust2.nav\n";
        std::cout << "  nav_cli cstrike/maps/de_dust2.bsp\n";
        return 1;
    }

    std::string bspPath = "";
    std::string navPath = "";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.size() > 4) {
            std::string ext = arg.substr(arg.size() - 4);
            if (ext == ".bsp") bspPath = arg;
            else if (ext == ".nav") navPath = arg;
        }
    }

    // If only one was provided, deduce the other
    if (!bspPath.empty() && navPath.empty()) {
        navPath = bspPath.substr(0, bspPath.size() - 4) + ".nav";
    } else if (!navPath.empty() && bspPath.empty()) {
        bspPath = navPath.substr(0, navPath.size() - 4) + ".bsp";
    }

    BSPFile bsp;
    NavMesh nav;

    // Load BSP
    if (!bspPath.empty()) {
        std::cout << "[BSP] Loading: " << bspPath << "...\n";
        if (bsp.Load(bspPath)) {
            std::cout << "  -> BSP loaded successfully!\n";
            std::cout << "  -> Entities: " << bsp.GetEntities().size() << "\n";
            std::cout << "  -> Models: " << bsp.GetModelCount() << "\n";
            std::cout << "  -> Leaves: " << bsp.GetLeafCount() << "\n";

            auto ctSpawns = bsp.FindEntities("info_player_start");
            auto tSpawns = bsp.FindEntities("info_player_deathmatch");
            std::cout << "  -> CT Spawns: " << ctSpawns.size() << "\n";
            std::cout << "  -> T Spawns: " << tSpawns.size() << "\n";
        } else {
            std::cout << "  -> Failed to open/parse BSP file.\n";
        }
    }

    // Load NAV
    if (!navPath.empty()) {
        std::cout << "\n[NAV] Loading: " << navPath << "...\n";
        if (nav.Load(navPath)) {
            std::cout << "  -> NAV loaded successfully!\n";
            std::cout << "  -> Format version: " << nav.GetVersion() << "\n";
            std::cout << "  -> Recorded BSP size: " << nav.GetBspSize() << " bytes\n";
            std::cout << "  -> Total Navigation Areas: " << nav.GetAreaCount() << "\n";
            std::cout << "  -> Places count: " << nav.GetPlaceNames().size() << "\n";

            std::cout << "  -> Place Directory:\n";
            for (size_t p = 0; p < nav.GetPlaceNames().size(); p++) {
                std::cout << "     [" << (p + 1) << "] " << nav.GetPlaceNames()[p] << "\n";
            }

            if (bsp.IsLoaded()) {
                nav.BuildLadders(&bsp);
                std::cout << "  -> Linked " << nav.GetLadders().size() << " ladder entities from BSP.\n";
            }
        } else {
            std::cout << "  -> Failed to open/parse NAV file.\n";
        }
    }

    // Test A* Pathfinding if both or nav loaded
    if (nav.IsLoaded()) {
        std::cout << "\n[PATHFINDING] Running A* Benchmark...\n";

        Vector3 startPos(0, 0, 0);
        Vector3 goalPos(0, 0, 0);

        if (bsp.IsLoaded()) {
            auto tSpawns = bsp.FindEntities("info_player_deathmatch");
            auto ctSpawns = bsp.FindEntities("info_player_start");
            if (!tSpawns.empty() && !ctSpawns.empty()) {
                tSpawns[0]->GetOrigin(startPos);
                ctSpawns[0]->GetOrigin(goalPos);
            }
        }

        if (startPos == Vector3(0, 0, 0) && nav.GetAreaCount() >= 2) {
            startPos = nav.GetArea(0)->GetCenter();
            goalPos = nav.GetArea(nav.GetAreaCount() - 1)->GetCenter();
        }

        std::cout << "  -> Start Position: (" << startPos.x << ", " << startPos.y << ", " << startPos.z << ")\n";
        std::cout << "  -> Goal Position:  (" << goalPos.x << ", " << goalPos.y << ", " << goalPos.z << ")\n";

        NavPath path;
        bool pathResult = NavPathFinder::BuildPath(
            nav.GetGrid(), startPos, goalPos, path, NAV_PATH_SMOOTH, bsp.IsLoaded() ? &bsp : nullptr
        );

        if (pathResult && path.IsValid()) {
            std::cout << "  -> Path found!\n";
            std::cout << "  -> Waypoints count: " << path.GetSegmentCount() << "\n";
            std::cout << "  -> Total Path length: " << std::fixed << std::setprecision(2) << path.GetLength() << " units\n";
            std::cout << "  -> Waypoints preview (first 5):\n";
            for (size_t s = 0; s < std::min(size_t(5), path.GetSegmentCount()); s++) {
                const auto* seg = path.GetSegment(s);
                std::cout << "     #" << s << ": (" << seg->pos.x << ", " << seg->pos.y << ", " << seg->pos.z << ")"
                          << " Area ID: " << (seg->area ? seg->area->GetID() : 0)
                          << " Place: " << (seg->area ? seg->area->GetPlaceName() : "") << "\n";
            }
        } else {
            std::cout << "  -> No path could be found between points.\n";
        }
    }

    std::cout << "\n[COMPLETED] Verification finished successfully.\n";
    return 0;
}
