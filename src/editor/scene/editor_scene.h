#ifndef EDITOR_SCENE_H
#define EDITOR_SCENE_H

#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include "bsp/bsp_file.h"
#include "nav/nav_file.h"
#include "editor/render/bsp_renderer.h"
#include "editor/render/nav_renderer.h"

struct AsyncLoadContext {
    std::atomic<bool> inProgress{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> success{false};
    std::atomic<float> progress{0.0f};

    std::string targetBspPath;
    std::string targetNavPath;
    std::string filename;
    std::string statusText;
    std::string errorMessage;

    std::unique_ptr<BSPFile> loadedBsp;
    std::unique_ptr<NavMesh> loadedNav;
    std::mutex mutex;

    float minDisplayTimer{0.0f};
    float displayProgress{0.0f};
};

class EditorScene {
public:
    EditorScene();
    ~EditorScene();

    // Synchronous loading
    bool LoadBSP(const std::string& bspPath);
    bool LoadNAV(const std::string& navPath);
    bool SaveNAV(const std::string& navPath = "");

    // Asynchronous loading with progress and stage tracking
    void StartAsyncLoad(const std::string& bspOrNavPath, const std::string& explicitNavPath = "");
    void UpdateAsyncLoading(float deltaTime);

    bool IsLoading() const { return m_loadCtx.inProgress.load(); }
    float GetLoadingProgress() const { return m_loadCtx.displayProgress; }
    const std::string& GetLoadingFilename() const { return m_loadCtx.filename; }
    const std::string& GetLoadingStatus() const { return m_loadCtx.statusText; }

    bool HasLoadingError() const { return !m_errorMessage.empty(); }
    const std::string& GetLoadingError() const { return m_errorMessage; }
    void ClearLoadingError() { m_errorMessage.clear(); }

    void SelectArea(uint32_t id);
    void SetHoveredArea(uint32_t id);
    NavArea* GetSelectedArea();
    uint32_t GetSelectedAreaID() const { return m_selectedAreaId; }

    void Render(const Shader& meshShader, const Shader& lineShader, const Matrix4& mvp, const Vector3& camPos);
    void RebuildNavRenderer();

    const BSPFile& GetBSP() const { return *m_bsp; }
    BSPFile& GetBSP() { return *m_bsp; }

    const NavMesh& GetNAV() const { return *m_nav; }
    NavMesh& GetNAV() { return *m_nav; }

    const std::string& GetBSPPath() const { return m_bspPath; }
    const std::string& GetNAVPath() const { return m_navPath; }

    bool HasBSP() const { return m_bsp && m_bsp->IsLoaded(); }
    bool HasNAV() const { return m_nav && m_nav->IsLoaded(); }

    BSPRenderMode GetBSPMode() const { return m_bspMode; }
    void SetBSPMode(BSPRenderMode mode) { m_bspMode = mode; }

    bool GetShowBSP() const { return m_showBSP; }
    void SetShowBSP(bool show) { m_showBSP = show; }

    bool GetShowNAV() const { return m_showNAV; }
    void SetShowNAV(bool show) { m_showNAV = show; }

    bool GetShowConnections() const { return m_showConnections; }
    void SetShowConnections(bool show) { m_showConnections = show; }

    bool GetShowWireframeOnSolid() const { return m_bspRenderer.GetShowWireframeOnSolid(); }
    void SetShowWireframeOnSolid(bool show) { m_bspRenderer.SetShowWireframeOnSolid(show); }

    // Blender-style 3D Transform and Connection modes
    enum EditorTransformMode {
        TRANSFORM_NONE = 0,
        TRANSFORM_TRANSLATE,  // Grab [G]
        TRANSFORM_SCALE,      // Scale [S]
        TRANSFORM_CONNECT     // Connect Mode [C]
    };

    enum EditorTransformAxis {
        AXIS_NONE = 0,
        AXIS_X = 1,
        AXIS_Y = 2,
        AXIS_Z = 3
    };

    EditorTransformMode GetTransformMode() const { return m_transformMode; }
    EditorTransformAxis GetTransformAxis() const { return m_transformAxis; }

    void StartGrab(const Vector3& initialHitPoint);
    void StartScale(const Vector3& initialHitPoint);
    void StartConnectMode();
    void SetTransformAxis(EditorTransformAxis axis);
    void ToggleTransformAxis(EditorTransformAxis axis);
    void CancelTransform();
    bool ConfirmTransform(class CommandManager& cmdMgr);

    void UpdateTransform(const Vector3& currentHitPoint, float mouseDeltaY = 0.0f);
    void SetConnectHoverArea(uint32_t areaId);
    uint32_t GetConnectHoverArea() const { return m_connectHoverAreaId; }

    void ConnectSelectedTo(uint32_t targetId, bool bidirectional, class CommandManager& cmdMgr);
    void DisconnectSelectedFrom(uint32_t targetId, bool bidirectional, class CommandManager& cmdMgr);

    void DuplicateSelectedArea(class CommandManager& cmdMgr);
    void DeleteSelectedArea(class CommandManager& cmdMgr);
    void RotateSelectedArea90(class CommandManager& cmdMgr);

private:
    std::unique_ptr<BSPFile> m_bsp;
    std::unique_ptr<NavMesh> m_nav;
    BSPRenderer m_bspRenderer;
    NavRenderer m_navRenderer;

    std::string m_bspPath;
    std::string m_navPath;
    std::string m_errorMessage;

    uint32_t m_selectedAreaId;
    uint32_t m_hoveredAreaId;

    BSPRenderMode m_bspMode;
    bool m_showBSP;
    bool m_showNAV;
    bool m_showConnections;

    EditorTransformMode m_transformMode{TRANSFORM_NONE};
    EditorTransformAxis m_transformAxis{AXIS_NONE};
    uint32_t m_connectHoverAreaId{0};
    NavExtent m_initialExtent;
    float m_initialNeZ{0.0f};
    float m_initialSwZ{0.0f};
    Vector3 m_initialHitPoint{0.0f, 0.0f, 0.0f};

    AsyncLoadContext m_loadCtx;
    std::thread m_loadThread;
};

#endif // EDITOR_SCENE_H
