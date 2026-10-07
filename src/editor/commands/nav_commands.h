#ifndef NAV_COMMANDS_H
#define NAV_COMMANDS_H

#include "editor/commands/command.h"
#include "editor/scene/editor_scene.h"
#include "editor/scene/editor_handles.h"
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
        : m_scene(scene), m_areaId(areaId), m_valid(false) {
        if (!scene || !scene->HasBSP()) return;
        NavArea* area = m_scene->GetNAV().GetAreaByID(m_areaId);
        if (!area) return;

        m_oldExt = area->GetExtent();
        m_oldNeZ = area->GetNEZ();
        m_oldSwZ = area->GetSWZ();
        m_newExt = m_oldExt;

        Vector3 center = area->GetCenter();

        // 1. Trace ground at area center using HULL_POINT
        // Test multiple starting offsets in case current center is slightly embedded in floor or under ceiling
        static const float kStartOffsets[] = { 48.0f, 16.0f, 4.0f, 0.0f, -8.0f, -32.0f };
        bool hitCenter = false;
        BSPTraceResult trCenter;
        for (float off : kStartOffsets) {
            Vector3 start(center.x, center.y, center.z + off);
            Vector3 end(center.x, center.y, center.z - 4096.0f);
            if (m_scene->GetBSP().TraceWorld(start, end, HULL_POINT, &trCenter)) {
                if (!trCenter.startsolid && !trCenter.allsolid && trCenter.fraction > 0.0f) {
                    hitCenter = true;
                    break;
                }
            }
        }

        // If center trace missed, test each corner as fallback reference
        if (!hitCenter) {
            Vector3 testPts[4] = {
                area->GetCorner(NAV_CORNER_NORTH_WEST),
                area->GetCorner(NAV_CORNER_NORTH_EAST),
                area->GetCorner(NAV_CORNER_SOUTH_EAST),
                area->GetCorner(NAV_CORNER_SOUTH_WEST)
            };
            for (const auto& pt : testPts) {
                Vector3 start(pt.x, pt.y, pt.z + 32.0f);
                Vector3 end(pt.x, pt.y, pt.z - 4096.0f);
                if (m_scene->GetBSP().TraceWorld(start, end, HULL_POINT, &trCenter)) {
                    if (!trCenter.startsolid && !trCenter.allsolid && trCenter.fraction > 0.0f) {
                        hitCenter = true;
                        break;
                    }
                }
            }
        }

        // Fallback: GetGround
        if (!hitCenter) {
            Vector3 gPos;
            if (m_scene->GetBSP().GetGround(center + Vector3(0, 0, 32.0f), &gPos, 2048.0f, HULL_POINT)) {
                trCenter.endpos = gPos;
                trCenter.planeNormal = Vector3(0.0f, 0.0f, 1.0f);
                hitCenter = true;
            }
        }

        if (!hitCenter) return;

        Vector3 planeNorm = trCenter.planeNormal;
        if (std::abs(planeNorm.z) < 0.2f) {
            planeNorm = Vector3(0.0f, 0.0f, 1.0f);
        }

        // Expected height on the floor plane:
        auto PlaneZ = [&](float x, float y) -> float {
            float dx = x - trCenter.endpos.x;
            float dy = y - trCenter.endpos.y;
            return trCenter.endpos.z - (planeNorm.x * dx + planeNorm.y * dy) / planeNorm.z;
        };

        // Sample each corner within a walkable step window (+/- 24 units) around the plane:
        auto SampleCornerZ = [&](float x, float y) -> float {
            float expZ = PlaneZ(x, y);
            Vector3 cStart(x, y, expZ + 24.0f);
            Vector3 cEnd(x, y, expZ - 24.0f);
            BSPTraceResult trCorner;
            if (m_scene->GetBSP().TraceWorld(cStart, cEnd, HULL_POINT, &trCorner)) {
                if (!trCorner.startsolid && !trCorner.allsolid && trCorner.fraction > 0.0f) {
                    return trCorner.endpos.z;
                }
            }
            return expZ;
        };

        float nwZ = SampleCornerZ(m_oldExt.lo.x, m_oldExt.lo.y);
        float neZ = SampleCornerZ(m_oldExt.hi.x, m_oldExt.lo.y);
        float seZ = SampleCornerZ(m_oldExt.hi.x, m_oldExt.hi.y);
        float swZ = SampleCornerZ(m_oldExt.lo.x, m_oldExt.hi.y);

        m_newExt.lo.z = nwZ; // NW corner Z
        m_newNeZ = neZ;      // NE corner Z
        m_newExt.hi.z = seZ; // SE corner Z
        m_newSwZ = swZ;      // SW corner Z
        m_valid = true;
    }

    void Execute() override {
        if (!m_valid) return;
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
        if (!m_valid) return;
        NavArea* area = m_scene->GetNAV().GetAreaByID(m_areaId);
        if (area) {
            m_scene->GetNAV().GetGrid().RemoveArea(area);
            area->SetExtent(m_oldExt);
            area->SetCornerHeights(m_oldNeZ, m_oldSwZ);
            m_scene->GetNAV().GetGrid().AddArea(area);
            m_scene->RebuildNavRenderer();
        }
    }

    const char* GetName() const override { return "Snap Area to Floor"; }

