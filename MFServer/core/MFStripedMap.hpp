#ifndef MFStripedMap_hpp
#define MFStripedMap_hpp

#include <array>
#include <shared_mutex>
#include <vector>
#include "MFMacro.h"

inline constexpr size_t MF_CACHE_LINE_SIZE = 64;

template<typename K, typename V, size_t N = 16>
class MFStripedMap {
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");

    struct alignas(MF_CACHE_LINE_SIZE) Stripe {
        MFFastMap<K, V> map;
        mutable std::shared_mutex mtx;
    };

    using Hasher = ankerl::unordered_dense::hash<K>;

    std::array<Stripe, N> m_stripes;

    Stripe& stripeOf(const K& key) {
        return m_stripes[Hasher{}(key) & (N - 1)];
    }

    const Stripe& stripeOf(const K& key) const {
        return m_stripes[Hasher{}(key) & (N - 1)];
    }

public:
    void insert(const K& key, const V& value) {
        auto& s = stripeOf(key);
        std::unique_lock ul(s.mtx);
        s.map[key] = value;
    }

    void insert(const K& key, V&& value) {
        auto& s = stripeOf(key);
        std::unique_lock ul(s.mtx);
        s.map[key] = std::move(value);
    }

    bool erase(const K& key) {
        auto& s = stripeOf(key);
        std::unique_lock ul(s.mtx);
        return s.map.erase(key) > 0;
    }

    bool find(const K& key, V& out) const {
        auto& s = stripeOf(key);
        std::shared_lock lk(s.mtx);
        auto it = s.map.find(key);
        if (it != s.map.end()) {
            out = it->second;
            return true;
        }
        return false;
    }

    template<typename Fn>
    bool find_fn(const K& key, Fn&& fn) const {
        auto& s = stripeOf(key);
        std::shared_lock lk(s.mtx);
        auto it = s.map.find(key);
        if (it != s.map.end()) {
            fn(it->second);
            return true;
        }
        return false;
    }

    template<typename Fn>
    void upsert(const K& key, Fn&& updateFn, const V& insertValue) {
        auto& s = stripeOf(key);
        std::unique_lock ul(s.mtx);
        auto it = s.map.find(key);
        if (it != s.map.end()) {
            updateFn(it->second);
        } else {
            s.map[key] = insertValue;
        }
    }

    template<typename Fn>
    bool erase_fn(const K& key, Fn&& fn) {
        auto& s = stripeOf(key);
        std::unique_lock ul(s.mtx);
        auto it = s.map.find(key);
        if (it != s.map.end()) {
            fn(it->second);
            s.map.erase(it);
            return true;
        }
        return false;
    }

    bool extract(const K& key, V& out) {
        auto& s = stripeOf(key);
        std::unique_lock ul(s.mtx);
        auto it = s.map.find(key);
        if (it != s.map.end()) {
            out = std::move(it->second);
            s.map.erase(it);
            return true;
        }
        return false;
    }

    template<typename Fn>
    void for_each(Fn&& fn) {
        for (auto& s : m_stripes) {
            std::shared_lock lk(s.mtx);
            for (auto& [k, v] : s.map) {
                fn(k, v);
            }
        }
    }

    template<typename Fn>
    void for_each_mut(Fn&& fn) {
        for (auto& s : m_stripes) {
            std::unique_lock ul(s.mtx);
            for (auto& [k, v] : s.map) {
                fn(k, v);
            }
        }
    }

    std::vector<V> values() const {
        std::vector<V> result;
        for (auto& s : m_stripes) {
            std::shared_lock lk(s.mtx);
            result.reserve(result.size() + s.map.size());
            for (auto& [k, v] : s.map) {
                result.push_back(v);
            }
        }
        return result;
    }

    void clear() {
        for (auto& s : m_stripes) {
            std::unique_lock ul(s.mtx);
            s.map.clear();
        }
    }

    size_t size() const {
        size_t total = 0;
        for (auto& s : m_stripes) {
            std::shared_lock lk(s.mtx);
            total += s.map.size();
        }
        return total;
    }
};

#endif /* MFStripedMap_hpp */
