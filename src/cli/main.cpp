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

static void PrintHelp() {
    std::cout << "Usage:\n";
    std::cout << "  nav_cli <path_to_bsp_or_nav> [optional_second_file]\n";
    std::cout << "  nav_cli generate <map.bsp> [output.nav] [options]\n";
    std::cout << "  nav_cli batch <maps_directory> [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  generate <map.bsp> [out.nav]  Auto-generate navigation mesh for a BSP map\n";
    std::cout << "  batch <maps_dir>              Mass-produce navigation meshes for all maps in directory\n";
    std::cout << "  <map.bsp|map.nav>             Verify BSP data, NAV headers, places, and A* pathfinding\n\n";
    std::cout << "Options:\n";
    std::cout << "  --output, -o <dir|file>       Specify output directory or file path\n";
    std::cout << "  --step <float>                Grid step size (default: 25.0)\n";
    std::cout << "  --threads <int>               Worker thread count for batch mode (default: CPU cores)\n";
    std::cout << "  --force, -f                   Overwrite existing .nav files\n";
    std::cout << "  --recursive, -r               Recursively search directories for BSP files\n";
    std::cout << "  --no-jump                     Disable jump drop connections\n";
    std::cout << "  --no-merge                    Disable adjacent coplanar area merging\n\n";
    std::cout << "Examples:\n";
    std::cout << "  nav_cli generate cstrike/maps/de_dust2.bsp\n";
    std::cout << "  nav_cli batch \"C:\\Steam\\Half-Life\\cstrike\\maps\" --threads 8\n";
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

int main(int argc, char* argv[]) {
    std::cout << "=========================================================\n";
    std::cout << " NavStudio CLI v1.5.2 - CS 1.6 BSP & NAV Tool\n";
    std::cout << "=========================================================\n\n";

    if (argc < 2) {
        // Interactive Console Session (keeps console open when double-clicked in Windows Explorer)
        std::cout << "Interactive Mode (Console opened with no arguments)\n\n";
        std::cout << "Select an action:\n";
        std::cout << "  [1] Generate Navigation Mesh for a BSP map\n";
        std::cout << "  [2] Batch Generate Navigation Meshes for a directory\n";
        std::cout << "  [3] Inspect & Verify a BSP or NAV file\n";
        std::cout << "  [4] View Help & Command-Line Usage\n";
        std::cout << "  [0] Exit\n\n";
        std::cout << "Enter choice [0-4]: ";

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
    if (firstArg == "batch" || firstArg == "mass" || firstArg == "generate-all") {
        return HandleBatch(argc, argv);
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


