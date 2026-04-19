#include "MFRedisClient.hpp"
#include "MFApplication.hpp"

#include <charconv>
#include <cmath>
#include <algorithm>
#include <limits>

// --- MFRedisReply ---
void MFRedisReply::setStr(std::string&& s) {
    type = Type::String;
    str = std::move(s);
}

void MFRedisReply::setStr(const char* p, size_t n) {
    type = Type::String;
    str.assign(p, n);
}

void MFRedisReply::setInteger(int64_t i) {
    type = Type::Integer;
    u_.integer = i;
}

void MFRedisReply::setBool(bool b) {
    type = Type::Bool;
    u_.bval = b;
}

void MFRedisReply::setDouble(double d) {
    type = Type::Double;
    u_.dval = d;
}

void MFRedisReply::setNil() {
    type = Type::Nil;
    str.clear();
}

void MFRedisReply::setError(std::string s) {
    type = Type::Error;
    str = std::move(s);
}

void MFRedisReply::setError(const char* s) {
    type = Type::Error;
    str = s;
}

void MFRedisReply::setError(const char* p, size_t n) {
    type = Type::Error;
    str.assign(p, n);
}

void MFRedisReply::setTypeArray() {
    type = Type::Array;
}

// --- MFRedisClient ---
MFRedisClient::MFRedisClient()
: m_port(6379)
, m_state(MFRedisState::Disconnected)
, m_eventLoopThread(nullptr) {
}

MFRedisClient::~MFRedisClient() {
    disconnect();
    m_client.reset();
    m_eventLoopThread->getLoop()->quit();
    delete m_eventLoopThread;
}

void MFRedisClient::init(const std::string& host, uint16_t port, const std::string& password) {
    m_host = host;
    m_port = port;
    m_password = password;

    m_eventLoopThread = new trantor::EventLoopThread("MFRedisClientEventLoop");
    m_eventLoopThread->run();

    trantor::InetAddress addr(host, port);
    m_client = std::make_shared<trantor::TcpClient>(m_eventLoopThread->getLoop(), addr, "RedisClient");

    m_client->setConnectionCallback([this](const trantor::TcpConnectionPtr& conn) {
        conn->connected() ? onConnect(conn) : onDisconnect(conn);
    });

    m_client->setMessageCallback([this](const trantor::TcpConnectionPtr& conn, trantor::MsgBuffer* buf) {
        onMessage(conn, buf);
    });

    m_client->setConnectionErrorCallback([this]() {
        MFApplication::getInstance()->logInfo("Redis connected fail from {}:{}", m_host, m_port);
    });

    m_state = MFRedisState::Connecting;
    m_client->connect();
    m_client->enableRetry();
}

void MFRedisClient::onConnect(const trantor::TcpConnectionPtr& conn) {
    m_connection = conn;
    m_connection->setTcpNoDelay(true);

    if (!m_password.empty()) {
        m_state = MFRedisState::Authenticating;
        sendCommand({"AUTH", m_password});
    } else {
        MFApplication::getInstance()->logInfo("Redis connected to {}:{}", m_host, m_port);
        m_state = MFRedisState::Connected;
        processPendingCommands();
    }
}

void MFRedisClient::onDisconnect(const trantor::TcpConnectionPtr& conn) {
    MFApplication::getInstance()->logInfo("Redis disconnected from {}:{}", m_host, m_port);
    m_connection = nullptr;
    m_state = MFRedisState::Disconnected;

    while (!m_commandQueue.empty()) {
        MFRedisCommand cmd = std::move(m_commandQueue.front());
        m_commandQueue.pop();
        MFRedisResult result(cmd.sessionId, cmd.serviceId);
        result.success = false;
        result.error = "Connection lost";
        cmd.fn(std::move(result));
    }
}