private:
    EditorScene* m_scene;
    uint32_t m_areaId;
    NavExtent m_oldExt;
    NavExtent m_newExt;
    float m_oldNeZ{0.0f};
    float m_oldSwZ{0.0f};
    float m_newNeZ{0.0f};
    float m_newSwZ{0.0f};
    bool m_valid{false};
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

// Command: Extrude Edge (Hammer-style Shift+Drag / Extrude)
class CmdExtrudeArea : public IEditCommand {
public:
    CmdExtrudeArea(EditorScene* scene, uint32_t srcId, SelectedHandleType edgeHandle, float length = 64.0f)
        : m_scene(scene), m_srcId(srcId), m_edge(edgeHandle), m_length(length), m_createdId(0) {}

    void Execute() override {
        NavArea* src = m_scene->GetNAV().GetAreaByID(m_srcId);
        if (!src) return;

        NavExtent srcExt = src->GetExtent();
        NavExtent newExt;
        NavDirType dirFromSrc = NAV_DIR_NORTH;

        if (m_edge == HANDLE_EDGE_NORTH) {
            newExt.lo = Vector3(srcExt.lo.x, srcExt.hi.y, srcExt.lo.z);
            newExt.hi = Vector3(srcExt.hi.x, srcExt.hi.y + m_length, srcExt.hi.z);
            dirFromSrc = NAV_DIR_NORTH;
        } else if (m_edge == HANDLE_EDGE_SOUTH) {
            newExt.lo = Vector3(srcExt.lo.x, srcExt.lo.y - m_length, srcExt.lo.z);
            newExt.hi = Vector3(srcExt.hi.x, srcExt.lo.y, srcExt.hi.z);
            dirFromSrc = NAV_DIR_SOUTH;
        } else if (m_edge == HANDLE_EDGE_EAST) {
            newExt.lo = Vector3(srcExt.hi.x, srcExt.lo.y, srcExt.lo.z);
            newExt.hi = Vector3(srcExt.hi.x + m_length, srcExt.hi.y, srcExt.hi.z);
            dirFromSrc = NAV_DIR_EAST;
        } else if (m_edge == HANDLE_EDGE_WEST) {
            newExt.lo = Vector3(srcExt.lo.x - m_length, srcExt.lo.y, srcExt.lo.z);
            newExt.hi = Vector3(srcExt.lo.x, srcExt.hi.y, srcExt.hi.z);
            dirFromSrc = NAV_DIR_WEST;
        } else {
            return;
        }

        NavArea* created = m_scene->GetNAV().CreateArea(newExt, src->GetNEZ(), src->GetSWZ());
        if (created) {
            m_createdId = created->GetID();
            created->SetAttributes(src->GetAttributes());
            created->SetPlace(src->GetPlace());
            created->SetPlaceName(src->GetPlaceName());

            m_scene->GetNAV().ConnectAreas(m_srcId, m_createdId, true, dirFromSrc);
            m_scene->SelectArea(m_createdId);
            m_scene->RebuildNavRenderer();
        }
    }

    void Undo() override {
        if (m_createdId != 0) {
            m_scene->GetNAV().RemoveArea(m_createdId);
            m_scene->SelectArea(m_srcId);
            m_scene->RebuildNavRenderer();
        }
    }

    uint32_t GetCreatedID() const { return m_createdId; }
    const char* GetName() const override { return "Extrude Edge"; }

private:
    EditorScene* m_scene;
    uint32_t m_srcId;
    SelectedHandleType m_edge;
    float m_length;
    uint32_t m_createdId;
};

