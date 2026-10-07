#include "editor/render/image_loader.h"
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cstring>

static std::string GetFileExtension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot);
    for (char& c : ext) c = static_cast<char>(std::tolower(c));
    return ext;
}

bool ImageLoader::LoadImage(const std::string& path, uint32_t& outWidth, uint32_t& outHeight, std::vector<uint8_t>& outRGBA) {
    std::string ext = GetFileExtension(path);
    if (ext == ".tga") {
        return LoadTGA(path, outWidth, outHeight, outRGBA);
    } else if (ext == ".bmp") {
        return LoadBMP(path, outWidth, outHeight, outRGBA);
    }

    // Try TGA first, then BMP
    if (LoadTGA(path, outWidth, outHeight, outRGBA)) return true;
    return LoadBMP(path, outWidth, outHeight, outRGBA);
}

bool ImageLoader::LoadTGA(const std::string& path, uint32_t& outWidth, uint32_t& outHeight, std::vector<uint8_t>& outRGBA) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize size = file.tellg();
    if (size < 18) return false;

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) return false;

    return LoadTGAFromMemory(buffer.data(), buffer.size(), outWidth, outHeight, outRGBA);
}

bool ImageLoader::LoadTGAFromMemory(const uint8_t* data, size_t size, uint32_t& outWidth, uint32_t& outHeight, std::vector<uint8_t>& outRGBA) {
    if (!data || size < 18) return false;

    uint8_t idLength = data[0];
    uint8_t imageType = data[2];
    uint16_t width = static_cast<uint16_t>(data[12] | (data[13] << 8));
    uint16_t height = static_cast<uint16_t>(data[14] | (data[15] << 8));
    uint8_t bpp = data[16];
    uint8_t descriptor = data[17];

    if (width == 0 || height == 0) return false;
    if (bpp != 24 && bpp != 32) return false;
    if (imageType != 2 && imageType != 10) return false; // 2: Uncompressed TrueColor, 10: RLE TrueColor

    uint32_t bytesPerPixel = bpp / 8;
    size_t offset = 18 + idLength;
    if (offset > size) return false;

    size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    outRGBA.resize(pixelCount * 4);

    if (imageType == 2) {
        // Uncompressed true-color
        if (offset + pixelCount * bytesPerPixel > size) return false;

        const uint8_t* src = data + offset;
        for (size_t i = 0; i < pixelCount; ++i) {
            uint8_t b = src[0];
            uint8_t g = src[1];
            uint8_t r = src[2];
            uint8_t a = (bytesPerPixel == 4) ? src[3] : 255;
            src += bytesPerPixel;

            outRGBA[i * 4 + 0] = r;
            outRGBA[i * 4 + 1] = g;
            outRGBA[i * 4 + 2] = b;
            outRGBA[i * 4 + 3] = a;
        }
    } else if (imageType == 10) {
        // RLE true-color
        size_t currentPixel = 0;
        const uint8_t* src = data + offset;
        const uint8_t* end = data + size;

        while (currentPixel < pixelCount && src < end) {
            uint8_t packetHeader = *src++;
            size_t count = (packetHeader & 0x7F) + 1;

            if (packetHeader & 0x80) {
                // RLE packet
                if (src + bytesPerPixel > end) return false;
                uint8_t b = src[0];
                uint8_t g = src[1];
                uint8_t r = src[2];
                uint8_t a = (bytesPerPixel == 4) ? src[3] : 255;
                src += bytesPerPixel;

                for (size_t c = 0; c < count && currentPixel < pixelCount; ++c) {
                    outRGBA[currentPixel * 4 + 0] = r;
                    outRGBA[currentPixel * 4 + 1] = g;
                    outRGBA[currentPixel * 4 + 2] = b;
                    outRGBA[currentPixel * 4 + 3] = a;
                    currentPixel++;
                }
            } else {
                // Raw packet
                for (size_t c = 0; c < count && currentPixel < pixelCount; ++c) {
                    if (src + bytesPerPixel > end) return false;
                    uint8_t b = src[0];
                    uint8_t g = src[1];
                    uint8_t r = src[2];
                    uint8_t a = (bytesPerPixel == 4) ? src[3] : 255;
                    src += bytesPerPixel;

                    outRGBA[currentPixel * 4 + 0] = r;
                    outRGBA[currentPixel * 4 + 1] = g;
                    outRGBA[currentPixel * 4 + 2] = b;
                    outRGBA[currentPixel * 4 + 3] = a;
                    currentPixel++;
                }
            }
        }

        if (currentPixel < pixelCount) return false;
    }

    // TGA origin handling: bit 5 set means top-to-bottom.
    // OpenGL texture coords expect row 0 at the bottom.
    // If bit 5 is set, image is stored top-to-bottom; flip vertically to bottom-to-top.
    bool topToBottom = (descriptor & 0x20) != 0;
    if (topToBottom) {
        size_t rowStride = static_cast<size_t>(width) * 4;
        std::vector<uint8_t> tempRow(rowStride);
        for (uint32_t y = 0; y < height / 2; ++y) {
            uint8_t* rowTop = outRGBA.data() + y * rowStride;
            uint8_t* rowBottom = outRGBA.data() + (height - 1 - y) * rowStride;
            std::memcpy(tempRow.data(), rowTop, rowStride);
            std::memcpy(rowTop, rowBottom, rowStride);
            std::memcpy(rowBottom, tempRow.data(), rowStride);
        }
    }

    outWidth = width;
    outHeight = height;
    return true;
}

