#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include "../bsp/bsp_file.h"
#include "../nav/nav_file.h"
#include "../nav/nav_path.h"
#include "../nav/nav_generator.h"
#include "../waypoint/waypoint_graph.h"
#include "../waypoint/waypoint_nav_converter.h"
#include "../waypoint/ebot_generator.h"

static void PrintHelp() {
    std::cout << "Usage:\n";
    std::cout << "  nav_cli <path_to_bsp_or_nav> [optional_second_file]\n";
    std::cout << "  nav_cli generate <map.bsp> [output.nav] [options]\n";
    std::cout << "  nav_cli generate-waypoints <map.bsp> [output.ewp] [options]\n";
    std::cout << "  nav_cli batch <maps_directory> [options]\n";
    std::cout << "  nav_cli convert <input> <output> [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  generate <map.bsp> [out.nav]           Auto-generate navigation mesh for a BSP map\n";
    std::cout << "  generate-waypoints <map.bsp> [out]     Auto-generate CS-EBOT bot waypoint graph\n";
    std::cout << "  batch <maps_dir>                       Mass-produce navigation meshes for all maps in directory\n";
    std::cout << "  convert <in> <out> [opts]              Convert bidirectionally between NAV and Bot Waypoints\n";
    std::cout << "  analyze <file> [map.bsp]               Auto-analyze flags, crouch, jumps & sightlines (Nav & Waypoints)\n";
    std::cout << "  <map.bsp|map.nav>                      Verify BSP data, NAV headers, places, and A* pathfinding\n\n";
    std::cout << "Options:\n";
    std::cout << "  --output, -o <dir|file>                Specify output directory or file path\n";
    std::cout << "  --step <float>                         Grid step size (default: 25.0)\n";
    std::cout << "  --spacing <float>                      Waypoint spacing (default: 110.0)\n";
    std::cout << "  --threads <int>                        Worker thread count for batch mode (default: CPU cores)\n";
    std::cout << "  --force, -f                            Overwrite existing files\n";
    std::cout << "  --recursive, -r                        Recursively search directories for BSP files\n";
    std::cout << "  --no-jump                              Disable jump drop connections\n";
    std::cout << "  --no-merge                             Disable adjacent coplanar area merging\n";
    std::cout << "  --no-camp                              Disable tactical sightline camp analysis\n";
    std::cout << "  --no-ladders                           Disable ladder waypoint generation\n";
    std::cout << "  --bot <ebot|sypb|yapb|podbot>          Bot engine target (default: ebot)\n";
    std::cout << "  --mod <standard|zp|dm>                 Game mod rules (default: standard)\n\n";
    std::cout << "Examples:\n";
    std::cout << "  nav_cli generate cstrike/maps/de_dust2.bsp\n";
    std::cout << "  nav_cli generate-waypoints de_dust2.bsp de_dust2.ewp --bot ebot\n";
    std::cout << "  nav_cli batch \"C:\\Steam\\Half-Life\\cstrike\\maps\" --threads 8\n";
    std::cout << "  nav_cli convert de_dust2.nav de_dust2.ewp --bot ebot --mod standard\n";
    std::cout << "  nav_cli convert zm_toxic.nav zm_toxic.pwf --bot yapb --mod zp\n";
    std::cout << "  nav_cli de_dust2.bsp de_dust2.nav\n";
}

