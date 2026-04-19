#ifndef MFObjectPool_hpp
#define MFObjectPool_hpp

#include "MFMacro.h"

template <typename T>
class MFObjectPool {
public:
    ~MFObjectPool() {
        clear();
    }
public:
    T* pop() {
        T* obj = nullptr;
        if (m_queue.try_dequeue(obj)) {
            return obj;
        }
        return new T();
    }

    void push(T* obj) {
        m_queue.enqueue(obj);
    }

    void clear() {
        T* obj = nullptr;
        while (m_queue.try_dequeue(obj)) {
            delete obj;
        }
    }
    //Returns an estimate of the total number 
    size_t size() {
        return m_queue.size_approx();
    }
private:
    MFConcurrentQueue<T*> m_queue{1024};
};

#endif // MFObjectPool_hpp

 
