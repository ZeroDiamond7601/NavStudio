#include "editor/render/texture_manager.h"
#include <algorithm>
#include <fstream>
#include <cstdio>

TextureManager::TextureManager()
    : m_checkerboardId(0)
{
}

TextureManager::~TextureManager() {
    Clear();
}

std::string TextureManager::ToLower(const std::string& str) {
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

void TextureManager::Clear() {
    std::vector<GLuint> texturesToDelete;
    texturesToDelete.reserve(m_textures.size() + 1);

    for (const auto& kv : m_textures) {
        if (kv.second.id != 0 && kv.second.id != m_checkerboardId) {
            texturesToDelete.push_back(kv.second.id);
        }
    }

    if (m_checkerboardId != 0) {
        texturesToDelete.push_back(m_checkerboardId);
        m_checkerboardId = 0;
    }

    if (!texturesToDelete.empty()) {
        glDeleteTextures(static_cast<GLsizei>(texturesToDelete.size()), texturesToDelete.data());
    }

    m_textures.clear();
    m_transparencyMap.clear();
    m_wads.clear();
    m_loadedWadPaths.clear();
}

void TextureManager::CreateCheckerboardTexture() {
    if (m_checkerboardId != 0) return;

    const int size = 64;
    const int tileSize = 16;
    std::vector<uint8_t> pixels(size * size * 4);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            bool tile = ((x / tileSize) % 2) ^ ((y / tileSize) % 2);
            size_t idx = (y * size + x) * 4;
            if (tile) {
                // Classic Hammer editor amber / dark orange
                pixels[idx + 0] = 215;
                pixels[idx + 1] = 125;
                pixels[idx + 2] = 28;
                pixels[idx + 3] = 255;
            } else {
                // Dark slate gray
                pixels[idx + 0] = 45;
                pixels[idx + 1] = 48;
                pixels[idx + 2] = 55;
                pixels[idx + 3] = 255;
            }
        }
    }

    m_checkerboardId = UploadTextureRGBA(pixels.data(), size, size, true);
}

