#ifndef MFSnowflake_hpp
#define MFSnowflake_hpp

#include <cstdint>
#include <chrono>
#include <mutex>
#include <stdexcept>

#include "MFSpinLock.hpp"
// using snowflake_t = MFSnowflake<1534832906275L>;
// snowflake_t uuid;
// uuid.init(1, 1);

// for (int64_t i = 0; i < 10000; ++i)
// {
//     auto id = uuid.nextid();
//     std::cout << id << "\n";
// }
class MFSnowflake
{
    static constexpr int64_t WORKER_ID_BITS = 5L;
    static constexpr int64_t DATACENTER_ID_BITS = 5L;
    static constexpr int64_t MAX_WORKER_ID = (1 << WORKER_ID_BITS) - 1;
    static constexpr int64_t MAX_DATACENTER_ID = (1 << DATACENTER_ID_BITS) - 1;

    static constexpr int64_t SEQUENCE_BITS = 12L;
    static constexpr int64_t TIMESTAMP_BITS = 41L;  // 时间戳占41位

    // 新的ID结构（从高位到低位）：datacenterId | workerId | sequence | timestamp
    static constexpr int64_t TIMESTAMP_SHIFT = 0L;  // timestamp在最低位
    static constexpr int64_t SEQUENCE_SHIFT = TIMESTAMP_BITS;  // sequence在第三位
    static constexpr int64_t WORKER_ID_SHIFT = TIMESTAMP_BITS + SEQUENCE_BITS;  // workerId在第二位
    static constexpr int64_t DATACENTER_ID_SHIFT = TIMESTAMP_BITS + SEQUENCE_BITS + WORKER_ID_BITS;  // datacenterId在最高位

    static constexpr int64_t SEQUENCE_MASK = (1 << SEQUENCE_BITS) - 1;
    static constexpr int64_t TIMESTAMP_MASK = ((1LL << TIMESTAMP_BITS) - 1);  // 41位时间戳掩码

    using time_point = std::chrono::time_point<std::chrono::system_clock>;

    time_point start_time_point_ = std::chrono::system_clock::now();
    int64_t startMilSecond = std::chrono::duration_cast<std::chrono::milliseconds>(start_time_point_.time_since_epoch()).count();

    int64_t m_lastTimestamp = -1;
    int64_t m_workerId = 0;
    int64_t m_datacenterId = 0;
    int64_t m_sequence = 0;
    int64_t m_twepoch;
    MFSpinLock m_spineLock;
    

public:
    explicit MFSnowflake(int64_t twepoch)
    : m_twepoch(twepoch)
    {
        
    };

    MFSnowflake(const MFSnowflake &) = delete;

    MFSnowflake &operator=(const MFSnowflake &) = delete;

    void init(int64_t workerId, int64_t datacenterId)
    {
        if (workerId > MAX_WORKER_ID || workerId < 0)
        {
            throw std::runtime_error("worker Id can't be greater than 31 or less than 0");
        }

        if (datacenterId > MAX_DATACENTER_ID || datacenterId < 0)
        {
            throw std::runtime_error("datacenter Id can't be greater than 31 or less than 0");
        }

        m_workerId = workerId;
        m_datacenterId = datacenterId;
    }

    int64_t nextId()
    {
        std::lock_guard<MFSpinLock> guard(m_spineLock);
        auto timestamp = milSecond();
        if (m_lastTimestamp == timestamp)
        {
            m_sequence = (m_sequence + 1) & SEQUENCE_MASK;
            if (m_sequence == 0)
            {
                timestamp = wait_next_millis(m_lastTimestamp);
            }
        }
        else
        {
            m_sequence = 0;
        }

        m_lastTimestamp = timestamp;

        int64_t timestampDiff = timestamp - m_twepoch;

        return (m_datacenterId << DATACENTER_ID_SHIFT) |
               (m_workerId << WORKER_ID_SHIFT) |
               ((m_sequence & SEQUENCE_MASK) << SEQUENCE_SHIFT) |
               ((timestampDiff & TIMESTAMP_MASK) << TIMESTAMP_SHIFT);
    }

private:
    int64_t milSecond() const noexcept
    {
        auto now = std::chrono::system_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_point_);
        return startMilSecond + diff.count();
    }

    int64_t wait_next_millis(int64_t last) const noexcept
    {
        auto timestamp = milSecond();
        while (timestamp <= last)
        {
            timestamp = milSecond();
        }
        return timestamp;
    }
};

#endif /* MFSnowflake_hpp */
