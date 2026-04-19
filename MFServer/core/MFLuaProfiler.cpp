#include "MFLuaProfiler.hpp"
#include "MFApplication.hpp"

#include <cstdio>

static const int kProfilerRegistryKey = 0;

MFLuaProfiler::MFLuaProfiler(lua_State* L, std::string serviceName)
: m_L(L)
, m_running(false)
, m_callDepth(0)
, m_skipReturns(0) 
, m_serviceName(std::move(serviceName)) {
}

MFLuaProfiler::~MFLuaProfiler() {
    stop();
}

uint64_t MFLuaProfiler::nowUs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

size_t MFLuaProfiler::luaMemBytes() const {
    int kb = lua_gc(m_L, LUA_GCCOUNT, 0);
    int b = lua_gc(m_L, LUA_GCCOUNTB, 0);
    return static_cast<size_t>(kb) * 1024 + static_cast<size_t>(b);
}

uint64_t MFLuaProfiler::computeFuncKey(const char* source, int lineDefined) {
    uint64_t key = ankerl::unordered_dense::hash<const void*>{}(source);
    key ^= ankerl::unordered_dense::hash<int>{}(lineDefined) + 0x9e3779b9 + (key << 6) + (key >> 2);
    return key;
}

void MFLuaProfiler::start() {
    if (m_running) {
        return;
    }
    m_running = true;
    m_callDepth = 0;
    m_skipReturns = 0;

    lua_Debug ar;
    struct StackEntry {
        lua_Debug ar;
        bool isC;
        std::string resolvedName;
    };
    std::vector<StackEntry> stack;
    for (int level = 0; lua_getstack(m_L, level, &ar); ++level) {
        lua_getinfo(m_L, "nSl", &ar);
        StackEntry e;
        e.ar = ar;
        e.isC = (ar.what != nullptr && ar.what[0] == 'C');
        if (ar.name != nullptr) {
            e.resolvedName = ar.name;
        } else if (!e.isC) {
            lua_getinfo(m_L, "f", &ar);
            lua_pushglobaltable(m_L);
            lua_pushnil(m_L);
            while (lua_next(m_L, -2)) {
                if (lua_rawequal(m_L, -1, -4)) {
                    const char* key = lua_tostring(m_L, -2);
                    if (key) e.resolvedName = key;
                    lua_pop(m_L, 2);
                    break;
                }
                lua_pop(m_L, 1);
            }
            lua_pop(m_L, 2);
        }
        stack.push_back(std::move(e));
    }

    // Push Lua frames from deepest (bottom of stack) to shallowest, skip C frames
    for (int i = static_cast<int>(stack.size()) - 1; i >= 0 && m_callDepth < kMaxCallDepth; --i) {
        if (stack[i].isC) {
            continue;
        }
        auto& d = stack[i].ar;
        const char* name = stack[i].resolvedName.empty() ? nullptr : stack[i].resolvedName.c_str();
        int reportLine = d.currentline > 0 ? d.currentline : d.linedefined;
        uint64_t key = computeFuncKey(d.short_src, reportLine);

        ProfileTreeNode* parent = (m_callDepth == 0) ? &m_treeRoot : m_callStack[m_callDepth - 1].treeNode;
        ProfileTreeNode* node = getOrCreateTreeChild(parent, key, name, d.short_src, reportLine);

        CallFrame& frame = m_callStack[m_callDepth++];
        frame.enterTimeUs = nowUs();
        frame.enterMem = luaMemBytes();
        frame.treeNode = node;
    }

    // Count C frames above the shallowest Lua frame — their RET hooks must be skipped
    for (int i = 0; i < static_cast<int>(stack.size()); ++i) {
        if (stack[i].isC) {
            ++m_skipReturns;
        } else {
            break;
        }
    }

    lua_gc(m_L, LUA_GCSTOP, 0);

    lua_pushlightuserdata(m_L, this);
    lua_rawsetp(m_L, LUA_REGISTRYINDEX, &kProfilerRegistryKey);
    lua_sethook(m_L, hookCallback, LUA_MASKCALL | LUA_MASKRET, 0);
}

