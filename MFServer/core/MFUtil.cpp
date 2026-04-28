#include "MFUtil.hpp"
#include <chrono>
#include <filesystem>
#include <sstream>
#include <thread>
#include <regex>
#include "MFValue.hpp"
#include "MFApplication.hpp"
#include "MFRedisClient.hpp"

#ifdef _WIN32
    #include<Windows.h>
    #include <shlwapi.h>
    #pragma comment(lib, "shlwapi.lib")
    
    char* dirname_win(char* path) {
        static char dir[MAX_PATH];
        strcpy_s(dir, MAX_PATH, path);
        PathRemoveFileSpecA(dir);
        if (strlen(dir) == 0) {
            strcpy_s(dir, MAX_PATH, ".");
        }
        return dir;
    }
    #define dirname dirname_win
#else
    #include <libgen.h>
#endif

#include <fstream>

std::atomic<size_t> MFUtil::m_sessionId = 1;
std::atomic<size_t> MFUtil::m_fdId = 1;
std::atomic<MFServiceId_t> MFUtil::m_serviceId = 1;
std::atomic<uint32_t> MFUtil::m_timerId = 1;
std::atomic<uint32_t> MFUtil::m_convId = 10000;

long long MFUtil::getMilliseconds()
{
    auto now = std::chrono::system_clock::now();
    auto duration_in_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return duration_in_ms.count();
}

uint64_t MFUtil::getThreadId()
{
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return std::stoll(oss.str(), nullptr, 16);
}

void MFUtil::addSearchPath(const std::string& path, sol::state& state)
{
	MFUtil::addLuaSearchPath(path, state.lua_state());
}

void MFUtil::addSearchPath(const std::string& path, const sol::this_state& state) 
{
    MFUtil::addLuaSearchPath(path, state.lua_state());
}

std::string MFUtil::getCurrentPath() 
{
    std::string filePath = __FILE__;
    const char* cstr = filePath.c_str();
    char* dirName = dirname(const_cast<char*>(cstr));
    std::string projectDir(dirName);
#ifdef _WIN32
    return projectDir.substr(0, projectDir.find_last_of("\\"));
#else
    return projectDir.substr(0, projectDir.find_last_of("/"));
#endif
}

