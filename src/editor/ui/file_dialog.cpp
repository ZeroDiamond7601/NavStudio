#include "editor/ui/file_dialog.h"
#include <cstdio>
#include <cstring>

namespace FileDialog {
    const char* kBSPFilter = "GoldSrc BSP (*.bsp)\0*.bsp\0All Files (*.*)\0*.*\0";
    const char* kNAVFilter = "Navigation Mesh (*.nav)\0*.nav\0All Files (*.*)\0*.*\0";
    const char* kAllFilter = "All Files (*.*)\0*.*\0";
}

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>

namespace FileDialog {

std::string OpenFile(const char* filter, const char* title) {
    char filename[1024] = "";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter ? filter : kAllFilter;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = sizeof(filename);
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (GetOpenFileNameA(&ofn)) {
        return std::string(filename);
    }
    return "";
}

std::string SaveFile(const char* filter, const char* defaultExt, const char* title) {
    char filename[1024] = "";
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter ? filter : kAllFilter;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = sizeof(filename);
    ofn.lpstrDefExt = defaultExt;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (GetSaveFileNameA(&ofn)) {
        return std::string(filename);
    }
    return "";
}

} // namespace FileDialog

#else

namespace FileDialog {

std::string OpenFile(const char* /*filter*/, const char* title) {
    std::string cmd = "zenity --file-selection --title=\"" + std::string(title ? title : "Open File") + "\" 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buffer[1024];
    std::string result = "";
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result = buffer;
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }
    }
    pclose(pipe);
    return result;
}

std::string SaveFile(const char* /*filter*/, const char* /*defaultExt*/, const char* title) {
    std::string cmd = "zenity --file-selection --save --confirm-overwrite --title=\"" + std::string(title ? title : "Save File") + "\" 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buffer[1024];
    std::string result = "";
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result = buffer;
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }
    }
    pclose(pipe);
    return result;
}

} // namespace FileDialog

#endif
