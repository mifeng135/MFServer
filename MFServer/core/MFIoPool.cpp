#include "MFIoPool.hpp"


MFIoPool::MFIoPool() {

}

MFIoPool::~MFIoPool() {
    for (std::thread* thread : m_threadVec) {
        if (thread && thread->joinable()) {
            thread->join();
        }
        delete thread;
    }
}

void MFIoPool::init(size_t poolSize) {
    for (size_t i = 0; i < poolSize; i++) {
        std::thread* thread = new std::thread(&MFIoPool::workThread, this);
        m_threadVec.emplace_back(thread);
    }
}

void MFIoPool::submit(const std::function<int()>& fn) {
    m_ioPool.enqueue(fn);
}

void MFIoPool::submit(std::function<int()>&& fn) {
    m_ioPool.enqueue(std::move(fn));
}

void MFIoPool::shutdown() {
    m_ioPool.enqueue([]() ->int {
        return 0;
    });
}


void MFIoPool::workThread() {
#if defined(__APPLE__)
    pthread_setname_np("MFIoPool");
#endif
    while (true)
    {
        std::function<int()> fn;
        m_ioPool.wait_dequeue(fn);
        if (fn() == 0) {
            break;
        }
    }
}


