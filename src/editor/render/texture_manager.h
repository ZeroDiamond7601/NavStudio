#ifndef TEXTURE_MANAGER_H
#define TEXTURE_MANAGER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "editor/glad/include/glad/glad.h"
#include "bsp/bsp_file.h"
#include "bsp/wad_file.h"

struct LoadedTextureInfo {
    GLuint id{0};
    uint32_t width{0};
    uint32_t height{0};
    bool isTransparent{false}; // True if name starts with '{' (masked cutout)
    bool isFluid{false};       // True if name starts with '!' (water/liquid)
};

class TextureManager {
public:
    TextureManager();
    ~TextureManager();

    // Reset and delete all GPU textures and loaded WADs
    void Clear();

    // Load textures needed by a BSP map
    // Resolves WADs from worldspawn "wad" key, map directory, parent directory, and gameDirectory
    // Resolves embedded textures directly from BSP LUMP_TEXTURES
    void LoadForBSP(const BSPFile& bsp, const std::string& bspPath, const std::string& gameDirectory);

    // Manually load or add a WAD archive (from file dialog or drag-and-drop)
    bool LoadWAD(const std::string& wadPath);

    // Retrieve OpenGL texture ID by name (case-insensitive)
    // Returns m_checkerboardId if texture is missing
    GLuint GetTextureID(const std::string& name) const;

    // Retrieve full texture info (dimensions and flags)
    bool GetTextureInfo(const std::string& name, LoadedTextureInfo& outInfo) const;

    // Check if texture has transparency
    bool IsTransparent(const std::string& name) const;
    bool IsTransparent(GLuint texId) const;

    // Fallback missing texture ID
    GLuint GetCheckerboardTextureID() const { return m_checkerboardId; }

    // Statistics & Diagnostics
    size_t GetLoadedTextureCount() const { return m_textures.size(); }
    size_t GetLoadedWADCount() const { return m_wads.size(); }
    const std::vector<std::string>& GetLoadedWADPaths() const { return m_loadedWadPaths; }

    // Upload raw RGBA buffer to OpenGL 2D texture with linear mipmaps & repeat wrap
    static GLuint UploadTextureRGBA(const uint8_t* rgba, uint32_t width, uint32_t height, bool generateMipmaps = true);

private:
    void CreateCheckerboardTexture();
    static std::string ToLower(const std::string& str);

    GLuint m_checkerboardId{0};
    std::unordered_map<std::string, LoadedTextureInfo> m_textures;
    std::unordered_map<GLuint, bool> m_transparencyMap;
    std::vector<std::unique_ptr<WADFile>> m_wads;
    std::vector<std::string> m_loadedWadPaths;
};

#endif // TEXTURE_MANAGER_H