static int HandleGenerate(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Error: 'generate' requires at least a path to a .bsp file.\n";
        PrintHelp();
        return 1;
    }

    std::string bspPath = argv[2];
    std::string navPath = "";
    NavGenerateOptions options;

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--output" || arg == "-o") {
            if (i + 1 < argc) navPath = argv[++i];
        } else if (arg == "--step") {
            if (i + 1 < argc) options.stepSize = std::stof(argv[++i]);
        } else if (arg == "--no-jump") {
            options.generateJumpConnections = false;
        } else if (arg == "--no-merge") {
            options.mergeAreas = false;
        } else if (navPath.empty() && arg.size() > 4 && arg.substr(arg.size() - 4) == ".nav") {
            navPath = arg;
        }
    }

    if (navPath.empty()) {
        if (bspPath.size() > 4 && bspPath.substr(bspPath.size() - 4) == ".bsp") {
            navPath = bspPath.substr(0, bspPath.size() - 4) + ".nav";
        } else {
            navPath = bspPath + ".nav";
        }
    }

    std::cout << "[GENERATE] Map: " << bspPath << "\n";
    std::cout << "[GENERATE] Output: " << navPath << "\n";
    std::cout << "[GENERATE] Grid Step: " << options.stepSize << " units\n\n";

    auto progressCallback = [](float progress, const std::string& msg) {
        std::cout << "\r[" << std::setw(3) << static_cast<int>(progress * 100.0f) << "%] "
                  << msg << std::string(20, ' ') << std::flush;
    };

    auto res = NavGenerator::GenerateToFile(bspPath, navPath, options, progressCallback);
    std::cout << "\n\n";

    if (res.success) {
        std::cout << "[SUCCESS] Generated NavMesh successfully!\n";
        std::cout << "  -> Total Areas:       " << res.areasGenerated << "\n";
        std::cout << "  -> Total Connections: " << res.connectionsCreated << "\n";
        std::cout << "  -> Linked Ladders:    " << res.laddersLinked << "\n";
        std::cout << "  -> Elapsed Time:      " << std::fixed << std::setprecision(2) << res.durationSeconds << " s\n";
        return 0;
    } else {
        std::cerr << "[ERROR] Generation failed: " << res.errorMessage << "\n";
        return 1;
    }
}

static int HandleBatch(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Error: 'batch' requires a directory path containing .bsp files.\n";
        PrintHelp();
        return 1;
    }

    std::string dirPath = argv[2];
    std::string outDir = "";
    bool overwrite = false;
    bool recursive = false;
    NavGenerateOptions options;

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--output" || arg == "-o") {
            if (i + 1 < argc) outDir = argv[++i];
        } else if (arg == "--threads") {
            if (i + 1 < argc) options.maxThreads = std::stoi(argv[++i]);
        } else if (arg == "--step") {
            if (i + 1 < argc) options.stepSize = std::stof(argv[++i]);
        } else if (arg == "--force" || arg == "-f") {
            overwrite = true;
        } else if (arg == "--recursive" || arg == "-r") {
            recursive = true;
        } else if (arg == "--no-jump") {
            options.generateJumpConnections = false;
        } else if (arg == "--no-merge") {
            options.mergeAreas = false;
        }
    }

    std::cout << "[BATCH] Scanning directory: " << dirPath << "\n";
    if (!outDir.empty()) std::cout << "[BATCH] Output directory:   " << outDir << "\n";
    std::cout << "[BATCH] Step Size:          " << options.stepSize << " units\n";
    std::cout << "[BATCH] Overwrite existing: " << (overwrite ? "Yes" : "No") << "\n";
    std::cout << "[BATCH] Recursive search:   " << (recursive ? "Yes" : "No") << "\n\n";

    auto batchProgress = [](size_t done, size_t total, const NavGenerator::BatchItem& item) {
        float pct = (total > 0) ? (static_cast<float>(done) / total * 100.0f) : 100.0f;
        std::cout << "[" << std::setw(3) << static_cast<int>(pct) << "%] ("
                  << done << "/" << total << ") " << item.bspPath << " -> ";
        if (item.result.success) {
            std::cout << "DONE (" << item.result.areasGenerated << " areas in "
                      << std::fixed << std::setprecision(1) << item.result.durationSeconds << "s)\n";
        } else {
            std::cout << "FAILED (" << item.result.errorMessage << ")\n";
        }
    };

    auto res = NavGenerator::GenerateDirectory(dirPath, outDir, options, recursive, overwrite, batchProgress);

    std::cout << "\n=========================================================\n";
    std::cout << " Batch Mass-Generation Complete\n";
    std::cout << "=========================================================\n";
    std::cout << "  -> Total Maps:    " << res.totalMaps << "\n";
    std::cout << "  -> Succeeded:     " << res.succeeded << "\n";
    std::cout << "  -> Failed:        " << res.failed << "\n";
    std::cout << "  -> Total Duration: " << std::fixed << std::setprecision(2) << res.totalDurationSeconds << " s\n";

    return (res.failed > 0) ? 1 : 0;
}

