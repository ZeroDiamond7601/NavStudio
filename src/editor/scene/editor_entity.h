#ifndef EDITOR_ENTITY_H
#define EDITOR_ENTITY_H

#include <string>
#include <vector>
#include <unordered_map>
#include "math/vector3.h"

enum EntityCategory {
    ENT_CAT_SPAWN_CT = 0,
    ENT_CAT_SPAWN_T,
    ENT_CAT_SPAWN_VIP,
    ENT_CAT_OBJECTIVE_BOMB,
    ENT_CAT_OBJECTIVE_HOSTAGE,
    ENT_CAT_OBJECTIVE_RESCUE,
    ENT_CAT_OBJECTIVE_BUYZONE,
    ENT_CAT_LIGHT,
    ENT_CAT_ITEM,
    ENT_CAT_SOUND,
    ENT_CAT_TRIGGER,
    ENT_CAT_BRUSH,
    ENT_CAT_OTHER,
    ENT_CAT_COUNT
};

struct Vector4 {
    float x, y, z, w;
    Vector4() : x(1.0f), y(1.0f), z(1.0f), w(1.0f) {}
    Vector4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

struct EditorEntity {
    int index{-1};
    std::string classname;
    std::string targetname;
    std::string target;
    std::string model;
    
    Vector3 origin{0.0f, 0.0f, 0.0f};
    Vector3 angles{0.0f, 0.0f, 0.0f};
    float yaw{0.0f};

    EntityCategory category{ENT_CAT_OTHER};
    bool isBrush{false};
    int brushModelIndex{-1};

    // Bounding boxes
    Vector3 mins{-16.0f, -16.0f, -16.0f};
    Vector3 maxs{16.0f, 16.0f, 16.0f};
    Vector3 worldMins{0.0f, 0.0f, 0.0f};
    Vector3 worldMaxs{0.0f, 0.0f, 0.0f};

    Vector4 color{0.6f, 0.6f, 0.65f, 1.0f};
    std::unordered_map<std::string, std::string> keyvalues;

    const char* GetCategoryName() const {
        switch (category) {
            case ENT_CAT_SPAWN_CT: return "CT Spawn";
            case ENT_CAT_SPAWN_T: return "T Spawn";
            case ENT_CAT_SPAWN_VIP: return "VIP Spawn";
            case ENT_CAT_OBJECTIVE_BOMB: return "Bomb Target";
            case ENT_CAT_OBJECTIVE_HOSTAGE: return "Hostage";
            case ENT_CAT_OBJECTIVE_RESCUE: return "Rescue Zone";
            case ENT_CAT_OBJECTIVE_BUYZONE: return "Buy Zone";
            case ENT_CAT_LIGHT: return "Light";
            case ENT_CAT_ITEM: return "Weapon / Item";
            case ENT_CAT_SOUND: return "Ambient Audio";
            case ENT_CAT_TRIGGER: return "Trigger Volume";
            case ENT_CAT_BRUSH: return "Brush Entity";
            default: return "Entity";
        }
    }

    const char* GetFriendlyName() const {
        if (classname == "info_player_start") return "Counter-Terrorist Spawn";
        if (classname == "info_player_deathmatch") return "Terrorist Spawn";
        if (classname == "info_vip_start") return "VIP Spawn";
        if (classname == "info_bomb_target" || classname == "func_bomb_target") return "Bomb Target";
        if (classname == "hostage_entity") return "Hostage";
        if (classname == "info_hostage_rescue" || classname == "func_hostage_rescue") return "Hostage Rescue Zone";
        if (classname == "func_buyzone") return "Buy Zone";
        if (classname == "func_vip_safetyzone") return "VIP Safety Zone";
        if (classname == "func_escapezone") return "Escape Zone";
        if (classname == "armoury_entity") return "Armoury Weapon Spawner";
        if (classname == "light") return "Point Light";
        if (classname == "light_spot") return "Spotlight";
        if (classname == "light_environment") return "Environment Sun Light";
        if (classname == "ambient_generic") return "Ambient Sound Player";
        if (classname == "func_door" || classname == "func_door_rotating") return "Door";
        if (classname == "func_breakable") return "Breakable Object";
        if (classname == "func_ladder") return "Ladder Volume";
        if (classname == "func_button" || classname == "func_rot_button") return "Button";
        if (classname == "trigger_multiple") return "Trigger Multiple";
        if (classname == "trigger_once") return "Trigger Once";
        if (classname == "trigger_teleport") return "Teleporter Trigger";
        if (classname == "trigger_push") return "Push Trigger";
        if (classname == "trigger_hurt") return "Damage Trigger";
        return classname.c_str();
    }

    std::string GetArmouryItemName() const {
        auto it = keyvalues.find("item");
        if (it == keyvalues.end()) return "Unknown Item";
        int id = std::atoi(it->second.c_str());
        switch (id) {
            case 0: return "MP5 Navy";
            case 1: return "TMP";
            case 2: return "P90";
            case 3: return "MAC-10";
            case 4: return "AK-47";
            case 5: return "SG-552 Commando";
            case 6: return "M4A1 Carbine";
            case 7: return "AUG";
            case 8: return "Scout";
            case 9: return "AWP Sniper";
            case 10: return "G3SG1";
            case 11: return "SG-550";
            case 12: return "M249 Para";
            case 13: return "M3 Super 90";
            case 14: return "XM1014 Auto";
            case 15: return "USP .45 Tactical";
            case 16: return "Glock 18";
            case 17: return "Desert Eagle";
            case 18: return "P228";
            case 19: return "Dual Elites";
            case 20: return "Five-Seven";
            case 21: return "Kevlar Vest";
            case 22: return "Kevlar + Helmet";
            case 23: return "Flashbang";
            case 24: return "HE Grenade";
            case 25: return "Smoke Grenade";
            default: return "Custom Weapon (" + std::to_string(id) + ")";
        }
    }
};

#endif // EDITOR_ENTITY_H