// Command: Split / Slice Area (Hammer Clipping Tool Shift+X)
class CmdSplitArea : public IEditCommand {
public:
    CmdSplitArea(EditorScene* scene, uint32_t areaId, bool splitAlongY)
        : m_scene(scene), m_areaId(areaId), m_splitAlongY(splitAlongY), m_createdId(0), m_valid(false) {
        NavArea* area = m_scene->GetNAV().GetAreaByID(areaId);
        if (!area) return;

        m_oldExt = area->GetExtent();
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
        NavArea* area1 = m_scene->GetNAV().GetAreaByID(m_areaId);
        if (!area1) return;

        NavExtent ext1 = m_oldExt;
        NavExtent ext2 = m_oldExt;

        if (m_splitAlongY) {
            float midY = (m_oldExt.lo.y + m_oldExt.hi.y) * 0.5f;
            ext1.hi.y = midY;
            ext2.lo.y = midY;
        } else {
            float midX = (m_oldExt.lo.x + m_oldExt.hi.x) * 0.5f;
            ext1.hi.x = midX;
            ext2.lo.x = midX;
        }

        m_scene->GetNAV().GetGrid().RemoveArea(area1);
        area1->SetExtent(ext1);
        m_scene->GetNAV().GetGrid().AddArea(area1);

        NavArea* area2 = m_scene->GetNAV().CreateArea(ext2, m_neZ, m_swZ);
        if (area2) {
            m_createdId = area2->GetID();
            area2->SetAttributes(m_attributes);
            area2->SetPlace(m_place);
            area2->SetPlaceName(m_placeName);

            if (m_splitAlongY) {
                m_scene->GetNAV().ConnectAreas(m_areaId, m_createdId, true, NAV_DIR_NORTH);
            } else {
                m_scene->GetNAV().ConnectAreas(m_areaId, m_createdId, true, NAV_DIR_EAST);
            }

            for (const auto& out : m_outgoing) {
                m_scene->GetNAV().ConnectAreas(m_areaId, out.targetId, false, out.dir);
                m_scene->GetNAV().ConnectAreas(m_createdId, out.targetId, false, out.dir);
            }
            for (const auto& in : m_incoming) {
                m_scene->GetNAV().ConnectAreas(in.targetId, m_areaId, false, in.dir);
                m_scene->GetNAV().ConnectAreas(in.targetId, m_createdId, false, in.dir);
            }

            m_scene->SelectArea(m_areaId);
            m_scene->RebuildNavRenderer();
        }
    }