static int HandleInspect(std::string bspPath, std::string navPath) {
    if (!bspPath.empty() && navPath.empty()) {
        navPath = bspPath.substr(0, bspPath.size() - 4) + ".nav";
    } else if (!navPath.empty() && bspPath.empty()) {
        bspPath = navPath.substr(0, navPath.size() - 4) + ".bsp";
    }

    BSPFile bsp;
    NavMesh nav;

    if (!bspPath.empty()) {
        std::cout << "[BSP] Loading: " << bspPath << "...\n";
        if (bsp.Load(bspPath)) {
            std::cout << "  -> BSP loaded successfully!\n";
            std::cout << "  -> Entities: " << bsp.GetEntities().size() << "\n";
            std::cout << "  -> Models:   " << bsp.GetModelCount() << "\n";
            std::cout << "  -> Leaves:   " << bsp.GetLeafCount() << "\n";
            std::cout << "  -> Nodes:    " << bsp.GetNodeCount() << "\n";
            std::cout << "  -> Planes:   " << bsp.GetPlaneCount() << "\n";
            std::cout << "  -> Faces:    " << bsp.GetFaceCount() << "\n";
            std::cout << "  -> Textures: " << bsp.GetTextureCount() << "\n";

            Vector3 mins, maxs;
            if (bsp.GetWorldBounds(mins, maxs)) {
                std::cout << "  -> World Bounds: Mins(" << mins.x << ", " << mins.y << ", " << mins.z << ") Maxs("
                          << maxs.x << ", " << maxs.y << ", " << maxs.z << ")\n";
            }

            std::string skyname, mapTitle, wadList;
            if (bsp.GetSkyname(skyname)) std::cout << "  -> Skyname:   '" << skyname << "'\n";
            if (bsp.GetMapTitle(mapTitle)) std::cout << "  -> Map Title: '" << mapTitle << "'\n";
            if (bsp.GetWadList(wadList)) std::cout << "  -> WAD List:   " << wadList << "\n";

            auto ctSpawns = bsp.FindEntities("info_player_start");
            auto tSpawns = bsp.FindEntities("info_player_deathmatch");
            std::cout << "  -> CT Spawns: " << ctSpawns.size() << "\n";
            std::cout << "  -> T Spawns:  " << tSpawns.size() << "\n";

            if (!ctSpawns.empty() && !tSpawns.empty()) {
                Vector3 ctPos, tPos;
                ctSpawns[0]->GetOrigin(ctPos);
                tSpawns[0]->GetOrigin(tPos);
                int ctLeaf = bsp.GetLeafIDAtPoint(ctPos);
                int tLeaf = bsp.GetLeafIDAtPoint(tPos);
                bool vis = bsp.CheckVis(ctLeaf, tLeaf);
                bool pas = bsp.CheckPAS(ctLeaf, tLeaf);
                std::cout << "  -> PVS Check (CT Leaf " << ctLeaf << " -> T Leaf " << tLeaf << "): "
                          << (vis ? "VISIBLE" : "OCCLUDED") << "\n";
                std::cout << "  -> PAS Check (CT Leaf " << ctLeaf << " -> T Leaf " << tLeaf << "): "
                          << (pas ? "AUDIBLE" : "SILENT") << "\n";

                int leafFaces = bsp.GetLeafFaceCount(ctLeaf);
                std::cout << "  -> CT Leaf Marksurfaces Count: " << leafFaces << "\n";

                Vector3 traceDown = ctPos;
                traceDown.z -= 500.0f;
                char texName[64] = {0};
                BSPMaterialType mat = bsp.TraceMaterial(ctPos, traceDown, texName, sizeof(texName));
                std::cout << "  -> Surface below CT spawn: Texture '" << texName << "', Material " << static_cast<int>(mat) << "\n";

                float brightness = 0.0f;
                Vector3 color(0.0f, 0.0f, 0.0f);
                if (bsp.GetPointLight(ctPos, traceDown, brightness, &color)) {
                    std::cout << "  -> Illumination below CT spawn: Brightness " << brightness
                              << " RGB(" << color.x << ", " << color.y << ", " << color.z << ")\n";
                }
            }
        } else {
            std::cout << "  -> Failed to open/parse BSP file.\n";
        }
    }

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

    std::cout << "\n[COMPLETED] Operation finished.\n";
    return 0;
}

