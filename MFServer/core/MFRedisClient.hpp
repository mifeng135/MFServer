#ifndef MFRedisClient_hpp
#define MFRedisClient_hpp

#include <string>
#include <vector>
#include <queue>
#include <functional>
#include <memory>

#include "trantor/net/TcpClient.h"
#include "trantor/net/EventLoopThread.h"
#include "MFMacro.h"


class MFApplication;

enum class MFRedisState {
    Disconnected,
    Connecting,
    Authenticating,
    Connected,
    Commanding,
};

struct MFRedisReply {
    enum class Type {
        Nil,
        String,
        Integer,
        Array,
        Error,
        Bool,
        Double
    };

    Type type = Type::Nil;
    std::string str;
    union {
        int64_t integer;
        bool bval;
        double dval;
    } u_{};
    std::vector<MFRedisReply> elements;
public:
    void setStr(std::string&& s);
    void setStr(const char* p, size_t n);
    void setInteger(int64_t i);
    void setBool(bool b);
    void setDouble(double d);
    void setNil();
    void setError(std::string s);
    void setError(const char* s);
    void setError(const char* p, size_t n);
    void setTypeArray();

    int64_t integer() const { return u_.integer; }
    bool bval() const { return u_.bval; }
    double dval() const { return u_.dval; }

    bool isNil() const { return type == Type::Nil; }
    bool isString() const { return type == Type::String; }
    bool isInteger() const { return type == Type::Integer; }
    bool isArray() const { return type == Type::Array; }
    bool isError() const { return type == Type::Error; }
    bool isBool() const { return type == Type::Bool; }
    bool isDouble() const { return type == Type::Double; }
};

struct MFRedisResult {
    MFRedisResult(size_t sid, MFServiceId_t svc)
    : success(true)
    , sessionId(sid)
    , serviceId(svc) {
    }
public:
    bool success;
    std::string error;
    MFRedisReply reply;
    size_t sessionId;
    MFServiceId_t serviceId;
};

struct MFRedisCommand {
    MFRedisCommand(std::vector<std::string> args, size_t sid, MFServiceId_t svc, std::function<void(MFRedisResult&&)> cb)
    : args(std::move(args))
    , sessionId(sid)
    , serviceId(svc)
    , fn(std::move(cb)) {
    }
public:
    std::vector<std::string> args;
    size_t sessionId;
    MFServiceId_t serviceId;
    std::function<void(MFRedisResult&&)> fn;
};

class MFRedisClient {
public:
    MFRedisClient();
    ~MFRedisClient();
public:
    void init(const std::string& host, uint16_t port, const std::string& password = "");
    void execute(const std::vector<std::string>& args, MFServiceId_t serviceId, size_t sessionId, const std::function<void(MFRedisResult&&)>& fn);
    void disconnect();
    MFRedisState getState() const { return m_state; }
private:
    void onConnect(const trantor::TcpConnectionPtr& conn);
    void onDisconnect(const trantor::TcpConnectionPtr& conn);
    void onMessage(const trantor::TcpConnectionPtr& conn, trantor::MsgBuffer* buffer);

    void sendCommand(const std::vector<std::string>& args);
    void processPendingCommands();

    bool parseReply(trantor::MsgBuffer* buffer, MFRedisReply& out);
    bool parseSimpleString(trantor::MsgBuffer* buffer, MFRedisReply& out);
    bool parseError(trantor::MsgBuffer* buffer, MFRedisReply& out);
    bool parseInteger(trantor::MsgBuffer* buffer, MFRedisReply& out);
    bool parseBulkString(trantor::MsgBuffer* buffer, MFRedisReply& out);
    bool parseArray(trantor::MsgBuffer* buffer, MFRedisReply& out);
    bool parseNil(trantor::MsgBuffer* buffer, MFRedisReply& out);
    bool parseBool(trantor::MsgBuffer* buffer, MFRedisReply& out);
    bool parseDouble(trantor::MsgBuffer* buffer, MFRedisReply& out);
    bool parseVerbatim(trantor::MsgBuffer* buffer, MFRedisReply& out);
    bool parseBignum(trantor::MsgBuffer* buffer, MFRedisReply& out);
    bool parseArrayLike(trantor::MsgBuffer* buffer, MFRedisReply& out, int multiplier);
    bool readLine(trantor::MsgBuffer* buffer, const char*& linePtr, size_t& lineLen);
    void setProtocolError(MFRedisReply& out, const char* msg);
    void setProtocolErrorByte(MFRedisReply& out, char byte);

public:
    std::chrono::steady_clock::time_point   createTime;
    std::chrono::steady_clock::time_point   lastUsedTime;
private:
    std::shared_ptr<trantor::TcpClient>     m_client;
    trantor::TcpConnectionPtr               m_connection;

    std::string                             m_host;
    std::string                             m_password;
    uint16_t                                m_port;

    MFRedisState                            m_state;
    std::queue<MFRedisCommand>              m_commandQueue;
    trantor::EventLoopThread*               m_eventLoopThread;
};

#endif
