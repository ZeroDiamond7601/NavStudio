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

// Command: Connect Areas (Directed or Bidirectional)
class CmdConnectAreas : public IEditCommand {
public:
    CmdConnectAreas(EditorScene* scene, uint32_t fromId, uint32_t toId, bool bidirectional = true, int explicitDir = -1)
        : m_scene(scene), m_fromId(fromId), m_toId(toId), m_bidirectional(bidirectional), m_explicitDir(explicitDir) {}

    void Execute() override {
        m_scene->GetNAV().ConnectAreas(m_fromId, m_toId, m_bidirectional, m_explicitDir);
        m_scene->RebuildNavRenderer();
    }

    void Undo() override {
        m_scene->GetNAV().DisconnectAreas(m_fromId, m_toId, m_bidirectional);
        m_scene->RebuildNavRenderer();
    }

    const char* GetName() const override { return "Connect Areas"; }

private:
    EditorScene* m_scene;
    uint32_t m_fromId;
    uint32_t m_toId;
    bool m_bidirectional;
    int m_explicitDir;
};

// Command: Disconnect Areas
class CmdDisconnectAreas : public IEditCommand {
public:
    CmdDisconnectAreas(EditorScene* scene, uint32_t fromId, uint32_t toId, bool bidirectional = false)
        : m_scene(scene), m_fromId(fromId), m_toId(toId), m_bidirectional(bidirectional) {
        NavArea* from = scene->GetNAV().GetAreaByID(fromId);
        NavArea* to = scene->GetNAV().GetAreaByID(toId);
        if (from && to) {
            for (int d = 0; d < 4; ++d) {
                if (from->IsConnected(to, d)) {
                    m_fromDir = d;
                    break;
                }
            }
            if (to->IsConnected(from)) {
                for (int d = 0; d < 4; ++d) {
                    if (to->IsConnected(from, d)) {
                        m_toDir = d;
                        m_bidirectional = true;
                        break;
                    }
                }
            }
        }
    }

    void Execute() override {
        m_scene->GetNAV().DisconnectAreas(m_fromId, m_toId, m_bidirectional);
        m_scene->RebuildNavRenderer();
    }

    void Undo() override {
        m_scene->GetNAV().ConnectAreas(m_fromId, m_toId, false, m_fromDir);
        if (m_bidirectional && m_toDir >= 0) {
            m_scene->GetNAV().ConnectAreas(m_toId, m_fromId, false, m_toDir);
        }
        m_scene->RebuildNavRenderer();
    }

    const char* GetName() const override { return "Disconnect Areas"; }

private:
    EditorScene* m_scene;
    uint32_t m_fromId;
    uint32_t m_toId;
    bool m_bidirectional{false};
    int m_fromDir{-1};
    int m_toDir{-1};
};

// Command: 3D Transform Area (Grab / Move, Scale, Height Adjust)
class CmdTransformArea : public IEditCommand {
public:
    CmdTransformArea(EditorScene* scene, uint32_t areaId,
                     const NavExtent& oldExt, float oldNeZ, float oldSwZ,
                     const NavExtent& newExt, float newNeZ, float newSwZ,
                     const char* name = "Transform Area")
        : m_scene(scene), m_areaId(areaId),
          m_oldExt(oldExt), m_oldNeZ(oldNeZ), m_oldSwZ(oldSwZ),
          m_newExt(newExt), m_newNeZ(newNeZ), m_newSwZ(newSwZ),
          m_name(name) {}

    void Execute() override {
        NavArea* area = m_scene->GetNAV().GetAreaByID(m_areaId);
        if (area) {
            m_scene->GetNAV().GetGrid().RemoveArea(area);
            area->SetExtent(m_newExt);
            area->SetCornerHeights(m_newNeZ, m_newSwZ);
            m_scene->GetNAV().GetGrid().AddArea(area);
            m_scene->RebuildNavRenderer();
        }
    }

    void Undo() override {
        NavArea* area = m_scene->GetNAV().GetAreaByID(m_areaId);
        if (area) {
            m_scene->GetNAV().GetGrid().RemoveArea(area);
            area->SetExtent(m_oldExt);
            area->SetCornerHeights(m_oldNeZ, m_oldSwZ);
            m_scene->GetNAV().GetGrid().AddArea(area);
            m_scene->RebuildNavRenderer();
        }
    }

    const char* GetName() const override { return m_name.c_str(); }

private:
    EditorScene* m_scene;
    uint32_t m_areaId;
    NavExtent m_oldExt;
    NavExtent m_newExt;
    float m_oldNeZ;
    float m_oldSwZ;
    float m_newNeZ;
    float m_newSwZ;
    std::string m_name;
};

