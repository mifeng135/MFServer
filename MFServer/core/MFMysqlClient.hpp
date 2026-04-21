#ifndef MFMysqlClient_hpp
#define MFMysqlClient_hpp

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <memory>

#include "trantor/net/TcpClient.h"
#include "trantor/net/EventLoopThread.h"
#include "MFValue.hpp"
#include "MFMacro.h"


class MFApplication;
class MFMysqlMessage;

enum class MFMysqlState {
    Disconnected,
    Connecting,
    Handshaking,
    Authenticating,
    Connected,
    Querying,
};

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

class MFMysqlClient {
public:
    using ReadyCallback = std::function<void(MFMysqlClient*, bool success)>;
private:
    enum class QueryPhase : uint8_t {
        Initial,        // 等待第一个响应包（OK / ERR / colCount）
        Columns,        // 正在读列定义
        ColumnsEof,     // 列定义读完，等 EOF 包
        Rows,           // 正在读行数据直到 EOF
    };
public:
    explicit MFMysqlClient();
    ~MFMysqlClient();

    void init(const std::string& host, uint16_t port,
              const std::string& user, const std::string& password,
              const std::string& database, ReadyCallback onReady);

    void execute(const std::string& sql, MFServiceId_t serviceId, size_t sessionId, const std::function<void(MFMysqlResult&& result)> &fn);

    void disconnect();

    MFMysqlState getState() const { return m_state; }
    bool isIdle() const { return m_state == MFMysqlState::Connected && !m_inflight; }
private:
    void onConnect(const trantor::TcpConnectionPtr& conn);
    void onDisconnect(const trantor::TcpConnectionPtr& conn);
    void onMessage(const trantor::TcpConnectionPtr& conn, trantor::MsgBuffer* buffer);
    void setState(MFMysqlState newState);
    void fireReady(bool success);
    void failInflight(const char* reason);
private:
    void handleHandshake(trantor::MsgBuffer* buffer);
    void handleAuthResponse(trantor::MsgBuffer* buffer);
    void handleAuthSwitch(const std::vector<uint8_t>& packet);
    bool handleQueryResponse(trantor::MsgBuffer* buffer);
    void completeQuery();
private:
    void sendAuthPacket();
    void sendQueryPacket(const std::string& sql);
    void sendPacket(const std::vector<uint8_t>& data);

private:
    bool readLogicalPacket(trantor::MsgBuffer* buffer, std::vector<uint8_t>& payload);
    void parseOkPacket(const std::vector<uint8_t>& packet, MFMysqlResult& result);
    void parseErrorPacket(const std::vector<uint8_t>& packet, MFMysqlResult& result);
    bool parseColumnDefinition(const std::vector<uint8_t>& packet, MFMysqlColumn& col);
    bool parseRow(const std::vector<uint8_t>& packet, uint64_t columnCount, std::vector<MFValue>& row);
    static bool isEofPacket(const std::vector<uint8_t>& packet);
    MFValue parseValue(std::string&& data, MFMysqlFieldType mysqlType, uint16_t flags);
private:
    std::string mysqlPassword(const std::string& password, const std::string& scramble);
    std::string cachingSha2Password(const std::string& password, const std::string& scramble);
    uint64_t readLengthEncodedInt(const uint8_t*& ptr);
    void skipLengthEncodedStr(const uint8_t*& ptr);
    std::string readLengthEncodedStr(const uint8_t*& ptr);
    std::string readNullStr(const uint8_t*& ptr);
private:
    std::shared_ptr<trantor::TcpClient> m_client;
    trantor::TcpConnectionPtr           m_connection;

    std::string                         m_host;
    std::string                         m_user;
    std::string                         m_password;
    std::string                         m_database;

    MFMysqlState                        m_state;
    uint8_t                             m_packetNumber; // MySQL 包序号：发送时使用并自增；接收时同步自服务器
    uint16_t                            m_port;

    std::string                         m_scramble;
    std::string                         m_authPlugin;

    QueryPhase                          m_queryPhase;
    uint64_t                            m_columnCount;
    std::optional<MFMysqlResult>        m_pendingResult;

    std::optional<MFMysqlQuery>         m_inflight;
    ReadyCallback                       m_onReady;
    trantor::EventLoopThread*           m_eventLoopThread;
};

#endif
