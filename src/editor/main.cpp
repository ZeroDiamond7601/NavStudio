#include "editor/glad/include/glad/glad.h"
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "editor/scene/editor_scene.h"
#include "editor/scene/scene_picker.h"
#include "editor/camera/camera.h"
#include "editor/commands/command.h"
#include "editor/commands/nav_commands.h"
#include "editor/ui/editor_ui.h"
#include "editor/render/shader.h"
#include "editor/render/editor_shaders.h"
#include "editor/res/app_icon_data.h"

#include <cstdio>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdlib>

static Camera g_camera;
static EditorScene* g_activeScene = nullptr;
static CommandManager* g_cmdMgr = nullptr;
static EditorUI* g_editorUI = nullptr;
static bool g_isRightMouseDown = false;
static bool g_isMiddleMouseDown = false;
static bool g_isAltDown = false;
static bool g_isAltOrbiting = false;
static bool g_isAltPanning = false;
static double g_lastMouseX = 0.0;
static double g_lastMouseY = 0.0;
static bool g_firstMouse = true;
static bool g_isBoxSelecting = false;
static double g_boxSelectStartX = 0.0;
static double g_boxSelectStartY = 0.0;
static double g_boxSelectCurrentX = 0.0;
static double g_boxSelectCurrentY = 0.0;

bool SaveScreenToBMP(const char* filename, int width, int height) {
    if (width <= 0 || height <= 0) {
        GLint vp[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, vp);
        width = vp[2];
        height = vp[3];
    }
    if (width <= 0 || height <= 0) return false;

#pragma pack(push, 1)
    struct BMPHeader {
        uint16_t bfType = 0x4D42;
        uint32_t bfSize = 0;
        uint16_t bfReserved1 = 0;
        uint16_t bfReserved2 = 0;
        uint32_t bfOffBits = 54;
        uint32_t biSize = 40;
        int32_t  biWidth = 0;
        int32_t  biHeight = 0;
        uint16_t biPlanes = 1;
        uint16_t biBitCount = 24;
        uint32_t biCompression = 0;
        uint32_t biSizeImage = 0;
        int32_t  biXPelsPerMeter = 2835;
        int32_t  biYPelsPerMeter = 2835;
        uint32_t biClrUsed = 0;
        uint32_t biClrImportant = 0;
    } header;
#pragma pack(pop)

    int rowStride = ((width * 3 + 3) / 4) * 4;
    header.biWidth = width;
    header.biHeight = height;
    header.biSizeImage = rowStride * height;
    header.bfSize = 54 + header.biSizeImage;

    std::vector<uint8_t> rgb(width * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());

    std::vector<uint8_t> bmpData(header.biSizeImage, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int srcIdx = (y * width + x) * 3;
            int dstIdx = y * rowStride + x * 3;
            bmpData[dstIdx + 0] = rgb[srcIdx + 2]; // B
            bmpData[dstIdx + 1] = rgb[srcIdx + 1]; // G
            bmpData[dstIdx + 2] = rgb[srcIdx + 0]; // R
        }
    }

    FILE* f = std::fopen(filename, "wb");
    if (!f) return false;
    std::fwrite(&header, 1, sizeof(header), f);
    std::fwrite(bmpData.data(), 1, bmpData.size(), f);
    std::fclose(f);
    std::printf("[Screenshot] Saved %dx%d screenshot to: %s\n", width, height, filename);
    return true;
}

bool SaveScreenToBMP(const char* filename) {
    return SaveScreenToBMP(filename, 0, 0);
}

bool SaveScreenToBMP(const std::string& filename, int width, int height) {
    return SaveScreenToBMP(filename.c_str(), width, height);
}

static void WindowCloseCallback(GLFWwindow* window) {
    if (g_activeScene && g_activeScene->HasNAV() &&
        (g_cmdMgr->HasUnsavedChanges() || g_activeScene->IsModified())) {
        glfwSetWindowShouldClose(window, GLFW_FALSE);
        if (g_editorUI) {
            g_editorUI->PromptQuit(*g_activeScene, *g_cmdMgr);
        }
    }
}

static void DropCallback(GLFWwindow* /*window*/, int count, const char** paths) {
    if (!g_activeScene || count <= 0 || !paths) return;

    std::string bspPath;
    std::string navPath;

    for (int i = 0; i < count; ++i) {
        if (!paths[i]) continue;
        std::string path = paths[i];
        std::string lowerPath = path;
        std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        if (lowerPath.length() >= 4 && lowerPath.compare(lowerPath.length() - 4, 4, ".bsp") == 0) {
            bspPath = path;
        } else if (lowerPath.length() >= 4 && lowerPath.compare(lowerPath.length() - 4, 4, ".nav") == 0) {
            navPath = path;
        } else if (lowerPath.length() >= 4 && lowerPath.compare(lowerPath.length() - 4, 4, ".wad") == 0) {
            std::printf("[DragDrop] Loading WAD texture archive: %s\n", path.c_str());
            g_activeScene->LoadWAD(path);
        }
    }

    if (!bspPath.empty()) {
        if (!g_editorUI || !g_editorUI->CheckUnsavedChanges(*g_activeScene, *g_cmdMgr, EditorUI::PENDING_OPEN_BSP, bspPath)) {
            g_cmdMgr->Clear();
            std::printf("[DragDrop] Loading BSP map: %s\n", bspPath.c_str());
            g_activeScene->StartAsyncLoad(bspPath, navPath);
        }
    } else if (!navPath.empty()) {
        if (!g_editorUI || !g_editorUI->CheckUnsavedChanges(*g_activeScene, *g_cmdMgr, EditorUI::PENDING_OPEN_NAV, navPath)) {
            g_cmdMgr->Clear();
            std::printf("[DragDrop] Loading NAV mesh: %s\n", navPath.c_str());
            g_activeScene->StartAsyncLoad(navPath);
        }
    }
}