void MFRedisClient::onMessage(const trantor::TcpConnectionPtr& conn, trantor::MsgBuffer* buffer) {
    try {
        switch (m_state) {
            case MFRedisState::Disconnected:
            case MFRedisState::Connecting:
            case MFRedisState::Connected: {
                break;
            }
            case MFRedisState::Authenticating: {
                MFRedisReply reply;
                if (!parseReply(buffer, reply)) {
                    return;
                }
                if (reply.isString() && reply.str == "OK") {
                    MFApplication::getInstance()->logInfo("Redis AUTH OK {}:{}", m_host, m_port);
                    m_state = MFRedisState::Connected;
                    processPendingCommands();
                } else if (reply.isError() || (reply.isString() && reply.str != "OK")) {
                    MFApplication::getInstance()->logInfo("Redis AUTH failed {}:{}", m_host, m_port);
                    disconnect();
                } else {
                    m_state = MFRedisState::Connected;
                    processPendingCommands();
                }
                break;
            }
            case MFRedisState::Commanding: {
                while (!m_commandQueue.empty() && buffer->readableBytes() >= 1) {
                    MFRedisResult result(m_commandQueue.front().sessionId, m_commandQueue.front().serviceId);
                    if (!parseReply(buffer, result.reply)) {
                        break;
                    }
                    MFRedisCommand cmd = std::move(m_commandQueue.front());
                    m_commandQueue.pop();

                    result.success = !result.reply.isError();
                    if (result.reply.isError()) {
                        result.error = result.reply.str;
                    }
                    cmd.fn(std::move(result));
                }
				bool isEmpty = m_commandQueue.empty();
                m_state = isEmpty ? MFRedisState::Connected : MFRedisState::Commanding;
                if (!isEmpty) {
                    const MFRedisCommand& next = m_commandQueue.front();
                    sendCommand(next.args);
                }
                break;
            }
        }
    } catch (const std::exception& e) {
        MFApplication::getInstance()->logInfo("Redis onMessage error: {}", e.what());
        disconnect();
    }
}

bool MFRedisClient::readLine(trantor::MsgBuffer* buffer, const char*& linePtr, size_t& lineLen) {
    const char* start = buffer->peek();
    size_t n = buffer->readableBytes();
    for (size_t i = 0; i + 1 < n; ++i) {
        if (start[i] == '\r' && start[i + 1] == '\n') {
            linePtr = start;
            lineLen = i;
            return true;
        }
    }
    return false;
}

void MFRedisClient::setProtocolError(MFRedisReply& out, const char* msg) {
    out.setError(msg);
}

void MFRedisClient::setProtocolErrorByte(MFRedisReply& out, char byte) {
    if (byte == '\r' || byte == '\n' || byte == '"' || byte == '\\' || std::isprint(static_cast<unsigned char>(byte))) {
        out.setError("Protocol error, got \"" + std::string(1, byte) + "\" as reply type byte");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "Protocol error, got \"\\x%02x\" as reply type byte", static_cast<unsigned char>(byte));
        out.setError(buf);
    }
}

static bool hasCrLf(const char* p, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (p[i] == '\r' || p[i] == '\n') {
            return true;
        }
    }
    return false;
}

static void consumeLine(trantor::MsgBuffer* buffer, size_t lineLen) {
    buffer->retrieve(lineLen + 2);
}

bool MFRedisClient::parseSimpleString(trantor::MsgBuffer* buffer, MFRedisReply& out) {
    const char* p;
    size_t len;
    if (!readLine(buffer, p, len)) return false;
    if (hasCrLf(p, len)) {
        setProtocolError(out, "Bad simple string value");
        consumeLine(buffer, len);
        return true;
    }
    out.setStr(p, len);
    consumeLine(buffer, len);
    return true;
}

bool MFRedisClient::parseError(trantor::MsgBuffer* buffer, MFRedisReply& out) {
    const char* p;
    size_t len;
    if (!readLine(buffer, p, len)) {
        return false;
    } 
    if (hasCrLf(p, len)) {
        setProtocolError(out, "Bad simple string value");
        consumeLine(buffer, len);
        return true;
    }
    out.setError(p, len);
    consumeLine(buffer, len);
    return true;
}

bool MFRedisClient::parseInteger(trantor::MsgBuffer* buffer, MFRedisReply& out) {
    const char* p;
    size_t len;
    if (!readLine(buffer, p, len)) {
        return false;
    }
    int64_t val = 0;
    auto [ptr, ec] = std::from_chars(p, p + len, val);
    consumeLine(buffer, len);
    if (ec != std::errc() || ptr != p + len) {
        setProtocolError(out, "Bad integer value");
        return true;
    }
    out.setInteger(val);
    return true;
}

