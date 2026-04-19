#ifndef MFMysqlClient_hpp
#define MFMysqlClient_hpp

#include <string>
#include <vector>
#include <queue>

#include "trantor/net/TcpClient.h"
#include "trantor/net/EventLoopThread.h"
#include "MFValue.hpp"
#include "MFMacro.h"


class MFApplication;
class MFMysqlMessage;

// MySQL 状态
enum class MFMysqlState {
    Disconnected,
    Connecting,
    Handshaking,
    Authenticating,
    Connected,
    Querying,
    Ping,
};

// MySQL 字段类型
enum class MFMysqlFieldType : uint8_t {
    DECIMAL = 0x00,
    TINY = 0x01,
    SHORT = 0x02,
    LONG = 0x03,
    FLOAT = 0x04,
    DOUBLE = 0x05,
    NULL_TYPE = 0x06,
    TIMESTAMP = 0x07,
    LONGLONG = 0x08,
    INT24 = 0x09,
    DATE = 0x0a,
    TIME = 0x0b,
    DATETIME = 0x0c,
    YEAR = 0x0d,
    VARCHAR = 0x0f,
    BIT = 0x10,
    JSON = 0xf5,
    NEWDECIMAL = 0xf6,
    ENUM = 0xf7,
    SET = 0xf8,
    TINY_BLOB = 0xf9,
    MEDIUM_BLOB = 0xfa,
    LONG_BLOB = 0xfb,
    BLOB = 0xfc,
    VAR_STRING = 0xfd,
    STRING = 0xfe,
    GEOMETRY = 0xff
};

// 列信息
struct MFMysqlColumn {
    std::string name;           // 列名
    MFMysqlFieldType type;      // 字段类型
    uint16_t flags;             // 标志位
};

// 查询结果
struct MFMysqlResult {
public:
    MFMysqlResult(size_t sid, MFServiceId_t service)
    : affectedRows(0)
    , insertId(0)
    , sessionId(sid)
    , serviceId(service)
    , success(true) {
    }
    ~MFMysqlResult() = default;
public:
    std::vector<MFMysqlColumn> columns;  // 列信息（包含类型等详细信息）
    std::vector<std::vector<MFValue>> rows;
    uint64_t affectedRows;
    uint64_t insertId;
    std::string error;
    size_t sessionId;
    MFServiceId_t serviceId;
    bool success;
};

// 查询任务
struct MFMysqlQuery {
public:
    MFMysqlQuery(std::string s, size_t sid, MFServiceId_t serviceId, std::function<void(MFMysqlResult&& result)> fn)
    : sql(std::move(s))
    , sessionId(sid)
    , serviceId(serviceId)
    , fn(std::move(fn)) {}
public:
    std::string sql;
    size_t sessionId;
    MFServiceId_t serviceId;
    std::function<void(MFMysqlResult&& result)> fn;
};
//所有的信息处理都是再m_eventLoopThread 这个线程中进行的
class MFMysqlClient {
public:
    explicit MFMysqlClient();
    ~MFMysqlClient();
    
    // 初始化连接
    void init(const std::string& host, uint16_t port, 
              const std::string& user, const std::string& password,
              const std::string& database);
    
    // 异步查询
    void execute(const std::string& sql, MFServiceId_t serviceId, size_t sessionId, const std::function<void(MFMysqlResult&& result)> &fn);
    
    // 断开连接
    void disconnect();
    
    MFMysqlState getState() { return m_state; }
private:
    // 网络回调
    void onConnect(const trantor::TcpConnectionPtr& conn);
    void onDisconnect(const trantor::TcpConnectionPtr& conn);
    void onMessage(const trantor::TcpConnectionPtr& conn, trantor::MsgBuffer* buffer);
    
    // 协议处理
    void handleHandshake(trantor::MsgBuffer* buffer);
    void handleAuthResponse(trantor::MsgBuffer* buffer);
    void handleAuthSwitch(const std::vector<uint8_t>& packet);
    void handleQueryResponse(trantor::MsgBuffer* buffer);
    void handlePingResponse(trantor::MsgBuffer* buffer);
    // 发送数据
    void sendAuthPacket();
    void sendQueryPacket(const std::string& sql);
    void sendPacket(const std::vector<uint8_t>& data);
    void sendPingPacket();
    // 读取数据
    std::vector<uint8_t> readPacket(trantor::MsgBuffer* buffer);
    size_t computeResultSetLength(trantor::MsgBuffer* buffer, uint64_t columnCount);
    void parseResultSetFromBlock(const uint8_t* block, size_t blockSize, uint64_t columnCount, MFMysqlResult& result);
    
    // 解析结果
    void parseOkPacket(const std::vector<uint8_t>& packet, MFMysqlResult& result);
    void parseErrorPacket(const std::vector<uint8_t>& packet, MFMysqlResult& result);
    void parseResultSet(trantor::MsgBuffer* buffer, uint64_t columnCount, MFMysqlResult& result);

    MFValue parseValue(std::string&& data, MFMysqlFieldType mysqlType, uint16_t flags);
    // 工具函数
    std::string mysqlPassword(const std::string& password, const std::string& scramble);
    std::string cachingSha2Password(const std::string& password, const std::string& scramble);
    uint64_t readLengthEncodedInt(const uint8_t*& ptr);
    void skipLengthEncodedStr(const uint8_t*& ptr);
    std::string readLengthEncodedStr(const uint8_t*& ptr);
    std::string readNullStr(const uint8_t*& ptr);

    // 队列处理
    void processPendingQueries();

private:
    std::shared_ptr<trantor::TcpClient> m_client;
    trantor::TcpConnectionPtr           m_connection;
    
    // 连接信息
    std::string                         m_host;
    std::string                         m_user;
    std::string                         m_password;
    std::string                         m_database;
    
    // 状态
    MFMysqlState                        m_state;
    uint8_t                             m_packetNumber;
    uint16_t                            m_port;

    std::string                         m_scramble;
    std::string                         m_authPlugin;
    
    // 查询队列
    std::queue<MFMysqlQuery>            m_queryQueue;
    trantor::EventLoopThread*           m_eventLoopThread;
};

#endif
