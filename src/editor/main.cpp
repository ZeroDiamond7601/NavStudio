#include "editor/glad/include/glad/glad.h"
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "editor/scene/editor_scene.h"
#include "editor/scene/scene_picker.h"
#include "editor/camera/camera.h"
#include "editor/commands/command.h"
#include "editor/ui/editor_ui.h"
#include "editor/render/shader.h"
#include "editor/render/editor_shaders.h"

#include <cstdio>
#include <iostream>

static Camera g_camera;
static bool g_isRightMouseDown = false;
static bool g_isAltDown = false;
static double g_lastMouseX = 0.0;
static double g_lastMouseY = 0.0;
static bool g_firstMouse = true;

static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            g_isRightMouseDown = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            g_firstMouse = true;
        } else if (action == GLFW_RELEASE) {
            g_isRightMouseDown = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }

    g_isAltDown = (mods & GLFW_MOD_ALT) != 0;
}

static void CursorPosCallback(GLFWwindow* /*window*/, double xpos, double ypos) {
    if (g_isRightMouseDown) {
        if (g_firstMouse) {
            g_lastMouseX = xpos;
            g_lastMouseY = ypos;
            g_firstMouse = false;
        }

        float xoffset = static_cast<float>(xpos - g_lastMouseX);
        float yoffset = static_cast<float>(g_lastMouseY - ypos); // Invert Y
        g_lastMouseX = xpos;
        g_lastMouseY = ypos;

        g_camera.ProcessMouseMovement(xoffset, yoffset);
    }
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
    CommandManager cmdMgr;
    EditorUI editorUI;
    editorUI.Init();

    // Load initial map if passed via arguments
    if (argc > 1) {
        std::string argPath = argv[1];
        if (argPath.find(".bsp") != std::string::npos) {
            scene.LoadBSP(argPath);
        } else if (argPath.find(".nav") != std::string::npos) {
            scene.LoadNAV(argPath);
        }
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

        int displayW = 0, displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);

        // Handle viewport raycasting for mouse selection
        if (!io.WantCaptureMouse && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            double mouseX, mouseY;
            glfwGetCursorPos(window, &mouseX, &mouseY);

            float aspect = (displayH > 0) ? (static_cast<float>(displayW) / static_cast<float>(displayH)) : 1.0f;
            Matrix4 viewMat = g_camera.GetViewMatrix();
            Matrix4 projMat = g_camera.GetProjectionMatrix(aspect);

            Ray ray = ScenePicker::ScreenPointToRay(
                static_cast<float>(mouseX), static_cast<float>(mouseY),
                static_cast<float>(displayW), static_cast<float>(displayH),
                viewMat, projMat
            );

            uint32_t hitArea = ScenePicker::PickNavArea(scene, ray);
            scene.SelectArea(hitArea);
        }

        // Render 3D Scene
        glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float aspect = (displayH > 0) ? (static_cast<float>(displayW) / static_cast<float>(displayH)) : 1.0f;
        Matrix4 mvp = g_camera.GetProjectionMatrix(aspect) * g_camera.GetViewMatrix();
        scene.Render(meshShader, lineShader, mvp);

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
