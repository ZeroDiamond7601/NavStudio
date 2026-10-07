#include "editor/render/skybox_renderer.h"
#include <iostream>
#include <algorithm>
#include <fstream>

static const char* kSkyboxVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;

uniform mat4 u_ViewNoTrans;
uniform mat4 u_Proj;
uniform float u_RotationYaw;

out vec2 vTexCoord;
out vec3 vWorldDir;

void main() {
    float rad = radians(u_RotationYaw);
    float c = cos(rad);
    float s = sin(rad);
    // Rotate around GoldSrc Z-axis (+Z is Up)
    vec3 rotPos = vec3(
        aPos.x * c - aPos.y * s,
        aPos.x * s + aPos.y * c,
        aPos.z
    );
    vTexCoord = aTexCoord;
    vWorldDir = normalize(rotPos);

    vec4 clipPos = u_Proj * u_ViewNoTrans * vec4(rotPos, 1.0);
    gl_Position = clipPos.xyww; // Force depth to far clipping plane (Z = 1.0 in NDC)
}
)";

static const char* kSkyboxFragmentShader = R"(#version 330 core
in vec2 vTexCoord;
in vec3 vWorldDir;

uniform sampler2D u_SkyTexture;
uniform float u_Exposure;

out vec4 FragColor;

void main() {
    vec4 tex = texture(u_SkyTexture, vTexCoord);
    FragColor = vec4(tex.rgb * u_Exposure, 1.0);
}
)";

SkyboxRenderer::SkyboxRenderer()
    : m_initialized(false)
    , m_enabled(true)
    , m_preset(SKY_PRESET_AUTO)
    , m_skyname("desert")
    , m_loadedPath("")
    , m_isProcedural(true)
    , m_rotationYaw(0.0f)
    , m_exposure(1.0f)
{
}

SkyboxRenderer::~SkyboxRenderer() {
    Clear();
}

void SkyboxRenderer::Clear() {
    for (int i = 0; i < 6; ++i) {
        if (m_faces[i].vao != 0) { glDeleteVertexArrays(1, &m_faces[i].vao); m_faces[i].vao = 0; }
        if (m_faces[i].vbo != 0) { glDeleteBuffers(1, &m_faces[i].vbo); m_faces[i].vbo = 0; }
        if (m_faces[i].ebo != 0) { glDeleteBuffers(1, &m_faces[i].ebo); m_faces[i].ebo = 0; }
        if (m_faces[i].textureId != 0) { glDeleteTextures(1, &m_faces[i].textureId); m_faces[i].textureId = 0; }
        m_faces[i].width = 0;
        m_faces[i].height = 0;
        m_faces[i].loadedFromDisk = false;
    }
}

bool SkyboxRenderer::Init() {
    if (m_initialized) return true;

    if (!m_shader.LoadFromSource(kSkyboxVertexShader, kSkyboxFragmentShader)) {
        std::fprintf(stderr, "[SkyboxRenderer] Failed to compile skybox shaders\n");
        return false;
    }

    BuildFaceGeometry();
    GenerateProceduralFaces();

    m_initialized = true;
    return true;
}