static int HandleConvert(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Error: 'convert' requires input file and output file.\n";
        std::cerr << "Usage: nav_cli convert <input.nav|wpt> <output.wpt|nav> [--bot ebot|sypb|yapb|podbot] [--mod standard|zp|dm]\n";
        return 1;
    }

    std::string inputPath = argv[2];
    std::string outputPath = argv[3];
    BotType botType = BotType::EBot;
    GameMod gameMod = GameMod::Standard;

    for (int i = 4; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--bot" && i + 1 < argc) {
            std::string b = argv[++i];
            std::transform(b.begin(), b.end(), b.begin(), ::tolower);
            if (b == "ebot" || b == "cs-ebot") botType = BotType::EBot;
            else if (b == "sypb") botType = BotType::SyPB;
            else if (b == "yapb") botType = BotType::YaPB;
            else if (b == "podbot" || b == "pod-bot" || b == "pod") botType = BotType::PODBot;
        } else if (arg == "--mod" && i + 1 < argc) {
            std::string m = argv[++i];
            std::transform(m.begin(), m.end(), m.begin(), ::tolower);
            if (m == "standard" || m == "cs") gameMod = GameMod::Standard;
            else if (m == "zp" || m == "zombie" || m == "zombieplague") gameMod = GameMod::ZombiePlague;
            else if (m == "dm" || m == "deathmatch") gameMod = GameMod::Deathmatch;
        }
    }

    auto GetExt = [](const std::string& p) {
        size_t dot = p.find_last_of('.');
        if (dot == std::string::npos) return std::string("");
        std::string ext = p.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext;
    };

    std::string inExt = GetExt(inputPath);
    std::string outExt = GetExt(outputPath);

    if (inExt == ".nav") {
        NavMesh nav;
        if (!nav.Load(inputPath)) {
            std::cerr << "[ERROR] Failed to load NavMesh: " << inputPath << "\n";
            return 1;
        }
        std::cout << "[CONVERT] Loaded NavMesh: " << nav.GetAreaCount() << " areas.\n";
        WaypointGraph graph;
        auto stats = WaypointNavConverter::NavToWaypoints(nav, graph, botType, gameMod);
        std::cout << "[CONVERT] Generated " << stats.waypointsCreated << " waypoints ("
                  << stats.connectionsCreated << " links, " << stats.laddersConverted << " ladders, "
                  << stats.sniperPointsMapped << " snipers, " << stats.campPointsMapped << " camps, "
                  << stats.zombieCampsMapped << " zombie perches).\n";
        if (graph.Save(outputPath, botType, gameMod)) {
            std::cout << "[SUCCESS] Saved bot waypoints to: " << outputPath << "\n";
            return 0;
        } else {
            std::cerr << "[ERROR] Failed to write waypoint file: " << outputPath << "\n";
            return 1;
        }
    } else {
        WaypointGraph graph;
        if (!graph.Load(inputPath)) {
            std::cerr << "[ERROR] Failed to load waypoints: " << inputPath << "\n";
            return 1;
        }
        std::cout << "[CONVERT] Loaded waypoint graph: " << graph.GetNodeCount() << " waypoints.\n";
        if (outExt == ".nav") {
            NavMesh nav;
            size_t areas = WaypointNavConverter::WaypointsToNav(graph, nav);
            std::cout << "[CONVERT] Generated " << areas << " NavAreas.\n";
            if (nav.Save(outputPath)) {
                std::cout << "[SUCCESS] Saved NavMesh to: " << outputPath << "\n";
                return 0;
            } else {
                std::cerr << "[ERROR] Failed to write NavMesh file: " << outputPath << "\n";
                return 1;
            }
        } else {
            if (graph.Save(outputPath, botType, gameMod)) {
                std::cout << "[SUCCESS] Converted and saved waypoints to: " << outputPath << "\n";
                return 0;
            } else {
                std::cerr << "[ERROR] Failed to convert waypoint file: " << outputPath << "\n";
                return 1;
            }
        }
    }
}

