#include "bsp_entity.h"
#include <cstdio>
#include <cstdlib>
#include <cctype>

bool BSPEntity::HasKey(const std::string& key) const {
    return keyvalues.find(key) != keyvalues.end();
}

std::string BSPEntity::GetString(const std::string& key, const std::string& defaultVal) const {
    auto it = keyvalues.find(key);
    if (it != keyvalues.end()) {
        return it->second;
    }
    return defaultVal;
}

bool BSPEntity::GetVector(const std::string& key, Vector3& outVec) const {
    auto it = keyvalues.find(key);
    if (it == keyvalues.end()) return false;

    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    if (std::sscanf(it->second.c_str(), "%f %f %f", &vx, &vy, &vz) == 3) {
        outVec.x = vx;
        outVec.y = vy;
        outVec.z = vz;
        return true;
    }
    return false;
}

float BSPEntity::GetFloat(const std::string& key, float defaultVal) const {
    auto it = keyvalues.find(key);
    if (it != keyvalues.end()) {
        return static_cast<float>(std::atof(it->second.c_str()));
    }
    return defaultVal;
}

int BSPEntity::GetInt(const std::string& key, int defaultVal) const {
    auto it = keyvalues.find(key);
    if (it != keyvalues.end()) {
        return std::atoi(it->second.c_str());
    }
    return defaultVal;
}

bool BSPEntity::GetOrigin(Vector3& outOrigin) const {
    return GetVector("origin", outOrigin);
}

bool BSPEntityParser::Parse(const char* data, size_t length, std::vector<BSPEntity>& outEntities) {
    outEntities.clear();
    if (!data || length == 0) return false;

    size_t i = 0;
    while (i < length) {
        // Skip whitespace
        while (i < length && std::isspace(static_cast<unsigned char>(data[i]))) i++;
        if (i >= length) break;

        // Skip line comments
        if (data[i] == '/' && i + 1 < length && data[i + 1] == '/') {
            while (i < length && data[i] != '\n') i++;
            continue;
        }

        // Entity block start '{'
        if (data[i] == '{') {
            i++;
            BSPEntity ent;

            while (i < length) {
                while (i < length && std::isspace(static_cast<unsigned char>(data[i]))) i++;
                if (i >= length) break;

                if (data[i] == '}') {
                    i++;
                    break;
                }

                if (data[i] == '/' && i + 1 < length && data[i + 1] == '/') {
                    while (i < length && data[i] != '\n') i++;
                    continue;
                }

                // Read quoted key
                if (data[i] == '"') {
                    i++;
                    size_t kStart = i;
                    while (i < length && data[i] != '"') i++;
                    std::string key(data + kStart, i - kStart);
                    if (i < length && data[i] == '"') i++;

                    // Find quoted value
                    while (i < length && data[i] != '"' && data[i] != '}') i++;
                    if (i < length && data[i] == '"') {
                        i++;
                        size_t vStart = i;
                        while (i < length && data[i] != '"') i++;
                        std::string value(data + vStart, i - vStart);
                        if (i < length && data[i] == '"') i++;

                        ent.keyvalues[key] = value;
                        if (key == "classname") {
                            ent.classname = value;
                        }
                    }
                } else {
                    i++;
                }
            }

            if (!ent.classname.empty()) {
                outEntities.push_back(std::move(ent));
            }
        } else {
            i++;
        }
    }

    return true;
}