// Command: Duplicate Area in 3D
class CmdDuplicateArea : public IEditCommand {
public:
    CmdDuplicateArea(EditorScene* scene, uint32_t srcId, const Vector3& offset = Vector3(32.0f, 32.0f, 0.0f))
        : m_scene(scene), m_srcId(srcId), m_offset(offset), m_createdId(0) {}

    void Execute() override {
        if (m_createdId == 0) {
            NavArea* copy = m_scene->GetNAV().DuplicateArea(m_srcId, m_offset);
            if (copy) {
                m_createdId = copy->GetID();
                m_scene->SelectArea(m_createdId);
                m_scene->RebuildNavRenderer();
            }
        } else {
            NavArea* src = m_scene->GetNAV().GetAreaByID(m_srcId);
            if (src) {
                NavExtent ext = src->GetExtent();
                ext.lo += m_offset;
                ext.hi += m_offset;
                NavArea* area = m_scene->GetNAV().CreateArea(ext, src->GetNEZ() + m_offset.z, src->GetSWZ() + m_offset.z);
                if (area) {
                    m_createdId = area->GetID();
                    m_scene->SelectArea(m_createdId);
                    m_scene->RebuildNavRenderer();
                }
            }
        }
    }

    void Undo() override {
        if (m_createdId != 0) {
            m_scene->GetNAV().RemoveArea(m_createdId);
            if (m_scene->GetSelectedAreaID() == m_createdId) {
                m_scene->SelectArea(m_srcId);
            }
            m_scene->RebuildNavRenderer();
        }
    }

    uint32_t GetCreatedID() const { return m_createdId; }
    const char* GetName() const override { return "Duplicate Area"; }

private:
    EditorScene* m_scene;
    uint32_t m_srcId;
    Vector3 m_offset;
    uint32_t m_createdId;
};

// Command: Delete Area with full connection restore
class CmdDeleteArea : public IEditCommand {
public:
    CmdDeleteArea(EditorScene* scene, uint32_t areaId)
        : m_scene(scene), m_areaId(areaId), m_valid(false) {
        NavArea* area = m_scene->GetNAV().GetAreaByID(areaId);
        if (!area) return;

        m_extent = area->GetExtent();
        m_neZ = area->GetNEZ();
        m_swZ = area->GetSWZ();
        m_attributes = area->GetAttributes();
        m_place = area->GetPlace();
        m_placeName = area->GetPlaceName();

        for (int d = 0; d < 4; ++d) {
            for (const auto& conn : area->GetAdjacentList(static_cast<NavDirType>(d))) {
                if (conn.area) {
                    m_outgoing.push_back({ conn.area->GetID(), static_cast<NavDirType>(d) });
                }
            }
        }

        for (const NavArea* other : m_scene->GetNAV().GetAreas()) {
            if (other && other->GetID() != areaId) {
                for (int d = 0; d < 4; ++d) {
                    if (other->IsConnected(area, d)) {
                        m_incoming.push_back({ other->GetID(), static_cast<NavDirType>(d) });
                    }
                }
            }
        }
        m_valid = true;
    }

    void Execute() override {
        if (!m_valid) return;
        m_scene->GetNAV().RemoveArea(m_areaId);
        if (m_scene->GetSelectedAreaID() == m_areaId) {
            m_scene->SelectArea(0);
        }
        m_scene->RebuildNavRenderer();
    }

    void Undo() override {
        if (!m_valid) return;
        NavArea* area = m_scene->GetNAV().CreateArea(m_extent, m_neZ, m_swZ);
        if (area) {
            m_scene->GetNAV().GetGrid().RemoveArea(area);
            area->SetID(m_areaId);
            m_scene->GetNAV().GetGrid().AddArea(area);
            area->SetAttributes(m_attributes);
            area->SetPlace(m_place);
            area->SetPlaceName(m_placeName);

            for (const auto& out : m_outgoing) {
                m_scene->GetNAV().ConnectAreas(m_areaId, out.targetId, false, out.dir);
            }
            for (const auto& in : m_incoming) {
                m_scene->GetNAV().ConnectAreas(in.targetId, m_areaId, false, in.dir);
            }

            m_scene->SelectArea(m_areaId);
            m_scene->RebuildNavRenderer();
        }
    }

    const char* GetName() const override { return "Delete Area"; }

private:
    struct SavedConn {
        uint32_t targetId;
        NavDirType dir;
    };

    EditorScene* m_scene;
    uint32_t m_areaId;
    NavExtent m_extent;
    float m_neZ{0.0f};
    float m_swZ{0.0f};
    uint8_t m_attributes{0};
    uint16_t m_place{0};
    std::string m_placeName;
    std::vector<SavedConn> m_outgoing;
    std::vector<SavedConn> m_incoming;
    bool m_valid;
};

#endif // NAV_COMMANDS_H
