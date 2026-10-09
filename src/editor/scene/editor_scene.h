#ifndef EDITOR_SCENE_H
#define EDITOR_SCENE_H

#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include "bsp/bsp_file.h"
#include "nav/nav_file.h"
#include "nav/nav_path.h"
#include "nav/nav_generator.h"
#include "editor/render/bsp_renderer.h"
#include "editor/render/nav_renderer.h"
#include "editor/render/entity_renderer.h"
#include "editor/render/texture_manager.h"
#include "editor/render/gizmo_renderer.h"
#include "editor/render/skybox_renderer.h"

#include "editor/scene/editor_handles.h"
#include "editor/scene/editor_preferences.h"
#include "waypoint/waypoint_graph.h"
#include "editor/render/waypoint_renderer.h"
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

struct SelectedConnection {
    uint32_t fromId{0};
    uint32_t toId{0};
    int dir{-1};

    bool valid() const { return fromId != 0 && toId != 0; }
    void clear() { fromId = 0; toId = 0; dir = -1; }
    bool operator==(const SelectedConnection& o) const {
        return fromId == o.fromId && toId == o.toId;
    }
    bool operator!=(const SelectedConnection& o) const {
        return !(*this == o);
    }
};

class EditorScene {
public:
    EditorScene();
    ~EditorScene();

    // Synchronous loading
    bool LoadBSP(const std::string& bspPath);
    bool LoadNAV(const std::string& navPath);
    bool SaveNAV(const std::string& navPath = "");
    bool ExportNAVToOBJ(const std::string& filepath) const;
    bool AutosaveNAV();
    void UpdateAutosave(float deltaTime);
    const std::string& GetLastAutosavePath() const { return m_lastAutosavePath; }
    bool GenerateNavMesh(const NavGenerateOptions& options = NavGenerateOptions());
    void UnloadNAV();
    void UnloadBSP();

    bool IsModified() const { return m_isModified; }
    void SetModified(bool mod) { m_isModified = mod; }

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

    // Selection & Multi-Selection
    void SelectArea(uint32_t id, bool additive = false, bool toggle = false);
    bool IsAreaSelected(uint32_t id) const;
    const std::vector<uint32_t>& GetSelectedAreaIDs() const { return m_selectedAreaIds; }
    size_t GetSelectedAreaCount() const { return m_selectedAreaIds.size(); }
    void ClearSelection();
    void SelectAllAreas();

    // Multi-Selection Batch Actions
    void BatchSetAttributes(uint8_t flags, class CommandManager& cmdMgr);
    void BatchSetPlace(const std::string& placeName, class CommandManager& cmdMgr);
    void BatchSnapToNeighbors(class CommandManager& cmdMgr);
    void BatchSnapToFloor(class CommandManager& cmdMgr);
    void BatchDuplicate(class CommandManager& cmdMgr);
    void BatchDelete(class CommandManager& cmdMgr);
    void BatchExtrude(class CommandManager& cmdMgr, SelectedHandleType edge = HANDLE_NONE, float length = 0.0f);

    void SetHoveredArea(uint32_t id);
    NavArea* GetSelectedArea();
    const NavArea* GetSelectedArea() const;
    uint32_t GetSelectedAreaID() const { return m_selectedAreaId; }

    // Connection Selection & Editing
    bool IsConnectionSelectionMode() const { return m_connectionSelectMode; }
    void SetConnectionSelectionMode(bool enable) { m_connectionSelectMode = enable; }
    void ToggleConnectionSelectionMode() { m_connectionSelectMode = !m_connectionSelectMode; }

    bool HasSelectedConnection() const { return m_selectedConnection.valid(); }
    const SelectedConnection& GetSelectedConnection() const { return m_selectedConnection; }
    void SelectConnection(uint32_t fromId, uint32_t toId, int dir = -1);
    void ClearSelectedConnection();
    bool DeleteSelectedConnection(class CommandManager& cmdMgr);
    bool ReverseSelectedConnection(class CommandManager& cmdMgr);
    bool ToggleSelectedConnectionBidirectional(class CommandManager& cmdMgr);

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

    void Render(const Shader& meshShader, const Shader& lineShader, const Matrix4& view, const Matrix4& proj, const Vector3& camPos);
    void Render(const Shader& meshShader, const Shader& lineShader, const Matrix4& mvp, const Vector3& camPos);
    void RebuildNavRenderer();
    NavRenderer& GetNavRenderer() { return m_navRenderer; }
    const NavRenderer& GetNavRenderer() const { return m_navRenderer; }

