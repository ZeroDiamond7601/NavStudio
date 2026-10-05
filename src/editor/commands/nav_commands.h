#ifndef NAV_COMMANDS_H
#define NAV_COMMANDS_H

#include "editor/commands/command.h"
#include "editor/scene/editor_scene.h"
#include "nav/nav_area.h"
#include <vector>

// Command: Change Area Attribute Flags (Crouch, Jump, Precise, etc.)
class CmdSetAreaAttributes : public IEditCommand {
public:
    CmdSetAreaAttributes(EditorScene* scene, uint32_t areaId, uint8_t newAttributes)
        : m_scene(scene), m_areaId(areaId), m_newAttributes(newAttributes), m_oldAttributes(0) {
        NavArea* area = m_scene->GetNAV().GetAreaByID(m_areaId);
        if (area) {
            m_oldAttributes = area->GetAttributes();
        }
    }

    void Execute() override {
        NavArea* area = m_scene->GetNAV().GetAreaByID(m_areaId);
        if (area) {
            area->SetAttributes(m_newAttributes);
            m_scene->RebuildNavRenderer();
        }
    }

    void Undo() override {
        NavArea* area = m_scene->GetNAV().GetAreaByID(m_areaId);
        if (area) {
            area->SetAttributes(m_oldAttributes);
            m_scene->RebuildNavRenderer();
        }
    }

    const char* GetName() const override { return "Set Area Attributes"; }

private:
    EditorScene* m_scene;
    uint32_t m_areaId;
    uint8_t m_newAttributes;
    uint8_t m_oldAttributes;
};

// Command: Assign Place Name to Area
class CmdSetAreaPlace : public IEditCommand {
public:
    CmdSetAreaPlace(EditorScene* scene, uint32_t areaId, const std::string& newPlace)
        : m_scene(scene), m_areaId(areaId), m_newPlace(newPlace) {
        NavArea* area = m_scene->GetNAV().GetAreaByID(m_areaId);
        if (area) {
            m_oldPlace = area->GetPlaceName();
        }
    }

    void Execute() override {
        NavArea* area = m_scene->GetNAV().GetAreaByID(m_areaId);
        if (area) {
            area->SetPlaceName(m_newPlace);
        }
    }

    void Undo() override {
        NavArea* area = m_scene->GetNAV().GetAreaByID(m_areaId);
        if (area) {
            area->SetPlaceName(m_oldPlace);
        }
    }

    const char* GetName() const override { return "Set Place Name"; }

private:
    EditorScene* m_scene;
    uint32_t m_areaId;
    std::string m_newPlace;
    std::string m_oldPlace;
};

// Command: Snap Area Elevation to Underlying BSP Geometry
class CmdSnapAreaToFloor : public IEditCommand {
public:
    CmdSnapAreaToFloor(EditorScene* scene, uint32_t areaId)
        : m_scene(scene), m_areaId(areaId), m_oldNeZ(0), m_oldSwZ(0), m_newNeZ(0), m_newSwZ(0), m_valid(false) {
        if (!scene || !scene->HasBSP()) return;
        NavArea* area = m_scene->GetNAV().GetAreaByID(m_areaId);
        if (!area) return;

        m_oldNeZ = area->GetNEZ();
        m_oldSwZ = area->GetSWZ();

        Vector3 ne = area->GetCorner(NAV_CORNER_NORTH_EAST);
        Vector3 sw = area->GetCorner(NAV_CORNER_SOUTH_WEST);

        Vector3 gNE, gSW;
        bool hitNE = m_scene->GetBSP().GetGround(ne + Vector3(0, 0, 32.0f), &gNE, 1024.0f);
        bool hitSW = m_scene->GetBSP().GetGround(sw + Vector3(0, 0, 32.0f), &gSW, 1024.0f);

        m_newNeZ = hitNE ? gNE.z : m_oldNeZ;
        m_newSwZ = hitSW ? gSW.z : m_oldSwZ;
        m_valid = true;
    }

    void Execute() override {
        if (!m_valid) return;
        NavArea* area = m_scene->GetNAV().GetAreaByID(m_areaId);
        if (area) {
            area->SetCornerHeights(m_newNeZ, m_newSwZ);
            m_scene->RebuildNavRenderer();
        }
    }

    void Undo() override {
        if (!m_valid) return;
        NavArea* area = m_scene->GetNAV().GetAreaByID(m_areaId);
        if (area) {
            area->SetCornerHeights(m_oldNeZ, m_oldSwZ);
            m_scene->RebuildNavRenderer();
        }
    }

    const char* GetName() const override { return "Snap Area to Floor"; }

private:
    EditorScene* m_scene;
    uint32_t m_areaId;
    float m_oldNeZ;
    float m_oldSwZ;
    float m_newNeZ;
    float m_newSwZ;
    bool m_valid;
};

// Command: Toggle Connection between Area A and Area B
class CmdToggleConnection : public IEditCommand {
public:
    CmdToggleConnection(EditorScene* scene, uint32_t fromId, uint32_t toId, NavDirType dir)
        : m_scene(scene), m_fromId(fromId), m_toId(toId), m_dir(dir), m_wasConnected(false) {
        NavArea* from = m_scene->GetNAV().GetAreaByID(m_fromId);
        NavArea* to = m_scene->GetNAV().GetAreaByID(m_toId);
        if (from && to) {
            m_wasConnected = from->IsConnected(to, m_dir);
        }
    }

    void Execute() override {
        NavArea* from = m_scene->GetNAV().GetAreaByID(m_fromId);
        NavArea* to = m_scene->GetNAV().GetAreaByID(m_toId);
        if (!from || !to) return;

        if (m_wasConnected) {
            from->Disconnect(to);
        } else {
            from->ConnectTo(to, m_dir);
        }
        m_scene->RebuildNavRenderer();
    }

    void Undo() override {
        NavArea* from = m_scene->GetNAV().GetAreaByID(m_fromId);
        NavArea* to = m_scene->GetNAV().GetAreaByID(m_toId);
        if (!from || !to) return;

        if (m_wasConnected) {
            from->ConnectTo(to, m_dir);
        } else {
            from->Disconnect(to);
        }
        m_scene->RebuildNavRenderer();
    }

    const char* GetName() const override { return "Toggle Area Connection"; }

private:
    EditorScene* m_scene;
    uint32_t m_fromId;
    uint32_t m_toId;
    NavDirType m_dir;
    bool m_wasConnected;
};

#endif // NAV_COMMANDS_H
