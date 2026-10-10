#ifndef EBOT_GENERATOR_H
#define EBOT_GENERATOR_H

#include "bsp/bsp_file.h"
#include "waypoint/waypoint_graph.h"
#include <functional>
#include <string>

struct EBotGenerateOptions {
    float nodeSpacing{110.0f};       // Distance between adjacent waypoint nodes (standard CS-EBOT: 80-120u)
    float minDistance{75.0f};        // Minimum distance to prevent duplicate / cluttered nodes
    float maxStepHeight{18.0f};      // Maximum step height walkable without jumping (standard: 18u)
    float maxJumpHeight{45.0f};      // Maximum jumpable elevation (standard: 45u)
    float maxDropHeight{250.0f};     // Maximum safe drop (standard: 250u)
    float connectRadius{150.0f};     // Maximum connection link radius between nodes
    bool generateLadders{true};      // Parse func_ladder entities and generate ladder waypoints
    bool generateCamps{true};        // Detect camping and sniper perches with radial LOS
    GameMod mod{GameMod::Standard};  // Target game mod (Standard, ZombiePlague, Deathmatch)
    BotType botType{BotType::EBot};  // Target bot format
};

struct EBotGenerateResult {
    bool success{false};
    std::string errorMessage;
    size_t waypointsCreated{0};
    size_t connectionsCreated{0};
    size_t laddersCreated{0};
    size_t campPointsCreated{0};
    size_t sniperPointsCreated{0};
    size_t zombieCampsCreated{0};
    double durationSeconds{0.0};
};

using EBotProgressCallback = std::function<void(float progress, const std::string& message)>;

class EBotGenerator {
public:
    static EBotGenerateResult Generate(
        const BSPFile& bsp,
        WaypointGraph& outGraph,
        const EBotGenerateOptions& options = EBotGenerateOptions(),
        EBotProgressCallback progress = nullptr
    );
};

#endif // EBOT_GENERATOR_H