void SkyboxRenderer::BuildFaceGeometry() {
    // 6 cube faces matching GoldSrc coordinate system:
    // +X: Front (ft), -X: Back (bk), +Y: Left (lf), -Y: Right (rt), +Z: Up (up), -Z: Down (dn)
    struct SkyVert {
        float x, y, z;
        float u, v;
    };

    const SkyVert faceVerts[6][4] = {
        // SKY_FACE_FRONT (ft, +X)
        {
            {  1.0f,  1.0f, -1.0f,  0.0f, 0.0f },
            {  1.0f, -1.0f, -1.0f,  1.0f, 0.0f },
            {  1.0f, -1.0f,  1.0f,  1.0f, 1.0f },
            {  1.0f,  1.0f,  1.0f,  0.0f, 1.0f }
        },
        // SKY_FACE_BACK (bk, -X)
        {
            { -1.0f, -1.0f, -1.0f,  0.0f, 0.0f },
            { -1.0f,  1.0f, -1.0f,  1.0f, 0.0f },
            { -1.0f,  1.0f,  1.0f,  1.0f, 1.0f },
            { -1.0f, -1.0f,  1.0f,  0.0f, 1.0f }
        },
        // SKY_FACE_LEFT (lf, +Y)
        {
            { -1.0f,  1.0f, -1.0f,  0.0f, 0.0f },
            {  1.0f,  1.0f, -1.0f,  1.0f, 0.0f },
            {  1.0f,  1.0f,  1.0f,  1.0f, 1.0f },
            { -1.0f,  1.0f,  1.0f,  0.0f, 1.0f }
        },
        // SKY_FACE_RIGHT (rt, -Y)
        {
            {  1.0f, -1.0f, -1.0f,  0.0f, 0.0f },
            { -1.0f, -1.0f, -1.0f,  1.0f, 0.0f },
            { -1.0f, -1.0f,  1.0f,  1.0f, 1.0f },
            {  1.0f, -1.0f,  1.0f,  0.0f, 1.0f }
        },
        // SKY_FACE_UP (up, +Z)
        {
            {  1.0f,  1.0f,  1.0f,  0.0f, 0.0f },
            {  1.0f, -1.0f,  1.0f,  1.0f, 0.0f },
            { -1.0f, -1.0f,  1.0f,  1.0f, 1.0f },
            { -1.0f,  1.0f,  1.0f,  0.0f, 1.0f }
        },
        // SKY_FACE_DOWN (dn, -Z)
        {
            { -1.0f,  1.0f, -1.0f,  0.0f, 0.0f },
            { -1.0f, -1.0f, -1.0f,  1.0f, 0.0f },
            {  1.0f, -1.0f, -1.0f,  1.0f, 1.0f },
            {  1.0f,  1.0f, -1.0f,  0.0f, 1.0f }
        }
    };

    const uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };

    for (int i = 0; i < 6; ++i) {
        if (m_faces[i].vao == 0) {
            glGenVertexArrays(1, &m_faces[i].vao);
            glGenBuffers(1, &m_faces[i].vbo);
            glGenBuffers(1, &m_faces[i].ebo);
        }

        glBindVertexArray(m_faces[i].vao);

        glBindBuffer(GL_ARRAY_BUFFER, m_faces[i].vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(faceVerts[i]), faceVerts[i], GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_faces[i].ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

        // aPos: location 0
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SkyVert), reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(0);

        // aTexCoord: location 1
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SkyVert), reinterpret_cast<void*>(offsetof(SkyVert, u)));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);
    }
}