    const BSPFile& GetBSP() const { return *m_bsp; }
    BSPFile& GetBSP() { return *m_bsp; }

    const NavMesh& GetNAV() const { return *m_nav; }
    NavMesh& GetNAV() { return *m_nav; }

    const std::string& GetBSPPath() const { return m_bspPath; }
    std::string GetNAVPath() const {
        if (!m_navPath.empty()) return m_navPath;
        if (!m_bspPath.empty()) {
            size_t dotPos = m_bspPath.find_last_of('.');
            if (dotPos != std::string::npos) return m_bspPath.substr(0, dotPos) + ".nav";
            return m_bspPath + ".nav";
        }
        return "";
    }
    void SetNAVPath(const std::string& path) { m_navPath = path; }

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

    bool GetShowSkybox() const { return m_bspRenderer.GetShowSkybox(); }
    void SetShowSkybox(bool show) { m_bspRenderer.SetShowSkybox(show); m_skyboxRenderer.SetEnabled(show); }

    bool GetShowSkyWireframe() const { return m_bspRenderer.GetShowSkyWireframe(); }
    void SetShowSkyWireframe(bool show) { m_bspRenderer.SetShowSkyWireframe(show); }

    SkyboxRenderer& GetSkyboxRenderer() { return m_skyboxRenderer; }
    const SkyboxRenderer& GetSkyboxRenderer() const { return m_skyboxRenderer; }

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

    // Mesh-to-Mesh Snapping (Snap flush against neighboring nav areas to eliminate gaps)
    bool GetMeshSnap() const { return m_meshSnap; }
    void SetMeshSnap(bool snap) { m_meshSnap = snap; }
    void ToggleMeshSnap() { m_meshSnap = !m_meshSnap; }
    float SnapToNeighborEdge(uint32_t currentAreaId, float candidateVal, bool isXAxis,
                             float refMinOtherAxis, float refMaxOtherAxis) const;
    void SnapSelectedAreaToNeighbors(class CommandManager& cmdMgr);

    // Edge Snapping on Move & Seamless Auto-Connections
    bool GetSnapToEdgeOnMove() const { return m_enableSnapToEdgeOnMove; }
    void SetSnapToEdgeOnMove(bool enable) { m_enableSnapToEdgeOnMove = enable; }
    void ToggleSnapToEdgeOnMove() { m_enableSnapToEdgeOnMove = !m_enableSnapToEdgeOnMove; }
    bool GetAutoConnectOnEdgeSnap() const { return m_autoConnectOnEdgeSnap; }
    void SetAutoConnectOnEdgeSnap(bool enable) { m_autoConnectOnEdgeSnap = enable; }
    void SnapMovedAreaToNeighborEdges(uint32_t areaId, NavExtent& inOutExt, float& inOutNeZ, float& inOutSwZ) const;
    void AutoConnectTouchingNeighbors(uint32_t areaId);

    // Camera-oriented view tracking for extrude & navigation
    void SetCameraForward(const Vector3& fwd) { m_cameraForward = fwd; }
    Vector3 GetCameraForward() const { return m_cameraForward; }
    SelectedHandleType GetCameraFacingEdge() const;
    bool GetExtrudeCameraFacing() const { return m_extrudeCameraFacing; }
    void SetExtrudeCameraFacing(bool enable) { m_extrudeCameraFacing = enable; }

    // Bridge Tool (Click Edge A and Edge B to create intermediate connecting NavArea)
    bool IsBridgeMode() const { return m_isBridgeMode; }
    void StartBridgeMode();
    void CancelBridgeMode();
    void ToggleBridgeMode();
    uint32_t GetBridgeFirstArea() const { return m_bridgeFirstAreaId; }
    SelectedHandleType GetBridgeFirstEdge() const { return m_bridgeFirstEdge; }
    uint32_t GetBridgeHoverArea() const { return m_bridgeHoverAreaId; }
    SelectedHandleType GetBridgeHoverEdge() const { return m_bridgeHoverEdge; }
    void SetBridgeHoverEdge(uint32_t areaId, SelectedHandleType edge);
    void OnBridgeClick(uint32_t areaId, SelectedHandleType edge, class CommandManager& cmdMgr);

