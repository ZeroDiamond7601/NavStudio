#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include <string>
#include <vector>
#include <cstdint>

enum SkyFaceIndex {
    SKY_FACE_FRONT = 0, // ft (+X)
    SKY_FACE_BACK  = 1, // bk (-X)
    SKY_FACE_LEFT  = 2, // lf (+Y)
    SKY_FACE_RIGHT = 3, // rt (-Y)
    SKY_FACE_UP    = 4, // up (+Z)
    SKY_FACE_DOWN  = 5  // dn (-Z)
};

enum SkyPresetId {
    SKY_PRESET_DESERT_ID = 0,
    SKY_PRESET_NIGHT_ID,
    SKY_PRESET_OVERCAST_ID,
    SKY_PRESET_SUNSET_ID,
    SKY_PRESET_AZURE_ID
};

class ImageLoader {
public:
    // Load image (auto-detects TGA or BMP format from extension / magic)
    // Returns 32-bit RGBA pixel buffer (width * height * 4 bytes)
    static bool LoadImage(const std::string& path, uint32_t& outWidth, uint32_t& outHeight, std::vector<uint8_t>& outRGBA);

    // Explicit TGA loader (24-bit RGB and 32-bit RGBA, uncompressed and RLE)
    static bool LoadTGA(const std::string& path, uint32_t& outWidth, uint32_t& outHeight, std::vector<uint8_t>& outRGBA);
    static bool LoadTGAFromMemory(const uint8_t* data, size_t size, uint32_t& outWidth, uint32_t& outHeight, std::vector<uint8_t>& outRGBA);

    // Explicit BMP loader (24-bit RGB and 32-bit RGBA uncompressed)
    static bool LoadBMP(const std::string& path, uint32_t& outWidth, uint32_t& outHeight, std::vector<uint8_t>& outRGBA);
    static bool LoadBMPFromMemory(const uint8_t* data, size_t size, uint32_t& outWidth, uint32_t& outHeight, std::vector<uint8_t>& outRGBA);

    // Generate high-resolution procedural cubemap face
    static void GenerateProceduralSkyFace(SkyPresetId preset, SkyFaceIndex face, uint32_t size, std::vector<uint8_t>& outRGBA);
};

#endif // IMAGE_LOADER_H