GLuint TextureManager::UploadTextureRGBA(const uint8_t* rgba, uint32_t width, uint32_t height, bool generateMipmaps) {
    if (!rgba || width == 0 || height == 0) return 0;

    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    if (generateMipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBindTexture(GL_TEXTURE_2D, 0);
    return texId;
}

bool TextureManager::LoadWAD(const std::string& wadPath) {
    if (wadPath.empty()) return false;

    // Check if this WAD path is already loaded
    for (const auto& p : m_loadedWadPaths) {
#if defined(_WIN32)
        if (_stricmp(p.c_str(), wadPath.c_str()) == 0) return true;
#else
        if (strcasecmp(p.c_str(), wadPath.c_str()) == 0) return true;
#endif
    }

    auto wad = std::make_unique<WADFile>();
    if (!wad->Load(wadPath)) {
        return false;
    }

    m_loadedWadPaths.push_back(wadPath);
    m_wads.push_back(std::move(wad));
    return true;
}

static bool FileExists(const std::string& path) {
    if (path.empty()) return false;
    std::ifstream f(path.c_str(), std::ios::binary);
    return f.good();
}

static std::string ExtractFilename(const std::string& path) {
    size_t sep = path.find_last_of("/\\");
    return (sep != std::string::npos) ? path.substr(sep + 1) : path;
}

void TextureManager::LoadForBSP(const BSPFile& bsp, const std::string& bspPath, const std::string& gameDirectory) {
    Clear();
    CreateCheckerboardTexture();

    if (!bsp.IsLoaded()) return;

    // Determine directory containing the BSP file
    std::string mapDir;
    size_t lastSep = bspPath.find_last_of("/\\");
    if (lastSep != std::string::npos) {
        mapDir = bspPath.substr(0, lastSep);
    } else {
        mapDir = ".";
    }

    // 1. Resolve and load WAD archives declared in worldspawn "wad" key
    std::string wadList;
    if (bsp.GetWadList(wadList)) {
        size_t start = 0;
        while (start < wadList.size()) {
            size_t semi = wadList.find(';', start);
            std::string token = (semi != std::string::npos) ? wadList.substr(start, semi - start) : wadList.substr(start);
            start = (semi != std::string::npos) ? semi + 1 : wadList.size();

            // Strip quotes and whitespace
            while (!token.empty() && (token.front() == '"' || token.front() == ' ' || token.front() == '\t')) token.erase(0, 1);
            while (!token.empty() && (token.back() == '"' || token.back() == ' ' || token.back() == '\t' || token.back() == '\r')) token.pop_back();
            if (token.empty()) continue;

            std::string fname = ExtractFilename(token);
            if (fname.empty()) continue;

            // Search order:
            // 1. Direct path
            // 2. Map directory
            // 3. Parent directory of map (e.g. cstrike/ for cstrike/maps/map.bsp)
            // 4. GameDirectory subdirectories (cstrike, valve, czero, cstrike_downloads)
            std::vector<std::string> candidates = {
                token,
                mapDir + "/" + fname,
                mapDir + "/../" + fname,
                mapDir + "\\" + fname,
                mapDir + "\\..\\" + fname
            };

            if (!gameDirectory.empty()) {
                candidates.push_back(gameDirectory + "/cstrike/" + fname);
                candidates.push_back(gameDirectory + "\\cstrike\\" + fname);
                candidates.push_back(gameDirectory + "/valve/" + fname);
                candidates.push_back(gameDirectory + "\\valve\\" + fname);
                candidates.push_back(gameDirectory + "/czero/" + fname);
                candidates.push_back(gameDirectory + "\\czero\\" + fname);
                candidates.push_back(gameDirectory + "/cstrike_downloads/" + fname);
                candidates.push_back(gameDirectory + "\\cstrike_downloads\\" + fname);
                candidates.push_back(gameDirectory + "/" + fname);
                candidates.push_back(gameDirectory + "\\" + fname);
            }

            for (const auto& cand : candidates) {
                if (FileExists(cand)) {
                    LoadWAD(cand);
                    break;
                }
            }
        }
    }

    // 2. Load standard default WADs from game directory if available
    if (!gameDirectory.empty()) {
        std::vector<std::string> baseWads = {
            gameDirectory + "/cstrike/cstrike.wad",
            gameDirectory + "\\cstrike\\cstrike.wad",
            gameDirectory + "/valve/halflife.wad",
            gameDirectory + "\\valve\\halflife.wad",
            gameDirectory + "/valve/decals.wad",
            gameDirectory + "\\valve\\decals.wad",
            gameDirectory + "/valve/liquids.wad",
            gameDirectory + "\\valve\\liquids.wad"
        };
        for (const auto& bw : baseWads) {
            if (FileExists(bw)) {
                LoadWAD(bw);
            }
        }
    }

    // 3. Iterate all textures defined in the BSP
    int numTex = bsp.GetTextureCount();
    for (int i = 0; i < numTex; ++i) {
        const char* name = bsp.GetTextureName(i);
        if (!name || name[0] == '\0') continue;

        std::string lowerName = ToLower(name);
        if (m_textures.find(lowerName) != m_textures.end()) {
            continue;
        }

        bool loaded = false;
        WADTexture wtex;

        // A. Check for embedded texture inside BSP LUMP_TEXTURES
        size_t remaining = 0;
        const uint8_t* mipData = bsp.GetMiptexData(i, &remaining);
        if (mipData && remaining >= sizeof(miptex_t)) {
            const miptex_t* mt = reinterpret_cast<const miptex_t*>(mipData);
            if (mt->offsets[0] > 0) {
                if (WADFile::DecodeMiptex(mipData, remaining, wtex)) {
                    GLuint texId = UploadTextureRGBA(wtex.rgba.data(), wtex.width, wtex.height, true);
                    if (texId != 0) {
                        LoadedTextureInfo info;
                        info.id = texId;
                        info.width = wtex.width;
                        info.height = wtex.height;
                        info.isTransparent = wtex.isTransparent;
                        info.isFluid = wtex.isFluid;
                        m_textures[lowerName] = info;
                        m_transparencyMap[texId] = wtex.isTransparent;
                        loaded = true;
                    }
                }
            }
        }

        // B. Check loaded WAD files
        if (!loaded) {
            for (const auto& wad : m_wads) {
                if (wad->GetTexture(name, wtex)) {
                    GLuint texId = UploadTextureRGBA(wtex.rgba.data(), wtex.width, wtex.height, true);
                    if (texId != 0) {
                        LoadedTextureInfo info;
                        info.id = texId;
                        info.width = wtex.width;
                        info.height = wtex.height;
                        info.isTransparent = wtex.isTransparent;
                        info.isFluid = wtex.isFluid;
                        m_textures[lowerName] = info;
                        m_transparencyMap[texId] = wtex.isTransparent;
                        loaded = true;
                        break;
                    }
                }
            }
        }

        // C. Fallback to checkerboard missing texture
        if (!loaded) {
            int bw = 64, bh = 64;
            bsp.GetTextureDimensions(i, bw, bh);
            if (bw <= 0) bw = 64;
            if (bh <= 0) bh = 64;

            LoadedTextureInfo info;
            info.id = m_checkerboardId;
            info.width = static_cast<uint32_t>(bw);
            info.height = static_cast<uint32_t>(bh);
            info.isTransparent = (name[0] == '{');
            info.isFluid = (name[0] == '!');
            m_textures[lowerName] = info;
        }
    }
}

GLuint TextureManager::GetTextureID(const std::string& name) const {
    if (name.empty()) return m_checkerboardId;
    auto it = m_textures.find(ToLower(name));
    if (it != m_textures.end() && it->second.id != 0) {
        return it->second.id;
    }
    return m_checkerboardId;
}

bool TextureManager::GetTextureInfo(const std::string& name, LoadedTextureInfo& outInfo) const {
    if (name.empty()) return false;
    auto it = m_textures.find(ToLower(name));
    if (it != m_textures.end()) {
        outInfo = it->second;
        return true;
    }
    return false;
}

bool TextureManager::IsTransparent(const std::string& name) const {
    if (name.empty()) return false;
    auto it = m_textures.find(ToLower(name));
    if (it != m_textures.end()) {
        return it->second.isTransparent;
    }
    return (name[0] == '{');
}

bool TextureManager::IsTransparent(GLuint texId) const {
    if (texId == 0) return false;
    auto it = m_transparencyMap.find(texId);
    if (it != m_transparencyMap.end()) {
        return it->second;
    }
    return false;
}
