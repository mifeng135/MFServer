#ifndef MFSHA256_hpp
#define MFSHA256_hpp

#include <string>
#include <cstdint>

class MFSHA256 {
public:
    static std::string hash(const std::string& data);
    
private:
    void reset();
    void update(const uint8_t* data, size_t length);
    void finalize();
    std::string getHash() const;
    void processBlock(const uint8_t* block);
    
    uint32_t m_state[8];
    uint64_t m_bitCount;
    uint8_t m_buffer[64];
    size_t m_bufferSize;
};

#endif