void MFLuaProfiler::stop() {
    if (!m_running) {
        return;
    }
    m_running = false;
    lua_sethook(m_L, nullptr, 0, 0);
    lua_gc(m_L, LUA_GCRESTART, 0);

    uint64_t now = nowUs();
    size_t mem = luaMemBytes();
    while (m_callDepth > 0) {
        CallFrame& frame = m_callStack[--m_callDepth];
        uint64_t elapsed = now - frame.enterTimeUs;
        int64_t memDelta = static_cast<int64_t>(mem) - static_cast<int64_t>(frame.enterMem);

        auto* treeNode = frame.treeNode;
        treeNode->callCount++;
        treeNode->totalTimeUs += elapsed;
        if (elapsed > treeNode->maxTimeUs) {
            treeNode->maxTimeUs = elapsed;
        }
        treeNode->totalMemDelta += memDelta;
        if (memDelta > treeNode->maxMemDelta) {
            treeNode->maxMemDelta = memDelta;
        }
    }

    m_skipReturns = 0;
    lua_pushnil(m_L);
    lua_rawsetp(m_L, LUA_REGISTRYINDEX, &kProfilerRegistryKey);
}

void MFLuaProfiler::reset() {
    m_callDepth = 0;
    m_skipReturns = 0;
    m_treeRoot = ProfileTreeNode{};
    m_treeNodePool.clear();
}

ProfileTreeNode* MFLuaProfiler::getOrCreateTreeChild(
    ProfileTreeNode* parent, uint64_t key,
    const char* name, const char* src, int line)
{
    auto it = parent->childIndex.find(key);
    if (it != parent->childIndex.end()) {
        return parent->children[it->second];
    }
    m_treeNodePool.emplace_back();
    auto* node = &m_treeNodePool.back();
    node->funcName = name ? name : "(anonymous)";
    node->source = src ? src : "?";
    node->lineDefined = line;
    int idx = static_cast<int>(parent->children.size());
    parent->children.push_back(node);
    parent->childIndex[key] = idx;
    return node;
}

void MFLuaProfiler::hookCallback(lua_State* L, lua_Debug* ar) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kProfilerRegistryKey);
    MFLuaProfiler* self = static_cast<MFLuaProfiler*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!self) return;

    if (ar->event == LUA_HOOKCALL || ar->event == LUA_HOOKTAILCALL) {
        if (ar->event == LUA_HOOKTAILCALL && self->m_callDepth > 0) {
            self->onReturn();
        }
        self->onCall(L, ar);
    } else if (ar->event == LUA_HOOKRET) {
        self->onReturn();
    }
}

void MFLuaProfiler::onCall(lua_State* L, lua_Debug* ar) {
    if (m_callDepth >= kMaxCallDepth) {
        return;
    }

    lua_getinfo(L, "Sl", ar);
    int reportLine = ar->currentline > 0 ? ar->currentline : ar->linedefined;
    uint64_t key = computeFuncKey(ar->short_src, reportLine);

    ProfileTreeNode* parent = (m_callDepth == 0) ? &m_treeRoot : m_callStack[m_callDepth - 1].treeNode;
    auto childIt = parent->childIndex.find(key);
    ProfileTreeNode* node;

    if (childIt != parent->childIndex.end()) {
        node = parent->children[childIt->second];
    } else {
        lua_getinfo(L, "n", ar);
        const char* callSrc = ar->short_src;
        int callLine = reportLine;
        lua_Debug caller;
        if (lua_getstack(L, 1, &caller) && lua_getinfo(L, "Sl", &caller)) {
            callSrc = caller.short_src;
            if (caller.currentline > 0) {
                callLine = caller.currentline;
            }
        }
        node = getOrCreateTreeChild(parent, key, ar->name, callSrc, callLine);
    }

    CallFrame& frame = m_callStack[m_callDepth++];
    frame.enterTimeUs = nowUs();
    frame.enterMem = luaMemBytes();
    frame.treeNode = node;
}

void MFLuaProfiler::onReturn() {
    if (m_skipReturns > 0) {
        --m_skipReturns;
        return;
    }
    if (m_callDepth <= 0) {
        return;
    }

    CallFrame& frame = m_callStack[--m_callDepth];
    uint64_t elapsed = nowUs() - frame.enterTimeUs;
    int64_t memDelta = static_cast<int64_t>(luaMemBytes()) - static_cast<int64_t>(frame.enterMem);

    auto* treeNode = frame.treeNode;
    treeNode->callCount++;
    treeNode->totalTimeUs += elapsed;

    if (elapsed > treeNode->maxTimeUs) {
        treeNode->maxTimeUs = elapsed;
    }
    treeNode->totalMemDelta += memDelta;

    if (memDelta > treeNode->maxMemDelta) {
        treeNode->maxMemDelta = memDelta;
    }
}

