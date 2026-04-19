#ifndef MFIoPool_hpp
#define MFIoPool_hpp

#include "MFLuaService.hpp"
#include "MFMacro.h"

class MFIoPool
{
public:
    MFIoPool();
    ~MFIoPool();
public:
    void init(size_t poolSize);
    void submit(const std::function<int()>& fn);
    void submit(std::function<int()>&& fn);
    void shutdown();
private:
    void workThread();
private:
    MFBlockingQueue<std::function<int()>>       m_ioPool;
    std::vector<std::thread*>                   m_threadVec;
};


#endif /* MFIoPool_hpp */
