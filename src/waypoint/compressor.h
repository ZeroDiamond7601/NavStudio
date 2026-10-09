#ifndef WAYPOINT_COMPRESSOR_H
#define WAYPOINT_COMPRESSOR_H

#include <cstdint>
#include <vector>
#include <cstring>
#include <algorithm>

// Haruhiko Okumura LZSS Compressor & Decompressor (identical to YaPB and CS-EBOT)
class WaypointCompressor {
private:
    static constexpr int MAXBUF = 4096;
    static constexpr int PADDING = 18;
    static constexpr int THRESHOLD = 2;
    static constexpr int NIL = MAXBUF;

    uint8_t m_buffer[MAXBUF + PADDING - 1];
    int m_matchPos{0};
    int m_matchLen{0};

    int m_left[MAXBUF + 1];
    int m_right[MAXBUF + 257];
    int m_parent[MAXBUF + 1];

    void InitTrees() {
        for (int i = MAXBUF + 1; i <= MAXBUF + 256; ++i) {
            m_right[i] = NIL;
        }
        for (int i = 0; i < MAXBUF; ++i) {
            m_parent[i] = NIL;
        }
    }

    void Insert(int node) {
        int i;
        int compare = 1;
        uint8_t* key = &m_buffer[node];
        int temp = MAXBUF + 1 + key[0];

        m_right[node] = m_left[node] = NIL;
        m_matchLen = 0;

        for (;;) {
            if (compare >= 0) {
                if (m_right[temp] != NIL) {
                    temp = m_right[temp];
                } else {
                    m_right[temp] = node;
                    m_parent[node] = temp;
                    return;
                }
            } else {
                if (m_left[temp] != NIL) {
                    temp = m_left[temp];
                } else {
                    m_left[temp] = node;
                    m_parent[node] = temp;
                    return;
                }
            }

            for (i = 1; i < PADDING; ++i) {
                if ((compare = key[i] - m_buffer[temp + i])) {
                    break;
                }
            }

            if (i > m_matchLen) {
                m_matchPos = temp;
                if ((m_matchLen = i) >= PADDING) {
                    break;
                }
            }
        }

        m_parent[node] = m_parent[temp];
        m_left[node] = m_left[temp];
        m_right[node] = m_right[temp];
        m_parent[m_left[temp]] = node;
        m_parent[m_right[temp]] = node;

        if (m_right[m_parent[temp]] == temp) {
            m_right[m_parent[temp]] = node;
        } else {
            m_left[m_parent[temp]] = node;
        }

        m_parent[temp] = NIL;
    }

    void Erase(int node) {
        int temp;
        if (m_parent[node] == NIL) return;

        if (m_right[node] == NIL) {
            temp = m_left[node];
        } else if (m_left[node] == NIL) {
            temp = m_right[node];
        } else {
            temp = m_left[node];
            if (m_right[temp] != NIL) {
                do {
                    temp = m_right[temp];
                } while (m_right[temp] != NIL);

                m_right[m_parent[temp]] = m_left[temp];
                m_parent[m_left[temp]] = m_parent[temp];
                m_left[temp] = m_left[node];
                m_parent[m_left[node]] = temp;
            }

            m_right[temp] = m_right[node];
            m_parent[m_right[node]] = temp;
        }

        m_parent[temp] = m_parent[node];
        if (m_right[m_parent[node]] == node) {
            m_right[m_parent[node]] = temp;
        } else {
            m_left[m_parent[node]] = temp;
        }

        m_parent[node] = NIL;
    }

public:
    WaypointCompressor() {
        std::memset(m_right, 0, sizeof(m_right));
        std::memset(m_left, 0, sizeof(m_left));
        std::memset(m_parent, 0, sizeof(m_parent));
        std::memset(m_buffer, 0, sizeof(m_buffer));
    }

