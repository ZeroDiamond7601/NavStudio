#ifndef EDITOR_SCENE_H
#define EDITOR_SCENE_H

#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include "bsp/bsp_file.h"
#include "nav/nav_file.h"
#include "nav/nav_generator.h"
#include "editor/render/bsp_renderer.h"
#include "editor/render/nav_renderer.h"
#include "editor/render/entity_renderer.h"
#include "editor/render/texture_manager.h"
#include "editor/render/gizmo_renderer.h"

#include "editor/scene/editor_handles.h"
#include <vector>

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
    bool GenerateNavMesh(const NavGenerateOptions& options = NavGenerateOptions());

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
    const NavArea* GetSelectedArea() const;
    uint32_t GetSelectedAreaID() const { return m_selectedAreaId; }

    EntityRenderer& GetEntityRenderer() { return m_entityRenderer; }
    const EntityRenderer& GetEntityRenderer() const { return m_entityRenderer; }
    int GetSelectedEntityIndex() const { return m_selectedEntityIndex; }
    void SelectEntity(int index) { m_selectedEntityIndex = index; }
    const EditorEntity* GetSelectedEntity() const { return m_entityRenderer.GetEntity(m_selectedEntityIndex); }
    EditorEntity* GetSelectedEntity() { return m_entityRenderer.GetEntity(m_selectedEntityIndex); }

    GizmoMode GetGizmoMode() const { return m_gizmoMode; }
    void SetGizmoMode(GizmoMode mode) { m_gizmoMode = mode; }
    GizmoRenderer& GetGizmoRenderer() { return m_gizmoRenderer; }
    const GizmoRenderer& GetGizmoRenderer() const { return m_gizmoRenderer; }

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

    // Game Resources Directory & Textures
    const std::string& GetGameDirectory() const { return m_gameDirectory; }
    void SetGameDirectory(const std::string& dir) { m_gameDirectory = dir; }
    bool HasGameDirectory() const { return !m_gameDirectory.empty(); }
    TextureManager& GetTextureManager() { return m_textureManager; }
    const TextureManager& GetTextureManager() const { return m_textureManager; }
    bool LoadWAD(const std::string& wadPath);

    // Recent Files Management
    const std::vector<std::string>& GetRecentFiles() const { return m_recentFiles; }
    void AddRecentFile(const std::string& path);
    void ClearRecentFiles();
    void LoadRecentFiles();
    void SaveRecentFiles();

    // Grid Snapping Management
    float GetGridSize() const { return m_gridSize; }
    void SetGridSize(float size);
    void IncreaseGridSize();
    void DecreaseGridSize();
    bool GetGridSnap() const { return m_gridSnap; }
    void SetGridSnap(bool snap) { m_gridSnap = snap; }
    void ToggleGridSnap();
    float SnapValue(float val) const;
    Vector3 SnapVector(const Vector3& v) const;

    // Interactive Handles (Gizmo Arrows, Edges, Corners)
    SelectedHandleType GetHoveredHandle() const { return m_hoveredHandle; }
    void SetHoveredHandle(SelectedHandleType h);
    SelectedHandleType GetSelectedHandle() const { return m_selectedHandle; }
    void SetSelectedHandle(SelectedHandleType h);

    bool IsDraggingHandle() const { return m_isDraggingHandle; }
    SelectedHandleType GetDraggedHandle() const { return m_draggedHandle; }
    void StartDragHandle(SelectedHandleType handle, float screenX, float screenY, const Ray& ray);
    void UpdateDragHandle(float screenX, float screenY, const Ray& ray, float deltaY = 0.0f);
    bool EndDragHandle(class CommandManager& cmdMgr);

    // 3D Transform and Connection modes
    enum EditorTransformMode {
        TRANSFORM_NONE = 0,
        TRANSFORM_TRANSLATE,  // Move [G]
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
    void StartGrabWithRay(const Ray& ray);
    void StartScale(const Vector3& initialHitPoint);
    void StartScaleWithScreen(float screenX, float screenY, float viewportW, float viewportH, const Matrix4& viewProj);
    void StartConnectMode();
    void SetTransformAxis(EditorTransformAxis axis);
    void ToggleTransformAxis(EditorTransformAxis axis);
    void CancelTransform();
    bool ConfirmTransform(class CommandManager& cmdMgr);

    void UpdateTransform(const Vector3& currentHitPoint, float mouseDeltaY = 0.0f);
    void UpdateTransformWithRay(const Ray& ray, float mouseX, float mouseY, float deltaY = 0.0f);
    void SetConnectHoverArea(uint32_t areaId);
    uint32_t GetConnectHoverArea() const { return m_connectHoverAreaId; }

    void ConnectSelectedTo(uint32_t targetId, bool bidirectional, class CommandManager& cmdMgr);
    void DisconnectSelectedFrom(uint32_t targetId, bool bidirectional, class CommandManager& cmdMgr);

    void DuplicateSelectedArea(class CommandManager& cmdMgr);
    void DeleteSelectedArea(class CommandManager& cmdMgr);
    void RotateSelectedArea90(class CommandManager& cmdMgr);

    // Hammer-style editing tools
    void ExtrudeSelectedEdge(class CommandManager& cmdMgr, float length = 0.0f);
    void SplitSelectedArea(class CommandManager& cmdMgr);
    void MergeSelectedArea(class CommandManager& cmdMgr);

private:
    std::unique_ptr<BSPFile> m_bsp;
    std::unique_ptr<NavMesh> m_nav;
    BSPRenderer m_bspRenderer;
    NavRenderer m_navRenderer;
    GizmoRenderer m_gizmoRenderer;
    EntityRenderer m_entityRenderer;
    TextureManager m_textureManager;

    std::string m_bspPath;
    std::string m_navPath;
    std::string m_errorMessage;
    std::string m_gameDirectory;

    uint32_t m_selectedAreaId;
    uint32_t m_hoveredAreaId;
    int m_selectedEntityIndex{-1};

    BSPRenderMode m_bspMode;
    bool m_showBSP;
    bool m_showNAV;
    bool m_showConnections;
    GizmoMode m_gizmoMode{GIZMO_MODE_COMBINED};

    // Recent files
    std::vector<std::string> m_recentFiles;

    // Grid Snapping
    float m_gridSize{32.0f};
    bool m_gridSnap{true};

    // Handles
    SelectedHandleType m_hoveredHandle{HANDLE_NONE};
    SelectedHandleType m_selectedHandle{HANDLE_NONE};
    SelectedHandleType m_draggedHandle{HANDLE_NONE};
    bool m_isDraggingHandle{false};

    // Handle Drag Initial State
    NavExtent m_dragStartExtent;
    float m_dragStartNeZ{0.0f};
    float m_dragStartSwZ{0.0f};
    Vector3 m_dragStartCenter{0.0f, 0.0f, 0.0f};
    Vector3 m_dragStartGroundHit{0.0f, 0.0f, 0.0f};
    Vector3 m_dragStartPlaneHit{0.0f, 0.0f, 0.0f};
    float m_dragStartAxisT{0.0f};
    float m_dragStartAngle{0.0f};
    float m_dragStartScreenX{0.0f};
    float m_dragStartScreenY{0.0f};
    float m_dragStartScaleDist{1.0f};
    Vector3 m_dragStartEntityOrigin{0.0f, 0.0f, 0.0f};
    Vector3 m_dragStartEntityAngles{0.0f, 0.0f, 0.0f};
    Vector3 m_dragStartEntityMins{0.0f, 0.0f, 0.0f};
    Vector3 m_dragStartEntityMaxs{0.0f, 0.0f, 0.0f};

    // Modal Transform Initial State
    EditorTransformMode m_transformMode{TRANSFORM_NONE};
    EditorTransformAxis m_transformAxis{AXIS_NONE};
    uint32_t m_connectHoverAreaId{0};
    NavExtent m_initialExtent;
    float m_initialNeZ{0.0f};
    float m_initialSwZ{0.0f};
    Vector3 m_initialHitPoint{0.0f, 0.0f, 0.0f};
    Vector3 m_grabOffset{0.0f, 0.0f, 0.0f};
    float m_scaleStartDist{50.0f};
    float m_scaleCenterScreenX{0.0f};
    float m_scaleCenterScreenY{0.0f};

    AsyncLoadContext m_loadCtx;
    std::thread m_loadThread;
};

#endif // EDITOR_SCENE_H
