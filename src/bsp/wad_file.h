#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

#define WAD3_MAGIC 0x33444157 // 'WAD3'

#pragma pack(push, 1)

struct wadheader_t {
    char identification[4]; // "WAD3"
    int32_t numlumps;
    int32_t infotableofs;
};

struct wadlump_t {
    int32_t filepos;
    int32_t disksize;
    int32_t size;
    int8_t  type;           // 0x43 ('C') = TYP_MIPTEX
    int8_t  compression;    // 0
    int16_t pad;
    char    name[16];
};

#pragma pack(pop)

enum WADLumpType {
    WAD_TYP_NONE     = 0,
    WAD_TYP_LABEL    = 0x40,
    WAD_TYP_LUMPY    = 0x42,
    WAD_TYP_MIPTEX   = 0x43, // Standard 3D surface miptex (4 mips + 256-color palette)
    WAD_TYP_FONT     = 0x46
};

struct WADTexture {
    std::string name;
    uint32_t width{0};
    uint32_t height{0};
    std::vector<uint8_t> rgba; // 32-bit RGBA pixels (width * height * 4)
    bool isTransparent{false}; // Name starts with '{' (color index 255 is transparent)
    bool isFluid{false};       // Name starts with '!' (liquid/water)
};

class WADFile {
public:
    WADFile();
    ~WADFile();

    bool Load(const std::string& filepath);
    bool LoadFromMemory(const uint8_t* data, size_t size);
    void Unload();

    bool IsLoaded() const { return m_loaded; }
    const std::string& GetPath() const { return m_filepath; }
    const std::string& GetFilename() const { return m_filename; }
    int GetTextureCount() const;
    std::vector<std::string> GetTextureNames() const;

    bool HasTexture(const std::string& name) const;
    bool GetTexture(const std::string& name, WADTexture& outTexture) const;

    // Static decoder for TYP_MIPTEX data (used for both WAD lumps and BSP embedded textures)
    static bool DecodeMiptex(const uint8_t* data, size_t size, WADTexture& outTexture);

    static std::string ToLower(const std::string& str);

private:
    struct LumpEntry {
        uint32_t filepos;
        uint32_t disksize;
        uint32_t size;
        uint8_t  type;
        std::string name;
    };

    bool m_loaded{false};
    std::string m_filepath;
    std::string m_filename;
    std::vector<uint8_t> m_rawData;
    std::unordered_map<std::string, LumpEntry> m_lumps;
};
