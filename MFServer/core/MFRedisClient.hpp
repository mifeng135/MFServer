#ifndef MFRedisClient_hpp
#define MFRedisClient_hpp

#include <string>
#include <vector>
#include <optional>
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
    using ReadyCallback = std::function<void(MFRedisClient*, bool success)>;
public:
    MFRedisClient();
    ~MFRedisClient();
public:
    void init(const std::string& host, uint16_t port, ReadyCallback onReady, const std::string& password = "");
    void execute(std::vector<std::string> args, MFServiceId_t serviceId, size_t sessionId, const std::function<void(MFRedisResult&&)>& fn);
    void disconnect();
    MFRedisState getState() const { return m_state; }
    bool isIdle() const { return m_state == MFRedisState::Connected && !m_inflight; }
private:
    void onConnect(const trantor::TcpConnectionPtr& conn);
    void onDisconnect(const trantor::TcpConnectionPtr& conn);
    void onMessage(const trantor::TcpConnectionPtr& conn, trantor::MsgBuffer* buffer);
    void setState(MFRedisState newState);
    void fireReady(bool success);
    void failInflight(const char* reason);

    void sendCommand(const std::vector<std::string>& args);
private:
    bool parseReply(const char* p, size_t n, MFRedisReply& out, size_t& consumed);
    bool parseSimpleString(const char* p, size_t n, MFRedisReply& out, size_t& consumed);
    bool parseError(const char* p, size_t n, MFRedisReply& out, size_t& consumed);
    bool parseInteger(const char* p, size_t n, MFRedisReply& out, size_t& consumed);
    bool parseBulkString(const char* p, size_t n, MFRedisReply& out, size_t& consumed);
    bool parseArray(const char* p, size_t n, MFRedisReply& out, size_t& consumed);
    bool parseNil(const char* p, size_t n, MFRedisReply& out, size_t& consumed);
    bool parseBool(const char* p, size_t n, MFRedisReply& out, size_t& consumed);
    bool parseDouble(const char* p, size_t n, MFRedisReply& out, size_t& consumed);
    bool parseVerbatim(const char* p, size_t n, MFRedisReply& out, size_t& consumed);
    bool parseBignum(const char* p, size_t n, MFRedisReply& out, size_t& consumed);
    bool parseArrayLike(const char* p, size_t n, MFRedisReply& out, size_t& consumed, int multiplier);
    static bool findCrLf(const char* p, size_t n, size_t& lineLen);
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
    std::optional<MFRedisCommand>           m_inflight;
    ReadyCallback                           m_onReady;
    trantor::EventLoopThread*               m_eventLoopThread;
};

#endif
