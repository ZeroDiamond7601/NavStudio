#pragma once

#include <cstdint>
#include <vector>
#include "../math/vector3.h"

#define NAV_MAGIC_NUMBER    0xFEEDFACE
#define NAV_VERSION_4       4
#define NAV_VERSION_5       5
#define NAV_VERSION_CURRENT NAV_VERSION_5

#define UNDEFINED_PLACE     0
#define ANY_PLACE           0xFFFF

enum NavDirType {
    NAV_DIR_NORTH = 0,
    NAV_DIR_EAST  = 1,
    NAV_DIR_SOUTH = 2,
    NAV_DIR_WEST  = 3,
    NUM_NAV_DIRECTIONS = 4
};

enum NavTraverseType {
    NAV_TRAVERSE_NORTH = 0,
    NAV_TRAVERSE_EAST  = 1,
    NAV_TRAVERSE_SOUTH = 2,
    NAV_TRAVERSE_WEST  = 3,
    NAV_TRAVERSE_LADDER_UP   = 4,
    NAV_TRAVERSE_LADDER_DOWN = 5,
    NAV_TRAVERSE_JUMP        = 6,
    NUM_NAV_TRAVERSE_TYPES
};

enum NavCornerType {
    NAV_CORNER_NORTH_WEST = 0,
    NAV_CORNER_NORTH_EAST = 1,
    NAV_CORNER_SOUTH_EAST = 2,
    NAV_CORNER_SOUTH_WEST = 3,
    NUM_NAV_CORNERS = 4
};

enum NavAttributeType {
    NAV_ATTR_CROUCH  = 0x01,
    NAV_ATTR_JUMP    = 0x02,
    NAV_ATTR_PRECISE = 0x04,
    NAV_ATTR_NO_JUMP = 0x08
};

enum NavHidingSpotFlags {
    HIDING_IN_COVER     = 0x01,
    HIDING_GOOD_SNIPER  = 0x02,
    HIDING_IDEAL_SNIPER = 0x04,
    HIDING_EXPOSED      = 0x08,

    // Backward compatibility aliases
    NAV_HIDING_IN_COVER         = HIDING_IN_COVER,
    NAV_HIDING_COVER            = HIDING_IN_COVER,
    NAV_HIDING_GOOD_SNIPER      = HIDING_GOOD_SNIPER,
    NAV_HIDING_GOOD_SNIPER_SPOT = HIDING_GOOD_SNIPER,
    NAV_HIDING_IDEAL_SNIPER     = HIDING_IDEAL_SNIPER,
    NAV_HIDING_EXPOSED          = HIDING_EXPOSED
};

struct NavExtent {
    Vector3 lo;
    Vector3 hi;

    NavExtent() : lo(), hi() {}
    NavExtent(const Vector3& _lo, const Vector3& _hi) : lo(_lo), hi(_hi) {}

    inline bool Contains2D(const Vector3& pos) const {
        return (pos.x >= lo.x && pos.x <= hi.x && pos.y >= lo.y && pos.y <= hi.y);
    }
};

class NavArea;
class NavLadder;

struct NavConnect {
    uint32_t id;
    NavArea* area;

    NavConnect() : id(0), area(nullptr) {}
    explicit NavConnect(uint32_t _id) : id(_id), area(nullptr) {}
    NavConnect(uint32_t _id, NavArea* _area) : id(_id), area(_area) {}
};

struct NavHidingSpot {
    uint32_t id;
    Vector3 pos;
    uint8_t flags;

    NavHidingSpot() : id(0), pos(), flags(0) {}
    NavHidingSpot(uint32_t _id, const Vector3& _pos, uint8_t _flags)
        : id(_id), pos(_pos), flags(_flags) {}
};

struct NavApproachInfo {
    uint32_t hereId;
    uint32_t prevId;
    NavTraverseType prevHow;
    uint32_t nextId;
    NavTraverseType nextHow;

    NavArea* here;
    NavArea* prev;
    NavArea* next;

    NavApproachInfo()
        : hereId(0), prevId(0), prevHow(NAV_TRAVERSE_NORTH),
          nextId(0), nextHow(NAV_TRAVERSE_NORTH),
          here(nullptr), prev(nullptr), next(nullptr) {}
};

struct NavSpotOrder {
    uint32_t id;
    float t;
    const NavHidingSpot* spot;

    NavSpotOrder() : id(0), t(0.0f), spot(nullptr) {}
};

struct NavSpotEncounter {
    uint32_t fromId;
    NavDirType fromDir;
    uint32_t toId;
    NavDirType toDir;
    NavArea* from;
    NavArea* to;
    std::vector<NavSpotOrder> spotList;

    NavSpotEncounter()
        : fromId(0), fromDir(NAV_DIR_NORTH), toId(0), toDir(NAV_DIR_NORTH),
          from(nullptr), to(nullptr) {}
};

class NavLadder {
public:
    uint32_t id;
    float width;
    float length;
    Vector3 top;
    Vector3 bottom;
    NavDirType dir;

    NavArea* topForwardArea;
    NavArea* topLeftArea;
    NavArea* topRightArea;
    NavArea* topBehindArea;
    NavArea* bottomArea;

    NavLadder()
        : id(0), width(0.0f), length(0.0f), top(), bottom(), dir(NAV_DIR_NORTH),
          topForwardArea(nullptr), topLeftArea(nullptr), topRightArea(nullptr),
          topBehindArea(nullptr), bottomArea(nullptr) {}
};

struct NavPathSegment {
    NavArea* area;
    NavTraverseType how;
    Vector3 pos;
    const NavLadder* ladder;

    NavPathSegment()
        : area(nullptr), how(NAV_TRAVERSE_NORTH), pos(), ladder(nullptr) {}
    NavPathSegment(NavArea* a, NavTraverseType h, const Vector3& p, const NavLadder* l = nullptr)
        : area(a), how(h), pos(p), ladder(l) {}
};
