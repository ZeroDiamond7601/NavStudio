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

    // Navigation & Generation
    float defaultGenStep{25.0f};
    float maxStepHeight{18.0f};
    bool autoOptimizeAfterGen{true};
    bool floodFillSmoothSeams{true};

    // Viewport & Camera
    float cameraMoveSpeed{800.0f};
    float cameraFastMultiplier{2.5f};
    float mouseSensitivity{0.15f};
    bool invertY{false};
    float fieldOfView{70.0f};

    // Visuals & Overlays
    bool showConnectionValidity{true};
    bool show3DSkybox{true};
    bool showFps{true};
    bool showCompass{true};
    int themeIndex{0}; // 0: Dark Modern, 1: Classic Dark, 2: Light

    // Recent Files History
    std::vector<std::string> recentFiles;

    void AddRecentFile(const std::string& path) {
        if (path.empty()) return;
        recentFiles.erase(std::remove(recentFiles.begin(), recentFiles.end(), path), recentFiles.end());
        recentFiles.insert(recentFiles.begin(), path);
        if (recentFiles.size() > 10) {
            recentFiles.resize(10);
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

        defaultGenStep = 25.0f;
        maxStepHeight = 18.0f;
        autoOptimizeAfterGen = true;
        floodFillSmoothSeams = true;

        cameraMoveSpeed = 800.0f;
        cameraFastMultiplier = 2.5f;
        mouseSensitivity = 0.15f;
        invertY = false;
        fieldOfView = 70.0f;

        showConnectionValidity = true;
        show3DSkybox = true;
        showFps = true;
        showCompass = true;
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
                else if (key == "defaultGenStep") defaultGenStep = std::stof(val);
                else if (key == "maxStepHeight") maxStepHeight = std::stof(val);
                else if (key == "autoOptimizeAfterGen") autoOptimizeAfterGen = (val == "1" || val == "true");
                else if (key == "floodFillSmoothSeams") floodFillSmoothSeams = (val == "1" || val == "true");
                else if (key == "cameraMoveSpeed") cameraMoveSpeed = std::stof(val);
                else if (key == "cameraFastMultiplier") cameraFastMultiplier = std::stof(val);
                else if (key == "mouseSensitivity") mouseSensitivity = std::stof(val);
                else if (key == "invertY") invertY = (val == "1" || val == "true");
                else if (key == "fieldOfView") fieldOfView = std::stof(val);
                else if (key == "showConnectionValidity") showConnectionValidity = (val == "1" || val == "true");
                else if (key == "show3DSkybox") show3DSkybox = (val == "1" || val == "true");
                else if (key == "showFps") showFps = (val == "1" || val == "true");
                else if (key == "showCompass") showCompass = (val == "1" || val == "true");
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
        file << "defaultGenStep=" << defaultGenStep << "\n";
        file << "maxStepHeight=" << maxStepHeight << "\n";
        file << "autoOptimizeAfterGen=" << (autoOptimizeAfterGen ? "1" : "0") << "\n";
        file << "floodFillSmoothSeams=" << (floodFillSmoothSeams ? "1" : "0") << "\n";
        file << "cameraMoveSpeed=" << cameraMoveSpeed << "\n";
        file << "cameraFastMultiplier=" << cameraFastMultiplier << "\n";
        file << "mouseSensitivity=" << mouseSensitivity << "\n";
        file << "invertY=" << (invertY ? "1" : "0") << "\n";
        file << "fieldOfView=" << fieldOfView << "\n";
        file << "showConnectionValidity=" << (showConnectionValidity ? "1" : "0") << "\n";
        file << "show3DSkybox=" << (show3DSkybox ? "1" : "0") << "\n";
        file << "showFps=" << (showFps ? "1" : "0") << "\n";
        file << "showCompass=" << (showCompass ? "1" : "0") << "\n";
        file << "themeIndex=" << themeIndex << "\n";

        std::string recStr;
        for (size_t i = 0; i < recentFiles.size(); ++i) {
            if (i > 0) recStr += ";";
            recStr += recentFiles[i];
        }
        file << "recentFiles=" << recStr << "\n";
    }
};