static int HandleAnalyze(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Error: 'analyze' requires at least a path to a .nav or waypoint file.\n";
        std::cerr << "Usage: nav_cli analyze <file.nav|ewp|pwf|spt|wpt> [map.bsp] [--mod standard|zp|dm]\n";
        return 1;
    }

    std::string filePath = argv[2];
    std::string bspPath = "";
    GameMod gameMod = GameMod::Standard;

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mod" && i + 1 < argc) {
            std::string m = argv[++i];
            std::transform(m.begin(), m.end(), m.begin(), ::tolower);
            if (m == "standard" || m == "cs") gameMod = GameMod::Standard;
            else if (m == "zp" || m == "zombie" || m == "zombieplague") gameMod = GameMod::ZombiePlague;
            else if (m == "dm" || m == "deathmatch") gameMod = GameMod::Deathmatch;
        } else if (bspPath.empty() && arg.size() > 4 && arg.substr(arg.size() - 4) == ".bsp") {
            bspPath = arg;
        }
    }

    if (bspPath.empty()) {
        size_t dot = filePath.find_last_of('.');
        if (dot != std::string::npos) {
            bspPath = filePath.substr(0, dot) + ".bsp";
        }
    }

    std::unique_ptr<BSPFile> bsp;
    if (!bspPath.empty()) {
        bsp = std::make_unique<BSPFile>();
        if (!bsp->Load(bspPath)) {
            std::cout << "[INFO] Associated BSP map not found (" << bspPath << "). Proceeding with topological analysis only.\n";
            bsp.reset();
        } else {
            std::cout << "[INFO] Loaded associated BSP map: " << bspPath << "\n";
        }
    }

    size_t dot = filePath.find_last_of('.');
    std::string ext = (dot != std::string::npos) ? filePath.substr(dot) : "";
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".nav") {
        NavMesh nav;
        if (!nav.Load(filePath)) {
            std::cerr << "[ERROR] Failed to load NavMesh: " << filePath << "\n";
            return 1;
        }
        std::cout << "[ANALYZE] Running NavMesh obstacle analyzer on: " << filePath << " (" << nav.GetAreaCount() << " areas)...\n";

        size_t crouchCount = 0, preciseCount = 0, jumpCount = 0, modCount = 0;
        for (NavArea* area : nav.GetAreas()) {
            if (!area) continue;
            uint8_t curr = area->GetAttributes();
            uint8_t detected = curr;

            if (bsp) {
                Vector3 center = area->GetCenter();
                Vector3 start(center.x, center.y, center.z + 4.0f);
                Vector3 end(center.x, center.y, center.z + 74.0f);
                BSPTraceResult tr;
                if (bsp->TraceWorld(start, end, HULL_POINT, &tr) && !tr.startsolid && !tr.allsolid && tr.fraction < 1.0f) {
                    float clearance = tr.endpos.z - center.z;
                    if (clearance < 72.0f && clearance >= 24.0f) {
                        detected |= NAV_ATTR_CROUCH;
                        if (!(curr & NAV_ATTR_CROUCH)) crouchCount++;
                    }
                }
            }

            float w = area->GetExtent().hi.x - area->GetExtent().lo.x;
            float l = area->GetExtent().hi.y - area->GetExtent().lo.y;
            if (w < 48.0f || l < 48.0f) {
                detected |= NAV_ATTR_PRECISE;
                if (!(curr & NAV_ATTR_PRECISE)) preciseCount++;
            }

            for (int d = 0; d < 4; ++d) {
                for (const auto& conn : area->GetAdjacentList(static_cast<NavDirType>(d))) {
                    if (conn.area && conn.area->GetCenter().z - area->GetCenter().z > 18.0f) {
                        detected |= NAV_ATTR_JUMP;
                        if (!(curr & NAV_ATTR_JUMP)) jumpCount++;
                        break;
                    }
                }
            }

            if (detected != curr) {
                area->SetAttributes(detected);
                modCount++;
            }
        }

        std::cout << "[SUCCESS] NavMesh analysis complete:\n";
        std::cout << "  -> Low Headroom (< 72u):     " << crouchCount << " Crouch flags assigned\n";
        std::cout << "  -> Narrow Passages (< 48u):  " << preciseCount << " Precise flags assigned\n";
        std::cout << "  -> Step Obstacles (> 18u):   " << jumpCount << " Jump flags assigned\n";
        std::cout << "  -> Total Modified Areas:     " << modCount << "\n";
        if (modCount > 0) {
            nav.Save(filePath);
            std::cout << "  -> Saved updated NavMesh to: " << filePath << "\n";
        }
        return 0;
    } else {
        WaypointGraph graph;
        if (!graph.Load(filePath)) {
            std::cerr << "[ERROR] Failed to load waypoint file: " << filePath << "\n";
            return 1;
        }
        std::cout << "[ANALYZE] Running Bot Waypoint analyzer (CS-EBOT & YaPB rules) on: "
                  << filePath << " (" << graph.GetNodeCount() << " nodes)...\n";
        auto stats = graph.AnalyzeGraph(bsp.get(), gameMod);
        std::cout << "[SUCCESS] Bot Waypoint analysis complete:\n";
        std::cout << "  -> Low Ceiling (< 72u):        " << stats.crouchAssigned << " Crouch flags assigned\n";
        std::cout << "  -> Step Obstacles (> 18u):     " << stats.jumpAssigned << " Jump flags/links assigned\n";
        std::cout << "  -> High Ledges / Cliffs:       " << stats.fallRiskAssigned << " Fall Risk flags assigned\n";
        std::cout << "  -> Ambush Sightlines:          " << stats.campAnglesCalculated << " Camp/Sniper angles calculated\n";
        std::cout << "  -> Barricades / Dead-Ends:     " << stats.zombieCampsAssigned << " Camp mesh flags assigned\n";
        std::cout << "  -> Obstructed Paths:           " << stats.blockedLinksPruned << " Blocked links pruned\n";
        std::cout << "  -> Total Modified Nodes:       " << stats.totalModified << "\n";
        if (stats.totalModified > 0) {
            graph.Save(filePath, graph.GetActiveBot(), gameMod);
            std::cout << "  -> Saved updated waypoints to: " << filePath << "\n";
        }
        return 0;
    }
}

