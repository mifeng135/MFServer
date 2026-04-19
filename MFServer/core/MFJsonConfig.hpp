#ifndef MF_JSON_CONFIG_HPP
#define MF_JSON_CONFIG_HPP

#include "sol/sol.hpp"
#include <json/json.h>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include "MFMacro.h"

class MFJsonConfig {
public:
    static MFJsonConfig& instance();
public:
    bool load(const std::string& fullPath, std::string& errOut);
    bool loadAllFromDirectory(const std::string& rootPath, bool recursive, std::string& errOut);
    void remove(const std::string& name);
    bool has(const std::string& name) const;
public:
    template <typename... Path>
    std::shared_ptr<const Json::Value> get(const std::string& name, Path&&... path) const {
        std::shared_lock lock(m_mutex);
        auto it = m_roots.find(name);
        if (it == m_roots.end() || !it->second) {
            return nullptr;
        }
        const std::shared_ptr<Json::Value>& rootPtr = it->second;
        const Json::Value* p = rootPtr.get();
        if constexpr (sizeof...(Path) > 0) {
            if (!(... && cppNavigateOne(p, std::forward<Path>(path)))) {
                return nullptr;
            }
            return std::shared_ptr<const Json::Value>(rootPtr, p);
        }
        return rootPtr;
    }
private:
    static bool cppNavigateOne(const Json::Value*& cur, const char* key);
    static bool cppNavigateOne(const Json::Value*& cur, const std::string& key);
    static bool cppNavigateOne(const Json::Value*& cur, std::string_view key);
    template <typename I, typename std::enable_if_t<std::is_integral_v<std::decay_t<I>> && !std::is_same_v<std::decay_t<I>, bool>, int> = 0>
    static bool cppNavigateOne(const Json::Value*& cur, I idx) {
        if (!cur->isArray()) {
            return false;
        }
        if constexpr (std::is_signed_v<std::decay_t<I>>) {
            if (idx < 0) {
                return false;
            }
        }
        const auto u = static_cast<Json::ArrayIndex>(static_cast<unsigned long long>(idx));
        if (u >= cur->size()) {
            return false;
        }
        cur = &(*cur)[u];
        return true;
    }
private:
    MFJsonConfig() = default;
    bool ingestFile(const std::string& fullPath, const std::string& configName, std::string& errOut);
private:
    mutable std::shared_mutex                               m_mutex;
    MFFastMap<std::string, std::shared_ptr<Json::Value>>    m_roots;
};

#endif