    // Draw Area Marquee Tool (2-Click rectangular area creation)
    bool IsDrawAreaMode() const { return m_isDrawAreaMode; }
    bool IsDrawAreaActive() const { return m_drawAreaActive; }
    void StartDrawAreaMode();
    void CancelDrawArea();
    void ExitDrawAreaMode();
    void ToggleDrawAreaMode();
    const Vector3& GetDrawAreaStart() const { return m_drawAreaStart; }
    const Vector3& GetDrawAreaCurrent() const { return m_drawAreaCurrent; }
    void UpdateDrawArea(const Ray& ray);
    void OnDrawAreaClick(const Ray& ray, class CommandManager& cmdMgr);
    float GetDrawAreaNwZ() const { return m_drawAreaNwZ; }
    float GetDrawAreaNeZ() const { return m_drawAreaNeZ; }
    float GetDrawAreaSeZ() const { return m_drawAreaSeZ; }
    float GetDrawAreaSwZ() const { return m_drawAreaSwZ; }
    bool SnapToAreaCorner(Vector3& pos, float tolerance = 8.0f);
    bool IsDrawAreaCornerSnapped() const { return m_drawAreaSnappedCorner; }
    const Vector3& GetDrawAreaCornerPos() const { return m_drawAreaCornerPos; }
    float GetCornerSnapTolerance() const { return m_cornerSnapTolerance; }
    void SetCornerSnapTolerance(float tol) { m_cornerSnapTolerance = tol; }

    // Area Copy / Paste across maps and sessions
    bool CopySelectedAreas();
    bool PasteAreas(const Vector3* targetPos, class CommandManager& cmdMgr);

    // Fill Area Tool (Click any floor to auto-fill room/surface with NavMesh)
    bool IsFillAreaMode() const { return m_isFillAreaMode; }
    void StartFillAreaMode();
    void ExitFillAreaMode();
    void ToggleFillAreaMode();
    void CancelFillAreaMode() { ExitFillAreaMode(); }
    size_t FloodFillAreaAt(const Ray& ray, class CommandManager& cmdMgr);

    // Box Selection Tool (Marquee 2D rectangular multi-selection [Shift+B])
    bool IsBoxSelectMode() const { return m_isBoxSelectMode; }
    void ToggleBoxSelectMode() { m_isBoxSelectMode = !m_isBoxSelectMode; }
    void SetBoxSelectMode(bool enabled) { m_isBoxSelectMode = enabled; }
    void BoxSelectAreas(const std::vector<uint32_t>& areaIds, bool additive, bool subtractive);

    // Split Area Knife Tool (Interactive Cutter [K] with 0°, 45°, 90°, 135° angles)
    enum KnifeCutAngle {
        KNIFE_ANGLE_0 = 0,    // Horizontal cut (along X, splitting Y)
        KNIFE_ANGLE_45 = 45,  // Diagonal cut 45°
        KNIFE_ANGLE_90 = 90,  // Vertical cut (along Y, splitting X)
        KNIFE_ANGLE_135 = 135 // Diagonal cut 135°
    };
    bool IsKnifeMode() const { return m_isKnifeMode; }
    void StartKnifeMode();
    void ExitKnifeMode();
    void ToggleKnifeMode();
    void UpdateKnife(const Ray& ray);
    void OnKnifeClick(const Ray& ray, class CommandManager& cmdMgr);
    uint32_t GetKnifeHoverArea() const { return m_knifeHoverAreaId; }
    bool GetKnifeSplitAlongY() const { return m_knifeAngle == KNIFE_ANGLE_0; }
    float GetKnifeSplitCoord() const { return m_knifeSplitCoord; }
    float GetKnifeSplitCoordX() const { return m_knifeSplitCoordX; }
    float GetKnifeSplitCoordY() const { return m_knifeSplitCoordY; }
    KnifeCutAngle GetKnifeAngle() const { return m_knifeAngle; }
    void SetKnifeAngle(KnifeCutAngle angle);
    void CycleKnifeAngle();
    void RotateKnifeAxis() { CycleKnifeAngle(); }
    bool GetKnifeAxisOverride() const { return m_knifeAngle == KNIFE_ANGLE_0; }
    bool IsKnifeAxisForced() const { return m_knifeForceAxis; }