    void Undo() override {
        if (!m_valid || m_createdId == 0) return;
        m_scene->GetNAV().RemoveArea(m_createdId);

        NavArea* area1 = m_scene->GetNAV().GetAreaByID(m_areaId);
        if (area1) {
            m_scene->GetNAV().GetGrid().RemoveArea(area1);
            area1->SetExtent(m_oldExt);
            m_scene->GetNAV().GetGrid().AddArea(area1);

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

    const char* GetName() const override { return "Split Area"; }

private:
    struct SavedConn { uint32_t targetId; NavDirType dir; };
    EditorScene* m_scene;
    uint32_t m_areaId;
    bool m_splitAlongY;
    uint32_t m_createdId;
    NavExtent m_oldExt;
    float m_neZ{0.0f}, m_swZ{0.0f};
    uint8_t m_attributes{0};
    uint16_t m_place{0};
    std::string m_placeName;
    std::vector<SavedConn> m_outgoing;
    std::vector<SavedConn> m_incoming;
    bool m_valid{false};
};

// Command: Merge Adjacent Areas (Hammer Join Tool)
class CmdMergeAreas : public IEditCommand {
public:
    CmdMergeAreas(EditorScene* scene, uint32_t keepId, uint32_t removeId)
        : m_scene(scene), m_keepId(keepId), m_removeId(removeId), m_valid(false) {
        NavArea* a1 = m_scene->GetNAV().GetAreaByID(keepId);
        NavArea* a2 = m_scene->GetNAV().GetAreaByID(removeId);
        if (!a1 || !a2) return;

        m_oldExt1 = a1->GetExtent();
        m_oldExt2 = a2->GetExtent();
        m_oldNeZ1 = a1->GetNEZ(); m_oldSwZ1 = a1->GetSWZ();
        m_oldNeZ2 = a2->GetNEZ(); m_oldSwZ2 = a2->GetSWZ();
        m_attr2 = a2->GetAttributes();
        m_place2 = a2->GetPlace();
        m_placeName2 = a2->GetPlaceName();

        m_mergedExt.lo.x = std::min(m_oldExt1.lo.x, m_oldExt2.lo.x);
        m_mergedExt.lo.y = std::min(m_oldExt1.lo.y, m_oldExt2.lo.y);
        m_mergedExt.lo.z = std::min(m_oldExt1.lo.z, m_oldExt2.lo.z);
        m_mergedExt.hi.x = std::max(m_oldExt1.hi.x, m_oldExt2.hi.x);
        m_mergedExt.hi.y = std::max(m_oldExt1.hi.y, m_oldExt2.hi.y);
        m_mergedExt.hi.z = std::max(m_oldExt1.hi.z, m_oldExt2.hi.z);

        for (int d = 0; d < 4; ++d) {
            for (const auto& conn : a2->GetAdjacentList(static_cast<NavDirType>(d))) {
                if (conn.area && conn.area->GetID() != keepId) {
                    m_outgoing2.push_back({ conn.area->GetID(), static_cast<NavDirType>(d) });
                }
            }
        }
        for (const NavArea* other : m_scene->GetNAV().GetAreas()) {
            if (other && other->GetID() != removeId && other->GetID() != keepId) {
                for (int d = 0; d < 4; ++d) {
                    if (other->IsConnected(a2, d)) {
                        m_incoming2.push_back({ other->GetID(), static_cast<NavDirType>(d) });
                    }
                }
            }
        }
        m_valid = true;
    }

    void Execute() override {
        if (!m_valid) return;
        NavArea* a1 = m_scene->GetNAV().GetAreaByID(m_keepId);
        if (!a1) return;

        m_scene->GetNAV().GetGrid().RemoveArea(a1);
        a1->SetExtent(m_mergedExt);
        m_scene->GetNAV().GetGrid().AddArea(a1);

        m_scene->GetNAV().RemoveArea(m_removeId);

        for (const auto& out : m_outgoing2) {
            m_scene->GetNAV().ConnectAreas(m_keepId, out.targetId, false, out.dir);
        }
        for (const auto& in : m_incoming2) {
            m_scene->GetNAV().ConnectAreas(in.targetId, m_keepId, false, in.dir);
        }

        m_scene->SelectArea(m_keepId);
        m_scene->RebuildNavRenderer();
    }

    void Undo() override {
        if (!m_valid) return;
        NavArea* a1 = m_scene->GetNAV().GetAreaByID(m_keepId);
        if (a1) {
            m_scene->GetNAV().GetGrid().RemoveArea(a1);
            a1->SetExtent(m_oldExt1);
            m_scene->GetNAV().GetGrid().AddArea(a1);
        }

        NavArea* a2 = m_scene->GetNAV().CreateArea(m_oldExt2, m_oldNeZ2, m_oldSwZ2);
        if (a2) {
            m_scene->GetNAV().GetGrid().RemoveArea(a2);
            a2->SetID(m_removeId);
            m_scene->GetNAV().GetGrid().AddArea(a2);
            a2->SetAttributes(m_attr2);
            a2->SetPlace(m_place2);
            a2->SetPlaceName(m_placeName2);

            for (const auto& out : m_outgoing2) {
                m_scene->GetNAV().ConnectAreas(m_removeId, out.targetId, false, out.dir);
            }
            for (const auto& in : m_incoming2) {
                m_scene->GetNAV().ConnectAreas(in.targetId, m_removeId, false, in.dir);
            }
        }
        m_scene->SelectArea(m_keepId);
        m_scene->RebuildNavRenderer();
    }

    const char* GetName() const override { return "Merge Areas"; }

private:
    struct SavedConn { uint32_t targetId; NavDirType dir; };
    EditorScene* m_scene;
    uint32_t m_keepId, m_removeId;
    NavExtent m_oldExt1, m_oldExt2, m_mergedExt;
    float m_oldNeZ1{0.0f}, m_oldSwZ1{0.0f}, m_oldNeZ2{0.0f}, m_oldSwZ2{0.0f};
    uint8_t m_attr2{0};
    uint16_t m_place2{0};
    std::string m_placeName2;
    std::vector<SavedConn> m_outgoing2;
    std::vector<SavedConn> m_incoming2;
    bool m_valid{false};
};

// Helper function to get 2D distance from point to segment
static inline float DistPointToSegment2D(const Vector3& pt, const Vector3& s0, const Vector3& s1) {
    float dx = s1.x - s0.x;
    float dy = s1.y - s0.y;
    float l2 = dx * dx + dy * dy;
    if (l2 < 1e-4f) return std::hypot(pt.x - s0.x, pt.y - s0.y);
    float t = std::max(0.0f, std::min(1.0f, ((pt.x - s0.x) * dx + (pt.y - s0.y) * dy) / l2));
    float px = s0.x + t * dx;
    float py = s0.y + t * dy;
    return std::hypot(pt.x - px, pt.y - py);
}

// Command: Bridge Two Edges (Create intermediate connecting NavArea between two edges)
class CmdBridgeEdges : public IEditCommand {
public:
    CmdBridgeEdges(EditorScene* scene, uint32_t area1Id, SelectedHandleType edge1,
                   uint32_t area2Id, SelectedHandleType edge2)
        : m_scene(scene), m_area1Id(area1Id), m_edge1(edge1), m_area2Id(area2Id), m_edge2(edge2), m_createdId(0) {}

    void Execute() override {
        NavArea* a1 = m_scene->GetNAV().GetAreaByID(m_area1Id);
        NavArea* a2 = m_scene->GetNAV().GetAreaByID(m_area2Id);
        if (!a1 || !a2 || a1 == a2) return;

        auto GetEdgeP1 = [](const NavArea* a, SelectedHandleType e) -> Vector3 {
            switch (e) {
                case HANDLE_EDGE_NORTH: return a->GetCorner(NAV_CORNER_NORTH_WEST);
                case HANDLE_EDGE_EAST:  return a->GetCorner(NAV_CORNER_NORTH_EAST);
                case HANDLE_EDGE_SOUTH: return a->GetCorner(NAV_CORNER_SOUTH_WEST);
                case HANDLE_EDGE_WEST:  return a->GetCorner(NAV_CORNER_NORTH_WEST);
                default: return a->GetCenter();
            }
        };
        auto GetEdgeP2 = [](const NavArea* a, SelectedHandleType e) -> Vector3 {
            switch (e) {
                case HANDLE_EDGE_NORTH: return a->GetCorner(NAV_CORNER_NORTH_EAST);
                case HANDLE_EDGE_EAST:  return a->GetCorner(NAV_CORNER_SOUTH_EAST);
                case HANDLE_EDGE_SOUTH: return a->GetCorner(NAV_CORNER_SOUTH_EAST);
                case HANDLE_EDGE_WEST:  return a->GetCorner(NAV_CORNER_SOUTH_WEST);
                default: return a->GetCenter();
            }
        };

        Vector3 p1a = GetEdgeP1(a1, m_edge1);
        Vector3 p1b = GetEdgeP2(a1, m_edge1);
        Vector3 p2a = GetEdgeP1(a2, m_edge2);
        Vector3 p2b = GetEdgeP2(a2, m_edge2);

        NavExtent ext;
        // North/South edge pair (gap in Y)
        if ((m_edge1 == HANDLE_EDGE_NORTH || m_edge1 == HANDLE_EDGE_SOUTH) &&
            (m_edge2 == HANDLE_EDGE_NORTH || m_edge2 == HANDLE_EDGE_SOUTH)) {
            ext.lo.y = std::min(p1a.y, p2a.y);
            ext.hi.y = std::max(p1a.y, p2a.y);

            float x1Min = std::min(p1a.x, p1b.x), x1Max = std::max(p1a.x, p1b.x);
            float x2Min = std::min(p2a.x, p2b.x), x2Max = std::max(p2a.x, p2b.x);
            float overlapMin = std::max(x1Min, x2Min);
            float overlapMax = std::min(x1Max, x2Max);

            if (overlapMax - overlapMin >= 16.0f) {
                ext.lo.x = overlapMin;
                ext.hi.x = overlapMax;
            } else {
                ext.lo.x = std::min(x1Min, x2Min);
                ext.hi.x = std::max(x1Max, x2Max);
            }
        }
        // East/West edge pair (gap in X)
        else if ((m_edge1 == HANDLE_EDGE_EAST || m_edge1 == HANDLE_EDGE_WEST) &&
                 (m_edge2 == HANDLE_EDGE_EAST || m_edge2 == HANDLE_EDGE_WEST)) {
            ext.lo.x = std::min(p1a.x, p2a.x);
            ext.hi.x = std::max(p1a.x, p2a.x);

            float y1Min = std::min(p1a.y, p1b.y), y1Max = std::max(p1a.y, p1b.y);
            float y2Min = std::min(p2a.y, p2b.y), y2Max = std::max(p2a.y, p2b.y);
            float overlapMin = std::max(y1Min, y2Min);
            float overlapMax = std::min(y1Max, y2Max);

            if (overlapMax - overlapMin >= 16.0f) {
                ext.lo.y = overlapMin;
                ext.hi.y = overlapMax;
            } else {
                ext.lo.y = std::min(y1Min, y2Min);
                ext.hi.y = std::max(y1Max, y2Max);
            }
        }
        // Arbitrary / diagonal / perpendicular edges
        else {
            ext.lo.x = std::min({p1a.x, p1b.x, p2a.x, p2b.x});
            ext.hi.x = std::max({p1a.x, p1b.x, p2a.x, p2b.x});
            ext.lo.y = std::min({p1a.y, p1b.y, p2a.y, p2b.y});
            ext.hi.y = std::max({p1a.y, p1b.y, p2a.y, p2b.y});
        }

        if (ext.hi.x - ext.lo.x < 16.0f) ext.hi.x = ext.lo.x + 16.0f;
        if (ext.hi.y - ext.lo.y < 16.0f) ext.hi.y = ext.lo.y + 16.0f;

        // Calculate smooth elevation interpolation across the 4 bridge corners
        auto CalcZ = [&](float x, float y) -> float {
            Vector3 pt(x, y, 0.0f);
            float d1 = DistPointToSegment2D(pt, p1a, p1b);
            float d2 = DistPointToSegment2D(pt, p2a, p2b);
            float total = d1 + d2;
            if (total < 1e-3f) return a1->GetZ(x, y);
            float w1 = d2 / total;
            float w2 = d1 / total;
            return w1 * a1->GetZ(x, y) + w2 * a2->GetZ(x, y);
        };

        float nwZ = CalcZ(ext.lo.x, ext.hi.y);
        float neZ = CalcZ(ext.hi.x, ext.hi.y);
        float seZ = CalcZ(ext.hi.x, ext.lo.y);
        float swZ = CalcZ(ext.lo.x, ext.lo.y);

        ext.lo.z = nwZ;
        ext.hi.z = seZ;

        NavArea* bridge = m_scene->GetNAV().CreateArea(ext, neZ, swZ);
        if (!bridge) return;

        m_createdId = bridge->GetID();
        bridge->SetAttributes(a1->GetAttributes());
        bridge->SetPlace(a1->GetPlace());
        bridge->SetPlaceName(a1->GetPlaceName());

        // Connect 2-way with both parent areas
        m_scene->GetNAV().ConnectAreas(m_area1Id, m_createdId, true);
        m_scene->GetNAV().ConnectAreas(m_createdId, m_area2Id, true);

        m_scene->SelectArea(m_createdId);
        m_scene->RebuildNavRenderer();
    }

    void Undo() override {
        if (m_createdId != 0) {
            m_scene->GetNAV().RemoveArea(m_createdId);
            m_scene->SelectArea(m_area1Id);
            m_scene->RebuildNavRenderer();
            m_createdId = 0;
        }
    }

    uint32_t GetCreatedID() const { return m_createdId; }
    const char* GetName() const override { return "Bridge Edges"; }

private:
    EditorScene* m_scene;
    uint32_t m_area1Id;
    SelectedHandleType m_edge1;
    uint32_t m_area2Id;
    SelectedHandleType m_edge2;
    uint32_t m_createdId;
};

// Command: Snap Area To Neighbors (Close gaps to adjacent collinear nav areas and connect)
class CmdSnapAreaToNeighbors : public IEditCommand {
public:
    CmdSnapAreaToNeighbors(EditorScene* scene, uint32_t areaId, float tolerance = 24.0f)
        : m_scene(scene), m_areaId(areaId), m_tolerance(tolerance), m_valid(false) {}

    void Execute() override {
        NavArea* area = m_scene->GetNAV().GetAreaByID(m_areaId);
        if (!area) return;

        m_oldExt = area->GetExtent();
        m_oldNeZ = area->GetNEZ();
        m_oldSwZ = area->GetSWZ();
        m_newExt = m_oldExt;
        m_newNeZ = m_oldNeZ;
        m_newSwZ = m_oldSwZ;

        m_connectedNeighbors.clear();

        for (const auto* other : m_scene->GetNAV().GetAreas()) {
            if (!other || other->GetID() == m_areaId) continue;
            const NavExtent& oExt = other->GetExtent();

            // Check North edge gap
            if (std::abs(m_newExt.hi.y - oExt.lo.y) <= m_tolerance &&
                m_newExt.hi.x > oExt.lo.x + 4.0f && m_newExt.lo.x < oExt.hi.x - 4.0f) {
                m_newExt.hi.y = oExt.lo.y;
                m_newExt.lo.z = other->GetCorner(NAV_CORNER_SOUTH_WEST).z;
                m_newNeZ = other->GetCorner(NAV_CORNER_SOUTH_EAST).z;
                m_connectedNeighbors.push_back({ other->GetID(), NAV_DIR_NORTH });
            }
            // Check South edge gap
            if (std::abs(m_newExt.lo.y - oExt.hi.y) <= m_tolerance &&
                m_newExt.hi.x > oExt.lo.x + 4.0f && m_newExt.lo.x < oExt.hi.x - 4.0f) {
                m_newExt.lo.y = oExt.hi.y;
                m_newSwZ = other->GetCorner(NAV_CORNER_NORTH_WEST).z;
                m_newExt.hi.z = other->GetCorner(NAV_CORNER_NORTH_EAST).z;
                m_connectedNeighbors.push_back({ other->GetID(), NAV_DIR_SOUTH });
            }
            // Check East edge gap
            if (std::abs(m_newExt.hi.x - oExt.lo.x) <= m_tolerance &&
                m_newExt.hi.y > oExt.lo.y + 4.0f && m_newExt.lo.y < oExt.hi.y - 4.0f) {
                m_newExt.hi.x = oExt.lo.x;
                m_newNeZ = other->GetCorner(NAV_CORNER_NORTH_WEST).z;
                m_newExt.hi.z = other->GetCorner(NAV_CORNER_SOUTH_WEST).z;
                m_connectedNeighbors.push_back({ other->GetID(), NAV_DIR_EAST });
            }
            // Check West edge gap
            if (std::abs(m_newExt.lo.x - oExt.hi.x) <= m_tolerance &&
                m_newExt.hi.y > oExt.lo.y + 4.0f && m_newExt.lo.y < oExt.hi.y - 4.0f) {
                m_newExt.lo.x = oExt.hi.x;
                m_newExt.lo.z = other->GetCorner(NAV_CORNER_NORTH_EAST).z;
                m_newSwZ = other->GetCorner(NAV_CORNER_SOUTH_EAST).z;
                m_connectedNeighbors.push_back({ other->GetID(), NAV_DIR_WEST });
            }
        }

        m_scene->GetNAV().GetGrid().RemoveArea(area);
        area->SetExtent(m_newExt);
        area->SetCornerHeights(m_newNeZ, m_newSwZ);
        m_scene->GetNAV().GetGrid().AddArea(area);

        for (const auto& conn : m_connectedNeighbors) {
            m_scene->GetNAV().ConnectAreas(m_areaId, conn.targetId, true, conn.dir);
        }

        m_valid = true;
        m_scene->RebuildNavRenderer();
    }

    void Undo() override {
        if (!m_valid) return;
        NavArea* area = m_scene->GetNAV().GetAreaByID(m_areaId);
        if (area) {
            m_scene->GetNAV().GetGrid().RemoveArea(area);
            area->SetExtent(m_oldExt);
            area->SetCornerHeights(m_oldNeZ, m_oldSwZ);
            m_scene->GetNAV().GetGrid().AddArea(area);
            m_scene->RebuildNavRenderer();
        }
    }

    const char* GetName() const override { return "Snap to Neighbors"; }

private:
    struct ConnInfo { uint32_t targetId; NavDirType dir; };
    EditorScene* m_scene;
    uint32_t m_areaId;
    float m_tolerance;
    NavExtent m_oldExt, m_newExt;
    float m_oldNeZ{0.0f}, m_oldSwZ{0.0f}, m_newNeZ{0.0f}, m_newSwZ{0.0f};
    std::vector<ConnInfo> m_connectedNeighbors;
    bool m_valid;
};

// Command: Group Multiple Commands as a Single Undoable Action
class CmdCompound : public IEditCommand {
public:
    CmdCompound(std::vector<std::unique_ptr<IEditCommand>> commands, const std::string& name = "Batch Edit")
        : m_commands(std::move(commands)), m_name(name) {}

    void Execute() override {
        for (auto& cmd : m_commands) {
            if (cmd) cmd->Execute();
        }
    }

    void Undo() override {
        for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) {
            if (*it) (*it)->Undo();
        }
    }

    const char* GetName() const override { return m_name.c_str(); }

private:
    std::vector<std::unique_ptr<IEditCommand>> m_commands;
    std::string m_name;
};

// Command: Create New Area (from interactive Draw Area Marquee Tool)
class CmdCreateArea : public IEditCommand {
public:
    CmdCreateArea(EditorScene* scene, const NavExtent& extent, float neZ, float swZ,
                  uint8_t attributes = 0, const std::string& placeName = "")
        : m_scene(scene), m_extent(extent), m_neZ(neZ), m_swZ(swZ),
          m_attributes(attributes), m_placeName(placeName), m_createdId(0) {}

