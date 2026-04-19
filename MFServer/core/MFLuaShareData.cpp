#include "MFLuaShareData.hpp"
#include "MFLuaServiceManager.hpp"
#include "MFLuaMessage.hpp"

MFLuaShareData::MFLuaShareData() {
    m_shareMsgPool = new MFObjectPool<MFShareDataMessage>();
}

MFLuaShareData::~MFLuaShareData() {
    m_shareMap.for_each([](const size_t&, MFShareInfo* v) {
        delete v;
    });
    m_shareMap.clear();
    delete m_shareMsgPool;
}

void MFLuaShareData::addOrUpdate(size_t key, void* data, size_t len) {
    m_shareMap.upsert(key, [data, len](MFShareInfo*& info) {
            free(info->m_data);
            info->m_data = data;
            info->m_len = len;
        },
        new MFShareInfo(data, len)
    );
}

std::tuple<sol::object, sol::object> MFLuaShareData::get(size_t key, const sol::this_state& ts) {
    sol::state_view lua(ts);
    std::tuple<sol::object, sol::object> result = std::make_tuple(sol::lua_nil, sol::lua_nil);
    m_shareMap.find_fn(key, [&lua, &result](MFShareInfo* const& info) {
        result = std::make_tuple(
            sol::make_object(lua, sol::lightuserdata_value(info->m_data)),
            sol::make_object(lua, static_cast<lua_Integer>(info->m_len))
        );
    });
    return result;
}

void MFLuaShareData::remove(size_t key) {
    m_shareMap.erase_fn(key, [](MFShareInfo*& info) {
        delete info;
    });
}