static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            if (g_activeScene && g_activeScene->IsAddWaypointMode()) {
                g_activeScene->SetAddWaypointMode(false);
                return;
            }
            if (g_activeScene && g_activeScene->IsBridgeMode()) {
                g_activeScene->CancelBridgeMode();
                return;
            }
            if (g_activeScene && g_activeScene->IsFillAreaMode()) {
                g_activeScene->ExitFillAreaMode();
                return;
            }
            if (g_activeScene && g_activeScene->GetTransformMode() != EditorScene::TRANSFORM_NONE) {
                // Right-click cancels active modal transform
                g_activeScene->CancelTransform();
                return;
            }
            g_isRightMouseDown = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            g_firstMouse = true;
        } else if (action == GLFW_RELEASE) {
            g_isRightMouseDown = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            bool altPressed = (mods & GLFW_MOD_ALT) != 0 ||
                              glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                              glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
            g_isMiddleMouseDown = true;
            g_isAltPanning = altPressed;
            g_firstMouse = true;
        } else if (action == GLFW_RELEASE) {
            g_isMiddleMouseDown = false;
            g_isAltPanning = false;
        }
    } else if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (!g_activeScene || !g_cmdMgr) return;

        bool altPressed = (mods & GLFW_MOD_ALT) != 0 ||
                          glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                          glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;

        if (action == GLFW_PRESS) {
            int displayW = 0, displayH = 0;
            glfwGetFramebufferSize(window, &displayW, &displayH);
            float aspect = (displayH > 0) ? (static_cast<float>(displayW) / static_cast<float>(displayH)) : 1.0f;
            double mouseX, mouseY;
            glfwGetCursorPos(window, &mouseX, &mouseY);

            // Alt+Click: test connection picking first before orbiting
            if (altPressed && g_activeScene) {
                uint32_t fromId = 0, toId = 0;
                int dir = -1;
                bool hitConn = ScenePicker::PickConnection(
                    *g_activeScene,
                    static_cast<float>(mouseX), static_cast<float>(mouseY),
                    static_cast<float>(displayW), static_cast<float>(displayH),
                    g_camera.GetViewMatrix(), g_camera.GetProjectionMatrix(aspect),
                    fromId, toId, dir, 18.0f
                );
                if (hitConn) {
                    g_activeScene->SelectConnection(fromId, toId, dir);
                    return;
                }
                if (!g_activeScene->IsConnectionSelectionMode()) {
                    g_isAltOrbiting = true;
                    g_firstMouse = true;
                    NavArea* sel = g_activeScene->GetSelectedArea();
                    if (sel) {
                        g_camera.SetTarget(sel->GetCenter());
                    } else {
                        g_camera.SetTarget(g_camera.GetPosition() + g_camera.GetForward() * 400.0f);
                    }
                    return;
                }
            }

            Ray ray = ScenePicker::ScreenPointToRay(
                static_cast<float>(mouseX), static_cast<float>(mouseY),
                static_cast<float>(displayW), static_cast<float>(displayH),
                g_camera.GetViewMatrix(), g_camera.GetProjectionMatrix(aspect)
            );

            // Path Tool Left Click: Click 1st area for Start, click 2nd area for Goal
            if (g_activeScene->IsPathToolActive()) {
                g_activeScene->OnPathToolClick(ray);
                return;
            }

            // Draw Area Mode Left Click: Click 1st corner, then 2nd corner
            if (g_activeScene->IsDrawAreaMode()) {
                g_activeScene->OnDrawAreaClick(ray, *g_cmdMgr);
                return;
            }

            // Waypoint Quick Add Tool Left Click: Click floor to place waypoint
            if (g_activeScene->IsAddWaypointMode()) {
                uint32_t wid = g_activeScene->OnAddWaypointClick(ray);
                if (wid != 0) {
                    std::printf("[NavStudio] Added waypoint #%u\n", wid);
                }
                return;
            }

            // Fill Area / Waypoints Mode Left Click: Click floor to generate room NavMesh or Waypoints
            if (g_activeScene->IsFillAreaMode()) {
                if (g_activeScene->IsWaypointMode()) {
                    size_t created = g_activeScene->FloodFillWaypointsAt(ray);
                    if (created > 0) {
                        std::printf("[NavStudio] Flood-fill generated %zu waypoints\n", created);
                    }
                } else {
                    size_t created = g_activeScene->FloodFillAreaAt(ray, *g_cmdMgr);
                    if (created > 0) {
                        std::printf("[NavStudio] Flood-fill generated %zu areas\n", created);
                    }
                }
                return;
            }

            // Knife Mode Left Click: Click area to slice along knife guideline
            if (g_activeScene->IsKnifeMode()) {
                g_activeScene->OnKnifeClick(ray, *g_cmdMgr);
                return;
            }

            // Bridge Mode Left Click: Click Edge 1 then Edge 2
            if (g_activeScene->IsBridgeMode()) {
                uint32_t edgeArea = g_activeScene->GetBridgeHoverArea();
                SelectedHandleType edgeHandle = g_activeScene->GetBridgeHoverEdge();
                if (edgeArea != 0 && edgeHandle != HANDLE_NONE) {
                    g_activeScene->OnBridgeClick(edgeArea, edgeHandle, *g_cmdMgr);
                    return;
                }
            }

            // Box Selection Tool: Click & drag selects marquee rectangle
            if (g_activeScene->IsBoxSelectMode()) {
                g_isBoxSelecting = true;
                g_boxSelectStartX = mouseX;
                g_boxSelectStartY = mouseY;
                g_boxSelectCurrentX = mouseX;
                g_boxSelectCurrentY = mouseY;
                return;
            }

            auto mode = g_activeScene->GetTransformMode();
            if (mode == EditorScene::TRANSFORM_TRANSLATE || mode == EditorScene::TRANSFORM_SCALE) {
                // Left-click confirms active modal transform
                g_activeScene->ConfirmTransform(*g_cmdMgr);
            } else if (mode == EditorScene::TRANSFORM_CONNECT) {
                if (g_activeScene->GetSelectedWaypointID() != 0) {
                    float wptDist = 0.0f;
                    uint32_t hitWpt = ScenePicker::PickWaypoint(*g_activeScene, ray, &wptDist);
                    if (hitWpt != 0 && hitWpt != g_activeScene->GetSelectedWaypointID()) {
                        int conType = g_activeScene->GetWaypointConnectType();
                        uint16_t flags = WPT_CONN_NONE;
                        if (conType == 3) flags |= WPT_CONN_JUMP;
                        else if (conType == 4) flags |= WPT_CONN_CROUCH;
                        bool bidi = (conType == 2);
                        g_activeScene->ConnectSelectedWaypointTo(hitWpt, flags, bidi);
                    }
                } else {
                    uint32_t hitArea = ScenePicker::PickNavArea(*g_activeScene, ray);
                    if (hitArea != 0 && hitArea != g_activeScene->GetSelectedAreaID()) {
                        bool shiftPressed = (mods & GLFW_MOD_SHIFT) != 0;
                        g_activeScene->ConnectSelectedTo(hitArea, !shiftPressed, *g_cmdMgr);
                    }
                }
            } else {
                // Test area gizmo handles, edges, and corners first
                SelectedHandleType handle = ScenePicker::PickAreaHandles(
                    *g_activeScene,
                    static_cast<float>(mouseX), static_cast<float>(mouseY),
                    static_cast<float>(displayW), static_cast<float>(displayH),
                    g_camera.GetViewMatrix(), g_camera.GetProjectionMatrix(aspect)
                );

                if (handle != HANDLE_NONE) {
                    bool shiftPressed = (mods & GLFW_MOD_SHIFT) != 0;
                    if (shiftPressed && (handle >= HANDLE_EDGE_NORTH && handle <= HANDLE_EDGE_WEST)) {
                        // Hammer Shift+Drag Edge Extrude: create new adjacent area & drag it
                        g_activeScene->SetSelectedHandle(handle);
                        g_activeScene->ExtrudeSelectedEdge(*g_cmdMgr, 0.0f, false);
                        g_activeScene->StartDragHandle(handle, static_cast<float>(mouseX), static_cast<float>(mouseY), ray);
                    } else {
                        g_activeScene->StartDragHandle(handle, static_cast<float>(mouseX), static_cast<float>(mouseY), ray);
                    }
                    return;
                }

                // Test directional connections when visible or in connection selection mode
                bool connSelectMode = g_activeScene && (g_activeScene->IsConnectionSelectionMode() || g_activeScene->GetShowConnections());
                if (connSelectMode) {
                    uint32_t fromId = 0, toId = 0;
                    int dir = -1;
                    float pickTol = g_activeScene->IsConnectionSelectionMode() ? 18.0f : 12.0f;
                    bool hitConn = ScenePicker::PickConnection(
                        *g_activeScene,
                        static_cast<float>(mouseX), static_cast<float>(mouseY),
                        static_cast<float>(displayW), static_cast<float>(displayH),
                        g_camera.GetViewMatrix(), g_camera.GetProjectionMatrix(aspect),
                        fromId, toId, dir, pickTol
                    );
                    if (hitConn) {
                        g_activeScene->SelectConnection(fromId, toId, dir);
                        return;
                    }
                }

                // Pick ladder, entity, waypoint, and NavArea with accurate distance comparison
                float ladDist = std::numeric_limits<float>::max();
                uint32_t hitLadder = ScenePicker::PickLadder(*g_activeScene, ray, &ladDist);

                float entDist = std::numeric_limits<float>::max();
                int hitEntity = ScenePicker::PickEntity(*g_activeScene, ray, &entDist);

                float wptDist = std::numeric_limits<float>::max();
                uint32_t hitWpt = 0;
                if (g_activeScene->IsShowWaypoints()) {
                    hitWpt = ScenePicker::PickWaypoint(*g_activeScene, ray, &wptDist);
                }

                Vector3 navHit(0, 0, 0);
                uint32_t hitArea = ScenePicker::PickNavArea(*g_activeScene, ray, &navHit);
                float navDist = (hitArea != 0) ? (navHit - ray.origin).Length() : std::numeric_limits<float>::max();

                bool additive = (mods & GLFW_MOD_SHIFT) != 0;
                bool toggle = (mods & GLFW_MOD_CONTROL) != 0;

                // Priority: waypoint -> ladder -> area -> entity
                if (hitWpt != 0 && (wptDist <= navDist + 8.0f && wptDist <= ladDist + 8.0f && wptDist <= entDist + 8.0f)) {
                    g_activeScene->SelectWaypoint(hitWpt);
                    g_activeScene->SelectLadder(0);
                    g_activeScene->ClearSelection();
                    g_activeScene->SelectEntity(-1);
                    if (g_activeScene->HasSelectedConnection()) {
                        g_activeScene->ClearSelectedConnection();
                    }
                } else if (hitLadder != 0 && (ladDist <= navDist + 8.0f && ladDist <= entDist + 8.0f)) {
                    g_activeScene->SelectWaypoint(0);
                    g_activeScene->SelectLadder(hitLadder);
                    g_activeScene->ClearSelection();
                    g_activeScene->SelectEntity(-1);
                    if (g_activeScene->HasSelectedConnection()) {
                        g_activeScene->ClearSelectedConnection();
                    }
                } else if (hitArea != 0 && (hitEntity < 0 || navDist <= entDist + 16.0f)) {
                    g_activeScene->SelectWaypoint(0);
                    g_activeScene->SelectLadder(0);
                    g_activeScene->SelectArea(hitArea, additive, toggle);
                    g_activeScene->SelectEntity(-1);
                    if (g_activeScene->HasSelectedConnection() && !g_activeScene->IsConnectionSelectionMode()) {
                        g_activeScene->ClearSelectedConnection();
                    }
                } else if (hitEntity >= 0) {
                    g_activeScene->SelectWaypoint(0);
                    g_activeScene->SelectLadder(0);
                    g_activeScene->SelectEntity(hitEntity);
                    g_activeScene->ClearSelection();
                    if (g_activeScene->HasSelectedConnection()) {
                        g_activeScene->ClearSelectedConnection();
                    }
                } else {
                    if (!additive && !toggle) {
                        g_activeScene->SelectWaypoint(0);
                        g_activeScene->SelectLadder(0);
                        g_activeScene->ClearSelection();
                        g_activeScene->ClearSelectedConnection();
                        g_activeScene->SelectEntity(-1);
                    }
                    // Clicking in empty space begins marquee box selection
                    g_isBoxSelecting = true;
                    g_boxSelectStartX = mouseX;
                    g_boxSelectStartY = mouseY;
                    g_boxSelectCurrentX = mouseX;
                    g_boxSelectCurrentY = mouseY;
                }
            }
        } else if (action == GLFW_RELEASE) {
            if (g_isAltOrbiting) {
                g_isAltOrbiting = false;
            }
            if (g_activeScene->IsDraggingHandle()) {
                g_activeScene->EndDragHandle(*g_cmdMgr);
            }
            if (g_isBoxSelecting) {
                g_isBoxSelecting = false;
                float dx = static_cast<float>(std::abs(g_boxSelectCurrentX - g_boxSelectStartX));
                float dy = static_cast<float>(std::abs(g_boxSelectCurrentY - g_boxSelectStartY));
                if (dx > 4.0f || dy > 4.0f) {
                    int displayW = 0, displayH = 0;
                    glfwGetFramebufferSize(window, &displayW, &displayH);
                    float aspect = (displayH > 0) ? (static_cast<float>(displayW) / static_cast<float>(displayH)) : 1.0f;
                    auto picked = ScenePicker::PickAreasInRect(
                        *g_activeScene,
                        static_cast<float>(g_boxSelectStartX), static_cast<float>(g_boxSelectStartY),
                        static_cast<float>(g_boxSelectCurrentX), static_cast<float>(g_boxSelectCurrentY),
                        static_cast<float>(displayW), static_cast<float>(displayH),
                        g_camera.GetViewMatrix(), g_camera.GetProjectionMatrix(aspect)
                    );
                    bool additive = (mods & GLFW_MOD_SHIFT) != 0;
                    bool subtractive = (mods & GLFW_MOD_ALT) != 0;
                    g_activeScene->BoxSelectAreas(picked, additive, subtractive);
                }
            }
        }
    }

    g_isAltDown = (mods & GLFW_MOD_ALT) != 0;
}

