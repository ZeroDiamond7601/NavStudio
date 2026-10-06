#include "bsp/wad_file.h"
#include "bsp/bsp_types.h"
#include <fstream>
#include <algorithm>
#include <cstring>

WADFile::WADFile()
    : m_loaded(false)
{
}

WADFile::~WADFile() {
    Unload();
}

void WADFile::Unload() {
    m_loaded = false;
    m_filepath.clear();
    m_filename.clear();
    m_rawData.clear();
    m_lumps.clear();
}

std::string WADFile::ToLower(const std::string& str) {
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool WADFile::Load(const std::string& filepath) {
    Unload();
    if (filepath.empty()) {
        return false;
    }

    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    std::streamsize fileSize = file.tellg();
    if (fileSize < static_cast<std::streamsize>(sizeof(wadheader_t))) {
        return false;
    }

    file.seekg(0, std::ios::beg);
    m_rawData.resize(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(m_rawData.data()), fileSize)) {
        Unload();
        return false;
    }

    m_filepath = filepath;
    size_t lastSep = filepath.find_last_of("/\\");
    m_filename = (lastSep != std::string::npos) ? filepath.substr(lastSep + 1) : filepath;

    return LoadFromMemory(m_rawData.data(), m_rawData.size());
}

bool WADFile::LoadFromMemory(const uint8_t* data, size_t size) {
    if (!data || size < sizeof(wadheader_t)) {
        return false;
    }

    const wadheader_t* header = reinterpret_cast<const wadheader_t*>(data);
    if (std::memcmp(header->identification, "WAD3", 4) != 0 &&
        std::memcmp(header->identification, "WAD2", 4) != 0) {
        return false;
    }

    int32_t numLumps = header->numlumps;
    int32_t tableOfs = header->infotableofs;
    if (numLumps <= 0 || tableOfs < 0) {
        return false;
    }

    uint64_t tableEnd = static_cast<uint64_t>(tableOfs) + static_cast<uint64_t>(numLumps) * sizeof(wadlump_t);
    if (tableEnd > size) {
        return false;
    }

    m_lumps.clear();
    m_lumps.reserve(static_cast<size_t>(numLumps));

    const wadlump_t* lumpTable = reinterpret_cast<const wadlump_t*>(data + tableOfs);
    for (int32_t i = 0; i < numLumps; ++i) {
        const wadlump_t* wl = &lumpTable[i];

        char nameBuf[17] = {0};
        std::memcpy(nameBuf, wl->name, 16);
        nameBuf[16] = '\0';
        std::string nameStr = nameBuf;

        if (nameStr.empty()) continue;

        LumpEntry entry;
        entry.filepos = static_cast<uint32_t>(wl->filepos);
        entry.disksize = static_cast<uint32_t>(wl->disksize);
        entry.size = static_cast<uint32_t>(wl->size);
        entry.type = static_cast<uint8_t>(wl->type);
        entry.name = nameStr;

        std::string lowerName = ToLower(nameStr);
        m_lumps[lowerName] = entry;
    }

    m_loaded = true;
    return true;
}

int WADFile::GetTextureCount() const {
    if (!m_loaded) return 0;
    int count = 0;
    for (const auto& kv : m_lumps) {
        if (kv.second.type == WAD_TYP_MIPTEX) {
            count++;
        }
    }
    return count;
}

std::vector<std::string> WADFile::GetTextureNames() const {
    std::vector<std::string> names;
    if (!m_loaded) return names;
    names.reserve(m_lumps.size());
    for (const auto& kv : m_lumps) {
        if (kv.second.type == WAD_TYP_MIPTEX) {
            names.push_back(kv.second.name);
        }
    }
    return names;
}

bool WADFile::HasTexture(const std::string& name) const {
    if (!m_loaded || name.empty()) return false;
    auto it = m_lumps.find(ToLower(name));
    return (it != m_lumps.end() && it->second.type == WAD_TYP_MIPTEX);
}

bool WADFile::GetTexture(const std::string& name, WADTexture& outTexture) const {
    if (!m_loaded || name.empty()) return false;

    auto it = m_lumps.find(ToLower(name));
    if (it == m_lumps.end() || it->second.type != WAD_TYP_MIPTEX) {
        return false;
    }

    const LumpEntry& entry = it->second;
    if (static_cast<uint64_t>(entry.filepos) + static_cast<uint64_t>(entry.disksize) > m_rawData.size()) {
        return false;
    }

    const uint8_t* lumpPtr = m_rawData.data() + entry.filepos;
    size_t lumpBytes = entry.disksize;

    return DecodeMiptex(lumpPtr, lumpBytes, outTexture);
}

bool WADFile::DecodeMiptex(const uint8_t* data, size_t size, WADTexture& outTexture) {
    if (!data || size < sizeof(miptex_t)) {
        return false;
    }

    const miptex_t* mt = reinterpret_cast<const miptex_t*>(data);
    uint32_t w = mt->width;
    uint32_t h = mt->height;

    if (w == 0 || h == 0 || w > 4096 || h > 4096) {
        return false;
    }

    // Offset 0 must point past miptex header
    if (mt->offsets[0] < sizeof(miptex_t) || mt->offsets[0] + w * h > size) {
        return false;
    }

    size_t mip0 = static_cast<size_t>(w) * h;
    size_t mip1 = (w / 2) * (h / 2);
    size_t mip2 = (w / 4) * (h / 4);
    size_t mip3 = (w / 8) * (h / 8);
    size_t totalMipPixels = mip0 + mip1 + mip2 + mip3;

    size_t palOfs = mt->offsets[0] + totalMipPixels;

    // Palette lookup
    const uint8_t* palData = nullptr;
    uint8_t defaultPal[768];

    if (palOfs + 2 + 768 <= size) {
        // GoldSrc format: 16-bit color count followed by 256 RGB triples
        palData = data + palOfs + 2;
    } else {
        // Fallback grayscale ramp
        for (int i = 0; i < 256; ++i) {
            defaultPal[i * 3 + 0] = static_cast<uint8_t>(i);
            defaultPal[i * 3 + 1] = static_cast<uint8_t>(i);
            defaultPal[i * 3 + 2] = static_cast<uint8_t>(i);
        }
        palData = defaultPal;
    }

    char nameBuf[17] = {0};
    std::memcpy(nameBuf, mt->name, 16);
    nameBuf[16] = '\0';

    outTexture.name = nameBuf;
    outTexture.width = w;
    outTexture.height = h;
    outTexture.isTransparent = (!outTexture.name.empty() && outTexture.name[0] == '{');
    outTexture.isFluid = (!outTexture.name.empty() && outTexture.name[0] == '!');

    outTexture.rgba.resize(mip0 * 4);
    const uint8_t* indices = data + mt->offsets[0];

    for (size_t i = 0; i < mip0; ++i) {
        uint8_t idx = indices[i];
        uint8_t r = palData[idx * 3 + 0];
        uint8_t g = palData[idx * 3 + 1];
        uint8_t b = palData[idx * 3 + 2];
        uint8_t a = 255;

        // Masked transparency for '{' textures
        if (outTexture.isTransparent && idx == 255) {
            a = 0;
        } else if (outTexture.isFluid) {
            a = 185;
        }

        outTexture.rgba[i * 4 + 0] = r;
        outTexture.rgba[i * 4 + 1] = g;
        outTexture.rgba[i * 4 + 2] = b;
        outTexture.rgba[i * 4 + 3] = a;
    }

    return true;
}
