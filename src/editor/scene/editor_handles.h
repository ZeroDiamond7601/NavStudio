#ifndef EDITOR_HANDLES_H
#define EDITOR_HANDLES_H

enum SelectedHandleType {
    HANDLE_NONE = 0,
    HANDLE_GIZMO_X,
    HANDLE_GIZMO_Y,
    HANDLE_GIZMO_Z,
    HANDLE_GIZMO_CENTER,
    HANDLE_EDGE_NORTH,
    HANDLE_EDGE_EAST,
    HANDLE_EDGE_SOUTH,
    HANDLE_EDGE_WEST,
    HANDLE_CORNER_NW,
    HANDLE_CORNER_NE,
    HANDLE_CORNER_SE,
    HANDLE_CORNER_SW
};

inline const char* GetHandleName(SelectedHandleType h) {
    switch (h) {
        case HANDLE_GIZMO_X: return "X-Axis Arrow (East/West)";
        case HANDLE_GIZMO_Y: return "Y-Axis Arrow (North/South)";
        case HANDLE_GIZMO_Z: return "Z-Axis Arrow (Elevation)";
        case HANDLE_GIZMO_CENTER: return "Center Position Handle";
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