    // Interactive Path Preview / Simulator
    struct PathPreviewState {
        uint32_t startAreaId{0};
        uint32_t goalAreaId{0};
        Vector3 startPos{0.0f, 0.0f, 0.0f};
        Vector3 goalPos{0.0f, 0.0f, 0.0f};
        bool hasStart{false};
        bool hasGoal{false};
        bool isToolActive{false};
        bool showPath{true};
        int flags{NAV_PATH_DEFAULT};
        NavPath path;
        std::vector<Vector3> renderPoints;
        std::vector<bool> jumpFlags;
    };

    PathPreviewState& GetPathPreview() { return m_pathPreview; }
    const PathPreviewState& GetPathPreview() const { return m_pathPreview; }
    void SetPathStart(uint32_t areaId, const Vector3& pos);
    void SetPathGoal(uint32_t areaId, const Vector3& pos);
    void ClearPath();
    bool RecomputePath();
    void TogglePathTool();
    bool IsPathToolActive() const { return m_pathPreview.isToolActive; }
    void SetPathToolActive(bool active) { m_pathPreview.isToolActive = active; }
    void OnPathToolClick(const Ray& ray);

    // Island / Disconnected Region Finder
    struct IslandCluster {
        int id{0};
        size_t areaCount{0};
        bool hasSpawn{false};
        Vector3 center{0.0f, 0.0f, 0.0f};
        std::vector<uint32_t> areaIds;
    };

    std::vector<IslandCluster> AnalyzeIslands() const;
    bool GetShowIslandColors() const { return m_showIslandColors; }
    void SetShowIslandColors(bool show);
    void ToggleIslandColors();
    void SelectIsland(int clusterId);
    void DeleteIsland(int clusterId, class CommandManager& cmdMgr);

    // Player Clearance Hull Visualizer
    bool GetShowClearanceHull() const { return m_showClearanceHull; }
    void SetShowClearanceHull(bool show) { m_showClearanceHull = show; }
    void ToggleClearanceHull() { m_showClearanceHull = !m_showClearanceHull; }
    bool GetClearanceCrouch() const { return m_clearanceCrouch; }
    void SetClearanceCrouch(bool crouch) { m_clearanceCrouch = crouch; }
    bool CheckClearance(const Vector3& pos, bool crouch, float* outCeilingDist = nullptr) const;

    // Map Landmarks & Teleport (Spawn Points, Objectives, Zones)
    struct MapLandmark {
        std::string name;
        std::string category;
        Vector3 origin;
        int entityIndex{-1};
    };
    std::vector<MapLandmark> GetMapLandmarks() const;

    // Autosave Crash & Recovery
    bool CheckAutosaveRecovery(std::string& outBakPath, std::string& outNavPath, int64_t* outBakAgeSec = nullptr) const;

    // Multi-Area Alignment & Layout
    enum AlignMode {
        ALIGN_MIN_X = 0,
        ALIGN_MAX_X,
        ALIGN_MIN_Y,
        ALIGN_MAX_Y,
        ALIGN_CENTER_X,
        ALIGN_CENTER_Y,
        ALIGN_FLOOR_Z
    };
    void AlignSelectedAreas(AlignMode mode, class CommandManager& cmdMgr);

    // Ladder Management & Manual Creation
    size_t BuildLaddersFromBSP();
    void ClearLadders();
    size_t GetLadderCount() const { return (m_nav && m_nav->IsLoaded()) ? m_nav->GetLadders().size() : 0; }
    uint32_t GetSelectedLadderID() const { return m_selectedLadderId; }
    void SelectLadder(uint32_t id);
    NavLadder* GetSelectedLadder() const;
    NavLadder* AddManualLadder(const Vector3& top, const Vector3& bottom, float width = 32.0f, NavDirType dir = NAV_DIR_NORTH);
    bool DeleteSelectedLadder();
    bool DeleteLadder(uint32_t id);
    void ConnectLadderToNearestAreas(uint32_t ladderId);

    // Tactical Hiding Spots
    void AddHidingSpot(uint32_t areaId, const Vector3& pos, uint8_t flags);
    void RemoveHidingSpot(uint32_t areaId, uint32_t spotId);
    void SetHidingSpotFlags(uint32_t areaId, uint32_t spotId, uint8_t flags);

    // Mesh Diagnostics ("Mesh Doctor")
    enum class DiagnosticSeverity {
        DiagError = 0,
        DiagWarning,
        DiagInfo
    };

    enum class DiagnosticType {
        ImpassableStep = 0,
        TrapArea,
        OrphanArea,
        CrouchClearance,
        SpawnDisconnected,
        ZeroDimension
    };

