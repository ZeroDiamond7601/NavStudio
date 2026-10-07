#ifndef SKYBOX_RENDERER_H
#define SKYBOX_RENDERER_H

#include <string>
#include <vector>
#include "editor/glad/include/glad/glad.h"
#include "editor/render/shader.h"
#include "editor/math/matrix4.h"
#include "bsp/bsp_file.h"
#include "editor/render/image_loader.h"

enum SkyboxPreset {
    SKY_PRESET_AUTO = 0,    // Auto from BSP skyname
    SKY_PRESET_DESERT,      // Sunny Desert (Dust / Mirage style)
    SKY_PRESET_NIGHT,       // Assault Night & Stars
    SKY_PRESET_OVERCAST,    // Overcast Cloudy (Aztec / Office style)
    SKY_PRESET_SUNSET,      // Warm Sunset Dusk
    SKY_PRESET_AZURE,       // Crisp Clear Azure Sky
    SKY_PRESET_CUSTOM       // Custom user loaded
};

struct SkyboxFaceMesh {
    GLuint vao{0};
    GLuint vbo{0};
    GLuint ebo{0};
    GLuint textureId{0};
    uint32_t width{0};
    uint32_t height{0};
    bool loadedFromDisk{false};
};

class SkyboxRenderer {
public:
    SkyboxRenderer();
    ~SkyboxRenderer();

    // Initialize shader and cube geometry
    bool Init();
    void Clear();

    // Load skybox textures associated with BSP (or auto-select preset if files missing)
    bool LoadFromBSP(const BSPFile& bsp, const std::string& bspPath, const std::string& gameDirectory);

    // Explicitly load a skybox by skyname and search folders
    bool LoadSkyname(const std::string& skyname, const std::string& searchDir, const std::string& gameDirectory);

    // Load from a specific custom directory containing the 6 face files
    bool LoadFromCustomFolder(const std::string& folderPath, const std::string& skyname);

    // Switch preset
    void SetPreset(SkyboxPreset preset);
    SkyboxPreset GetPreset() const { return m_preset; }

    // Render 6-sided cubic skybox around camera
    void Render(const Matrix4& viewMatrix, const Matrix4& projMatrix);

    // Controls
    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool enabled) { m_enabled = enabled; }

    float GetRotationYaw() const { return m_rotationYaw; }
    void SetRotationYaw(float yaw) { m_rotationYaw = yaw; }

    float GetExposure() const { return m_exposure; }
    void SetExposure(float exposure) { m_exposure = exposure; }

    const std::string& GetSkyname() const { return m_skyname; }
    const std::string& GetLoadedPath() const { return m_loadedPath; }
    bool IsUsingProcedural() const { return m_isProcedural; }

    static const char* GetPresetName(SkyboxPreset p);

private:
    void BuildFaceGeometry();
    void UploadFaceTexture(SkyFaceIndex face, const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height, bool fromDisk);
    void GenerateProceduralFaces();
    SkyboxPreset GuessPresetFromSkyname(const std::string& skyname) const;

    bool m_initialized;
    bool m_enabled;
    SkyboxPreset m_preset;
    std::string m_skyname;
    std::string m_loadedPath;
    bool m_isProcedural;
    float m_rotationYaw;
    float m_exposure;

    Shader m_shader;
    SkyboxFaceMesh m_faces[6];
};

#endif // SKYBOX_RENDERER_H