bool ImageLoader::LoadBMP(const std::string& path, uint32_t& outWidth, uint32_t& outHeight, std::vector<uint8_t>& outRGBA) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize size = file.tellg();
    if (size < 54) return false;

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) return false;

    return LoadBMPFromMemory(buffer.data(), buffer.size(), outWidth, outHeight, outRGBA);
}

bool ImageLoader::LoadBMPFromMemory(const uint8_t* data, size_t size, uint32_t& outWidth, uint32_t& outHeight, std::vector<uint8_t>& outRGBA) {
    if (!data || size < 54) return false;

    if (data[0] != 'B' || data[1] != 'M') return false;

    uint32_t pixelOffset = *reinterpret_cast<const uint32_t*>(data + 10);
    int32_t width = *reinterpret_cast<const int32_t*>(data + 18);
    int32_t height = *reinterpret_cast<const int32_t*>(data + 22);
    uint16_t bpp = *reinterpret_cast<const uint16_t*>(data + 28);
    uint32_t compression = *reinterpret_cast<const uint32_t*>(data + 30);

    if (width <= 0 || height == 0) return false;
    if (bpp != 24 && bpp != 32) return false;
    if (compression != 0) return false; // Only uncompressed BI_RGB supported

    bool topDown = (height < 0);
    uint32_t absHeight = static_cast<uint32_t>(topDown ? -height : height);
    uint32_t absWidth = static_cast<uint32_t>(width);

    size_t rowBytes = (static_cast<size_t>(absWidth) * (bpp / 8) + 3) & ~3;
    if (pixelOffset + rowBytes * absHeight > size) return false;

    outRGBA.resize(static_cast<size_t>(absWidth) * absHeight * 4);
    uint32_t bytesPerPixel = bpp / 8;

    for (uint32_t y = 0; y < absHeight; ++y) {
        uint32_t srcRow = topDown ? y : (absHeight - 1 - y);
        const uint8_t* src = data + pixelOffset + srcRow * rowBytes;
        uint8_t* dst = outRGBA.data() + (absHeight - 1 - y) * (absWidth * 4);

        for (uint32_t x = 0; x < absWidth; ++x) {
            uint8_t b = src[0];
            uint8_t g = src[1];
            uint8_t r = src[2];
            uint8_t a = (bytesPerPixel == 4) ? src[3] : 255;
            src += bytesPerPixel;

            dst[x * 4 + 0] = r;
            dst[x * 4 + 1] = g;
            dst[x * 4 + 2] = b;
            dst[x * 4 + 3] = a;
        }
    }

    outWidth = absWidth;
    outHeight = absHeight;
    return true;
}

