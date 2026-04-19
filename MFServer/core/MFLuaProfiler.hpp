#ifndef MFLuaProfiler_hpp
#define MFLuaProfiler_hpp

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include "MFMacro.h"
#include "sol/sol.hpp"

struct ProfileTreeNode {
    std::string funcName;
    std::string source;
    int lineDefined = 0;
    uint32_t callCount = 0;
    uint64_t totalTimeUs = 0;
    uint64_t maxTimeUs = 0;
    int64_t totalMemDelta = 0;
    int64_t maxMemDelta = 0;
    std::vector<ProfileTreeNode*> children;
    MFFastMap<uint64_t, int> childIndex;
};

class MFLuaProfiler {
    static constexpr int kMaxCallDepth = 128;

    struct CallFrame {
        uint64_t enterTimeUs;
        size_t enterMem;
        ProfileTreeNode* treeNode;
    };

public:
    explicit MFLuaProfiler(lua_State* L, std::string serviceName);
    ~MFLuaProfiler();

    void start();
    void stop();
    void reset();
    bool isRunning() const { return m_running; }

    std::string reportTree(int maxDepth = 20) const;

    static std::string formatTime(uint64_t us);
    static std::string formatBytes(int64_t bytes);

private:
    static void hookCallback(lua_State* L, lua_Debug* ar);
    void onCall(lua_State* L, lua_Debug* ar);
    void onReturn();

    static uint64_t nowUs();
    size_t luaMemBytes() const;

    static uint64_t computeFuncKey(const char* source, int lineDefined);
    ProfileTreeNode* getOrCreateTreeChild(ProfileTreeNode* parent, uint64_t key, const char* name, const char* src, int line);
private:
    lua_State*                  m_L;
    bool                        m_running;
    int                         m_callDepth;
    int                         m_skipReturns;
    CallFrame                   m_callStack[kMaxCallDepth];
    ProfileTreeNode             m_treeRoot;
    std::deque<ProfileTreeNode> m_treeNodePool;
	std::string					m_serviceName; 
};

#endif