std::string MFLuaProfiler::formatTime(uint64_t us) {
    char buf[32];
    if (us < 1000) {
        snprintf(buf, sizeof(buf), "%lluus", static_cast<unsigned long long>(us));
    } else if (us < 1000000) {
        snprintf(buf, sizeof(buf), "%.1fms", us / 1000.0);
    } else {
        snprintf(buf, sizeof(buf), "%.2fs", us / 1000000.0);
    }
    return buf;
}

std::string MFLuaProfiler::formatBytes(int64_t bytes) {
    char buf[32];
    const char* sign = bytes < 0 ? "-" : "+";
    double abs = static_cast<double>(bytes < 0 ? -bytes : bytes);
    const char* units[] = {"B", "KB", "MB", "GB"};
    int idx = 0;
    while (abs >= 1024.0 && idx < 3) {
        abs /= 1024.0;
        idx++;
    }
    snprintf(buf, sizeof(buf), "%s%.1f%s", sign, abs, units[idx]);
    return buf;
}

static void appendPadRight(std::string& out, const char* s, int width) {
    int len = static_cast<int>(strlen(s));
    out.append(s, len);
    if (len < width) {
        out.append(width - len, ' ');
    }
}

static void appendPadLeft(std::string& out, const char* s, int width) {
    int len = static_cast<int>(strlen(s));
    if (len < width) {
        out.append(width - len, ' ');
    }
    out.append(s, len);
}

static std::string shortSource(const std::string& source) {
    auto pos = source.find_last_of("/\\");
    if (pos != std::string::npos) {
        return source.substr(pos + 1);
    }
    return source;
}

static void printTreeNodeImpl(std::string& out, const ProfileTreeNode* node,
                               const std::string& prefix, bool isLast,
                               int depth, int maxDepth)
{
    if (depth > maxDepth) return;

    static const char* kBranch   = "|-- ";
    static const char* kCorner   = "+-- ";
    static const char* kVertical = "|   ";
    static const char* kBlank    = "    ";

    const char* connector    = isLast ? kCorner : kBranch;
    const char* continuation = isLast ? kBlank  : kVertical;

    int nameDisplayWidth = depth * 4 + static_cast<int>(node->funcName.size());
    const int kNameCol = 30;
    int pad = kNameCol - nameDisplayWidth;
    if (pad < 1) pad = 1;

    out += prefix;
    out += connector;
    out += node->funcName;
    out.append(pad, ' ');

    char buf[256];
    uint64_t avg = node->callCount > 0 ? node->totalTimeUs / node->callCount : 0;
    snprintf(buf, sizeof(buf), "%6u %10s %10s %10s %10s",
        node->callCount,
        MFLuaProfiler::formatTime(node->totalTimeUs).c_str(),
        MFLuaProfiler::formatTime(avg).c_str(),
        MFLuaProfiler::formatTime(node->maxTimeUs).c_str(),
        MFLuaProfiler::formatBytes(node->totalMemDelta).c_str());
    out += buf;

    std::string src = shortSource(node->source);
    if (node->lineDefined > 0) {
        snprintf(buf, sizeof(buf), "  %s:%d", src.c_str(), node->lineDefined);
        out += buf;
    } else if (src != "?") {
        out += "  ";
        out += src;
    }
    out += '\n';

    std::string childPrefix = prefix + continuation;
    const auto& children = node->children;

    for (size_t i = 0; i < children.size(); ++i) {
        printTreeNodeImpl(out, children[i], childPrefix, i == children.size() - 1,
                          depth + 1, maxDepth);
    }
}

std::string MFLuaProfiler::reportTree(int maxDepth) const {
    std::string out;
    out.reserve(4096);

    out += "===================<" + m_serviceName + ">====================\n";
    appendPadRight(out, "Function", 30);
    appendPadLeft(out, "Calls", 6);
    appendPadLeft(out, "Total", 10);
    appendPadLeft(out, "Avg", 10);
    appendPadLeft(out, "Max", 10);
    appendPadLeft(out, "Mem", 10);
    out += "        Source\n";
    out.append(90, '-');
    out += '\n';

    const auto& roots = m_treeRoot.children;
    for (size_t i = 0; i < roots.size(); ++i) {
        printTreeNodeImpl(out, roots[i], "", i == roots.size() - 1, 1, maxDepth);
    }

    return out;
}