static void KeyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    if (!g_activeScene || !g_cmdMgr) return;

    // While holding right-click (mouse2), the user is controlling camera navigation (WASD, Q, E).
    // Editor hotkeys (such as S for scaling or G for grab) must not trigger during camera movement.
    if (g_isRightMouseDown || glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        return;
    }

    // When in drawing mode, knife mode, or add waypoint mode, WASD keys navigate the camera
    if (g_activeScene && (g_activeScene->IsDrawAreaMode() || g_activeScene->IsKnifeMode() || g_activeScene->IsAddWaypointMode())) {
        if (key == GLFW_KEY_W || key == GLFW_KEY_A || key == GLFW_KEY_S || key == GLFW_KEY_D) {
            return;
        }
    }

    if (action == GLFW_PRESS) {
        bool ctrlDown = (mods & GLFW_MOD_CONTROL) != 0 ||
                        glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                        glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;

        auto mode = g_activeScene->GetTransformMode();

        if (mode == EditorScene::TRANSFORM_TRANSLATE || mode == EditorScene::TRANSFORM_SCALE) {
            if (key == GLFW_KEY_X) {
                g_activeScene->ToggleTransformAxis(EditorScene::AXIS_X);
            } else if (key == GLFW_KEY_Y) {
                g_activeScene->ToggleTransformAxis(EditorScene::AXIS_Y);
            } else if (key == GLFW_KEY_Z && mode == EditorScene::TRANSFORM_TRANSLATE) {
                g_activeScene->ToggleTransformAxis(EditorScene::AXIS_Z);
            } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
                g_activeScene->ConfirmTransform(*g_cmdMgr);
            } else if (key == GLFW_KEY_ESCAPE) {
                g_activeScene->CancelTransform();
            }
        } else if (mode == EditorScene::TRANSFORM_CONNECT) {
            if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_C) {
                g_activeScene->CancelTransform();
            }
        } else {
            // Grid Snap Shortcuts
            if (key == GLFW_KEY_LEFT_BRACKET) { // '[': Decrease grid
                g_activeScene->DecreaseGridSize();
            } else if (key == GLFW_KEY_RIGHT_BRACKET) { // ']': Increase grid
                g_activeScene->IncreaseGridSize();
            } else if (key == GLFW_KEY_W && (mods & GLFW_MOD_SHIFT) != 0) { // Shift+W: Toggle snap
                g_activeScene->ToggleGridSnap();
            } else if (key == GLFW_KEY_F3) { // F3: Toggle Entities
                auto& entR = g_activeScene->GetEntityRenderer();
                entR.SetShowEntities(!entR.GetShowEntities());
            } else if (key == GLFW_KEY_F4) { // F4: Cycle Shading Mode
                auto cur = g_activeScene->GetBSPMode();
                int next = (static_cast<int>(cur) + 1) % 4;
                g_activeScene->SetBSPMode(static_cast<BSPRenderMode>(next));
            } else if (key == GLFW_KEY_F6) { // F6: Toggle Bot Waypoint System
                g_activeScene->ToggleShowWaypoints();
            } else if (key == GLFW_KEY_1 && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) == 0) { // 1: Move Gizmo
                g_activeScene->SetGizmoMode(GIZMO_MODE_TRANSLATE);
            } else if (key == GLFW_KEY_2 && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) == 0) { // 2: Rotate Gizmo
                g_activeScene->SetGizmoMode(GIZMO_MODE_ROTATE);
            } else if (key == GLFW_KEY_3 && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) == 0) { // 3: Scale Gizmo
                g_activeScene->SetGizmoMode(GIZMO_MODE_SCALE);
            } else if (key == GLFW_KEY_4 && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) == 0) { // 4: All / Combined Gizmo
                g_activeScene->SetGizmoMode(GIZMO_MODE_COMBINED);
            } else if (key == GLFW_KEY_B && (mods & GLFW_MOD_SHIFT) != 0) { // Shift+B: Toggle Marquee Box Select
                g_activeScene->ToggleBoxSelectMode();
            } else if (key == GLFW_KEY_B && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) == 0) { // B: Bridge Tool
                g_activeScene->ToggleBridgeMode();
            } else if (key == GLFW_KEY_N && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) == 0) { // N: Draw Area Tool or Add Waypoint Tool
                if (g_activeScene->IsWaypointMode()) {
                    g_activeScene->ToggleAddWaypointMode();
                } else {
                    g_activeScene->ToggleDrawAreaMode();
                }
            } else if (key == GLFW_KEY_K && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) == 0) { // K: Knife / Split Tool
                g_activeScene->ToggleKnifeMode();
            } else if (key == GLFW_KEY_R && action == GLFW_PRESS && g_activeScene->IsKnifeMode()) { // R: Cycle knife angle
                g_activeScene->CycleKnifeAngle();
            } else if (key == GLFW_KEY_F && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) == 0 &&
                       g_activeScene->GetSelectedAreaID() == 0 && g_activeScene->GetSelectedAreaIDs().empty() &&
                       g_activeScene->GetSelectedEntityIndex() < 0 && g_activeScene->GetSelectedWaypointID() == 0) { // F: Fill Tool (when nothing selected)
                g_activeScene->ToggleFillAreaMode();
            } else if (key == GLFW_KEY_C && (mods & GLFW_MOD_ALT) != 0) { // Alt+C: Toggle Connection Selection Mode
                g_activeScene->ToggleConnectionSelectionMode();
            } else if (key == GLFW_KEY_C && ctrlDown && (mods & GLFW_MOD_SHIFT) == 0) { // Ctrl+C: Copy areas
                g_activeScene->CopySelectedAreas();
            } else if (key == GLFW_KEY_V && ctrlDown && (mods & GLFW_MOD_SHIFT) == 0) { // Ctrl+V: Paste areas
                double mouseX, mouseY;
                glfwGetCursorPos(window, &mouseX, &mouseY);
                int displayW = 0, displayH = 0;
                glfwGetFramebufferSize(window, &displayW, &displayH);
                float aspect = (displayH > 0) ? (static_cast<float>(displayW) / static_cast<float>(displayH)) : 1.0f;
                Ray ray = ScenePicker::ScreenPointToRay(
                    static_cast<float>(mouseX), static_cast<float>(mouseY),
                    static_cast<float>(displayW), static_cast<float>(displayH),
                    g_camera.GetViewMatrix(), g_camera.GetProjectionMatrix(aspect)
                );
                Vector3 hit;
                bool hasHit = false;
                if (g_activeScene->HasBSP()) {
                    hasHit = ScenePicker::PickBSPFloor(*g_activeScene, ray, &hit);
                }
                g_activeScene->PasteAreas(hasHit ? &hit : nullptr, *g_cmdMgr);
            } else if (key == GLFW_KEY_V && ctrlDown && (mods & GLFW_MOD_SHIFT) != 0) { // Ctrl+Shift+V: Toggle validity overlay
                auto& navR = g_activeScene->GetNavRenderer();
                navR.SetShowConnectionValidity(!navR.GetShowConnectionValidity());
                g_activeScene->RebuildNavRenderer();
            } else if (key == GLFW_KEY_P && ctrlDown && (mods & GLFW_MOD_ALT) == 0) { // Ctrl+P: Command Palette
                if (g_editorUI) g_editorUI->ToggleCommandPalette();
            } else if (key == GLFW_KEY_H && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) == 0) { // H: Clearance Hull Visualizer
                g_activeScene->ToggleClearanceHull();
            } else if (key == GLFW_KEY_P && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) == 0 &&
                       g_activeScene->GetSelectedAreaID() == 0 && g_activeScene->GetSelectedAreaIDs().empty() &&
                       g_activeScene->GetSelectedEntityIndex() < 0) { // P: Path Simulation Tool
                g_activeScene->TogglePathTool();
                if (g_editorUI) g_editorUI->TogglePathPanel();
            } else if (key == GLFW_KEY_COMMA && ctrlDown) { // Ctrl+,: Preferences
                if (g_editorUI) g_editorUI->OpenPreferences();
            } else if (key == GLFW_KEY_F && ctrlDown) { // Ctrl+F: Find Area Modal
                if (g_editorUI) g_editorUI->OpenFindModal();
            } else if (key == GLFW_KEY_R && ctrlDown && (mods & GLFW_MOD_SHIFT) == 0) { // Ctrl+R: Reload Map & NAV
                if (g_activeScene) g_activeScene->ReloadCurrentMap();
            } else if (key == GLFW_KEY_L && ctrlDown) { // Ctrl+L: Landmarks / Spawns
                if (g_editorUI) g_editorUI->OpenLandmarksModal();
            } else if (key == GLFW_KEY_L && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) == 0 &&
                       g_activeScene->GetSelectedAreaID() == 0 && g_activeScene->GetSelectedAreaIDs().empty() &&
                       g_activeScene->GetSelectedEntityIndex() < 0) { // L: Create Ladder Modal
                if (g_editorUI) g_editorUI->OpenLadderCreateModal();
            } else if (key == GLFW_KEY_A && ctrlDown) { // Ctrl+A: Select All
                g_activeScene->SelectAllAreas();
            } else if (key == GLFW_KEY_F12 && action == GLFW_PRESS) { // F12: Screenshot
                int displayW = 0, displayH = 0;
                glfwGetFramebufferSize(window, &displayW, &displayH);
                std::time_t now = std::time(nullptr);
                char buf[64];
                std::strftime(buf, sizeof(buf), "navstudio_%Y%m%d_%H%M%S.bmp", std::localtime(&now));
                if (SaveScreenToBMP(buf, displayW, displayH)) {
                    if (g_activeScene) {
                        g_activeScene->ShowToast(std::string("Screenshot saved: ") + buf);
                    }
                }
            } else if (key == GLFW_KEY_ESCAPE) {
                if (g_activeScene->IsBoxSelectMode()) {
                    g_activeScene->SetBoxSelectMode(false);
                } else if (g_activeScene->IsAddWaypointMode()) {
                    g_activeScene->SetAddWaypointMode(false);
                } else if (g_activeScene->IsKnifeMode()) {
                    g_activeScene->ExitKnifeMode();
                } else if (g_activeScene->IsDrawAreaMode()) {
                    g_activeScene->CancelDrawArea();
                } else if (g_activeScene->IsFillAreaMode()) {
                    g_activeScene->ExitFillAreaMode();
                } else if (g_activeScene->IsBridgeMode()) {
                    g_activeScene->CancelBridgeMode();
                } else if (g_activeScene->IsPathToolActive()) {
                    g_activeScene->SetPathToolActive(false);
                } else if (g_activeScene->HasSelectedConnection()) {
                    g_activeScene->ClearSelectedConnection();
                } else if (g_activeScene->GetSelectedLadderID() != 0) {
                    g_activeScene->SelectLadder(0);
                } else if (g_activeScene->GetSelectedWaypointID() != 0) {
                    g_activeScene->SelectWaypoint(0);
                } else if (!g_activeScene->GetSelectedAreaIDs().empty() || g_activeScene->GetSelectedAreaID() != 0) {
                    g_activeScene->ClearSelection();
                } else if (g_activeScene->GetSelectedEntityIndex() >= 0) {
                    g_activeScene->SelectEntity(-1);
                }
            }

            // Normal Selection Mode Hotkeys (Connection & Ladder selection take priority over area selection)
            if (key != GLFW_KEY_ESCAPE) {
                if (g_activeScene->HasSelectedConnection()) {
                    if (key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE || ((key == GLFW_KEY_X) && (mods & GLFW_MOD_SHIFT) == 0)) {
                        g_activeScene->DeleteSelectedConnection(*g_cmdMgr);
                    } else if (key == GLFW_KEY_R && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) == 0) {
                        g_activeScene->ReverseSelectedConnection(*g_cmdMgr);
                    } else if ((key == GLFW_KEY_2 || key == GLFW_KEY_T) && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) == 0) {
                        g_activeScene->ToggleSelectedConnectionBidirectional(*g_cmdMgr);
                    } else if (key == GLFW_KEY_F) {
                        const auto& sc = g_activeScene->GetSelectedConnection();
                        const NavArea* a1 = g_activeScene->GetNAV().GetAreaByID(sc.fromId);
                        const NavArea* a2 = g_activeScene->GetNAV().GetAreaByID(sc.toId);
                        if (a1 && a2) {
                            Vector3 mid = (a1->GetCenter() + a2->GetCenter()) * 0.5f;
                            g_camera.FocusOn(mid);
                        }
                    }
                } else if (g_activeScene->GetSelectedLadderID() != 0) {
                    if (key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE || ((key == GLFW_KEY_X) && (mods & GLFW_MOD_SHIFT) == 0)) {
                        g_activeScene->DeleteSelectedLadder();
                    } else if (key == GLFW_KEY_F) {
                        NavLadder* lad = g_activeScene->GetSelectedLadder();
                        if (lad) {
                            Vector3 mid = (lad->top + lad->bottom) * 0.5f;
                            g_camera.FocusOn(mid);
                        }
                    }
                } else if (g_activeScene->GetSelectedAreaID() != 0 || !g_activeScene->GetSelectedAreaIDs().empty()) {
                    NavArea* sel = g_activeScene->GetSelectedArea();

                    int displayW = 0, displayH = 0;
                    glfwGetFramebufferSize(window, &displayW, &displayH);
                    float aspect = (displayH > 0) ? (static_cast<float>(displayW) / static_cast<float>(displayH)) : 1.0f;
                    double mouseX, mouseY;
                    glfwGetCursorPos(window, &mouseX, &mouseY);

                    Ray ray = ScenePicker::ScreenPointToRay(
                        static_cast<float>(mouseX), static_cast<float>(mouseY),
                        static_cast<float>(displayW), static_cast<float>(displayH),
                        g_camera.GetViewMatrix(), g_camera.GetProjectionMatrix(aspect)
                    );

                    if (key == GLFW_KEY_G) {
                        if (sel) g_activeScene->StartGrabWithRay(ray);
                    } else if (key == GLFW_KEY_S && (mods & GLFW_MOD_SHIFT) != 0) { // Shift+S: Snap to Neighbors
                        if (g_activeScene->GetSelectedAreaIDs().size() > 1) {
                            g_activeScene->BatchSnapToNeighbors(*g_cmdMgr);
                        } else if (sel) {
                            g_activeScene->SnapSelectedAreaToNeighbors(*g_cmdMgr);
                        }
                    } else if (key == GLFW_KEY_S && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT)) == 0) {
                        if (sel) {
                            Matrix4 viewProj = g_camera.GetProjectionMatrix(aspect) * g_camera.GetViewMatrix();
                            g_activeScene->StartScaleWithScreen(
                                static_cast<float>(mouseX), static_cast<float>(mouseY),
                                static_cast<float>(displayW), static_cast<float>(displayH),
                                viewProj
                            );
                        }
                    } else if (key == GLFW_KEY_E && (mods & GLFW_MOD_CONTROL) == 0) { // Hammer Edge Extrude
                        if (g_activeScene->GetSelectedAreaIDs().size() > 1) {
                            g_activeScene->BatchExtrude(*g_cmdMgr);
                        } else if (sel) {
                            g_activeScene->ExtrudeSelectedEdge(*g_cmdMgr);
                        }
                    } else if (key == GLFW_KEY_X && (mods & GLFW_MOD_SHIFT) != 0) { // Hammer Shift+X Split Area
                        if (sel) g_activeScene->SplitSelectedArea(*g_cmdMgr);
                    } else if (key == GLFW_KEY_M && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) == 0) { // Quick Merge [M]
                        g_activeScene->MergeSelectedArea(*g_cmdMgr);
                    } else if (key == GLFW_KEY_R) {
                        if (sel) g_activeScene->RotateSelectedArea90(*g_cmdMgr);
                    } else if (key == GLFW_KEY_C) {
                        if (sel) g_activeScene->StartConnectMode();
                    } else if (key == GLFW_KEY_D && (mods & GLFW_MOD_SHIFT) != 0) {
                        if (g_activeScene->GetSelectedAreaIDs().size() > 1) {
                            g_activeScene->BatchDuplicate(*g_cmdMgr);
                        } else if (sel) {
                            g_activeScene->DuplicateSelectedArea(*g_cmdMgr);
                        }
                    } else if ((key == GLFW_KEY_X && (mods & GLFW_MOD_SHIFT) == 0) || key == GLFW_KEY_DELETE) {
                        if (g_activeScene->GetSelectedAreaIDs().size() > 1) {
                            g_activeScene->BatchDelete(*g_cmdMgr);
                        } else if (sel) {
                            g_activeScene->DeleteSelectedArea(*g_cmdMgr);
                        }
                    } else if (key == GLFW_KEY_F) {
                        if (sel) g_camera.FocusOn(sel->GetCenter());
                    } else if (key == GLFW_KEY_SPACE) {
                        if (g_activeScene->GetSelectedAreaIDs().size() > 1) {
                            g_activeScene->BatchSnapToFloor(*g_cmdMgr);
                        } else if (g_activeScene->HasBSP() && sel) {
                            g_cmdMgr->ExecuteCommand(std::make_unique<CmdSnapAreaToFloor>(g_activeScene, sel->GetID()));
                        }
                    }
                } else if (g_activeScene->GetSelectedWaypointID() != 0) {
                    uint32_t selWpt = g_activeScene->GetSelectedWaypointID();
                    WaypointNode* node = g_activeScene->GetWaypoints().GetNode(selWpt);
                    if (node) {
                        if (key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE || ((key == GLFW_KEY_X) && (mods & GLFW_MOD_SHIFT) == 0)) {
                            g_activeScene->DeleteSelectedWaypoint();
                        } else if (key == GLFW_KEY_F) {
                            g_camera.FocusOn(node->origin);
                        } else if (key == GLFW_KEY_SPACE) {
                            g_activeScene->SnapSelectedWaypointToFloor();
                        } else if (key == GLFW_KEY_C && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) == 0) {
                            g_activeScene->StartConnectMode();
                        }
                    }
                } else if (g_activeScene->GetSelectedEntityIndex() >= 0) {
                    if (key == GLFW_KEY_F) {
                        const EditorEntity* ent = g_activeScene->GetSelectedEntity();
                        if (ent) g_camera.FocusOn(ent->origin);
                    }
                }
            }

            // DCC View Presets on Numpad (Hammer / Blender style)
            if ((mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SHIFT)) == 0) {
                if (key == GLFW_KEY_KP_7) {
                    g_camera.SnapToPreset(0);
                    g_camera.SetMode(CAMERA_MODE_TOPDOWN_2D);
                    if (g_activeScene) g_activeScene->ShowToast("Camera: Top-Down (2D Ortho)");
                } else if (key == GLFW_KEY_KP_1) {
                    g_camera.SnapToPreset(1);
                    g_camera.SetMode(CAMERA_MODE_FPS);
                    if (g_activeScene) g_activeScene->ShowToast("Camera: Front View");
                } else if (key == GLFW_KEY_KP_3) {
                    g_camera.SnapToPreset(2);
                    g_camera.SetMode(CAMERA_MODE_FPS);
                    if (g_activeScene) g_activeScene->ShowToast("Camera: Side View");
                } else if (key == GLFW_KEY_KP_5) {
                    if (g_camera.GetMode() == CAMERA_MODE_TOPDOWN_2D) {
                        g_camera.SetMode(CAMERA_MODE_FPS);
                        if (g_activeScene) g_activeScene->ShowToast("Camera: Perspective (3D)");
                    } else {
                        g_camera.SetMode(CAMERA_MODE_TOPDOWN_2D);
                        if (g_activeScene) g_activeScene->ShowToast("Camera: Orthographic (2D)");
                    }
                } else if (key == GLFW_KEY_KP_0) {
                    g_camera.SnapToPreset(3);
                    g_camera.SetMode(CAMERA_MODE_FPS);
                    if (g_activeScene) g_activeScene->ShowToast("Camera: Perspective 3D");
                }
            }

            // Camera Bookmarks: Ctrl+0..9 to Save, Alt+0..9 or Alt+Numpad 0..9 to Recall
            for (int slot = 0; slot <= 9; ++slot) {
                int keyNum = (slot == 0) ? GLFW_KEY_0 : (GLFW_KEY_0 + slot);
                int keyKp = (slot == 0) ? GLFW_KEY_KP_0 : (GLFW_KEY_KP_0 + slot);
                if (key == keyNum || (key == keyKp && (mods & GLFW_MOD_ALT) != 0)) {
                    if ((mods & GLFW_MOD_CONTROL) != 0 && (mods & (GLFW_MOD_ALT | GLFW_MOD_SHIFT)) == 0) {
                        g_camera.SaveBookmark(slot);
                        std::printf("[NavStudio] Saved camera bookmark slot %d @ (%.0f, %.0f, %.0f)\n",
                            slot, g_camera.GetPosition().x, g_camera.GetPosition().y, g_camera.GetPosition().z);
                    } else if (((mods & GLFW_MOD_ALT) != 0) && (mods & GLFW_MOD_CONTROL) == 0) {
                        if (g_camera.RecallBookmark(slot)) {
                            std::printf("[NavStudio] Recalled camera bookmark slot %d\n", slot);
                        }
                    }
                }
            }

            // Global Undo / Redo
            if ((mods & GLFW_MOD_CONTROL) != 0) {
                if (key == GLFW_KEY_Z) {
                    if ((mods & GLFW_MOD_SHIFT) != 0) {
                        g_cmdMgr->Redo();
                    } else {
                        g_cmdMgr->Undo();
                    }
                } else if (key == GLFW_KEY_Y) {
                    g_cmdMgr->Redo();
                }
            }
        }
    }
}

