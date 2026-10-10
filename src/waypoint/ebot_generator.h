#ifndef EBOT_GENERATOR_H
#define EBOT_GENERATOR_H

#include "bsp/bsp_file.h"
#include "waypoint/waypoint_graph.h"
#include <functional>
#include <string>

struct EBotGenerateOptions {
    float nodeSpacing{160.0f};       // Distance between adjacent waypoint nodes (optimized for wide wayzones)
    float minDistance{110.0f};       // Minimum distance to prevent duplicate / cluttered nodes
    float maxStepHeight{18.0f};      // Maximum step height walkable without jumping (standard: 18u)
    float maxJumpHeight{55.0f};      // Maximum jumpable elevation (crouch-jump up to 55u)
    float maxDropHeight{250.0f};     // Maximum safe drop (standard: 250u)
    float connectRadius{190.0f};     // Maximum connection link radius between nodes
    bool generateLadders{true};      // Parse func_ladder entities and generate ladder waypoints
    bool generateCamps{true};        // Detect camping and sniper perches with radial LOS
    bool generateParkour{true};      // Detect and link parkour jumps, crate climbs, and chasm leaps
    bool pruneCrossingLinks{true};   // Prune redundant diagonal cross-links to prevent bot confusion
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
    size_t parkourLinksCreated{0};
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