bool MFRedisClient::parseBulkString(trantor::MsgBuffer* buffer, MFRedisReply& out) {
    const char* p;
    size_t len;
    if (!readLine(buffer, p, len)) {
        return false;
    }
    int64_t bulkLen = 0;
    auto [ptr, ec] = std::from_chars(p, p + len, bulkLen);
    consumeLine(buffer, len);
    if (ec != std::errc() || ptr != p + len) {
        setProtocolError(out, "Bad bulk string length");
        return true;
    }
    if (bulkLen < -1) {
        setProtocolError(out, "Bulk string length out of range");
        return true;
    }
    if (bulkLen == -1) {
        out.setNil();
        return true;
    }
    size_t ulen = static_cast<size_t>(bulkLen);
    if (buffer->readableBytes() < ulen + 2) {
        return false;
    } 
    out.setStr(buffer->peek(), ulen);
    buffer->retrieve(ulen + 2);
    return true;
}

static constexpr long long kMaxArrayElements = ((1LL << 32) - 1);

bool MFRedisClient::parseArrayLike(trantor::MsgBuffer* buffer, MFRedisReply& out, int multiplier) {
    const char* p;
    size_t len;
    if (!readLine(buffer, p, len)) {
        return false;
    }
    int64_t count = 0;
    auto [ptr, ec] = std::from_chars(p, p + len, count);
    consumeLine(buffer, len);
    if (ec != std::errc() || ptr != p + len) {
        setProtocolError(out, "Bad multi-bulk length");
        return true;
    }
    if (count < -1) {
        setProtocolError(out, "Multi-bulk length out of range");
        return true;
    }
    if (multiplier > 1 && count >= 0) {
        if (count > (kMaxArrayElements / multiplier)) {
            setProtocolError(out, "Multi-bulk length out of range");
            return true;
        }
        count *= multiplier;
    }

    if (count > kMaxArrayElements) {
        setProtocolError(out, "Multi-bulk length out of range");
        return true;
    }
    if (count == -1) {
        out.setNil();
        return true;
    }
    out.setTypeArray();
    out.elements.clear();
    out.elements.reserve(static_cast<size_t>(count));
    for (int64_t i = 0; i < count; ++i) {
        MFRedisReply elem;
        if (!parseReply(buffer, elem)) {
            return false;
        }
        out.elements.push_back(std::move(elem));
    }
    return true;
}

bool MFRedisClient::parseArray(trantor::MsgBuffer* buffer, MFRedisReply& out) {
    return parseArrayLike(buffer, out, 1);
}

bool MFRedisClient::parseNil(trantor::MsgBuffer* buffer, MFRedisReply& out) {
    const char* p;
    size_t len;
    if (!readLine(buffer, p, len)) {
        return false;
    }
    consumeLine(buffer, len);
    if (len != 0) {
        setProtocolError(out, "Bad nil value");
        return true;
    }
    out.setNil();
    return true;
}

bool MFRedisClient::parseBool(trantor::MsgBuffer* buffer, MFRedisReply& out) {
    const char* p;
    size_t len;
    if (!readLine(buffer, p, len)) {
        return false;
    } 
    consumeLine(buffer, len);
    if (len != 1 || (p[0] != 't' && p[0] != 'T' && p[0] != 'f' && p[0] != 'F')) {
        setProtocolError(out, "Bad bool value");
        return true;
    }
    out.setBool(p[0] == 't' || p[0] == 'T');
    return true;
}

bool MFRedisClient::parseDouble(trantor::MsgBuffer* buffer, MFRedisReply& out) {
    const char* p;
    size_t len;
    if (!readLine(buffer, p, len)) {
        return false;
    }
    if (len >= 326) {
        setProtocolError(out, "Double value is too large");
        consumeLine(buffer, len);
        return true;
    }
    auto ci = [](const char* s, size_t n, const char* t) {
        size_t tn = std::strlen(t);
        if (n != tn) {
            return false;
        } 
        for (size_t i = 0; i < n; ++i) {
            if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(t[i]))) {
                return false;
            } 
        }

        return true;
    };
    double d = 0.0;
    if (len == 3 && ci(p, len, "inf")) {
        d = std::numeric_limits<double>::infinity();
    } else if (len == 4 && p[0] == '-' && ci(p + 1, 3, "inf")) {
        d = -std::numeric_limits<double>::infinity();
    } else if ((len == 3 && ci(p, len, "nan")) || (len == 4 && p[0] == '-' && ci(p + 1, 3, "nan"))) {
        d = std::numeric_limits<double>::quiet_NaN();
    } else {
        char buf[326];
        if (len > 0) {
            std::memcpy(buf, p, len);
        } 
        buf[len] = '\0';
        char* eptr = nullptr;
        d = std::strtod(buf, &eptr);
        if (len == 0 || (eptr != buf + len) || !std::isfinite(d)) {
            setProtocolError(out, "Bad double value");
            consumeLine(buffer, len);
            return true;
        }
    }
    consumeLine(buffer, len);
    out.setDouble(d);
    return true;
}

