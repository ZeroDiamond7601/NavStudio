#ifndef ENTITY_RENDERER_H
#define ENTITY_RENDERER_H

#include <vector>
#include <string>
#include "editor/glad/include/glad/glad.h"
#include "bsp/bsp_file.h"
#include "editor/scene/editor_entity.h"
#include "editor/render/shader.h"
#include "editor/render/bsp_renderer.h"
#include "editor/math/matrix4.h"

class EntityRenderer {
public:
    EntityRenderer();
    ~EntityRenderer();

    bool BuildFromBSP(const BSPFile& bsp);
    void Clear();

    void Render(
        const Shader& meshShader,
        const Shader& lineShader,
        const Matrix4& mvp,
        int selectedEntityIndex,
        const Vector3& camPos
    );

    bool IsLoaded() const { return m_loaded; }
    size_t GetEntityCount() const { return m_entities.size(); }
    const std::vector<EditorEntity>& GetEntities() const { return m_entities; }
    const EditorEntity* GetEntity(int index) const;

    // Category Counts
    int GetSpawnCTCount() const { return m_countSpawnCT; }
    int GetSpawnTCount() const { return m_countSpawnT; }
    int GetObjectiveCount() const { return m_countObjective; }
    int GetLightCount() const { return m_countLight; }
    int GetItemCount() const { return m_countItem; }
    int GetTriggerCount() const { return m_countTrigger; }
    int GetBrushCount() const { return m_countBrush; }

    // Visibility Filters
    bool GetShowEntities() const { return m_showEntities; }
    void SetShowEntities(bool show) { m_showEntities = show; }

    bool GetShowSpawns() const { return m_showSpawns; }
    void SetShowSpawns(bool show) { m_showSpawns = show; }

    bool GetShowObjectives() const { return m_showObjectives; }
    void SetShowObjectives(bool show) { m_showObjectives = show; }

    bool GetShowLights() const { return m_showLights; }
    void SetShowLights(bool show) { m_showLights = show; }

    bool GetShowItems() const { return m_showItems; }
    void SetShowItems(bool show) { m_showItems = show; }

    bool GetShowTriggers() const { return m_showTriggers; }
    void SetShowTriggers(bool show) { m_showTriggers = show; }

    bool GetShowBrushes() const { return m_showBrushes; }
    void SetShowBrushes(bool show) { m_showBrushes = show; }

    bool GetShowTargetLines() const { return m_showTargetLines; }
    void SetShowTargetLines(bool show) { m_showTargetLines = show; }

    bool IsEntityVisible(const EditorEntity& ent) const;

private:
    void GenerateBuffers(const std::vector<BSPVertex>& vertices, const std::vector<uint32_t>& indices);
    void GenerateWireBuffers(const std::vector<BSPVertex>& vertices, const std::vector<uint32_t>& indices);
    void GenerateTargetLineBuffers(const std::vector<BSPVertex>& vertices, const std::vector<uint32_t>& indices);
    void GenerateSelectBuffers(const std::vector<BSPVertex>& vertices, const std::vector<uint32_t>& indices);

    std::vector<EditorEntity> m_entities;

    GLuint m_solidVao;
    GLuint m_solidVbo;
    GLuint m_solidEbo;
    GLsizei m_solidIndexCount;

    GLuint m_wireVao;
    GLuint m_wireVbo;
    GLuint m_wireEbo;
    GLsizei m_wireIndexCount;

    GLuint m_targetLineVao;
    GLuint m_targetLineVbo;
    GLuint m_targetLineEbo;
    GLsizei m_targetLineIndexCount;

    GLuint m_selectVao;
    GLuint m_selectVbo;
    GLuint m_selectEbo;
    GLsizei m_selectIndexCount;
    int m_cachedSelectedEntityIndex{-1};

    bool m_loaded;
    bool m_showEntities;
    bool m_showSpawns;
    bool m_showObjectives;
    bool m_showLights;
    bool m_showItems;
    bool m_showTriggers;
    bool m_showBrushes;
    bool m_showTargetLines;

    int m_countSpawnCT{0};
    int m_countSpawnT{0};
    int m_countObjective{0};
    int m_countLight{0};
    int m_countItem{0};
    int m_countTrigger{0};
    int m_countBrush{0};
};

#endif // ENTITY_RENDERER_H