static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    float xoffset = static_cast<float>(xpos - g_lastMouseX);
    float yoffset = static_cast<float>(g_lastMouseY - ypos); // Invert Y

    if (g_isRightMouseDown) {
        if (g_firstMouse) {
            g_firstMouse = false;
        } else {
            g_camera.ProcessMouseMovement(xoffset, yoffset);
        }
    } else if (g_isAltOrbiting) {
        if (g_firstMouse) {
            g_firstMouse = false;
        } else {
            g_camera.Orbit(xoffset, yoffset);
        }
    } else if (g_isMiddleMouseDown || g_isAltPanning) {
        if (g_firstMouse) {
            g_firstMouse = false;
        } else {
            g_camera.Pan(xoffset, yoffset);
        }
    } else if (g_activeScene) {
        int displayW = 0, displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        float aspect = (displayH > 0) ? (static_cast<float>(displayW) / static_cast<float>(displayH)) : 1.0f;

        Ray ray = ScenePicker::ScreenPointToRay(
            static_cast<float>(xpos), static_cast<float>(ypos),
            static_cast<float>(displayW), static_cast<float>(displayH),
            g_camera.GetViewMatrix(), g_camera.GetProjectionMatrix(aspect)
        );

        if (g_isBoxSelecting) {
            g_boxSelectCurrentX = xpos;
            g_boxSelectCurrentY = ypos;
        }

        if (g_activeScene->IsDraggingHandle()) {
            g_activeScene->UpdateDragHandle(
                static_cast<float>(xpos), static_cast<float>(ypos),
                ray, static_cast<float>(ypos - g_lastMouseY)
            );
        } else if (g_activeScene->IsKnifeMode()) {
            g_activeScene->UpdateKnife(ray);
        } else if (g_activeScene->IsDrawAreaMode()) {
            g_activeScene->UpdateDrawArea(ray);
        } else if (g_activeScene->IsBridgeMode()) {
            uint32_t edgeAreaId = 0;
            SelectedHandleType edgeHandle = HANDLE_NONE;
            ScenePicker::PickAnyAreaEdge(
                *g_activeScene,
                static_cast<float>(xpos), static_cast<float>(ypos),
                static_cast<float>(displayW), static_cast<float>(displayH),
                g_camera.GetViewMatrix(), g_camera.GetProjectionMatrix(aspect),
                edgeAreaId, edgeHandle, 20.0f
            );
            g_activeScene->SetBridgeHoverEdge(edgeAreaId, edgeHandle);
        } else {
            auto mode = g_activeScene->GetTransformMode();
            if (mode == EditorScene::TRANSFORM_TRANSLATE || mode == EditorScene::TRANSFORM_SCALE) {
                g_activeScene->UpdateTransformWithRay(
                    ray, static_cast<float>(xpos), static_cast<float>(ypos),
                    static_cast<float>(ypos - g_lastMouseY)
                );
            } else if (mode == EditorScene::TRANSFORM_CONNECT) {
                uint32_t hoverArea = ScenePicker::PickNavArea(*g_activeScene, ray);
                g_activeScene->SetConnectHoverArea(hoverArea);
            } else {
                ImGuiIO& io = ImGui::GetIO();
                if (!io.WantCaptureMouse) {
                    SelectedHandleType hoverHandle = ScenePicker::PickAreaHandles(
                        *g_activeScene,
                        static_cast<float>(xpos), static_cast<float>(ypos),
                        static_cast<float>(displayW), static_cast<float>(displayH),
                        g_camera.GetViewMatrix(), g_camera.GetProjectionMatrix(aspect)
                    );
                    g_activeScene->SetHoveredHandle(hoverHandle);
                    if (hoverHandle == HANDLE_NONE) {
                        uint32_t hoverArea = ScenePicker::PickNavArea(*g_activeScene, ray);
                        g_activeScene->SetHoveredArea(hoverArea);
                    } else {
                        g_activeScene->SetHoveredArea(0);
                    }
                }
            }
        }
    }

    g_lastMouseX = xpos;
    g_lastMouseY = ypos;
}

