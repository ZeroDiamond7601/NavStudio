#ifndef FILE_DIALOG_H
#define FILE_DIALOG_H

#include <string>

namespace FileDialog {
    // Standard filters with double-null termination
    extern const char* kBSPFilter;
    extern const char* kNAVFilter;
    extern const char* kAllFilter;

    // Opens native OS file picker to select an existing file
    std::string OpenFile(const char* filter, const char* title);

    // Opens native OS file picker to specify a save file location
    std::string SaveFile(const char* filter, const char* defaultExt, const char* title);
}

#endif // FILE_DIALOG_H
