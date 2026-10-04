#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "../math/vector3.h"

class BSPEntity {
public:
    std::string classname;
    std::unordered_map<std::string, std::string> keyvalues;

    bool HasKey(const std::string& key) const;
    std::string GetString(const std::string& key, const std::string& defaultVal = "") const;
    bool GetVector(const std::string& key, Vector3& outVec) const;
    float GetFloat(const std::string& key, float defaultVal = 0.0f) const;
    int GetInt(const std::string& key, int defaultVal = 0) const;
    bool GetOrigin(Vector3& outOrigin) const;
};

class BSPEntityParser {
public:
    static bool Parse(const char* data, size_t length, std::vector<BSPEntity>& outEntities);
};
