#ifndef EDITOR_HANDLES_H
#define EDITOR_HANDLES_H

#include "math/vector3.h"

struct Ray {
    Vector3 origin;
    Vector3 direction;
};

enum SelectedHandleType {
    HANDLE_NONE = 0,
    // Translate Handles
    HANDLE_GIZMO_X,
    HANDLE_GIZMO_Y,
    HANDLE_GIZMO_Z,
    HANDLE_GIZMO_CENTER,
    // Scale Handles
    HANDLE_SCALE_X,
    HANDLE_SCALE_Y,
    HANDLE_SCALE_Z,
    // Rotate Handles
    HANDLE_ROTATE_X,
    HANDLE_ROTATE_Y,
    HANDLE_ROTATE_Z,
    HANDLE_ROTATE_SCREEN,
    // Area Edges
    HANDLE_EDGE_NORTH,
    HANDLE_EDGE_EAST,
    HANDLE_EDGE_SOUTH,
    HANDLE_EDGE_WEST,
    // Area Corners
    HANDLE_CORNER_NW,
    HANDLE_CORNER_NE,
    HANDLE_CORNER_SE,
    HANDLE_CORNER_SW
};

inline const char* GetHandleName(SelectedHandleType h) {
    switch (h) {
        case HANDLE_GIZMO_X: return "Translate X-Axis (East/West)";
        case HANDLE_GIZMO_Y: return "Translate Y-Axis (North/South)";
        case HANDLE_GIZMO_Z: return "Translate Z-Axis (Up/Down Elevation)";
        case HANDLE_GIZMO_CENTER: return "Translate Center (Free Movement)";
        case HANDLE_SCALE_X: return "Scale X-Axis (Width)";
        case HANDLE_SCALE_Y: return "Scale Y-Axis (Length)";
        case HANDLE_SCALE_Z: return "Scale Z-Axis (Height/Slope)";
        case HANDLE_ROTATE_X: return "Rotate Pitch (Around X)";
        case HANDLE_ROTATE_Y: return "Rotate Roll (Around Y)";
        case HANDLE_ROTATE_Z: return "Rotate Yaw (Around Z)";
        case HANDLE_ROTATE_SCREEN: return "Rotate Screen Trackball";
        case HANDLE_EDGE_NORTH: return "North Edge (+Y)";
        case HANDLE_EDGE_EAST: return "East Edge (+X)";
        case HANDLE_EDGE_SOUTH: return "South Edge (-Y)";
        case HANDLE_EDGE_WEST: return "West Edge (-X)";
        case HANDLE_CORNER_NW: return "North-West Corner";
        case HANDLE_CORNER_NE: return "North-East Corner";
        case HANDLE_CORNER_SE: return "South-East Corner";
        case HANDLE_CORNER_SW: return "South-West Corner";
        default: return "None";
    }
}

#endif // EDITOR_HANDLES_H
