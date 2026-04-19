#include "MFSHA1.hpp"
#include <cstring>

#define ROL32(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

std::string MFSHA1::hash(const std::string& data) {
    return hash(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

std::string MFSHA1::hash(const uint8_t* data, size_t length) {
    MFSHA1 sha1;
    sha1.reset();
    sha1.update(data, length);
    sha1.finalize();
    return sha1.getHash();
}

void MFSHA1::reset() {
    m_state[0] = 0x67452301;
    m_state[1] = 0xEFCDAB89;
    m_state[2] = 0x98BADCFE;
    m_state[3] = 0x10325476;
    m_state[4] = 0xC3D2E1F0;
    m_bitCount = 0;
    m_bufferSize = 0;
    m_finalized = false;
}

void MFSHA1::update(const uint8_t* data, size_t length) {
    if (m_finalized) return;
    
    for (size_t i = 0; i < length; i++) {
        m_buffer[m_bufferSize++] = data[i];
        if (m_bufferSize == 64) {
            processBlock(m_buffer);
            m_bitCount += 512;
            m_bufferSize = 0;
        }
    }
}

void MFSHA1::finalize() {
    if (m_finalized) return;
    
    uint64_t totalBits = m_bitCount + (m_bufferSize * 8);
    m_buffer[m_bufferSize++] = 0x80;
    
    if (m_bufferSize > 56) {
        while (m_bufferSize < 64) {
            m_buffer[m_bufferSize++] = 0;
        }
        processBlock(m_buffer);
        m_bufferSize = 0;
    }
    
    while (m_bufferSize < 56) {
        m_buffer[m_bufferSize++] = 0;
    }
    
    for (int i = 7; i >= 0; i--) {
        m_buffer[56 + i] = totalBits & 0xFF;
        totalBits >>= 8;
    }
    
    processBlock(m_buffer);
    m_finalized = true;
}

void MFSHA1::processBlock(const uint8_t* block) {
    uint32_t w[80];
    
    for (int i = 0; i < 16; i++) {
        w[i] = (block[i * 4] << 24) | (block[i * 4 + 1] << 16) |
               (block[i * 4 + 2] << 8) | block[i * 4 + 3];
    }
    
    for (int i = 16; i < 80; i++) {
        w[i] = ROL32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    
    uint32_t a = m_state[0];
    uint32_t b = m_state[1];
    uint32_t c = m_state[2];
    uint32_t d = m_state[3];
    uint32_t e = m_state[4];
    
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        
        uint32_t temp = ROL32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = ROL32(b, 30);
        b = a;
        a = temp;
    }
    
    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
}

std::string MFSHA1::getHash() const {
    if (!m_finalized) return "";
    
    std::string result;
    result.resize(20);
    
    for (int i = 0; i < 5; i++) {
        result[i * 4] = (m_state[i] >> 24) & 0xFF;
        result[i * 4 + 1] = (m_state[i] >> 16) & 0xFF;
        result[i * 4 + 2] = (m_state[i] >> 8) & 0xFF;
        result[i * 4 + 3] = m_state[i] & 0xFF;
    }
    
    return result;
}