    void Execute() override {
        NavArea* area = m_scene->GetNAV().CreateArea(m_extent, m_neZ, m_swZ);
        if (area) {
            m_createdId = area->GetID();
            area->SetAttributes(m_attributes);
            if (!m_placeName.empty()) {
                area->SetPlaceName(m_placeName);
            }
            m_scene->SelectArea(m_createdId);
            m_scene->RebuildNavRenderer();
        }
    }

    void Undo() override {
        if (m_createdId != 0) {
            m_scene->GetNAV().RemoveArea(m_createdId);
            m_scene->SelectArea(0);
            m_scene->RebuildNavRenderer();
            m_createdId = 0;
        }
    }

    uint32_t GetCreatedID() const { return m_createdId; }
    const char* GetName() const override { return "Create NavArea"; }

private:
    EditorScene* m_scene;
    NavExtent m_extent;
    float m_neZ{0.0f}, m_swZ{0.0f};
    uint8_t m_attributes{0};
    std::string m_placeName;
    uint32_t m_createdId{0};
};

// Command: Batch Change Attributes for Multiple Areas
class CmdBatchSetAttributes : public IEditCommand {
public:
    CmdBatchSetAttributes(EditorScene* scene, const std::vector<uint32_t>& areaIds, uint8_t newAttributes)
        : m_scene(scene), m_areaIds(areaIds), m_newAttributes(newAttributes) {
        for (uint32_t id : m_areaIds) {
            NavArea* a = m_scene->GetNAV().GetAreaByID(id);
            if (a) {
                m_oldAttributes.push_back({ id, a->GetAttributes() });
            }
        }
    }

