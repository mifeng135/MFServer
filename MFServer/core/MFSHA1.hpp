#ifndef MFSHA1_hpp
#define MFSHA1_hpp

#include <string>
#include <cstdint>

class MFSHA1 {
public:
    static std::string hash(const std::string& data);
    static std::string hash(const uint8_t* data, size_t length);
    
private:
    void reset();
    void update(const uint8_t* data, size_t length);
    void finalize();
    std::string getHash() const;
    
    void processBlock(const uint8_t* block);
    
    uint32_t m_state[5];
    bool m_finalized;

    uint64_t m_bitCount;
    uint8_t m_buffer[64];
    size_t m_bufferSize;
};

#endif