static int HandleGenerateWaypoints(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Error: 'generate-waypoints' requires at least a path to a .bsp file.\n";
        PrintHelp();
        return 1;
    }

    std::string bspPath = argv[2];
    std::string outPath = "";
    EBotGenerateOptions options;
    BotType botType = BotType::EBot;
    GameMod mod = GameMod::Standard;

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--output" || arg == "-o") {
            if (i + 1 < argc) outPath = argv[++i];
        } else if (arg == "--bot") {
            if (i + 1 < argc) {
                std::string b = argv[++i];
                if (b == "sypb") botType = BotType::SyPB;
                else if (b == "yapb") botType = BotType::YaPB;
                else if (b == "podbot" || b == "pod") botType = BotType::PODBot;
                else botType = BotType::EBot;
            }
        } else if (arg == "--mod") {
            if (i + 1 < argc) {
                std::string m = argv[++i];
                if (m == "zp" || m == "zombie") mod = GameMod::ZombiePlague;
                else if (m == "dm" || m == "deathmatch") mod = GameMod::Deathmatch;
                else mod = GameMod::Standard;
            }
        } else if (arg == "--spacing") {
            if (i + 1 < argc) options.nodeSpacing = std::stof(argv[++i]);
        } else if (arg == "--min-dist") {
            if (i + 1 < argc) options.minDistance = std::stof(argv[++i]);
        } else if (arg == "--connect-radius") {
            if (i + 1 < argc) options.connectRadius = std::stof(argv[++i]);
        } else if (arg == "--no-camp") {
            options.generateCamps = false;
        } else if (arg == "--no-ladders") {
            options.generateLadders = false;
        } else if (outPath.empty() && arg.find('.') != std::string::npos) {
            outPath = arg;
        }
    }

    if (outPath.empty()) {
        std::string ext = ".ewp";
        if (botType == BotType::SyPB) ext = ".spt";
        else if (botType == BotType::YaPB) ext = ".pwf";
        else if (botType == BotType::PODBot) ext = ".wpt";

        if (bspPath.size() > 4 && bspPath.substr(bspPath.size() - 4) == ".bsp") {
            outPath = bspPath.substr(0, bspPath.size() - 4) + ext;
        } else {
            outPath = bspPath + ext;
        }
    }

    options.botType = botType;
    options.mod = mod;

    BSPFile bsp;
    if (!bsp.Load(bspPath)) {
        std::cerr << "[ERROR] Failed to load BSP file: " << bspPath << "\n";
        return 1;
    }

    WaypointGraph graph;
    std::cout << "[GENERATE-WAYPOINTS] Map: " << bspPath << "\n";
    std::cout << "[GENERATE-WAYPOINTS] Output: " << outPath << "\n";

    auto progressCallback = [](float progress, const std::string& msg) {
        std::cout << "\r[" << std::setw(3) << static_cast<int>(progress * 100.0f) << "%] "
                  << msg << std::string(20, ' ') << std::flush;
    };

    auto res = EBotGenerator::Generate(bsp, graph, options, progressCallback);
    std::cout << "\n\n";

    if (res.success) {
        if (!graph.Save(outPath, botType, mod)) {
            std::cerr << "[ERROR] Failed to save waypoints to: " << outPath << "\n";
            return 1;
        }
        std::cout << "[SUCCESS] Generated Bot Waypoints successfully!\n";
        std::cout << "  -> Total Waypoints:   " << res.waypointsCreated << "\n";
        std::cout << "  -> Total Connections: " << res.connectionsCreated << "\n";
        std::cout << "  -> Ladder Nodes:      " << res.laddersCreated << "\n";
        std::cout << "  -> Tactical Camps:    " << res.campPointsCreated << "\n";
        std::cout << "  -> Sniper Perches:    " << res.sniperPointsCreated << "\n";
        if (res.zombieCampsCreated > 0) {
            std::cout << "  -> Zombie Camps:      " << res.zombieCampsCreated << "\n";
        }
        std::cout << "  -> Elapsed Time:      " << std::fixed << std::setprecision(2) << res.durationSeconds << " s\n";
        std::cout << "  -> Saved to:          " << outPath << "\n";
        return 0;
    } else {
        std::cerr << "[ERROR] Generation failed: " << res.errorMessage << "\n";
        return 1;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "=========================================================\n";
    std::cout << " NavStudio CLI v1.6.8 - CS 1.6 BSP & NAV Tool\n";
    std::cout << "=========================================================\n\n";

    if (argc < 2) {
        std::cout << "Interactive Mode (Console opened with no arguments)\n\n";
        std::cout << "Select an action:\n";
        std::cout << "  [1] Generate Navigation Mesh for a BSP map\n";
        std::cout << "  [2] Batch Generate Navigation Meshes for a directory\n";
        std::cout << "  [3] Inspect & Verify a BSP or NAV file\n";
        std::cout << "  [4] Convert between NavMesh and Bot Waypoints\n";
        std::cout << "  [5] Auto-Analyze NavMesh or Bot Waypoints\n";
        std::cout << "  [6] Generate Bot Waypoints for a BSP map (CS-EBOT)\n";
        std::cout << "  [7] View Help & Command-Line Usage\n";
        std::cout << "  [0] Exit\n\n";
        std::cout << "Enter choice [0-7]: ";

        std::string choice;
        if (!std::getline(std::cin, choice) || choice == "0" || choice == "q" || choice == "exit") {
            return 0;
        }

        auto CleanPath = [](std::string p) {
            while (!p.empty() && (p.front() == ' ' || p.front() == '"' || p.front() == '\'')) p.erase(p.begin());
            while (!p.empty() && (p.back() == ' ' || p.back() == '"' || p.back() == '\'')) p.pop_back();
            return p;
        };

        if (choice == "1") {
            std::cout << "\nEnter path to .bsp file (or drag-and-drop file here): ";
            std::string path;
            std::getline(std::cin, path);
            path = CleanPath(path);
            if (!path.empty()) {
                char* customArgv[] = { argv[0], (char*)"generate", (char*)path.c_str() };
                HandleGenerate(3, customArgv);
            }
        } else if (choice == "2") {
            std::cout << "\nEnter directory path containing .bsp files: ";
            std::string dirPath;
            std::getline(std::cin, dirPath);
            dirPath = CleanPath(dirPath);
            if (!dirPath.empty()) {
                char* customArgv[] = { argv[0], (char*)"batch", (char*)dirPath.c_str() };
                HandleBatch(3, customArgv);
            }
        } else if (choice == "3") {
            std::cout << "\nEnter path to .bsp or .nav file: ";
            std::string path;
            std::getline(std::cin, path);
            path = CleanPath(path);
            if (!path.empty()) {
                std::string bspP = (path.size() > 4 && path.substr(path.size() - 4) == ".bsp") ? path : "";
                std::string navP = (path.size() > 4 && path.substr(path.size() - 4) == ".nav") ? path : "";
                HandleInspect(bspP, navP);
            }
        } else if (choice == "4") {
            std::cout << "\nEnter input file (.nav, .ewp, .spt, .pwf, .wpt): ";
            std::string inPath;
            std::getline(std::cin, inPath);
            inPath = CleanPath(inPath);
            std::cout << "Enter output file: ";
            std::string outPath;
            std::getline(std::cin, outPath);
            outPath = CleanPath(outPath);
            if (!inPath.empty() && !outPath.empty()) {
                char* customArgv[] = { argv[0], (char*)"convert", (char*)inPath.c_str(), (char*)outPath.c_str() };
                HandleConvert(4, customArgv);
            }
        } else if (choice == "5") {
            std::cout << "\nEnter path to .nav or waypoint file to analyze: ";
            std::string filePath;
            std::getline(std::cin, filePath);
            filePath = CleanPath(filePath);
            if (!filePath.empty()) {
                char* customArgv[] = { argv[0], (char*)"analyze", (char*)filePath.c_str() };
                HandleAnalyze(3, customArgv);
            }
        } else if (choice == "6") {
            std::cout << "\nEnter path to .bsp file to generate waypoints for: ";
            std::string bspPath;
            std::getline(std::cin, bspPath);
            bspPath = CleanPath(bspPath);
            if (!bspPath.empty()) {
                char* customArgv[] = { argv[0], (char*)"generate-waypoints", (char*)bspPath.c_str() };
                HandleGenerateWaypoints(3, customArgv);
            }
        } else {
            PrintHelp();
        }

        std::cout << "\nPress Enter to exit...";
        std::string dummy;
        std::getline(std::cin, dummy);
        return 0;
    }

    if (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h" ||
        std::string(argv[1]) == "-v" || std::string(argv[1]) == "--version") {
        PrintHelp();
        return 0;
    }

    std::string firstArg = argv[1];
    if (firstArg == "generate" || firstArg == "-g") {
        return HandleGenerate(argc, argv);
    }
    if (firstArg == "generate-waypoints" || firstArg == "gen-wpt" || firstArg == "-gw") {
        return HandleGenerateWaypoints(argc, argv);
    }
    if (firstArg == "batch" || firstArg == "mass" || firstArg == "generate-all") {
        return HandleBatch(argc, argv);
    }
    if (firstArg == "convert" || firstArg == "-c") {
        return HandleConvert(argc, argv);
    }
    if (firstArg == "analyze" || firstArg == "-a") {
        return HandleAnalyze(argc, argv);
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

    return HandleInspect(bspPath, navPath);
}