bool MFRedisClient::parseVerbatim(trantor::MsgBuffer* buffer, MFRedisReply& out) {
    const char* p;
    size_t len;
    if (!readLine(buffer, p, len)) {
        return false;
    } 
    int64_t verbatimLen = 0;
    auto [ptr, ec] = std::from_chars(p, p + len, verbatimLen);
    consumeLine(buffer, len);
    if (ec != std::errc() || ptr != p + len || verbatimLen < 4) {
        setProtocolError(out, "Verbatim string 4 bytes of content type are missing or incorrectly encoded.");
        return true;
    }
    if (verbatimLen < -1) {
        setProtocolError(out, "Bulk string length out of range");
        return true;
    }
    size_t ulen = static_cast<size_t>(verbatimLen);
    if (buffer->readableBytes() < ulen + 2) {
        return false;
    } 
    const char* body = buffer->peek();
    if (ulen >= 4 && body[3] != ':') {
        setProtocolError(out, "Verbatim string 4 bytes of content type are missing or incorrectly encoded.");
        return true;
    }
    out.setStr(body, ulen);
    buffer->retrieve(ulen + 2);
    return true;
}

bool MFRedisClient::parseBignum(trantor::MsgBuffer* buffer, MFRedisReply& out) {
    const char* p;
    size_t len;
    if (!readLine(buffer, p, len)) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        if (i == 0 && p[0] == '-') {
            continue;
        } 
        if (p[i] < '0' || p[i] > '9') {
            setProtocolError(out, "Bad bignum value");
            consumeLine(buffer, len);
            return true;
        }
    }
    out.setStr(p, len);
    consumeLine(buffer, len);
    return true;
}

bool MFRedisClient::parseReply(trantor::MsgBuffer* buffer, MFRedisReply& out) {
    if (buffer->readableBytes() < 1) {
        return false;
    }
    char first = *buffer->peek();
    buffer->retrieve(1);
    switch (first) {
    case '+':
        return parseSimpleString(buffer, out);
    case '-':
        return parseError(buffer, out);
    case ':':
        return parseInteger(buffer, out);
    case '$':
        return parseBulkString(buffer, out);
    case '*':
        return parseArray(buffer, out);
    case '_':
        return parseNil(buffer, out);
    case '#':
        return parseBool(buffer, out);
    case ',':
        return parseDouble(buffer, out);
    case '=':
        return parseVerbatim(buffer, out);
    case '%':
    case '|':
        return parseArrayLike(buffer, out, 2);
    case '~':
    case '>':
        return parseArray(buffer, out);
    case '(':
        return parseBignum(buffer, out);
    default:
        setProtocolErrorByte(out, first);
        return true;
    }
}

void MFRedisClient::sendCommand(const std::vector<std::string>& args) {
    if (!m_connection) {
        return;
    }
    std::string msg;
    msg.reserve(256);
    msg += '*';
    msg += std::to_string(args.size());
    msg += "\r\n";
    for (const auto& a : args) {
        msg += '$';
        msg += std::to_string(a.size());
        msg += "\r\n";
        msg += a;
        msg += "\r\n";
    }
    m_connection->send(msg);
}

void MFRedisClient::processPendingCommands() {
    if (m_commandQueue.empty() || m_state != MFRedisState::Connected) {
        return;
    }
    m_state = MFRedisState::Commanding;
    const MFRedisCommand& cmd = m_commandQueue.front();
    sendCommand(cmd.args);
}

void MFRedisClient::execute(const std::vector<std::string>& args, MFServiceId_t serviceId, size_t sessionId, const std::function<void(MFRedisResult&&)>& fn) {
    m_eventLoopThread->getLoop()->runInLoop([this, args, serviceId, sessionId, fn] {
        m_commandQueue.push(MFRedisCommand(args, sessionId, serviceId, fn));
        if (m_state == MFRedisState::Connected) {
            processPendingCommands();
        }
    });
}

void MFRedisClient::disconnect() {
    if (!m_client) {
        return;
    }
    m_client->disconnect();
    m_state = MFRedisState::Disconnected;
}
