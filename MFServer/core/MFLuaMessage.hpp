#ifndef MFMessage_HPP
#define MFMessage_HPP

#include "drogon/drogon.h"
#include "MFMacro.h"
#include "MFObjectPool.hpp"


class MFLuaService;

class MFMessage {
public:
    MFMessage()
    : m_sessionId(0)
    , m_messageType(0)
    , m_src(0)
    , m_dst(0) {
    }

    virtual ~MFMessage() = default;
    virtual void reset() {
        m_src = 0;
        m_dst = 0;
        m_sessionId = 0;
        m_messageType = 0;
    }
public:
    MFProperty(size_t, m_sessionId, SessionId);
    MFProperty(uint32_t, m_messageType, MessageType);
    MFProperty(MFServiceId_t, m_src, Src);
    MFProperty(MFServiceId_t, m_dst, Dst);
};

class MFScheduleMessage final : public MFMessage {
public:
    MFScheduleMessage()
    : m_pool(nullptr) {

    }
    void reset() override {
        MFMessage::reset();
        m_pool->push(this);
    }
    MFProperty(MFObjectPool<MFScheduleMessage>*, m_pool, Pool);
};

class MFShareDataMessage final : public MFMessage {
public:
    MFShareDataMessage()
    : m_pool(nullptr) {

    }
    void reset() override {
        MFMessage::reset();
        m_pool->push(this);
    }
    MFProperty(MFObjectPool<MFShareDataMessage>*, m_pool, Pool);
};


class MFForwardMessage final : public MFMessage
{
public:
    MFForwardMessage()
    : m_data(nullptr)
    , m_len(0)
    , m_pool(nullptr) {
    }

    void reset() override {
        MFMessage::reset();
        if (m_data) {
            free(m_data);
        }
        m_data = nullptr;
        m_len = 0;
        m_pool->push(this);
    }
public:
    MFProperty(void*, m_data, Data);
    MFProperty(size_t, m_len, Len);
    MFProperty(MFObjectPool<MFForwardMessage>*, m_pool, Pool);
};

class MFMultiForwardMessage final : public MFMessage {
public:
    MFMultiForwardMessage()
    : m_data(nullptr)
    , m_len(0)
    , m_pool(nullptr)
    , m_refCount(0) {
    }


    void reset() override {
        if (--m_refCount == 0) {
            MFMessage::reset();
            free(m_data);
            m_data = nullptr;
            m_len = 0;
            m_pool->push(this);
        }
    }

    MFProperty(void*, m_data, Data);
    MFProperty(size_t, m_len, Len);
    MFProperty(MFObjectPool<MFMultiForwardMessage>*, m_pool, Pool);

    void setRefCount(int value) {
        m_refCount = value;
    }
    std::atomic<uint32_t> m_refCount;
};

class MFHttpMessage final : public MFMessage {
public:
    MFHttpMessage()
    : m_fd(0)
    , m_pool(nullptr) {
    }
    void reset() override {
        MFMessage::reset();
        m_fd = 0;
        m_pool->push(this);
    }
public:
    MFProperty(size_t, m_fd, Fd);
    MFProperty(MFObjectPool<MFHttpMessage>*, m_pool, Pool);

    void setViewData(const char* data, size_t len) {
        m_viewData.assign(data, len);
    }

    const std::string& getViewData() const {
        return m_viewData;
    }

    void setIp(const std::string& ip) {
		m_ip.assign(ip.c_str(), ip.size());
    }
    const std::string& getIp() const {
        return m_ip;
	}
private:
    std::string m_viewData;
    std::string m_ip;
};

class MFSocketMessage final : public MFMessage {
public:
    MFSocketMessage()
    : m_cmd(0)
    , m_unique(0)
    , m_fd(0)
    , m_pool(nullptr) {
    }
public:
    void reset() override {
        MFMessage::reset();
        m_cmd = 0;
        m_unique = 0;
		m_viewData.clear();
        m_fd = 0;
        m_pool->push(this);
    }
    MFProperty(uint8_t, m_cmd, Cmd);
    MFProperty(uint32_t, m_unique, Unique);
    MFProperty(size_t, m_fd, Fd);
    MFProperty(MFObjectPool<MFSocketMessage>*, m_pool, Pool);

    void setViewData(const char* data, uint32_t len) {
		m_viewData.assign(data, len);
	}

    const std::string& getViewData() const {
        return m_viewData;
	}
private:
	std::string m_viewData;
};


class MFMysqlMessage final : public MFMessage {
public:
    MFMysqlMessage()
    : m_query(true)
    , m_queryOne(false)
    , m_execRes(false)
    , m_success(true)
    , m_pool(nullptr) {
    }
public:
    void reset() override {
        MFMessage::reset();
        m_query = true;
        m_queryOne = false;
        m_execRes = false;
        m_callback = nullptr;
        m_pool->push(this);
    }
    MFProperty(bool, m_query, Query)
    MFProperty(bool, m_queryOne, QueryOne)
    MFProperty(bool, m_execRes, ExecRes)
    MFProperty(bool, m_success, Success)
    MFProperty(MFObjectPool<MFMysqlMessage>*, m_pool, Pool);

    void setCallback(std::function<void(MFLuaService*)> fn) {
        m_callback = std::move(fn);
    }
    void executeCallback(MFLuaService* service) {
        if (m_callback) {
            m_callback(service);
        }
	}
private:
	std::function<void(MFLuaService*)> m_callback;
};

class MFRedisMessage final : public MFMessage {
public:
    MFRedisMessage()
    : m_pool(nullptr)
    , m_callback(nullptr) {
    }

    void reset() override {
        MFMessage::reset();
        m_callback = nullptr;
        m_pool->push(this);
    }

    void setCallback(std::function<void(MFLuaService*)> fn) {
        m_callback = std::move(fn);
    }
    void executeCallback(MFLuaService* service) {
        if (m_callback) {
            m_callback(service);
        }
    }
public:
    MFProperty(MFObjectPool<MFRedisMessage>*, m_pool, Pool);
private:
    std::function<void(MFLuaService*)> m_callback;
};

class MFHotReloadMessage final : public MFMessage {
public:
    MFHotReloadMessage()
    : m_pool(nullptr)
    , m_refCount(0) {

    }

    void reset() override {
        if (--m_refCount == 0) {
            MFMessage::reset();
            m_pool->push(this);
            m_module = "";
            m_fullPath = "";
        }
    }
public:
    MFPropertyRef(std::string, m_module, Module);
    MFPropertyRef(std::string, m_fullPath, FullPath);
    MFProperty(MFObjectPool<MFHotReloadMessage>*, m_pool, Pool);

    void setRefCount(int value) {
        m_refCount = value;
    }
    std::atomic<uint32_t> m_refCount;
};

class MFHttpClientMessage final : public MFMessage {
public:
    MFHttpClientMessage()
    : m_result(0)
    , m_pool(nullptr) {

    }
    void reset() override {
        MFMessage::reset();
        m_viewData.clear();
        m_result = 0;
        m_pool->push(this);
    }
    MFProperty(uint8_t, m_result, Result);
    MFProperty(MFObjectPool<MFHttpClientMessage>*, m_pool, Pool);

    void setViewData(const char* data) {
        m_viewData.assign(data);
	}

    const std::string& getViewData() const {
        return m_viewData;
    }
private:
	std::string m_viewData;
};

#endif //MFMessage_HPP
