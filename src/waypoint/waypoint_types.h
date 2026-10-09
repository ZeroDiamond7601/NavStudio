#ifndef WAYPOINT_TYPES_H
#define WAYPOINT_TYPES_H

#include "math/vector3.h"
#include <cstdint>
#include <string>
#include <vector>

// Target Bot Engines
enum class BotType : int {
    EBot = 0,    // CS-EBOT (.ewp) - v127 LZSS / v126 / v125
    SyPB = 1,    // SyPB (.spt / .pwf) - Zombie & tactical bot
    YaPB = 2,    // YaPB (.pwf) - Modern POD-Bot evolution
    PODBot = 3   // POD-Bot mm (.wpt / .pwf) - Classic GoldSrc bot
};

// Target Game Modes
enum class GameMod : int {
    Standard = 0,      // Normal CS 1.6 / CZ (Defusal, Hostage, VIP)
    ZombiePlague = 1,  // Zombie Plague, Biohazard, Zombie Escape
    Deathmatch = 2     // CSDM, GunGame, Team Deathmatch
};

// Universal Waypoint Bitflags (Unified super-set across EBot, SyPB, YaPB, POD-Bot)
enum WaypointFlags : uint32_t {
    WPT_FLAG_NONE             = 0,
    WPT_FLAG_LIFT             = (1 << 1),   // Wait for elevator / lift
    WPT_FLAG_CROUCH           = (1 << 2),   // Must duck to traverse
    WPT_FLAG_CROSSING         = (1 << 3),   // Navigation crossing / waypoint
    WPT_FLAG_GOAL             = (1 << 4),   // Mission objective (bomb site, hostage)
    WPT_FLAG_LADDER           = (1 << 5),   // Ladder climb waypoint
    WPT_FLAG_RESCUE           = (1 << 6),   // Hostage rescue zone
    WPT_FLAG_CAMP             = (1 << 7),   // Defense / camping perch
    WPT_FLAG_DJUMP            = (1 << 9),   // Double jump / boost assist
    WPT_FLAG_ZMHMCAMP         = (1 << 10),  // Zombie / human barricade camp
    WPT_FLAG_AVOID            = (1 << 11),  // Avoid unless necessary
    WPT_FLAG_USEBUTTON        = (1 << 12),  // Interactive button trigger
    WPT_FLAG_HMCAMPMESH       = (1 << 13),  // Human defense camp mesh
    WPT_FLAG_ZOMBIEONLY       = (1 << 14),  // Restrict to zombies only
    WPT_FLAG_HUMANONLY        = (1 << 15),  // Restrict to humans only
    WPT_FLAG_ZOMBIEPUSH       = (1 << 16),  // Directional zombie rush
    WPT_FLAG_FALLRISK         = (1 << 17),  // High fall hazard / no strafing
    WPT_FLAG_SPECIFICGRAVITY  = (1 << 18),  // Low/custom gravity required
    WPT_FLAG_ONLYONE          = (1 << 19),  // 1 bot at a time to prevent jamming
    WPT_FLAG_WAITUNTIL        = (1 << 20),  // Wait for condition
    WPT_FLAG_HELICOPTER       = (1 << 21),  // Zombie escape helicopter extraction
    WPT_FLAG_FALLCHECK        = (1 << 26),  // Ground safety check
    WPT_FLAG_JUMP             = (1 << 27),  // Requires jump
    WPT_FLAG_SNIPER           = (1 << 28),  // Long-range sniper nest
    WPT_FLAG_TERRORIST        = (1 << 29),  // Terrorist team exclusive
    WPT_FLAG_COUNTER          = (1 << 30)   // CT team exclusive
};

// Waypoint Connection Link Flags
enum WaypointConnectionFlags : uint16_t {
    WPT_CONN_NONE             = 0,
    WPT_CONN_JUMP             = (1 << 0),   // Leap required
    WPT_CONN_DOUBLE           = (1 << 1),   // Boost / double jump required
    WPT_CONN_VISIBLE          = (1 << 2)    // Line of sight must remain clear
};

// Maximum connection degree per waypoint across GoldSrc formats
constexpr int WPT_MAX_CONNECTIONS = 8;

// Universal In-Memory Waypoint Node
struct WaypointNode {
    uint32_t id{0};
    Vector3 origin{0.0f, 0.0f, 0.0f};
    uint32_t flags{WPT_FLAG_NONE};
    float radius{16.0f};           // Navigation tolerance radius (0 - 255)
    uint8_t mesh{0};              // Camp mesh / cluster group
    float gravity{0.0f};          // Specific gravity (EBot)
    float campPitch{0.0f};        // Aim pitch (-89 to +89)
    float campYaw{0.0f};          // Aim yaw (0 to 360)

    // Up to 8 outgoing links
    int16_t connections[WPT_MAX_CONNECTIONS]{ -1, -1, -1, -1, -1, -1, -1, -1 };
    uint16_t connectionFlags[WPT_MAX_CONNECTIONS]{ 0, 0, 0, 0, 0, 0, 0, 0 };

    bool HasConnectionTo(int16_t targetId) const {
        if (targetId < 0) return false;
        for (int i = 0; i < WPT_MAX_CONNECTIONS; ++i) {
            if (connections[i] == targetId) return true;
        }
        return false;
    }

    bool AddConnection(int16_t targetId, uint16_t flags = WPT_CONN_NONE) {
        if (targetId < 0 || targetId == static_cast<int16_t>(id)) return false;
        for (int i = 0; i < WPT_MAX_CONNECTIONS; ++i) {
            if (connections[i] == targetId) {
                connectionFlags[i] = flags;
                return true;
            }
        }
        for (int i = 0; i < WPT_MAX_CONNECTIONS; ++i) {
            if (connections[i] < 0) {
                connections[i] = targetId;
                connectionFlags[i] = flags;
                return true;
            }
        }
        return false;
    }

    bool RemoveConnection(int16_t targetId) {
        bool found = false;
        for (int i = 0; i < WPT_MAX_CONNECTIONS; ++i) {
            if (connections[i] == targetId) {
                connections[i] = -1;
                connectionFlags[i] = 0;
                found = true;
            }
        }
        return found;
    }
};

// Generic binary header for POD-Bot / SyPB / YaPB / EBot
#pragma pack(push, 1)
struct WaypointFileHeader {
    char magic[8];        // "EBOTWP!\0", "SyPB!\0", "YaPB!\0", "PODWAY!\0"
    int32_t fileVersion;  // 127 (EBot), 125 (SyPB), 5/6/7 (YaPB/POD)
    int32_t pointNumber;  // Total count of waypoints
    char mapName[32];     // Associated BSP map name
    char author[32];      // Waypointer author
};
#pragma pack(pop)

#endif // WAYPOINT_TYPES_H
