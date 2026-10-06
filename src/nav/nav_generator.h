#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include "../math/vector3.h"
#include "nav_types.h"

class BSPFile;
class NavMesh;

struct NavGenerateOptions {
    float stepSize = 25.0f;              // Grid step size in world units (standard CS Bot value: 25.0)
    float maxStepHeight = 18.0f;         // Max step height walkable without jumping (standard: 18.0)
    float maxJumpHeight = 45.0f;         // Max jumpable step height (standard: 45.0)
    float maxDrop = 300.0f;              // Max drop height without death (standard: 300.0)
    float humanHeight = 72.0f;           // Standing player height (standard: 72.0)
    float crouchHeight = 36.0f;          // Crouched player height (standard: 36.0)
    float humanRadius = 16.0f;           // Player collision hull radius (standard: 16.0)
    float maxSlopeNormalZ = 0.7071f;     // Walkable floor slope threshold (~45 degrees)
    bool generateCrouch = true;          // Flag low-clearance areas with NAV_ATTR_CROUCH
    bool generateJumpConnections = true; // Create one-way jump drop connections
    bool generateLadders = true;         // Parse func_ladder entities and connect
    bool mergeAreas = true;              // Merge coplanar adjacent rectangular areas
    int maxThreads = 0;                  // Threads for batch generation (0 = auto-detect hardware cores)
};

struct NavGenerateResult {
    bool success = false;
    std::string errorMessage;
    size_t areasGenerated = 0;
    size_t connectionsCreated = 0;
    size_t laddersLinked = 0;
    double durationSeconds = 0.0;
};

// Callback for single map progress reporting: progress [0.0 - 1.0], message
using NavGenerateProgressCallback = std::function<void(float progress, const std::string& message)>;

class NavGenerator {
public:
    // Generate navigation mesh from an already loaded BSPFile into a NavMesh
    static NavGenerateResult Generate(
        const BSPFile& bsp,
        NavMesh& outNav,
        const NavGenerateOptions& options = NavGenerateOptions(),
        NavGenerateProgressCallback progress = nullptr
    );

    // Load BSP file, generate navigation mesh, and write out .nav file
    static NavGenerateResult GenerateToFile(
        const std::string& bspPath,
        const std::string& navPath,
        const NavGenerateOptions& options = NavGenerateOptions(),
        NavGenerateProgressCallback progress = nullptr
    );

    // Batch generation item
    struct BatchItem {
        std::string bspPath;
        std::string navPath;
        NavGenerateResult result;
    };

    struct BatchResult {
        size_t totalMaps = 0;
        size_t succeeded = 0;
        size_t failed = 0;
        double totalDurationSeconds = 0.0;
        std::vector<BatchItem> items;
    };

    // Callback for batch progress reporting
    using BatchProgressCallback = std::function<void(size_t completed, size_t total, const BatchItem& currentItem)>;

    // Mass-produce NavMeshes for an explicit list of BSP files
    static BatchResult GenerateBatch(
        const std::vector<std::string>& bspFiles,
        const std::string& outputDirectory = "", // empty = save beside each BSP file
        const NavGenerateOptions& options = NavGenerateOptions(),
        bool overwriteExisting = false,
        BatchProgressCallback batchProgress = nullptr
    );

    // Mass-produce NavMeshes by scanning a directory for *.bsp files
    static BatchResult GenerateDirectory(
        const std::string& directoryPath,
        const std::string& outputDirectory = "",
        const NavGenerateOptions& options = NavGenerateOptions(),
        bool recursive = false,
        bool overwriteExisting = false,
        BatchProgressCallback batchProgress = nullptr
    );
};
