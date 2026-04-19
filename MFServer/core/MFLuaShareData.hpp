#ifndef MF_LUA_SHARE_DATA_H
#define MF_LUA_SHARE_DATA_H

#include "sol/sol.hpp"
#include "MFObjectPool.hpp"
#include "MFStripedMap.hpp"

class MFShareDataMessage;

class MFShareInfo {
public:
    MFShareInfo(void* data, size_t len)
    : m_data(data)
    , m_len(len) {

    }
    ~MFShareInfo() {
        if (m_data) {
            free(m_data);
            m_data = nullptr;
        }
    }
public:
    void* m_data;
    size_t m_len;
};

class MFLuaShareData {
public:
    MFLuaShareData();
    ~MFLuaShareData();
public:
    void addOrUpdate(size_t key, void* data, size_t len);
    std::tuple<sol::object, sol::object> get(size_t key, const sol::this_state& ts);
    void remove(size_t key);
public:
    MFStripedMap<size_t, MFShareInfo*>                  m_shareMap;
    MFObjectPool<MFShareDataMessage>*                   m_shareMsgPool;

};

#endif //MF_LUA_SHARE_DATA_H
