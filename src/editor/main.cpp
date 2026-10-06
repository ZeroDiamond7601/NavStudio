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

#include <cstdio>
#include <iostream>
#include <algorithm>
#include <cctype>

static Camera g_camera;
static EditorScene* g_activeScene = nullptr;
static CommandManager* g_cmdMgr = nullptr;
static bool g_isRightMouseDown = false;
static bool g_isAltDown = false;
static double g_lastMouseX = 0.0;
static double g_lastMouseY = 0.0;
static bool g_firstMouse = true;

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
        }
    }

    if (!bspPath.empty()) {
        std::printf("[DragDrop] Loading BSP map: %s\n", bspPath.c_str());
        g_activeScene->StartAsyncLoad(bspPath, navPath);
    } else if (!navPath.empty()) {
        std::printf("[DragDrop] Loading NAV mesh: %s\n", navPath.c_str());
        g_activeScene->StartAsyncLoad(navPath);
    } else {
        std::printf("[DragDrop] Unsupported file format\n");
    }
}

static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            if (g_activeScene && g_activeScene->GetTransformMode() != EditorScene::TRANSFORM_NONE) {
                // Right-click cancels active Blender transform!
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
    } else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        if (!g_activeScene || !g_cmdMgr) return;

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

        auto mode = g_activeScene->GetTransformMode();
        if (mode == EditorScene::TRANSFORM_TRANSLATE || mode == EditorScene::TRANSFORM_SCALE) {
            // Left-click confirms Blender transform!
            g_activeScene->ConfirmTransform(*g_cmdMgr);
        } else if (mode == EditorScene::TRANSFORM_CONNECT) {
            uint32_t hitArea = ScenePicker::PickNavArea(*g_activeScene, ray);
            if (hitArea != 0 && hitArea != g_activeScene->GetSelectedAreaID()) {
                bool shiftPressed = (mods & GLFW_MOD_SHIFT) != 0;
                g_activeScene->ConnectSelectedTo(hitArea, !shiftPressed, *g_cmdMgr);
            }
        } else {
            uint32_t hitArea = ScenePicker::PickNavArea(*g_activeScene, ray);
            g_activeScene->SelectArea(hitArea);
        }
    }

    g_isAltDown = (mods & GLFW_MOD_ALT) != 0;
}

static void KeyCallback(GLFWwindow* /*window*/, int key, int /*scancode*/, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    if (!g_activeScene || !g_cmdMgr) return;

    if (action == GLFW_PRESS) {
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
            // Normal Selection Mode Hotkeys (Blender-style)
            if (g_activeScene->GetSelectedAreaID() != 0) {
                NavArea* sel = g_activeScene->GetSelectedArea();
                if (key == GLFW_KEY_G) {
                    if (sel) g_activeScene->StartGrab(sel->GetCenter());
                } else if (key == GLFW_KEY_S && (mods & GLFW_MOD_CONTROL) == 0) {
                    if (sel) g_activeScene->StartScale(sel->GetCenter());
                } else if (key == GLFW_KEY_R) {
                    g_activeScene->RotateSelectedArea90(*g_cmdMgr);
                } else if (key == GLFW_KEY_C) {
                    g_activeScene->StartConnectMode();
                } else if (key == GLFW_KEY_D && (mods & GLFW_MOD_SHIFT) != 0) {
                    g_activeScene->DuplicateSelectedArea(*g_cmdMgr);
                } else if (key == GLFW_KEY_X || key == GLFW_KEY_DELETE) {
                    g_activeScene->DeleteSelectedArea(*g_cmdMgr);
                } else if (key == GLFW_KEY_F) {
                    if (sel) g_camera.FocusOn(sel->GetCenter());
                } else if (key == GLFW_KEY_SPACE) {
                    if (g_activeScene->HasBSP()) {
                        g_cmdMgr->ExecuteCommand(std::make_unique<CmdSnapAreaToFloor>(g_activeScene, sel->GetID()));
                    }
                } else if (key == GLFW_KEY_ESCAPE) {
                    g_activeScene->SelectArea(0);
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
    } else if (g_activeScene) {
        int displayW = 0, displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        float aspect = (displayH > 0) ? (static_cast<float>(displayW) / static_cast<float>(displayH)) : 1.0f;

        Ray ray = ScenePicker::ScreenPointToRay(
            static_cast<float>(xpos), static_cast<float>(ypos),
            static_cast<float>(displayW), static_cast<float>(displayH),
            g_camera.GetViewMatrix(), g_camera.GetProjectionMatrix(aspect)
        );

        auto mode = g_activeScene->GetTransformMode();
        if (mode == EditorScene::TRANSFORM_TRANSLATE || mode == EditorScene::TRANSFORM_SCALE) {
            NavArea* sel = g_activeScene->GetSelectedArea();
            if (sel) {
                Vector3 c = sel->GetCenter();
                if (std::abs(ray.direction.z) > 1e-4f) {
                    float t = (c.z - ray.origin.z) / ray.direction.z;
                    Vector3 hitPoint = ray.origin + ray.direction * t;
                    g_activeScene->UpdateTransform(hitPoint, static_cast<float>(ypos - g_lastMouseY));
                }
            }
        } else if (mode == EditorScene::TRANSFORM_CONNECT) {
            uint32_t hoverArea = ScenePicker::PickNavArea(*g_activeScene, ray);
            g_activeScene->SetConnectHoverArea(hoverArea);
        } else {
            ImGuiIO& io = ImGui::GetIO();
            if (!io.WantCaptureMouse) {
                uint32_t hoverArea = ScenePicker::PickNavArea(*g_activeScene, ray);
                g_activeScene->SetHoveredArea(hoverArea);
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
    if (g_isRightMouseDown || g_camera.GetMode() == CAMERA_MODE_TOPDOWN_2D) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) g_camera.ProcessKeyboard(0, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) g_camera.ProcessKeyboard(1, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) g_camera.ProcessKeyboard(2, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) g_camera.ProcessKeyboard(3, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) g_camera.ProcessKeyboard(4, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) g_camera.ProcessKeyboard(5, deltaTime);
    }
}

int main(int argc, char* argv[]) {
    std::printf("====================================================\n");
    std::printf("  NavStudio - AMXX NavMesh & BSP 3D Editor\n");
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
    GLFWwindow* window = glfwCreateWindow(initialWidth, initialHeight, "NavStudio - AMXX NavMesh & BSP Editor", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[Error] Failed to create GLFW window\n");
        glfwTerminate();
        return -1;
    }

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

    // Load initial map if passed via arguments
    if (argc > 1) {
        std::string argPath = argv[1];
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

        // Update background scene loading and stage progress
        scene.UpdateAsyncLoading(deltaTime);

        int displayW = 0, displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);

        // Render 3D Scene
        glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float aspect = (displayH > 0) ? (static_cast<float>(displayW) / static_cast<float>(displayH)) : 1.0f;
        Matrix4 mvp = g_camera.GetProjectionMatrix(aspect) * g_camera.GetViewMatrix();
        scene.Render(meshShader, lineShader, mvp, g_camera.GetPosition());

        // Render ImGui Overlays and Dockspace
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        editorUI.Render(scene, g_camera, cmdMgr, deltaTime);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

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
