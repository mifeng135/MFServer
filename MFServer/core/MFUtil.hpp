#ifndef MFUtil_hpp
#define MFUtil_hpp

#include <string>
#include <filesystem>

#include "sol/sol.hpp"
#include "MFMysqlClient.hpp"

struct MFRedisReply;

class MFUtil
{
public:
    static long long getMilliseconds();
    static uint64_t getThreadId();
    static void addSearchPath(const std::string& path, sol::state& state);
    static void addSearchPath(const std::string& path, const sol::this_state& state);
    static std::string getCurrentPath();
    static std::string readFile(const std::string& fileName);
    static bool isDebug();
    static size_t genSessionId();
    static sol::object redisReplyToLuaObject(const MFRedisReply& reply, sol::state_view& lua);
    static sol::object getFileList(const std::string& path, const sol::this_state& ts);
    static sol::object rowsToLuaTable(const std::vector<std::vector<MFValue>>& rows, const std::vector<MFMysqlColumn>& columns, lua_State* L);
    static void rowToTable(const std::vector<MFValue>& row, sol::table& rowTable, const std::vector<MFMysqlColumn>& columns);
    static bool isLinux();
	static bool isWindows();
    static std::vector<std::string> splitString(const std::string& str, const std::string& regex);
    static std::string trim(const std::string& s, const std::string& whitespace = " \t\n\r\f\v");
    static bool writeFile(std::string_view filePath, std::string_view content);
    static size_t genConnectFd();
    static MFServiceId_t genServiceId();
    static bool checkFileExists(const std::string& fullPath);
    static uint32_t genTimerId();
	static uint32_t genConvId();
    static unsigned int readUint32(const char *buf);
	static void preloadSharedTable(const std::string& rootDir);
public:
	static std::string getPathPrefix(const std::filesystem::path& p);
	static std::string getFileName(const std::filesystem::path& p);
    static void getFileRecursive(const std::filesystem::path& dir, std::vector<std::string>& allFile);
private:
    static void addLuaSearchPath(const std::string& path, lua_State* state);
private:
    static std::atomic<size_t>              m_sessionId;
    static std::atomic<size_t>              m_fdId;
    static std::atomic<MFServiceId_t>       m_serviceId;
    static std::atomic<uint32_t>            m_timerId;
    static std::atomic<uint32_t>            m_convId;
};



#endif /* MFUtil_hpp */
