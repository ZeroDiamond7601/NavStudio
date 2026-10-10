#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

struct EditorPreferences {
    // General
    float defaultGridSize{25.0f};
    bool defaultGridSnap{true};
    bool defaultMeshSnap{true};
    float meshSnapTolerance{4.0f};
    float cornerSnapTolerance{8.0f};
    bool enableCollinearSnap{true};
    float collinearSnapTolerance{6.0f};
    bool enableSnapToEdgeOnMove{true};
    bool autoConnectOnEdgeSnap{true};
    bool extrudeCameraFacing{true};
    int maxUndoSteps{100};
    bool enableAutosave{true};
    int autosaveIntervalMinutes{5};
    bool enableWaypointAutosave{true};
    int maxBackupsToKeep{5};
    bool confirmDeleteArea{false};
    bool showTooltips{true};
    float statusToastDuration{3.0f};
    int maxRecentFiles{10};

    // Precision Nudge & Gizmo Defaults
    float defaultLinearNudgeStep{16.0f};
    float defaultAngularNudgeStep{15.0f};
    bool showGizmoNudgeHUD{true};

    // Navigation & NavMesh Generation Defaults
    float defaultGenStep{25.0f};
    float maxStepHeight{18.0f};
    float navMaxJumpHeight{58.0f};
    float navMaxDropHeight{300.0f};
    bool navDetectCrouchVents{true};
    bool navConnectJumpSpots{true};
    bool navLinkLadders{true};
    bool navSeedLadderTops{true};
    bool autoOptimizeAfterGen{true};
    bool floodFillSmoothSeams{true};
    float navMeshOpacity{0.65f};
    bool navWireframeEdges{true};
    bool navShowAreaIDs{true};
    float navLadderConnectRadius{250.0f};
    float navCrouchCeilingThreshold{68.0f};

    // Bot Waypoint System Defaults
    float defaultWptSpacing{160.0f};
    float defaultWptMinDist{110.0f};
    float defaultWptConnectRadius{190.0f};
    float defaultWptMaxJump{55.0f};
    bool defaultWptPruneCrossing{true};
    bool defaultWptPruneCollinear{true};
    float defaultWptCollinearAngle{18.0f};
    bool defaultWptCalcWayzones{true};
    int defaultBotType{0}; // 0: EBot, 1: SyPB, 2: YaPB, 3: PODBot
    int defaultGameMod{0}; // 0: Standard, 1: ZombiePlague, 2: Deathmatch
    bool defaultShowWptDirection{false};
    bool defaultShowWptRadii{true};
    bool defaultShowWptConnections{true};
    float wptLineWidth{3.5f};
    float wayzoneAlpha{0.35f};
    float wptNodeRenderSize{6.0f};
    bool wptShowLabels{true};
    float wptMaxLabelDistance{900.0f};
    bool wptShowCampAngles{true};
    bool wptAutoFloorSnap{true};
    float wptPenTraceSpacing{120.0f};

    // Viewport & Camera
    float cameraMoveSpeed{800.0f};
    float cameraFastMultiplier{2.5f};
    float mouseSensitivity{0.15f};
    bool invertY{false};
    float fieldOfView{70.0f};
    float nearClipDistance{4.0f};
    float farClipDistance{16000.0f};
    bool cameraSmoothFlight{true};
    float cameraSmoothFactor{12.0f};
    float cameraSpeedScrollStep{50.0f};
    int viewportBkgColor{0}; // 0: Modern Slate, 1: Deep Navy, 2: Warm Grey, 3: Hammer Classic
    int navigationPreset{0}; // 0: FPS Flycam (Default), 1: Blender Turntable, 2: Valve Hammer

    // Visuals & Overlays
    bool showConnectionValidity{true};
    bool show3DSkybox{true};
    bool showFps{true};
    bool showCompass{true};
    bool showClearanceHullDefault{false};
    bool showViewportBottomHUD{true};
    bool showTutorialOnStartup{false};
    int bspWireframeColor{0}; // 0: Cyan Blue, 1: Clean White, 2: GoldSrc Amber, 3: Matrix Green
    float bspGhostAlpha{0.25f};
    bool showSelectedAABB{true};
    bool showEntityModels{true};
    int themeIndex{0}; // 0: Modern Slate, 1: Classic GoldSrc, 2: Clean Studio, 3: Light Studio