    struct DiagnosticIssue {
        DiagnosticSeverity severity{DiagnosticSeverity::DiagWarning};
        DiagnosticType type{DiagnosticType::ImpassableStep};
        uint32_t areaId{0};
        Vector3 location{0.0f, 0.0f, 0.0f};
        std::string title;
        std::string description;
        std::string fixActionName;
    };

    void RunDiagnostics();
    const std::vector<DiagnosticIssue>& GetDiagnostics() const { return m_diagnostics; }
    void ClearDiagnostics() { m_diagnostics.clear(); }
    bool FixDiagnosticIssue(size_t index, class CommandManager& cmdMgr);
    uint32_t GetDiagnosticHighlightArea() const { return m_diagnosticHighlightAreaId; }
    void SetDiagnosticHighlightArea(uint32_t id);

    // Full Map Auto-Generation
    struct NavGenProgress {
        std::atomic<bool> isGenerating{false};
        std::atomic<float> progress{0.0f};
        std::string statusMessage{"Idle"};
        bool completed{false};
        bool success{false};
        size_t generatedAreas{0};
        std::string errorMessage;
    };
    NavGenProgress& GetNavGenProgress() { return m_navGenProgress; }
    const NavGenProgress& GetNavGenProgress() const { return m_navGenProgress; }
    bool StartFullMapNavGeneration(float stepHeight, float maxJump, float crouchClearance, float gridResolution);
    void UpdateNavGeneration();

    // Toast Notification System
    struct ToastNotification {
        std::string message;
        float remainingSeconds{3.0f};
    };
    void ShowToast(const std::string& message, float duration = 3.5f);
    void UpdateToasts(float deltaTime);
    const std::vector<ToastNotification>& GetToasts() const { return m_toasts; }

    // Workflow QoL (Hot Reload, JSON & Pawn Export)
    bool ReloadCurrentMap();
    bool ExportNavToJSON(const std::string& jsonFilePath) const;
    std::string GeneratePawnWaypointsCode(bool fromSelectionOnly = false) const;

    // Bot Waypoint System (CS-EBOT, SyPB, YaPB, POD-Bot)
    WaypointGraph& GetWaypoints() { return m_waypoints; }
    const WaypointGraph& GetWaypoints() const { return m_waypoints; }
    WaypointRenderer& GetWaypointRenderer() { return m_waypointRenderer; }
    const WaypointRenderer& GetWaypointRenderer() const { return m_waypointRenderer; }

    bool HasWaypoints() const { return !m_waypoints.IsEmpty(); }
    bool GetShowWaypoints() const { return m_showWaypoints; }
    bool IsShowWaypoints() const { return m_showWaypoints; }
    void SetShowWaypoints(bool show);
    void ToggleShowWaypoints();

    uint32_t GetSelectedWaypointID() const { return m_selectedWaypointId; }
    void SelectWaypoint(uint32_t id);
    WaypointNode* GetSelectedWaypoint();
    const WaypointNode* GetSelectedWaypoint() const;

    bool LoadWaypoints(const std::string& path);
    bool SaveWaypoints(const std::string& path, BotType bot, GameMod mod);
    bool ConvertNavToWaypoints(BotType bot, GameMod mod);
    size_t ConvertWaypointsToNav();
    void RebuildWaypointRenderer();
    WaypointGraph::WaypointAnalysisStats AutoAnalyzeWaypoints();

    // User Preferences
    EditorPreferences& GetPreferences() { return m_prefs; }
    const EditorPreferences& GetPreferences() const { return m_prefs; }
    void ApplyPreferences();

    // Auto-Crouch & Obstacle Flag Analyzer
    struct AnalyzerStats {
        size_t totalScanned{0};
        size_t crouchCount{0};
        size_t preciseCount{0};
        size_t jumpCount{0};
        size_t totalModified{0};
    };
    AnalyzerStats AutoAnalyzeFlags(class CommandManager& cmdMgr, bool selectedOnly = false);

    // Mesh Optimization / Area Simplification (1-Click Coplanar Merge)
    struct OptimizeMeshStats {
        size_t initialAreaCount{0};
        size_t finalAreaCount{0};
        size_t mergedCount{0};
    };
    OptimizeMeshStats OptimizeMesh(class CommandManager& cmdMgr, bool selectedOnly = false);

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
    void ExtrudeSelectedEdge(class CommandManager& cmdMgr, float length = 0.0f, bool useCameraFacing = true);
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
    SkyboxRenderer m_skyboxRenderer;