void SkyboxRenderer::UploadFaceTexture(SkyFaceIndex face, const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height, bool fromDisk) {
    if (rgba.empty() || width == 0 || height == 0) return;

    if (m_faces[face].textureId == 0) {
        glGenTextures(1, &m_faces[face].textureId);
    }

    glBindTexture(GL_TEXTURE_2D, m_faces[face].textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Clamp to edge to prevent border bleeding between skybox faces
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    m_faces[face].width = width;
    m_faces[face].height = height;
    m_faces[face].loadedFromDisk = fromDisk;
}

void SkyboxRenderer::GenerateProceduralFaces() {
    SkyPresetId pId = SKY_PRESET_DESERT_ID;
    switch (m_preset) {
        case SKY_PRESET_NIGHT:    pId = SKY_PRESET_NIGHT_ID; break;
        case SKY_PRESET_OVERCAST: pId = SKY_PRESET_OVERCAST_ID; break;
        case SKY_PRESET_SUNSET:   pId = SKY_PRESET_SUNSET_ID; break;
        case SKY_PRESET_AZURE:    pId = SKY_PRESET_AZURE_ID; break;
        case SKY_PRESET_AUTO:
        case SKY_PRESET_DESERT:
        default:
            pId = SKY_PRESET_DESERT_ID;
            break;
    }

    std::vector<uint8_t> faceRGBA;
    const uint32_t size = 256;
    for (int f = 0; f < 6; ++f) {
        ImageLoader::GenerateProceduralSkyFace(pId, static_cast<SkyFaceIndex>(f), size, faceRGBA);
        UploadFaceTexture(static_cast<SkyFaceIndex>(f), faceRGBA, size, size, false);
    }
    m_isProcedural = true;
}

SkyboxPreset SkyboxRenderer::GuessPresetFromSkyname(const std::string& skyname) const {
    std::string lower = skyname;
    for (char& c : lower) c = static_cast<char>(std::tolower(c));

    if (lower.find("night") != std::string::npos || lower.find("assault") != std::string::npos ||
        lower.find("dark") != std::string::npos || lower.find("black") != std::string::npos ||
        lower.find("space") != std::string::npos || lower.find("star") != std::string::npos) {
        return SKY_PRESET_NIGHT;
    }
    if (lower.find("overcast") != std::string::npos || lower.find("aztec") != std::string::npos ||
        lower.find("cloud") != std::string::npos || lower.find("gray") != std::string::npos ||
        lower.find("grey") != std::string::npos || lower.find("office") != std::string::npos ||
        lower.find("hav") != std::string::npos) {
        return SKY_PRESET_OVERCAST;
    }
    if (lower.find("sunset") != std::string::npos || lower.find("dusk") != std::string::npos ||
        lower.find("evening") != std::string::npos || lower.find("orange") != std::string::npos ||
        lower.find("dawn") != std::string::npos) {
        return SKY_PRESET_SUNSET;
    }
    if (lower.find("blue") != std::string::npos || lower.find("clear") != std::string::npos ||
        lower.find("day") != std::string::npos || lower.find("militia") != std::string::npos) {
        return SKY_PRESET_AZURE;
    }
    return SKY_PRESET_DESERT;
}

static bool CheckFile(const std::string& path) {
    if (path.empty()) return false;
    std::ifstream f(path.c_str(), std::ios::binary);
    return f.good();
}

bool SkyboxRenderer::LoadSkyname(const std::string& skyname, const std::string& searchDir, const std::string& gameDirectory) {
    if (!m_initialized) {
        Init();
    }

    m_skyname = skyname;
    if (m_skyname.empty()) m_skyname = "desert";

    const char* suffixes[6] = { "ft", "bk", "lf", "rt", "up", "dn" };
    const char* suffixesUpper[6] = { "FT", "BK", "LF", "RT", "UP", "DN" };

    std::vector<std::string> searchFolders;
    if (!searchDir.empty()) {
        searchFolders.push_back(searchDir + "/gfx/env");
        searchFolders.push_back(searchDir + "/../gfx/env");
        searchFolders.push_back(searchDir);
    }
    if (!gameDirectory.empty()) {
        searchFolders.push_back(gameDirectory + "/cstrike/gfx/env");
        searchFolders.push_back(gameDirectory + "/valve/gfx/env");
        searchFolders.push_back(gameDirectory + "/czero/gfx/env");
        searchFolders.push_back(gameDirectory + "/gfx/env");
    }

    // Common standard Half-Life Steam installs
    searchFolders.push_back("C:/Program Files (x86)/Steam/steamapps/common/Half-Life/cstrike/gfx/env");
    searchFolders.push_back("C:/Program Files (x86)/Steam/steamapps/common/Half-Life/valve/gfx/env");
    searchFolders.push_back("C:/Program Files/Steam/steamapps/common/Half-Life/cstrike/gfx/env");

    int foundCount = 0;
    std::string matchedFolder = "";

    for (const auto& folder : searchFolders) {
        // Quick check if the front face exists in this folder
        std::string testPath = folder + "/" + m_skyname + "ft.tga";
        std::string testPathUpper = folder + "/" + m_skyname + "FT.tga";
        std::string testPathBmp = folder + "/" + m_skyname + "ft.bmp";

        if (CheckFile(testPath) || CheckFile(testPathUpper) || CheckFile(testPathBmp)) {
            matchedFolder = folder;
            break;
        }
    }

    if (!matchedFolder.empty()) {
        std::vector<uint8_t> faceRGBA;
        uint32_t w = 0, h = 0;

        for (int f = 0; f < 6; ++f) {
            std::vector<std::string> candidateFiles = {
                matchedFolder + "/" + m_skyname + suffixes[f] + ".tga",
                matchedFolder + "/" + m_skyname + suffixesUpper[f] + ".tga",
                matchedFolder + "/" + m_skyname + "_" + suffixes[f] + ".tga",
                matchedFolder + "/" + m_skyname + suffixes[f] + ".bmp",
                matchedFolder + "/" + m_skyname + suffixesUpper[f] + ".bmp"
            };

            bool loaded = false;
            for (const auto& cPath : candidateFiles) {
                if (CheckFile(cPath) && ImageLoader::LoadImage(cPath, w, h, faceRGBA)) {
                    UploadFaceTexture(static_cast<SkyFaceIndex>(f), faceRGBA, w, h, true);
                    foundCount++;
                    loaded = true;
                    break;
                }
            }

            if (!loaded) {
                // If one face was missing, generate fallback procedural face
                SkyPresetId pId = (m_preset == SKY_PRESET_AUTO)
                    ? static_cast<SkyPresetId>(GuessPresetFromSkyname(m_skyname))
                    : static_cast<SkyPresetId>(m_preset - 1);
                ImageLoader::GenerateProceduralSkyFace(pId, static_cast<SkyFaceIndex>(f), 256, faceRGBA);
                UploadFaceTexture(static_cast<SkyFaceIndex>(f), faceRGBA, 256, 256, false);
            }
        }
    }

    if (foundCount >= 4) {
        m_loadedPath = matchedFolder;
        m_isProcedural = false;
        return true;
    }

    // No valid textures found on disk: automatically generate matching procedural skybox
    m_loadedPath = "Procedural Skybox";
    if (m_preset == SKY_PRESET_AUTO) {
        SkyboxPreset guessed = GuessPresetFromSkyname(m_skyname);
        SkyPresetId pId = static_cast<SkyPresetId>(guessed - 1);
        std::vector<uint8_t> faceRGBA;
        for (int f = 0; f < 6; ++f) {
            ImageLoader::GenerateProceduralSkyFace(pId, static_cast<SkyFaceIndex>(f), 256, faceRGBA);
            UploadFaceTexture(static_cast<SkyFaceIndex>(f), faceRGBA, 256, 256, false);
        }
    } else {
        GenerateProceduralFaces();
    }

    m_isProcedural = true;
    return true;
}

bool SkyboxRenderer::LoadFromCustomFolder(const std::string& folderPath, const std::string& skyname) {
    if (folderPath.empty()) return false;
    m_preset = SKY_PRESET_CUSTOM;
    return LoadSkyname(skyname, folderPath, "");
}

bool SkyboxRenderer::LoadFromBSP(const BSPFile& bsp, const std::string& bspPath, const std::string& gameDirectory) {
    std::string skyname;
    if (!bsp.GetSkyname(skyname) || skyname.empty()) {
        skyname = "desert";
    }

    std::string mapDir;
    size_t lastSep = bspPath.find_last_of("/\\");
    if (lastSep != std::string::npos) {
        mapDir = bspPath.substr(0, lastSep);
    } else {
        mapDir = ".";
    }

    return LoadSkyname(skyname, mapDir, gameDirectory);
}

void SkyboxRenderer::SetPreset(SkyboxPreset preset) {
    m_preset = preset;
    if (preset == SKY_PRESET_AUTO) {
        LoadSkyname(m_skyname, "", "");
    } else {
        GenerateProceduralFaces();
    }
}

const char* SkyboxRenderer::GetPresetName(SkyboxPreset p) {
    switch (p) {
        case SKY_PRESET_AUTO:     return "Auto (From Map)";
        case SKY_PRESET_DESERT:   return "Sunny Desert";
        case SKY_PRESET_NIGHT:    return "Assault Night & Stars";
        case SKY_PRESET_OVERCAST: return "Overcast Daylight";
        case SKY_PRESET_SUNSET:   return "Warm Sunset Glow";
        case SKY_PRESET_AZURE:    return "Azure Clear Sky";
        case SKY_PRESET_CUSTOM:   return "Custom Skybox";
        default:                  return "Unknown";
    }
}

void SkyboxRenderer::Render(const Matrix4& viewMatrix, const Matrix4& projMatrix) {
    if (!m_enabled || !m_initialized) return;

    // View matrix with camera translation removed
    Matrix4 viewNoTrans = viewMatrix;
    viewNoTrans.m[12] = 0.0f;
    viewNoTrans.m[13] = 0.0f;
    viewNoTrans.m[14] = 0.0f;

    // Save previous OpenGL depth configuration
    GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLint prevDepthFunc = GL_LESS;
    glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE); // Skybox never writes to depth buffer

    m_shader.Bind();
    m_shader.SetMat4("u_ViewNoTrans", viewNoTrans);
    m_shader.SetMat4("u_Proj", projMatrix);
    m_shader.SetFloat("u_RotationYaw", m_rotationYaw);
    m_shader.SetFloat("u_Exposure", m_exposure);
    m_shader.SetInt("u_SkyTexture", 0);

    glActiveTexture(GL_TEXTURE0);

    for (int i = 0; i < 6; ++i) {
        if (m_faces[i].vao != 0 && m_faces[i].textureId != 0) {
            glBindTexture(GL_TEXTURE_2D, m_faces[i].textureId);
            glBindVertexArray(m_faces[i].vao);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        }
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_shader.Unbind();

    // Restore previous OpenGL depth configuration
    glDepthMask(prevDepthMask);
    glDepthFunc(prevDepthFunc);
    if (!depthTestWasEnabled) {
        glDisable(GL_DEPTH_TEST);
    }
}