std::string MFUtil::readFile(const std::string& fileName)
{
    std::ifstream file(fileName, std::ios::binary);
    if (!file) {
        return "";
    }
    std::string content((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    file.close();
    return content;
}

bool MFUtil::isDebug()
{
#ifdef DEBUG
    return true;
#endif
    return false;
}

size_t MFUtil::genSessionId()
{
    return m_sessionId.fetch_add(1, std::memory_order_relaxed);
}

sol::object MFUtil::redisReplyToLuaObject(const MFRedisReply& reply, sol::state_view& lua)
{
    using Type = MFRedisReply::Type;
    switch (reply.type) {
        case Type::Nil:
            return sol::lua_nil;
        case Type::String:
            if (reply.str == "OK") {
                return sol::make_object(lua, true);
            }
            return sol::make_object(lua, reply.str);
        case Type::Error:
            return sol::make_object(lua, reply.str);
        case Type::Integer:
            return sol::make_object(lua, reply.integer());
        case Type::Bool:
            return sol::make_object(lua, reply.bval());
        case Type::Double:
            return sol::make_object(lua, reply.dval());
        case Type::Array: {
            sol::table arrayTable = lua.create_table();
            for (size_t i = 0; i < reply.elements.size(); ++i) {
                arrayTable[i + 1] = redisReplyToLuaObject(reply.elements[i], lua);
            }
            return arrayTable;
        }
        default:
            return sol::lua_nil;
    }
}

sol::object MFUtil::getFileList(const std::string& path, const sol::this_state& ts)
{
    std::vector<std::string> files;
    getFileRecursive(path, files);
    sol::state_view lua(ts);
    sol::table result = lua.create_table();
    int index = 1;
    for (const std::string& fileName : files) {
        result[index] = fileName;
        index++;
    }
    return result;
}


void MFUtil::rowToTable(const std::vector<MFValue> &row, sol::table &rowTable, const std::vector<MFMysqlColumn> &columns) {
    lua_State* L = rowTable.lua_state();
    rowTable.push();
    
    auto colCount = row.size();
    for (unsigned i = 0; i < colCount; ++i) {
        const MFValue &value = row[i];
        const std::string& name = columns[i].name;
        lua_pushlstring(L, name.c_str(), name.size());
        switch (value.getType()) {
            case MFValue::Type::UINT64:
                lua_pushinteger(L, static_cast<lua_Integer>(value.getUInt()));
                break;
            case MFValue::Type::INT64:
                lua_pushinteger(L, static_cast<lua_Integer>(value.getSint()));
                break;
            case MFValue::Type::FLOAT:
                lua_pushnumber(L, static_cast<lua_Number>(value.getFloat()));
                break;
            case MFValue::Type::DOUBLE:
                lua_pushnumber(L, static_cast<lua_Number>(value.getDouble()));
                break;
            case MFValue::Type::BOOL:
                lua_pushboolean(L, value.getBool());
                break;
            case MFValue::Type::STRING:
            case MFValue::Type::RAW: {
                const std::string& str = value.getString();
                lua_pushlstring(L, str.c_str(), str.size());
                break;
            }
            case MFValue::Type::VNULL:
                lua_pushnil(L);
                break;
            default: {
                const std::string& str = value.getString();
                lua_pushlstring(L, str.c_str(), str.size());
                break;
            }
        }
        
        lua_rawset(L, -3);
    }
    
    lua_pop(L, 1); 
}

sol::object MFUtil::rowsToLuaTable(const std::vector<std::vector<MFValue>>& rows, const std::vector<MFMysqlColumn>& columns, lua_State* L) {
    int colCount = static_cast<int>(columns.size());
    int rowCount = static_cast<int>(rows.size());
    
    lua_createtable(L, rowCount, 0); 
    int outerTable = lua_gettop(L);
    
    for (int r = 0; r < rowCount; ++r) {
        lua_createtable(L, 0, colCount);
        
        const std::vector<MFValue>& row = rows[r];
        for (int c = 0; c < colCount && c < static_cast<int>(row.size()); ++c) {
            const std::string& name = columns[c].name;
            const MFValue& value = row[c];
            
            lua_pushlstring(L, name.c_str(), name.size());
            
            switch (value.getType()) {
                case MFValue::Type::UINT64:
                    lua_pushinteger(L, static_cast<lua_Integer>(value.getUInt()));
                    break;
                case MFValue::Type::INT64:
                    lua_pushinteger(L, static_cast<lua_Integer>(value.getSint()));
                    break;
                case MFValue::Type::FLOAT:
                    lua_pushnumber(L, static_cast<lua_Number>(value.getFloat()));
                    break;
                case MFValue::Type::DOUBLE:
                    lua_pushnumber(L, static_cast<lua_Number>(value.getDouble()));
                    break;
                case MFValue::Type::BOOL:
                    lua_pushboolean(L, value.getBool());
                    break;
                case MFValue::Type::STRING:
                case MFValue::Type::RAW: {
                    const std::string& str = value.getString();
                    lua_pushlstring(L, str.c_str(), str.size());
                    break;
                }
                case MFValue::Type::VNULL:
                    lua_pushnil(L);
                    break;
                default: {
                    const std::string& str = value.getString();
                    lua_pushlstring(L, str.c_str(), str.size());
                    break;
                }
            }
            
            lua_rawset(L, -3);
        }
        lua_rawseti(L, outerTable, static_cast<lua_Integer>(r) + 1);
    }
    return sol::stack::pop<sol::object>(L);
}

bool MFUtil::isLinux() {
#ifdef __linux__
    return true;
#endif
    return false;
}

bool MFUtil::isWindows() {
#ifdef _WIN32
    return true;
#endif
	return false;
}

std::vector<std::string> MFUtil::splitString(const std::string &str, const std::string &regex) {
    if (str == "" || regex == "") {
        return {};
    }
    std::regex re(regex);
    std::sregex_token_iterator iter(str.begin(), str.end(), re, -1);
    std::sregex_token_iterator end;

    std::vector<std::string> tokens(iter, end);
    return tokens;
}

std::string MFUtil::trim(const std::string &s, const std::string &whitespace) {
    auto start = s.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }
    auto end = s.find_last_not_of(whitespace);
    return s.substr(start, end - start + 1);
}

bool MFUtil::writeFile(std::string_view filePath, std::string_view content) {
    try {
        std::filesystem::path path(filePath);
        std::filesystem::path dir = path.parent_path();
        if (!dir.empty() && !std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }
        std::ofstream file((filePath.data()), std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }

        file.write(content.data(), content.size());
        file.close();

        return file.good();
    } catch (const std::exception& e) {
        MFApplication::getInstance()->logInfo("Failed to write file {}: {}", filePath, e.what());
        return false;
    }
}

size_t MFUtil::genConnectFd() {
    return m_fdId.fetch_add(1, std::memory_order_relaxed);
}

MFServiceId_t MFUtil::genServiceId() {
    return m_serviceId.fetch_add(1, std::memory_order_relaxed);
}

bool MFUtil::checkFileExists(const std::string &fullPath) {
    return std::filesystem::exists(fullPath);
}

uint32_t MFUtil::genTimerId() {
    return m_timerId.fetch_add(1, std::memory_order_relaxed);
}

uint32_t MFUtil::genConvId() {
    return m_convId.fetch_add(1, std::memory_order_relaxed);
}

uint32_t MFUtil::readUint32(const char *buf) {
    const unsigned char *p = reinterpret_cast<const unsigned char *>(buf);
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

void MFUtil::preloadSharedTable(const std::string& rootDir)
{
    std::vector<std::string> fileFullPathVec;
    getFileRecursive(rootDir, fileFullPathVec);
    for (const std::string& fullPath : fileFullPathVec) {
        std::string prefix = getPathPrefix(fullPath);
        if (prefix != ".lua") {
            continue;
        }
	    std::string moduleName = getFileName(fullPath);
        int result = luaP_sharetable(moduleName.c_str(), fullPath.c_str());
        if (result != 0) {
            MFApplication::getInstance()->logInfo("Failed to preload shared table from {}: error code {}", moduleName, result);
        }
	}
}

uint32_t MFUtil::iclock()
{
    return static_cast<uint32_t>(MFUtil::getMilliseconds() & 0xfffffffful);
}

std::string MFUtil::getPathPrefix(const std::filesystem::path& p)
{
    return p.extension().string();
}

std::string MFUtil::getFileName(const std::filesystem::path& p)
{
    return p.stem().string();
}

void MFUtil::getFileRecursive(const std::filesystem::path& dir, std::vector<std::string>& allFile) {
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (!filename.empty() && filename[0] != '.') {
                    allFile.emplace_back(entry.path().string());
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        MFApplication::getInstance()->logInfo("Filesystem error while accessing {}: {}", dir.string(), e.what());
	}
}

void MFUtil::addLuaSearchPath(const std::string& path, lua_State* state)
{
    lua_getglobal(state, "package");
    lua_getfield(state, -1, "path");
    const char* curPath = lua_tostring(state, -1);
    lua_pushfstring(state, "%s;%s/?.lua", curPath, path.c_str());
    lua_setfield(state, -3, "path");
    lua_pop(state, 2);
}
