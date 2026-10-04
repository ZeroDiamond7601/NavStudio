#pragma once

#include <cstdint>
#include <cstring>
#include "../math/vector3.h"

#pragma pack(push, 1)

#define BSP_VERSION_GOLDSRC 30
#define BSP_HEADER_LUMPS    15

// Lump indices
enum BSPLumpIndex {
    LUMP_ENTITIES     = 0,
    LUMP_PLANES       = 1,
    LUMP_TEXTURES     = 2,
    LUMP_VERTICES     = 3,
    LUMP_VISIBILITY   = 4,
    LUMP_NODES        = 5,
    LUMP_TEXINFO      = 6,
    LUMP_FACES        = 7,
    LUMP_LIGHTING     = 8,
    LUMP_CLIPNODES    = 9,
    LUMP_LEAVES       = 10,
    LUMP_MARKSURFACES = 11,
    LUMP_EDGES        = 12,
    LUMP_SURFEDGES    = 13,
    LUMP_MODELS       = 14
};

// Hull types for GoldSrc
enum BSPHullType {
    HULL_POINT  = 0, // Ray / bullet / zero size point
    HULL_HUMAN  = 1, // Standing player hull (32x32x72)
    HULL_LARGE  = 2, // Large hull (64x64x64)
    HULL_HEAD   = 3  // Crouching player hull (32x32x36)
};

// Plane types
enum BSPPlaneType {
    PLANE_X = 0,
    PLANE_Y = 1,
    PLANE_Z = 2,
    PLANE_ANYX = 3,
    PLANE_ANYY = 4,
    PLANE_ANYZ = 5
};

// Contents types
enum BSPContents {
    CONTENTS_EMPTY        = -1,
    CONTENTS_SOLID        = -2,
    CONTENTS_WATER        = -3,
    CONTENTS_SLIME        = -4,
    CONTENTS_LAVA         = -5,
    CONTENTS_SKY          = -6,
    CONTENTS_ORIGIN       = -7,
    CONTENTS_CLIP         = -8,
    CONTENTS_CURRENT_0    = -9,
    CONTENTS_CURRENT_90   = -10,
    CONTENTS_CURRENT_180  = -11,
    CONTENTS_CURRENT_270  = -12,
    CONTENTS_CURRENT_UP   = -13,
    CONTENTS_CURRENT_DOWN = -14,
    CONTENTS_TRANSLUCENT  = -15
};

struct dentry_t {
    int32_t fileofs;
    int32_t filelen;
};

struct dheader_t {
    int32_t version;
    dentry_t lumps[BSP_HEADER_LUMPS];
};

struct dplane_t {
    Vector3 normal;
    float dist;
    int32_t type;
};

struct dnode_t {
    int32_t planenum;
    int16_t children[2];    // If < 0, ~(child) is leaf index
    int16_t mins[3];
    int16_t maxs[3];
    uint16_t firstface;
    uint16_t numfaces;
};

struct dclipnode_t {
    int32_t planenum;
    int16_t children[2];    // If < 0, child is contents (e.g. CONTENTS_SOLID = -2)
};

struct dleaf_t {
    int32_t contents;
    int32_t visofs;         // -1 if no vis info
    int16_t mins[3];
    int16_t maxs[3];
    uint16_t firstmarksurface;
    uint16_t nummarksurfaces;
    uint8_t ambient_level[4];
};

struct dmodel_t {
    Vector3 mins;
    Vector3 maxs;
    Vector3 origin;
    int32_t headnode[4];    // Index into nodes (0) or clipnodes (1..3)
    int32_t visleafs;
    int32_t firstface;
    int32_t numfaces;
};

struct dmiptexlump_t {
    int32_t nummiptex;
    int32_t dataofs[1];
};

struct miptex_t {
    char name[16];
    uint32_t width;
    uint32_t height;
    uint32_t offsets[4];
};

struct texinfo_t {
    float vecs[2][4];
    int32_t miptex;
    int32_t flags;
};

struct dface_t {
    int16_t planenum;
    int16_t side;
    int32_t firstedge;
    int16_t numedges;
    int16_t texinfo;
    uint8_t styles[4];
    int32_t lightofs;
};

struct dvertex_t {
    Vector3 point;
};

struct dedge_t {
    uint16_t v[2];
};

#pragma pack(pop)

struct BSPTraceResult {
    bool allsolid;
    bool startsolid;
    float fraction;
    Vector3 endpos;
    Vector3 planeNormal;
    float planeDist;
    int32_t hitContents;
    int32_t hitPlane;
    int32_t hitFace;
    char hitTexture[64];

    BSPTraceResult()
        : allsolid(false), startsolid(false), fraction(1.0f),
          endpos(), planeNormal(), planeDist(0.0f), hitContents(CONTENTS_EMPTY),
          hitPlane(-1), hitFace(-1) {
        hitTexture[0] = '\0';
    }
};

struct BSPTextureInfo {
    char name[16];
    int width;
    int height;
};

enum BSPMaterialType {
    MAT_UNKNOWN = 0,
    MAT_CONCRETE,       // 'C' - Stone / rock / concrete
    MAT_METAL,          // 'M' - Solid metal / iron / steel
    MAT_DIRT,           // 'D' - Dirt / sand / gravel
    MAT_VENT,           // 'V' - Ventilation duct
    MAT_GRATE,          // 'G' - Metal grate / chainlink fence
    MAT_TILE,           // 'T' - Tile / marble
    MAT_SLOSH,          // 'S' - Water / fluid / slime
    MAT_WOOD,           // 'W' - Wood / crate / plank
    MAT_COMPUTER,       // 'P' - Computer terminal / electronics
    MAT_GLASS,          // 'Y' - Glass / window
    MAT_FLESH,          // 'X' - Organic / flesh
    MAT_FOLIAGE,        // 'F' - Grass / leaves / foliage
    NUM_MATERIALS
};

enum BSPTextureFlag {
    TEX_FLAG_NONE     = 0,
    TEX_FLAG_LIGHT    = 0x0001,
    TEX_FLAG_SLICK    = 0x0002,
    TEX_FLAG_SKY      = 0x0004,
    TEX_FLAG_WARP     = 0x0008,
    TEX_FLAG_TRANS33  = 0x0010,
    TEX_FLAG_TRANS66  = 0x0020,
    TEX_FLAG_FLOWING  = 0x0040,
    TEX_FLAG_NODRAW   = 0x0080
};