    void Execute() override {
        for (uint32_t id : m_areaIds) {
            NavArea* a = m_scene->GetNAV().GetAreaByID(id);
            if (a) a->SetAttributes(m_newAttributes);
        }
        m_scene->RebuildNavRenderer();
    }

    void Undo() override {
        for (const auto& item : m_oldAttributes) {
            NavArea* a = m_scene->GetNAV().GetAreaByID(item.first);
            if (a) a->SetAttributes(item.second);
        }
        m_scene->RebuildNavRenderer();
    }

    const char* GetName() const override { return "Batch Set Attributes"; }

private:
    EditorScene* m_scene;
    std::vector<uint32_t> m_areaIds;
    uint8_t m_newAttributes;
    std::vector<std::pair<uint32_t, uint8_t>> m_oldAttributes;
};

// Command: Batch Set Place Name for Multiple Areas
class CmdBatchSetPlace : public IEditCommand {
public:
    CmdBatchSetPlace(EditorScene* scene, const std::vector<uint32_t>& areaIds, const std::string& newPlace)
        : m_scene(scene), m_areaIds(areaIds), m_newPlace(newPlace) {
        for (uint32_t id : m_areaIds) {
            NavArea* a = m_scene->GetNAV().GetAreaByID(id);
            if (a) {
                m_oldPlaces.push_back({ id, a->GetPlaceName() });
            }
        }
    }

    void Execute() override {
        for (uint32_t id : m_areaIds) {
            NavArea* a = m_scene->GetNAV().GetAreaByID(id);
            if (a) a->SetPlaceName(m_newPlace);
        }
    }

    void Undo() override {
        for (const auto& item : m_oldPlaces) {
            NavArea* a = m_scene->GetNAV().GetAreaByID(item.first);
            if (a) a->SetPlaceName(item.second);
        }
    }

    const char* GetName() const override { return "Batch Set Place Name"; }

private:
    EditorScene* m_scene;
    std::vector<uint32_t> m_areaIds;
    std::string m_newPlace;
    std::vector<std::pair<uint32_t, std::string>> m_oldPlaces;
};

#endif // NAV_COMMANDS_H