    std::string m_bspPath;
    std::string m_navPath;
    std::string m_errorMessage;
    std::string m_gameDirectory;

    uint32_t m_selectedAreaId;
    uint32_t m_hoveredAreaId;
    int m_selectedEntityIndex{-1};
    SelectedConnection m_selectedConnection;
    bool m_connectionSelectMode{false};

    BSPRenderMode m_bspMode;
    bool m_showBSP;
    bool m_showNAV;
    bool m_showConnections;
    bool m_isModified{false};
    float m_autosaveTimer{0.0f};
    std::string m_lastAutosavePath;
    GizmoMode m_gizmoMode{GIZMO_MODE_COMBINED};

    // Recent files
    std::vector<std::string> m_recentFiles;

    // Grid Snapping
    float m_gridSize{32.0f};
    bool m_gridSnap{true};

    // Mesh-to-Mesh Snapping
    bool m_meshSnap{true};
    float m_meshSnapTolerance{16.0f};
    bool m_enableSnapToEdgeOnMove{true};
    bool m_autoConnectOnEdgeSnap{true};
    bool m_extrudeCameraFacing{true};
    Vector3 m_cameraForward{0.0f, -1.0f, 0.0f};

    // Bridge Tool
    bool m_isBridgeMode{false};
    uint32_t m_bridgeFirstAreaId{0};
    SelectedHandleType m_bridgeFirstEdge{HANDLE_NONE};
    uint32_t m_bridgeHoverAreaId{0};
    SelectedHandleType m_bridgeHoverEdge{HANDLE_NONE};

    // Draw Area Marquee Tool
    bool m_isDrawAreaMode{false};
    bool m_drawAreaActive{false};
    Vector3 m_drawAreaStart{0.0f, 0.0f, 0.0f};
    Vector3 m_drawAreaCurrent{0.0f, 0.0f, 0.0f};
    float m_drawAreaElevation{0.0f};
    float m_drawAreaNwZ{0.0f};
    float m_drawAreaNeZ{0.0f};
    float m_drawAreaSeZ{0.0f};
    float m_drawAreaSwZ{0.0f};
    bool m_drawAreaSnappedCorner{false};
    Vector3 m_drawAreaCornerPos{0.0f, 0.0f, 0.0f};
    float m_cornerSnapTolerance{8.0f};

    // Fill Area Tool
    bool m_isFillAreaMode{false};

    // Box Selection Tool
    bool m_isBoxSelectMode{false};

    // Multi-Selection
    std::vector<uint32_t> m_selectedAreaIds;
    struct MultiDragState {
        uint32_t areaId{0};
        NavExtent startExtent;
        float startNeZ{0.0f};
        float startSwZ{0.0f};
    };
    std::vector<MultiDragState> m_multiDragStates;

    // Split Area Knife Tool State
    bool m_isKnifeMode{false};
    bool m_knifeActive{false};
    uint32_t m_knifeHoverAreaId{0};
    bool m_knifeSplitAlongY{false};
    float m_knifeSplitCoord{0.0f};
    float m_knifeSplitCoordX{0.0f};
    float m_knifeSplitCoordY{0.0f};
    bool m_knifeForceAxis{false};
    bool m_knifeAxisOverride{false};
    KnifeCutAngle m_knifeAngle{KNIFE_ANGLE_0};

    // User Preferences
    EditorPreferences m_prefs;

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

    PathPreviewState m_pathPreview;
    bool m_showIslandColors{false};
    bool m_showClearanceHull{false};
    bool m_clearanceCrouch{false};

    // Diagnostics & Ladders state
    std::vector<DiagnosticIssue> m_diagnostics;
    uint32_t m_diagnosticHighlightAreaId{0};
    uint32_t m_selectedLadderId{0};

    // Full Map Generator thread state
    NavGenProgress m_navGenProgress;
    std::thread m_navGenThread;

    // Toast notifications
    std::vector<ToastNotification> m_toasts;

    // Waypoint state
    WaypointGraph m_waypoints;
    WaypointRenderer m_waypointRenderer;
    bool m_showWaypoints{true};
    uint32_t m_selectedWaypointId{0};

    void PostGenerateOptimize();
};

#endif // EDITOR_SCENE_H