static void ScrollCallback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    g_camera.ProcessMouseScroll(static_cast<float>(yoffset));
}

static void ProcessInput(GLFWwindow* window, float deltaTime) {
    if (g_isRightMouseDown || g_camera.GetMode() == CAMERA_MODE_TOPDOWN_2D || (g_activeScene && g_activeScene->IsDrawAreaMode())) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) g_camera.ProcessKeyboard(0, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) g_camera.ProcessKeyboard(1, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) g_camera.ProcessKeyboard(2, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) g_camera.ProcessKeyboard(3, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) g_camera.ProcessKeyboard(4, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) g_camera.ProcessKeyboard(5, deltaTime);
    }
}

static void UpdateAppTitle(GLFWwindow* window, const EditorScene& scene) {
    static std::string lastTitle = "";
    std::string title = "NavStudio v1.6.3";
    if (scene.HasBSP() || scene.HasNAV()) {
        std::string map = "";
        if (scene.HasBSP()) {
            map = scene.GetBSP().GetMapName();
        } else if (!scene.GetNAVPath().empty()) {
            size_t slash = scene.GetNAVPath().find_last_of("/\\");
            map = (slash != std::string::npos) ? scene.GetNAVPath().substr(slash + 1) : scene.GetNAVPath();
        }
        if (!map.empty()) {
            title += " - " + map;
        }
        if (scene.IsModified()) {
            title += " *";
        }
    }
    if (title != lastTitle) {
        glfwSetWindowTitle(window, title.c_str());
        lastTitle = title;
    }
}