void ImageLoader::GenerateProceduralSkyFace(SkyPresetId preset, SkyFaceIndex face, uint32_t size, std::vector<uint8_t>& outRGBA) {
    if (size == 0) size = 256;
    outRGBA.resize(static_cast<size_t>(size) * size * 4);

    auto SampleSky = [preset](float dx, float dy, float dz, uint8_t& outR, uint8_t& outG, uint8_t& outB) {
        float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len > 1e-6f) { dx /= len; dy /= len; dz /= len; }

        float z = dz;
        float r = 0.5f, g = 0.5f, b = 0.5f;

        if (preset == SKY_PRESET_DESERT_ID) {
            // Sunny Desert (warm amber horizon, rich blue zenith, warm sun)
            float t = (z > 0.0f) ? std::pow(z, 0.55f) : 0.0f;
            float hz = (z >= 0.0f) ? (1.0f - t) : 1.0f;

            float zR = 0.22f, zG = 0.46f, zB = 0.82f;
            float hR = 0.88f, hG = 0.80f, hB = 0.65f;
            float gR = 0.38f, gG = 0.32f, gB = 0.22f;

            if (z >= 0.0f) {
                r = zR * t + hR * hz;
                g = zG * t + hG * hz;
                b = zB * t + hB * hz;
            } else {
                float gt = std::clamp(-z * 4.0f, 0.0f, 1.0f);
                r = hR * (1.0f - gt) + gR * gt;
                g = hG * (1.0f - gt) + gG * gt;
                b = hB * (1.0f - gt) + gB * gt;
            }

            // Key Sunlight
            float sX = 0.45f, sY = 0.35f, sZ = 0.82f;
            float sLen = std::sqrt(sX * sX + sY * sY + sZ * sZ);
            sX /= sLen; sY /= sLen; sZ /= sLen;
            float dotSun = std::max(0.0f, dx * sX + dy * sY + dz * sZ);
            float sunCore = std::pow(dotSun, 128.0f) * 1.5f;
            float sunHalo = std::pow(dotSun, 16.0f) * 0.35f;

            r += 1.0f * sunCore + 0.9f * sunHalo;
            g += 0.95f * sunCore + 0.7f * sunHalo;
            b += 0.80f * sunCore + 0.4f * sunHalo;

        } else if (preset == SKY_PRESET_NIGHT_ID) {
            // Assault Night (deep navy zenith, cyan horizon glow, moonlight)
            float t = (z > 0.0f) ? std::pow(z, 0.7f) : 0.0f;
            float zR = 0.02f, zG = 0.04f, zB = 0.12f;
            float hR = 0.08f, hG = 0.10f, hB = 0.20f;
            float gR = 0.01f, gG = 0.02f, gB = 0.04f;

            if (z >= 0.0f) {
                r = zR * t + hR * (1.0f - t);
                g = zG * t + hG * (1.0f - t);
                b = zB * t + hB * (1.0f - t);
            } else {
                float gt = std::clamp(-z * 4.0f, 0.0f, 1.0f);
                r = hR * (1.0f - gt) + gR * gt;
                g = hG * (1.0f - gt) + gG * gt;
                b = hB * (1.0f - gt) + gB * gt;
            }

            // Moon
            float mX = -0.3f, mY = 0.6f, mZ = 0.74f;
            float mLen = std::sqrt(mX * mX + mY * mY + mZ * mZ);
            mX /= mLen; mY /= mLen; mZ /= mLen;
            float dotMoon = std::max(0.0f, dx * mX + dy * mY + dz * mZ);
            float moonCore = std::pow(dotMoon, 256.0f) * 2.0f;
            float moonHalo = std::pow(dotMoon, 24.0f) * 0.3f;

            r += 0.9f * moonCore + 0.3f * moonHalo;
            g += 0.95f * moonCore + 0.4f * moonHalo;
            b += 1.0f * moonCore + 0.6f * moonHalo;

        } else if (preset == SKY_PRESET_OVERCAST_ID) {
            // Overcast Cloudy (soft slate gray-blue daylight)
            float t = (z > 0.0f) ? std::pow(z, 0.6f) : 0.0f;
            float zR = 0.45f, zG = 0.48f, zB = 0.54f;
            float hR = 0.68f, hG = 0.70f, hB = 0.74f;
            float gR = 0.25f, gG = 0.26f, gB = 0.28f;

            if (z >= 0.0f) {
                r = zR * t + hR * (1.0f - t);
                g = zG * t + hG * (1.0f - t);
                b = zB * t + hB * (1.0f - t);
            } else {
                float gt = std::clamp(-z * 4.0f, 0.0f, 1.0f);
                r = hR * (1.0f - gt) + gR * gt;
                g = hG * (1.0f - gt) + gG * gt;
                b = hB * (1.0f - gt) + gB * gt;
            }

        } else if (preset == SKY_PRESET_SUNSET_ID) {
            // Sunset Glow (coral horizon, amber glow, purple-violet zenith)
            float zR = 0.12f, zG = 0.14f, zB = 0.38f;
            float mR = 0.78f, mG = 0.35f, mB = 0.22f;
            float hR = 0.95f, hG = 0.65f, hB = 0.30f;
            float gR = 0.15f, gG = 0.10f, gB = 0.08f;

            if (z >= 0.0f) {
                if (z > 0.3f) {
                    float t = (z - 0.3f) / 0.7f;
                    r = mR * (1.0f - t) + zR * t;
                    g = mG * (1.0f - t) + zG * t;
                    b = mB * (1.0f - t) + zB * t;
                } else {
                    float t = z / 0.3f;
                    r = hR * (1.0f - t) + mR * t;
                    g = hG * (1.0f - t) + mG * t;
                    b = hB * (1.0f - t) + mB * t;
                }
                // Setting Sun
                float sX = 0.9f, sY = 0.1f, sZ = 0.12f;
                float sLen = std::sqrt(sX * sX + sY * sY + sZ * sZ);
                sX /= sLen; sY /= sLen; sZ /= sLen;
                float dotSun = std::max(0.0f, dx * sX + dy * sY + dz * sZ);
                float sunCore = std::pow(dotSun, 64.0f) * 1.2f;
                r += 1.0f * sunCore;
                g += 0.8f * sunCore;
                b += 0.4f * sunCore;
            } else {
                float gt = std::clamp(-z * 4.0f, 0.0f, 1.0f);
                r = hR * (1.0f - gt) + gR * gt;
                g = hG * (1.0f - gt) + gG * gt;
                b = hB * (1.0f - gt) + gB * gt;
            }
        } else {
            // Azure Clear Sky (high-noon vibrant cyan-blue)
            float t = (z > 0.0f) ? std::pow(z, 0.65f) : 0.0f;
            float zR = 0.15f, zG = 0.40f, zB = 0.85f;
            float hR = 0.65f, hG = 0.80f, hB = 0.95f;
            float gR = 0.20f, gG = 0.22f, gB = 0.25f;

            if (z >= 0.0f) {
                r = zR * t + hR * (1.0f - t);
                g = zG * t + hG * (1.0f - t);
                b = zB * t + hB * (1.0f - t);
            } else {
                float gt = std::clamp(-z * 4.0f, 0.0f, 1.0f);
                r = hR * (1.0f - gt) + gR * gt;
                g = hG * (1.0f - gt) + gG * gt;
                b = hB * (1.0f - gt) + gB * gt;
            }
        }

        outR = static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f));
        outG = static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f));
        outB = static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f));
    };

    for (uint32_t py = 0; py < size; ++py) {
        float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(size);
        for (uint32_t px = 0; px < size; ++px) {
            float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(size);

            float dx = 0.0f, dy = 0.0f, dz = 0.0f;
            switch (face) {
                case SKY_FACE_FRONT: // ft (+X)
                    dx = 1.0f;
                    dy = 1.0f - 2.0f * u;
                    dz = 2.0f * v - 1.0f;
                    break;
                case SKY_FACE_BACK:  // bk (-X)
                    dx = -1.0f;
                    dy = 2.0f * u - 1.0f;
                    dz = 2.0f * v - 1.0f;
                    break;
                case SKY_FACE_LEFT:  // lf (+Y)
                    dx = 2.0f * u - 1.0f;
                    dy = 1.0f;
                    dz = 2.0f * v - 1.0f;
                    break;
                case SKY_FACE_RIGHT: // rt (-Y)
                    dx = 1.0f - 2.0f * u;
                    dy = -1.0f;
                    dz = 2.0f * v - 1.0f;
                    break;
                case SKY_FACE_UP:    // up (+Z)
                    dx = 1.0f - 2.0f * v;
                    dy = 1.0f - 2.0f * u;
                    dz = 1.0f;
                    break;
                case SKY_FACE_DOWN:  // dn (-Z)
                    dx = 2.0f * v - 1.0f;
                    dy = 1.0f - 2.0f * u;
                    dz = -1.0f;
                    break;
            }

            uint8_t r = 0, g = 0, b = 0;
            SampleSky(dx, dy, dz, r, g, b);

            size_t idx = (static_cast<size_t>(py) * size + px) * 4;
            outRGBA[idx + 0] = r;
            outRGBA[idx + 1] = g;
            outRGBA[idx + 2] = b;
            outRGBA[idx + 3] = 255;
        }
    }
}