    // Encodes uncompressed buffer into LZSS compressed bytes
    bool Encode(const uint8_t* inData, size_t inSize, std::vector<uint8_t>& outData) {
        if (!inData || inSize == 0) return false;

        InitTrees();
        outData.clear();

        int node = MAXBUF - PADDING;
        for (int i = 0; i < node; ++i) {
            m_buffer[i] = ' ';
        }

        int length = 0;
        size_t bp = 0;
        for (; length < PADDING && bp < inSize; ++length) {
            m_buffer[node + length] = inData[bp++];
        }

        if (length == 0) return false;

        for (int i = 1; i <= PADDING; ++i) {
            Insert(node - i);
        }
        Insert(node);

        uint8_t cb[17]{0};
        uint8_t mask = 1;
        int cbp = 1;
        cb[0] = 0;
        int ptr = 0;

        do {
            if (m_matchLen > length) m_matchLen = length;

            if (m_matchLen <= THRESHOLD) {
                m_matchLen = 1;
                cb[0] |= mask;
                cb[cbp++] = m_buffer[node];
            } else {
                cb[cbp++] = static_cast<uint8_t>(m_matchPos & 0xff);
                cb[cbp++] = static_cast<uint8_t>(((m_matchPos >> 4) & 0xf0) | (m_matchLen - (THRESHOLD + 1)));
            }

            if (!(mask <<= 1)) {
                for (int i = 0; i < cbp; ++i) {
                    outData.push_back(cb[i]);
                }
                cb[0] = 0;
                cbp = mask = 1;
            }

            int last = m_matchLen;
            for (int i = 0; i < last && bp < inSize; ++i) {
                uint8_t bit = inData[bp++];
                Erase(ptr);
                m_buffer[ptr] = bit;
                if (ptr < PADDING - 1) {
                    m_buffer[ptr + MAXBUF] = bit;
                }
                ptr = (ptr + 1) & (MAXBUF - 1);
                node = (node + 1) & (MAXBUF - 1);
                Insert(node);
            }

            while (last > 0 && length > 0) {
                --last;
                Erase(ptr);
                ptr = (ptr + 1) & (MAXBUF - 1);
                node = (node + 1) & (MAXBUF - 1);
                --length;
                if (length > 0) {
                    Insert(node);
                }
            }
        } while (length > 0 || bp < inSize);

        if (cbp > 1) {
            for (int i = 0; i < cbp; ++i) {
                outData.push_back(cb[i]);
            }
        }

        return true;
    }

    // Decodes LZSS compressed bytes into uncompressed target buffer
    bool Decode(const uint8_t* inData, size_t inSize, uint8_t* outData, size_t maxOutSize, size_t& actualOutSize) {
        if (!inData || inSize == 0 || !outData || maxOutSize == 0) return false;

        int node = MAXBUF - PADDING;
        for (int i = 0; i < node; ++i) {
            m_buffer[i] = ' ';
        }

        unsigned int flags = 0;
        size_t inPtr = 0;
        size_t outPtr = 0;

        for (;;) {
            if (!((flags >>= 1) & 256)) {
                if (inPtr >= inSize) break;
                uint8_t bit = inData[inPtr++];
                flags = bit | 0xff00;
            }

            if (flags & 1) {
                if (inPtr >= inSize) break;
                uint8_t bit = inData[inPtr++];
                if (outPtr >= maxOutSize) return false;

                outData[outPtr++] = bit;
                m_buffer[node++] = bit;
                node &= (MAXBUF - 1);
            } else {
                if (inPtr + 1 >= inSize) break;
                int i = inData[inPtr++];
                int j = inData[inPtr++];
                i |= ((j & 0xf0) << 4);
                j = (j & 0x0f) + THRESHOLD;

                for (int k = 0; k <= j; ++k) {
                    uint8_t bit = m_buffer[(i + k) & (MAXBUF - 1)];
                    if (outPtr >= maxOutSize) return false;

                    outData[outPtr++] = bit;
                    m_buffer[node++] = bit;
                    node &= (MAXBUF - 1);
                }
            }
        }

        actualOutSize = outPtr;
        return true;
    }
};

#endif // WAYPOINT_COMPRESSOR_H