int main(int argc, char* argv[]) {
    std::printf("====================================================\n");
    std::printf("  NavStudio v1.6.3\n");
    std::printf("====================================================\n");

    if (!glfwInit()) {
        std::fprintf(stderr, "[Error] Failed to initialize GLFW\n");
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    int initialWidth = 1440;
    int initialHeight = 900;
    std::string argPath = "";
    std::string screenshotPath = "";
    int screenshotWaitFrames = 45;
    int customShading = -1;
    int customEntities = -1;
    uint32_t customSelectArea = 0;
    bool hasCustomCamPos = false;
    Vector3 customCamPos{0.0f, 0.0f, 0.0f};
    bool hasCustomCamAngles = false;
    float customPitch = -20.0f;
    float customYaw = 90.0f;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--screenshot" && i + 1 < argc) {
            screenshotPath = argv[++i];
        } else if (arg == "--frames" && i + 1 < argc) {
            screenshotWaitFrames = std::atoi(argv[++i]);
        } else if (arg == "--shading" && i + 1 < argc) {
            customShading = std::atoi(argv[++i]);
        } else if (arg == "--entities" && i + 1 < argc) {
            customEntities = std::atoi(argv[++i]);
        } else if (arg == "--select-area" && i + 1 < argc) {
            customSelectArea = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--cam-pos" && i + 3 < argc) {
            customCamPos.x = static_cast<float>(std::atof(argv[++i]));
            customCamPos.y = static_cast<float>(std::atof(argv[++i]));
            customCamPos.z = static_cast<float>(std::atof(argv[++i]));
            hasCustomCamPos = true;
        } else if (arg == "--cam-angles" && i + 2 < argc) {
            customPitch = static_cast<float>(std::atof(argv[++i]));
            customYaw = static_cast<float>(std::atof(argv[++i]));
            hasCustomCamAngles = true;
        } else if (arg == "--width" && i + 1 < argc) {
            initialWidth = std::atoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            initialHeight = std::atoi(argv[++i]);
        } else if (argPath.empty() && !arg.empty() && arg[0] != '-') {
            argPath = arg;
        }
    }

    if (!screenshotPath.empty() && initialWidth == 1440 && initialHeight == 900) {
        initialWidth = 1920;
        initialHeight = 1080;
    }

    GLFWwindow* window = glfwCreateWindow(initialWidth, initialHeight, "NavStudio v1.6.3", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[Error] Failed to create GLFW window\n");
        glfwTerminate();
        return -1;
    }

    // Set application window & taskbar icon
    GLFWimage iconImage;
    iconImage.width = APP_ICON_WIDTH;
    iconImage.height = APP_ICON_HEIGHT;
    iconImage.pixels = const_cast<unsigned char*>(APP_ICON_PIXELS);
    glfwSetWindowIcon(window, 1, &iconImage);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable VSync

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::fprintf(stderr, "[Error] Failed to initialize GLAD (OpenGL 3.3)\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    std::printf("[OpenGL] Renderer: %s\n", glGetString(GL_RENDERER));
    std::printf("[OpenGL] Version:  %s\n", glGetString(GL_VERSION));

    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, CursorPosCallback);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetDropCallback(window, DropCallback);

    // Setup Dear ImGui context with docking
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // Initialize Shaders
    Shader meshShader;
    if (!meshShader.LoadFromSource(kMeshVertexShader, kMeshFragmentShader)) {
        std::fprintf(stderr, "[Error] Failed to build mesh shader\n");
    }

    Shader lineShader;
    if (!lineShader.LoadFromSource(kLineVertexShader, kLineFragmentShader)) {
        std::fprintf(stderr, "[Error] Failed to build line shader\n");
    }

    EditorScene scene;
    g_activeScene = &scene;
    CommandManager cmdMgr;
    g_cmdMgr = &cmdMgr;
    EditorUI editorUI;
    editorUI.Init();
    editorUI.ApplyPreferencesToRuntime(scene, g_camera, cmdMgr);
    g_editorUI = &editorUI;
    glfwSetWindowCloseCallback(window, WindowCloseCallback);

    // Load initial map if passed via arguments
    if (!argPath.empty()) {
        scene.StartAsyncLoad(argPath);
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    double lastTime = glfwGetTime();

    // Main application loop
    while (!glfwWindowShouldClose(window) && !editorUI.RequestQuit()) {
        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - lastTime);
        lastTime = currentTime;

        glfwPollEvents();
        ProcessInput(window, deltaTime);
        scene.SetCameraForward(g_camera.GetForward());
        UpdateAppTitle(window, scene);

        // Update background scene loading and stage progress
        scene.UpdateAsyncLoading(deltaTime);
        scene.UpdateAutosave(deltaTime);

        if (!screenshotPath.empty() && !scene.IsLoading()) {
            static bool s_configured = false;
            if (!s_configured) {
                s_configured = true;
                if (customShading >= 0) {
                    scene.SetBSPMode(static_cast<BSPRenderMode>(customShading));
                }
                if (customEntities >= 0) {
                    scene.GetEntityRenderer().SetShowEntities(customEntities != 0);
                }
                if (customSelectArea > 0) {
                    scene.SelectArea(customSelectArea);
                    NavArea* a = scene.GetNAV().GetAreaByID(customSelectArea);
                    if (a && !hasCustomCamPos) {
                        g_camera.FocusOn(a->GetCenter(), 450.0f);
                    }
                } else if (!hasCustomCamPos) {
                    if (scene.HasNAV() && scene.GetNAV().GetAreaCount() > 0) {
                        size_t idx = scene.GetNAV().GetAreaCount() / 3;
                        const NavArea* a = scene.GetNAV().GetArea(idx);
                        if (a) {
                            scene.SelectArea(a->GetID());
                            g_camera.FocusOn(a->GetCenter(), 450.0f);
                        }
                    }
                }
                if (hasCustomCamPos) {
                    g_camera.SetPosition(customCamPos);
                }
                if (hasCustomCamAngles) {
                    g_camera.SetAngles(customPitch, customYaw);
                }
            }
        }

        int displayW = 0, displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);

        // Render 3D Scene
        glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float aspect = (displayH > 0) ? (static_cast<float>(displayW) / static_cast<float>(displayH)) : 1.0f;
        Matrix4 view = g_camera.GetViewMatrix();
        Matrix4 proj = g_camera.GetProjectionMatrix(aspect);
        scene.Render(meshShader, lineShader, view, proj, g_camera.GetPosition());

        // Render ImGui Overlays and Dockspace
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        editorUI.SetMarqueeBox(
            g_isBoxSelecting,
            static_cast<float>(g_boxSelectStartX), static_cast<float>(g_boxSelectStartY),
            static_cast<float>(g_boxSelectCurrentX), static_cast<float>(g_boxSelectCurrentY)
        );
        editorUI.Render(scene, g_camera, cmdMgr, deltaTime);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (!screenshotPath.empty() && !scene.IsLoading()) {
            static int s_renderFrames = 0;
            s_renderFrames++;
            if (s_renderFrames >= screenshotWaitFrames) {
                SaveScreenToBMP(screenshotPath, displayW, displayH);
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