    // Recent Files History
    std::vector<std::string> recentFiles;

    void AddRecentFile(const std::string& path) {
        if (path.empty()) return;
        recentFiles.erase(std::remove(recentFiles.begin(), recentFiles.end(), path), recentFiles.end());
        recentFiles.insert(recentFiles.begin(), path);
        if (recentFiles.size() > static_cast<size_t>(maxRecentFiles)) {
            recentFiles.resize(static_cast<size_t>(maxRecentFiles));
        }
        Save();
    }

    void ResetToDefaults() {
        defaultGridSize = 25.0f;
        defaultGridSnap = true;
        defaultMeshSnap = true;
        meshSnapTolerance = 4.0f;
        cornerSnapTolerance = 8.0f;
        enableCollinearSnap = true;
        collinearSnapTolerance = 6.0f;
        enableSnapToEdgeOnMove = true;
        autoConnectOnEdgeSnap = true;
        extrudeCameraFacing = true;
        maxUndoSteps = 100;
        enableAutosave = true;
        autosaveIntervalMinutes = 5;
        enableWaypointAutosave = true;
        maxBackupsToKeep = 5;
        confirmDeleteArea = false;
        showTooltips = true;
        statusToastDuration = 3.0f;
        maxRecentFiles = 10;

        defaultLinearNudgeStep = 16.0f;
        defaultAngularNudgeStep = 15.0f;
        showGizmoNudgeHUD = true;

        defaultGenStep = 25.0f;
        maxStepHeight = 18.0f;
        navMaxJumpHeight = 58.0f;
        navMaxDropHeight = 300.0f;
        navDetectCrouchVents = true;
        navConnectJumpSpots = true;
        navLinkLadders = true;
        navSeedLadderTops = true;
        autoOptimizeAfterGen = true;
        floodFillSmoothSeams = true;
        navMeshOpacity = 0.65f;
        navWireframeEdges = true;
        navShowAreaIDs = true;
        navLadderConnectRadius = 250.0f;
        navCrouchCeilingThreshold = 68.0f;

        defaultWptSpacing = 160.0f;
        defaultWptMinDist = 110.0f;
        defaultWptConnectRadius = 190.0f;
        defaultWptMaxJump = 55.0f;
        defaultWptPruneCrossing = true;
        defaultWptPruneCollinear = true;
        defaultWptCollinearAngle = 18.0f;
        defaultWptCalcWayzones = true;
        defaultBotType = 0;
        defaultGameMod = 0;
        defaultShowWptDirection = false;
        defaultShowWptRadii = true;
        defaultShowWptConnections = true;
        wptLineWidth = 3.5f;
        wayzoneAlpha = 0.35f;
        wptNodeRenderSize = 6.0f;
        wptShowLabels = true;
        wptMaxLabelDistance = 900.0f;
        wptShowCampAngles = true;
        wptAutoFloorSnap = true;
        wptPenTraceSpacing = 120.0f;

        cameraMoveSpeed = 800.0f;
        cameraFastMultiplier = 2.5f;
        mouseSensitivity = 0.15f;
        invertY = false;
        fieldOfView = 70.0f;
        nearClipDistance = 4.0f;
        farClipDistance = 16000.0f;
        cameraSmoothFlight = true;
        cameraSmoothFactor = 12.0f;
        cameraSpeedScrollStep = 50.0f;
        viewportBkgColor = 0;
        navigationPreset = 0;

        showConnectionValidity = true;
        show3DSkybox = true;
        showFps = true;
        showCompass = true;
        showClearanceHullDefault = false;
        showViewportBottomHUD = true;
        showTutorialOnStartup = false;
        bspWireframeColor = 0;
        bspGhostAlpha = 0.25f;
        showSelectedAABB = true;
        showEntityModels = true;
        themeIndex = 0;
    }

    void Load(const std::string& path = "navstudio_prefs.ini") {
        std::ifstream file(path);
        if (!file.is_open()) return;

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.erase(key.begin());
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r')) val.pop_back();

            try {
                if (key == "defaultGridSize") defaultGridSize = std::stof(val);
                else if (key == "defaultGridSnap") defaultGridSnap = (val == "1" || val == "true");
                else if (key == "defaultMeshSnap") defaultMeshSnap = (val == "1" || val == "true");
                else if (key == "meshSnapTolerance") meshSnapTolerance = std::stof(val);
                else if (key == "cornerSnapTolerance") cornerSnapTolerance = std::stof(val);
                else if (key == "enableCollinearSnap") enableCollinearSnap = (val == "1" || val == "true");
                else if (key == "collinearSnapTolerance") collinearSnapTolerance = std::stof(val);
                else if (key == "enableSnapToEdgeOnMove") enableSnapToEdgeOnMove = (val == "1" || val == "true");
                else if (key == "autoConnectOnEdgeSnap") autoConnectOnEdgeSnap = (val == "1" || val == "true");
                else if (key == "extrudeCameraFacing") extrudeCameraFacing = (val == "1" || val == "true");
                else if (key == "maxUndoSteps") maxUndoSteps = std::stoi(val);
                else if (key == "enableAutosave") enableAutosave = (val == "1" || val == "true");
                else if (key == "autosaveIntervalMinutes") autosaveIntervalMinutes = std::stoi(val);
                else if (key == "enableWaypointAutosave") enableWaypointAutosave = (val == "1" || val == "true");
                else if (key == "maxBackupsToKeep") maxBackupsToKeep = std::stoi(val);
                else if (key == "confirmDeleteArea") confirmDeleteArea = (val == "1" || val == "true");
                else if (key == "showTooltips") showTooltips = (val == "1" || val == "true");
                else if (key == "statusToastDuration") statusToastDuration = std::stof(val);
                else if (key == "maxRecentFiles") maxRecentFiles = std::stoi(val);
                else if (key == "defaultLinearNudgeStep") defaultLinearNudgeStep = std::stof(val);
                else if (key == "defaultAngularNudgeStep") defaultAngularNudgeStep = std::stof(val);
                else if (key == "showGizmoNudgeHUD") showGizmoNudgeHUD = (val == "1" || val == "true");
                else if (key == "defaultGenStep") defaultGenStep = std::stof(val);
                else if (key == "maxStepHeight") maxStepHeight = std::stof(val);
                else if (key == "navMaxJumpHeight") navMaxJumpHeight = std::stof(val);
                else if (key == "navMaxDropHeight") navMaxDropHeight = std::stof(val);
                else if (key == "navDetectCrouchVents") navDetectCrouchVents = (val == "1" || val == "true");
                else if (key == "navConnectJumpSpots") navConnectJumpSpots = (val == "1" || val == "true");
                else if (key == "navLinkLadders") navLinkLadders = (val == "1" || val == "true");
                else if (key == "navSeedLadderTops") navSeedLadderTops = (val == "1" || val == "true");
                else if (key == "autoOptimizeAfterGen") autoOptimizeAfterGen = (val == "1" || val == "true");
                else if (key == "floodFillSmoothSeams") floodFillSmoothSeams = (val == "1" || val == "true");
                else if (key == "navMeshOpacity") navMeshOpacity = std::stof(val);
                else if (key == "navWireframeEdges") navWireframeEdges = (val == "1" || val == "true");
                else if (key == "navShowAreaIDs") navShowAreaIDs = (val == "1" || val == "true");
                else if (key == "navLadderConnectRadius") navLadderConnectRadius = std::stof(val);
                else if (key == "navCrouchCeilingThreshold") navCrouchCeilingThreshold = std::stof(val);
                else if (key == "defaultWptSpacing") defaultWptSpacing = std::stof(val);
                else if (key == "defaultWptMinDist") defaultWptMinDist = std::stof(val);
                else if (key == "defaultWptConnectRadius") defaultWptConnectRadius = std::stof(val);
                else if (key == "defaultWptMaxJump") defaultWptMaxJump = std::stof(val);
                else if (key == "defaultWptPruneCrossing") defaultWptPruneCrossing = (val == "1" || val == "true");
                else if (key == "defaultWptPruneCollinear") defaultWptPruneCollinear = (val == "1" || val == "true");
                else if (key == "defaultWptCollinearAngle") defaultWptCollinearAngle = std::stof(val);
                else if (key == "defaultWptCalcWayzones") defaultWptCalcWayzones = (val == "1" || val == "true");
                else if (key == "defaultBotType") defaultBotType = std::stoi(val);
                else if (key == "defaultGameMod") defaultGameMod = std::stoi(val);
                else if (key == "defaultShowWptDirection") defaultShowWptDirection = (val == "1" || val == "true");
                else if (key == "defaultShowWptRadii") defaultShowWptRadii = (val == "1" || val == "true");
                else if (key == "defaultShowWptConnections") defaultShowWptConnections = (val == "1" || val == "true");
                else if (key == "wptLineWidth") wptLineWidth = std::stof(val);
                else if (key == "wayzoneAlpha") wayzoneAlpha = std::stof(val);
                else if (key == "wptNodeRenderSize") wptNodeRenderSize = std::stof(val);
                else if (key == "wptShowLabels") wptShowLabels = (val == "1" || val == "true");
                else if (key == "wptMaxLabelDistance") wptMaxLabelDistance = std::stof(val);
                else if (key == "wptShowCampAngles") wptShowCampAngles = (val == "1" || val == "true");
                else if (key == "wptAutoFloorSnap") wptAutoFloorSnap = (val == "1" || val == "true");
                else if (key == "wptPenTraceSpacing") wptPenTraceSpacing = std::stof(val);
                else if (key == "cameraMoveSpeed") cameraMoveSpeed = std::stof(val);
                else if (key == "cameraFastMultiplier") cameraFastMultiplier = std::stof(val);
                else if (key == "mouseSensitivity") mouseSensitivity = std::stof(val);
                else if (key == "invertY") invertY = (val == "1" || val == "true");
                else if (key == "fieldOfView") fieldOfView = std::stof(val);
                else if (key == "nearClipDistance") nearClipDistance = std::stof(val);
                else if (key == "farClipDistance") farClipDistance = std::stof(val);
                else if (key == "cameraSmoothFlight") cameraSmoothFlight = (val == "1" || val == "true");
                else if (key == "cameraSmoothFactor") cameraSmoothFactor = std::stof(val);
                else if (key == "cameraSpeedScrollStep") cameraSpeedScrollStep = std::stof(val);
                else if (key == "viewportBkgColor") viewportBkgColor = std::stoi(val);
                else if (key == "navigationPreset") navigationPreset = std::stoi(val);
                else if (key == "showConnectionValidity") showConnectionValidity = (val == "1" || val == "true");
                else if (key == "show3DSkybox") show3DSkybox = (val == "1" || val == "true");
                else if (key == "showFps") showFps = (val == "1" || val == "true");
                else if (key == "showCompass") showCompass = (val == "1" || val == "true");
                else if (key == "showClearanceHullDefault") showClearanceHullDefault = (val == "1" || val == "true");
                else if (key == "showViewportBottomHUD") showViewportBottomHUD = (val == "1" || val == "true");
                else if (key == "showTutorialOnStartup") showTutorialOnStartup = (val == "1" || val == "true");
                else if (key == "bspWireframeColor") bspWireframeColor = std::stoi(val);
                else if (key == "bspGhostAlpha") bspGhostAlpha = std::stof(val);
                else if (key == "showSelectedAABB") showSelectedAABB = (val == "1" || val == "true");
                else if (key == "showEntityModels") showEntityModels = (val == "1" || val == "true");
                else if (key == "themeIndex") themeIndex = std::stoi(val);
                else if (key == "recentFiles") {
                    recentFiles.clear();
                    std::stringstream ss(val);
                    std::string item;
                    while (std::getline(ss, item, ';')) {
                        if (!item.empty()) recentFiles.push_back(item);
                    }
                }
            } catch (...) {}
        }
    }

    void Save(const std::string& path = "navstudio_prefs.ini") const {
        std::ofstream file(path);
        if (!file.is_open()) return;

        file << "# NavStudio Preferences\n";
        file << "defaultGridSize=" << defaultGridSize << "\n";
        file << "defaultGridSnap=" << (defaultGridSnap ? "1" : "0") << "\n";
        file << "defaultMeshSnap=" << (defaultMeshSnap ? "1" : "0") << "\n";
        file << "meshSnapTolerance=" << meshSnapTolerance << "\n";
        file << "cornerSnapTolerance=" << cornerSnapTolerance << "\n";
        file << "enableCollinearSnap=" << (enableCollinearSnap ? "1" : "0") << "\n";
        file << "collinearSnapTolerance=" << collinearSnapTolerance << "\n";
        file << "enableSnapToEdgeOnMove=" << (enableSnapToEdgeOnMove ? "1" : "0") << "\n";
        file << "autoConnectOnEdgeSnap=" << (autoConnectOnEdgeSnap ? "1" : "0") << "\n";
        file << "extrudeCameraFacing=" << (extrudeCameraFacing ? "1" : "0") << "\n";
        file << "maxUndoSteps=" << maxUndoSteps << "\n";
        file << "enableAutosave=" << (enableAutosave ? "1" : "0") << "\n";
        file << "autosaveIntervalMinutes=" << autosaveIntervalMinutes << "\n";
        file << "enableWaypointAutosave=" << (enableWaypointAutosave ? "1" : "0") << "\n";
        file << "maxBackupsToKeep=" << maxBackupsToKeep << "\n";
        file << "confirmDeleteArea=" << (confirmDeleteArea ? "1" : "0") << "\n";
        file << "showTooltips=" << (showTooltips ? "1" : "0") << "\n";
        file << "statusToastDuration=" << statusToastDuration << "\n";
        file << "maxRecentFiles=" << maxRecentFiles << "\n";
        file << "defaultLinearNudgeStep=" << defaultLinearNudgeStep << "\n";
        file << "defaultAngularNudgeStep=" << defaultAngularNudgeStep << "\n";
        file << "showGizmoNudgeHUD=" << (showGizmoNudgeHUD ? "1" : "0") << "\n";
        file << "defaultGenStep=" << defaultGenStep << "\n";
        file << "maxStepHeight=" << maxStepHeight << "\n";
        file << "navMaxJumpHeight=" << navMaxJumpHeight << "\n";
        file << "navMaxDropHeight=" << navMaxDropHeight << "\n";
        file << "navDetectCrouchVents=" << (navDetectCrouchVents ? "1" : "0") << "\n";
        file << "navConnectJumpSpots=" << (navConnectJumpSpots ? "1" : "0") << "\n";
        file << "navLinkLadders=" << (navLinkLadders ? "1" : "0") << "\n";
        file << "navSeedLadderTops=" << (navSeedLadderTops ? "1" : "0") << "\n";
        file << "autoOptimizeAfterGen=" << (autoOptimizeAfterGen ? "1" : "0") << "\n";
        file << "floodFillSmoothSeams=" << (floodFillSmoothSeams ? "1" : "0") << "\n";
        file << "navMeshOpacity=" << navMeshOpacity << "\n";
        file << "navWireframeEdges=" << (navWireframeEdges ? "1" : "0") << "\n";
        file << "navShowAreaIDs=" << (navShowAreaIDs ? "1" : "0") << "\n";
        file << "navLadderConnectRadius=" << navLadderConnectRadius << "\n";
        file << "navCrouchCeilingThreshold=" << navCrouchCeilingThreshold << "\n";
        file << "defaultWptSpacing=" << defaultWptSpacing << "\n";
        file << "defaultWptMinDist=" << defaultWptMinDist << "\n";
        file << "defaultWptConnectRadius=" << defaultWptConnectRadius << "\n";
        file << "defaultWptMaxJump=" << defaultWptMaxJump << "\n";
        file << "defaultWptPruneCrossing=" << (defaultWptPruneCrossing ? "1" : "0") << "\n";
        file << "defaultWptPruneCollinear=" << (defaultWptPruneCollinear ? "1" : "0") << "\n";
        file << "defaultWptCollinearAngle=" << defaultWptCollinearAngle << "\n";
        file << "defaultWptCalcWayzones=" << (defaultWptCalcWayzones ? "1" : "0") << "\n";
        file << "defaultBotType=" << defaultBotType << "\n";
        file << "defaultGameMod=" << defaultGameMod << "\n";
        file << "defaultShowWptDirection=" << (defaultShowWptDirection ? "1" : "0") << "\n";
        file << "defaultShowWptRadii=" << (defaultShowWptRadii ? "1" : "0") << "\n";
        file << "defaultShowWptConnections=" << (defaultShowWptConnections ? "1" : "0") << "\n";
        file << "wptLineWidth=" << wptLineWidth << "\n";
        file << "wayzoneAlpha=" << wayzoneAlpha << "\n";
        file << "wptNodeRenderSize=" << wptNodeRenderSize << "\n";
        file << "wptShowLabels=" << (wptShowLabels ? "1" : "0") << "\n";
        file << "wptMaxLabelDistance=" << wptMaxLabelDistance << "\n";
        file << "wptShowCampAngles=" << (wptShowCampAngles ? "1" : "0") << "\n";
        file << "wptAutoFloorSnap=" << (wptAutoFloorSnap ? "1" : "0") << "\n";
        file << "wptPenTraceSpacing=" << wptPenTraceSpacing << "\n";
        file << "cameraMoveSpeed=" << cameraMoveSpeed << "\n";
        file << "cameraFastMultiplier=" << cameraFastMultiplier << "\n";
        file << "mouseSensitivity=" << mouseSensitivity << "\n";
        file << "invertY=" << (invertY ? "1" : "0") << "\n";
        file << "fieldOfView=" << fieldOfView << "\n";
        file << "nearClipDistance=" << nearClipDistance << "\n";
        file << "farClipDistance=" << farClipDistance << "\n";
        file << "cameraSmoothFlight=" << (cameraSmoothFlight ? "1" : "0") << "\n";
        file << "cameraSmoothFactor=" << cameraSmoothFactor << "\n";
        file << "cameraSpeedScrollStep=" << cameraSpeedScrollStep << "\n";
        file << "viewportBkgColor=" << viewportBkgColor << "\n";
        file << "navigationPreset=" << navigationPreset << "\n";
        file << "showConnectionValidity=" << (showConnectionValidity ? "1" : "0") << "\n";
        file << "show3DSkybox=" << (show3DSkybox ? "1" : "0") << "\n";
        file << "showFps=" << (showFps ? "1" : "0") << "\n";
        file << "showCompass=" << (showCompass ? "1" : "0") << "\n";
        file << "showClearanceHullDefault=" << (showClearanceHullDefault ? "1" : "0") << "\n";
        file << "showViewportBottomHUD=" << (showViewportBottomHUD ? "1" : "0") << "\n";
        file << "showTutorialOnStartup=" << (showTutorialOnStartup ? "1" : "0") << "\n";
        file << "bspWireframeColor=" << bspWireframeColor << "\n";
        file << "bspGhostAlpha=" << bspGhostAlpha << "\n";
        file << "showSelectedAABB=" << (showSelectedAABB ? "1" : "0") << "\n";
        file << "showEntityModels=" << (showEntityModels ? "1" : "0") << "\n";
        file << "themeIndex=" << themeIndex << "\n";

        std::string recStr;
        for (size_t i = 0; i < recentFiles.size(); ++i) {
            if (i > 0) recStr += ";";
            recStr += recentFiles[i];
        }
        file << "recentFiles=" << recStr << "\n";
    }
};
