#include "MFRedisClient.hpp"
#include "MFApplication.hpp"

#include <charconv>
#include <cmath>
#include <algorithm>
#include <limits>

// --- MFRedisReply ---
void MFRedisReply::setStr(std::string &&s) {
    type = Type::String;
    str = std::move(s);
}

void MFRedisReply::setStr(const char *p, size_t n) {
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

void MFRedisReply::setError(const char *s) {
    type = Type::Error;
    str = s;
}

void MFRedisReply::setError(const char *p, size_t n) {
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
    if (m_eventLoopThread) {
        m_eventLoopThread->getLoop()->quit();
        delete m_eventLoopThread;
        m_eventLoopThread = nullptr;
    }
}

void MFRedisClient::init(const std::string &host, uint16_t port, ReadyCallback onReady, const std::string &password) {
    m_host = host;
    m_port = port;
    m_password = password;
    m_onReady = std::move(onReady);

    m_eventLoopThread = new trantor::EventLoopThread("MFRedisClientEventLoop");
    m_eventLoopThread->run();

    trantor::InetAddress addr(host, port);
    m_client = std::make_shared<trantor::TcpClient>(m_eventLoopThread->getLoop(), addr, "RedisClient");

    m_client->setConnectionCallback([this](const trantor::TcpConnectionPtr &conn) {
        conn->connected() ? onConnect(conn) : onDisconnect(conn);
    });

    m_client->setMessageCallback([this](const trantor::TcpConnectionPtr &conn, trantor::MsgBuffer *buf) {
        onMessage(conn, buf);
    });

    m_client->setConnectionErrorCallback([this]() {
        MFApplication::getInstance()->logInfo("Redis connected fail from {}:{}", m_host, m_port);
        setState(MFRedisState::Disconnected);
    });

    setState(MFRedisState::Connecting);
    m_client->connect();
    m_client->enableRetry();
}

void MFRedisClient::onConnect(const trantor::TcpConnectionPtr &conn) {
    m_connection = conn;
    m_connection->setTcpNoDelay(true);

    if (!m_password.empty()) {
        setState(MFRedisState::Authenticating);
        sendCommand({"AUTH", m_password});
    } else {
        MFApplication::getInstance()->logInfo("Redis connected to {}:{}", m_host, m_port);
        setState(MFRedisState::Connected);
    }
}

void MFRedisClient::onDisconnect(const trantor::TcpConnectionPtr &conn) {
    MFApplication::getInstance()->logInfo("Redis disconnected from {}:{}", m_host, m_port);
    setState(MFRedisState::Disconnected);
}

void MFRedisClient::setState(MFRedisState newState) {
    if (m_state == newState) {
        return;
    }
    m_state = newState;
    switch (newState) {
        case MFRedisState::Connected:
            fireReady(true);
            break;
        case MFRedisState::Disconnected:
            m_connection = nullptr;
            failInflight("Connection closed");
            fireReady(false);
            break;
        default:
            break;
    }
}

void MFRedisClient::fireReady(bool success) {
    if (!m_onReady) {
        return;
    }
    m_onReady(this, success);
    m_onReady = nullptr;
}

void MFRedisClient::failInflight(const char *reason) {
    if (!m_inflight) {
        return;
    }
    MFRedisCommand cmd = std::move(*m_inflight);
    m_inflight.reset();
    MFRedisResult result(cmd.sessionId, cmd.serviceId);
    result.success = false;
    result.error = reason;
    cmd.fn(std::move(result));
}

void MFRedisClient::onMessage(const trantor::TcpConnectionPtr &conn, trantor::MsgBuffer *buffer) {
    try {
        while (buffer->readableBytes() > 0) {
            const size_t bytesBefore = buffer->readableBytes();
            switch (m_state) {
                case MFRedisState::Disconnected:
                case MFRedisState::Connecting:
                    return;
                case MFRedisState::Authenticating: {
                    MFRedisReply reply;
                    size_t consumed = 0;
                    if (!parseReply(buffer->peek(), buffer->readableBytes(), reply, consumed)) {
                        return;
                    }
                    buffer->retrieve(consumed);
                    if (reply.isString() && reply.str == "OK") {
                        MFApplication::getInstance()->logInfo("Redis AUTH OK {}:{}", m_host, m_port);
                        setState(MFRedisState::Connected);
                    } else {
                        MFApplication::getInstance()->logInfo("Redis AUTH failed {}:{}", m_host, m_port);
                        disconnect();
                    }
                    break;
                }
                case MFRedisState::Connected: {
                    if (!m_inflight) {
                        MFApplication::getInstance()->logInfo("Redis unexpected data while idle, disconnect {}:{}", m_host, m_port);
                        disconnect();
                        break;
                    }
                    MFRedisResult result(m_inflight->sessionId, m_inflight->serviceId);
                    size_t consumed = 0;
                    if (!parseReply(buffer->peek(), buffer->readableBytes(), result.reply, consumed)) {
                        return;
                    }
                    buffer->retrieve(consumed);
                    MFRedisCommand cmd = std::move(*m_inflight);
                    m_inflight.reset();

                    result.success = !result.reply.isError();
                    if (result.reply.isError()) {
                        result.error = result.reply.str;
                    }
                    cmd.fn(std::move(result));
                    break;
                }
            }
            if (buffer->readableBytes() == bytesBefore) {
                break;
            }
        }
    } catch (const std::exception &e) {
        MFApplication::getInstance()->logInfo("Redis onMessage error: {}", e.what());
        disconnect();
    }
}

bool MFRedisClient::findCrLf(const char *p, size_t n, size_t &lineLen) {
    for (size_t i = 0; i + 1 < n; ++i) {
        if (p[i] == '\r' && p[i + 1] == '\n') {
            lineLen = i;
            return true;
        }
    }
    return false;
}

void MFRedisClient::setProtocolError(MFRedisReply &out, const char *msg) {
    out.setError(msg);
}

void MFRedisClient::setProtocolErrorByte(MFRedisReply &out, char byte) {
    if (byte == '\r' || byte == '\n' || byte == '"' || byte == '\\' || std::isprint(static_cast<unsigned char>(byte))) {
        out.setError("Protocol error, got \"" + std::string(1, byte) + "\" as reply type byte");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "Protocol error, got \"\\x%02x\" as reply type byte", static_cast<unsigned char>(byte));
        out.setError(buf);
    }
}

static bool hasCrLf(const char *p, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (p[i] == '\r' || p[i] == '\n') {
            return true;
        }
    }
    return false;
}

bool MFRedisClient::parseSimpleString(const char *p, size_t n, MFRedisReply &out, size_t &consumed) {
    size_t lineLen;
    if (!findCrLf(p, n, lineLen)) {
        return false;
    }
    if (hasCrLf(p, lineLen)) {
        setProtocolError(out, "Bad simple string value");
    } else {
        out.setStr(p, lineLen);
    }
    consumed = lineLen + 2;
    return true;
}

bool MFRedisClient::parseError(const char *p, size_t n, MFRedisReply &out, size_t &consumed) {
    size_t lineLen;
    if (!findCrLf(p, n, lineLen)) {
        return false;
    }
    if (hasCrLf(p, lineLen)) {
        setProtocolError(out, "Bad simple string value");
    } else {
        out.setError(p, lineLen);
    }
    consumed = lineLen + 2;
    return true;
}

bool MFRedisClient::parseInteger(const char *p, size_t n, MFRedisReply &out, size_t &consumed) {
    size_t lineLen;
    if (!findCrLf(p, n, lineLen)) {
        return false;
    }
    int64_t val = 0;
    auto [ptr, ec] = std::from_chars(p, p + lineLen, val);
    if (ec != std::errc() || ptr != p + lineLen) {
        setProtocolError(out, "Bad integer value");
    } else {
        out.setInteger(val);
    }
    consumed = lineLen + 2;
    return true;
}

bool MFRedisClient::parseBulkString(const char *p, size_t n, MFRedisReply &out, size_t &consumed) {
    size_t lineLen;
    if (!findCrLf(p, n, lineLen)) {
        return false;
    }
    int64_t bulkLen = 0;
    auto [ptr, ec] = std::from_chars(p, p + lineLen, bulkLen);
    size_t headLen = lineLen + 2;
    if (ec != std::errc() || ptr != p + lineLen) {
        setProtocolError(out, "Bad bulk string length");
        consumed = headLen;
        return true;
    }
    if (bulkLen < -1) {
        setProtocolError(out, "Bulk string length out of range");
        consumed = headLen;
        return true;
    }
    if (bulkLen == -1) {
        out.setNil();
        consumed = headLen;
        return true;
    }
    size_t ulen = static_cast<size_t>(bulkLen);
    if (n < headLen + ulen + 2) {
        return false;
    }
    out.setStr(p + headLen, ulen);
    consumed = headLen + ulen + 2;
    return true;
}

static constexpr long long kMaxArrayElements = ((1LL << 32) - 1);

bool MFRedisClient::parseArrayLike(const char *p, size_t n, MFRedisReply &out, size_t &consumed, int multiplier) {
    size_t lineLen;
    if (!findCrLf(p, n, lineLen)) {
        return false;
    }
    int64_t count = 0;
    auto [ptr, ec] = std::from_chars(p, p + lineLen, count);
    size_t headLen = lineLen + 2;
    if (ec != std::errc() || ptr != p + lineLen) {
        setProtocolError(out, "Bad multi-bulk length");
        consumed = headLen;
        return true;
    }
    if (count < -1) {
        setProtocolError(out, "Multi-bulk length out of range");
        consumed = headLen;
        return true;
    }
    if (multiplier > 1 && count >= 0) {
        if (count > (kMaxArrayElements / multiplier)) {
            setProtocolError(out, "Multi-bulk length out of range");
            consumed = headLen;
            return true;
        }
        count *= multiplier;
    }
    if (count > kMaxArrayElements) {
        setProtocolError(out, "Multi-bulk length out of range");
        consumed = headLen;
        return true;
    }
    if (count == -1) {
        out.setNil();
        consumed = headLen;
        return true;
    }
    out.setTypeArray();
    out.elements.clear();
    out.elements.reserve(static_cast<size_t>(count));
    size_t off = headLen;
    for (int64_t i = 0; i < count; ++i) {
        MFRedisReply elem;
        size_t elemConsumed = 0;
        if (!parseReply(p + off, n - off, elem, elemConsumed)) {
            return false;
        }
        out.elements.push_back(std::move(elem));
        off += elemConsumed;
    }
    consumed = off;
    return true;
}

bool MFRedisClient::parseArray(const char *p, size_t n, MFRedisReply &out, size_t &consumed) {
    return parseArrayLike(p, n, out, consumed, 1);
}

bool MFRedisClient::parseNil(const char *p, size_t n, MFRedisReply &out, size_t &consumed) {
    size_t lineLen;
    if (!findCrLf(p, n, lineLen)) {
        return false;
    }
    if (lineLen != 0) {
        setProtocolError(out, "Bad nil value");
    } else {
        out.setNil();
    }
    consumed = lineLen + 2;
    return true;
}

bool MFRedisClient::parseBool(const char *p, size_t n, MFRedisReply &out, size_t &consumed) {
    size_t lineLen;
    if (!findCrLf(p, n, lineLen)) {
        return false;
    }
    if (lineLen != 1 || (p[0] != 't' && p[0] != 'T' && p[0] != 'f' && p[0] != 'F')) {
        setProtocolError(out, "Bad bool value");
    } else {
        out.setBool(p[0] == 't' || p[0] == 'T');
    }
    consumed = lineLen + 2;
    return true;
}

bool MFRedisClient::parseDouble(const char *p, size_t n, MFRedisReply &out, size_t &consumed) {
    size_t lineLen;
    if (!findCrLf(p, n, lineLen)) {
        return false;
    }
    consumed = lineLen + 2;
    if (lineLen >= 326) {
        setProtocolError(out, "Double value is too large");
        return true;
    }
    auto ci = [](const char *s, size_t sn, const char *t) {
        size_t tn = std::strlen(t);
        if (sn != tn) {
            return false;
        }
        for (size_t i = 0; i < sn; ++i) {
            if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(t[i]))) {
                return false;
            }
        }
        return true;
    };
    double d = 0.0;
    if (lineLen == 3 && ci(p, lineLen, "inf")) {
        d = std::numeric_limits<double>::infinity();
    } else if (lineLen == 4 && p[0] == '-' && ci(p + 1, 3, "inf")) {
        d = -std::numeric_limits<double>::infinity();
    } else if ((lineLen == 3 && ci(p, lineLen, "nan")) || (lineLen == 4 && p[0] == '-' && ci(p + 1, 3, "nan"))) {
        d = std::numeric_limits<double>::quiet_NaN();
    } else {
        char buf[326];
        if (lineLen > 0) {
            std::memcpy(buf, p, lineLen);
        }
        buf[lineLen] = '\0';
        char *eptr = nullptr;
        d = std::strtod(buf, &eptr);
        if (lineLen == 0 || (eptr != buf + lineLen) || !std::isfinite(d)) {
            setProtocolError(out, "Bad double value");
            return true;
        }
    }
    out.setDouble(d);
    return true;
}

bool MFRedisClient::parseVerbatim(const char *p, size_t n, MFRedisReply &out, size_t &consumed) {
    size_t lineLen;
    if (!findCrLf(p, n, lineLen)) {
        return false;
    }
    int64_t verbatimLen = 0;
    auto [ptr, ec] = std::from_chars(p, p + lineLen, verbatimLen);
    size_t headLen = lineLen + 2;
    if (ec != std::errc() || ptr != p + lineLen || verbatimLen < 4) {
        setProtocolError(out, "Verbatim string 4 bytes of content type are missing or incorrectly encoded.");
        consumed = headLen;
        return true;
    }
    size_t ulen = static_cast<size_t>(verbatimLen);
    if (n < headLen + ulen + 2) {
        return false;
    }
    const char *body = p + headLen;
    if (ulen >= 4 && body[3] != ':') {
        setProtocolError(out, "Verbatim string 4 bytes of content type are missing or incorrectly encoded.");
        consumed = headLen + ulen + 2;
        return true;
    }
    out.setStr(body, ulen);
    consumed = headLen + ulen + 2;
    return true;
}

bool MFRedisClient::parseBignum(const char *p, size_t n, MFRedisReply &out, size_t &consumed) {
    size_t lineLen;
    if (!findCrLf(p, n, lineLen)) {
        return false;
    }
    consumed = lineLen + 2;
    for (size_t i = 0; i < lineLen; ++i) {
        if (i == 0 && p[0] == '-') {
            continue;
        }
        if (p[i] < '0' || p[i] > '9') {
            setProtocolError(out, "Bad bignum value");
            return true;
        }
    }
    out.setStr(p, lineLen);
    return true;
}

bool MFRedisClient::parseReply(const char *p, size_t n, MFRedisReply &out, size_t &consumed) {
    if (n < 1) {
        return false;
    }
    char first = p[0];
    size_t sub = 0;
    bool ok = false;
    switch (first) {
        case '+':
            ok = parseSimpleString(p + 1, n - 1, out, sub);
            break;
        case '-':
            ok = parseError(p + 1, n - 1, out, sub);
            break;
        case ':':
            ok = parseInteger(p + 1, n - 1, out, sub);
            break;
        case '$':
            ok = parseBulkString(p + 1, n - 1, out, sub);
            break;
        case '*':
            ok = parseArray(p + 1, n - 1, out, sub);
            break;
        case '_':
            ok = parseNil(p + 1, n - 1, out, sub);
            break;
        case '#':
            ok = parseBool(p + 1, n - 1, out, sub);
            break;
        case ',':
            ok = parseDouble(p + 1, n - 1, out, sub);
            break;
        case '=':
            ok = parseVerbatim(p + 1, n - 1, out, sub);
            break;
        case '%':
        case '|':
            ok = parseArrayLike(p + 1, n - 1, out, sub, 2);
            break;
        case '~':
        case '>':
            ok = parseArray(p + 1, n - 1, out, sub);
            break;
        case '(':
            ok = parseBignum(p + 1, n - 1, out, sub);
            break;
        default:
            setProtocolErrorByte(out, first);
            consumed = 1;
            return true;
    }
    if (!ok) {
        return false;
    }
    consumed = 1 + sub;
    return true;
}

void MFRedisClient::sendCommand(const std::vector<std::string> &args) {
    if (!m_connection) {
        return;
    }
    std::string msg;
    msg.reserve(256);
    msg += '*';
    msg += std::to_string(args.size());
    msg += "\r\n";
    for (const auto &a: args) {
        msg += '$';
        msg += std::to_string(a.size());
        msg += "\r\n";
        msg += a;
        msg += "\r\n";
    }
    m_connection->send(msg);
}

void MFRedisClient::execute(std::vector<std::string> args, MFServiceId_t serviceId, size_t sessionId, const std::function<void(MFRedisResult &&)> &fn) {
    m_eventLoopThread->getLoop()->runInLoop([this, moveArgs = std::move(args), serviceId, sessionId, fn]() mutable {
        if (m_state != MFRedisState::Connected) {
            MFRedisResult result(sessionId, serviceId);
            result.success = false;
            result.error = "Redis connection not ready";
            fn(std::move(result));
            return;
        }
        if (m_inflight) {
            MFRedisResult result(sessionId, serviceId);
            result.success = false;
            result.error = "Redis connection busy";
            fn(std::move(result));
            return;
        }
        sendCommand(moveArgs);
        m_inflight.emplace(std::move(moveArgs), sessionId, serviceId, fn);
    });
}

void MFRedisClient::disconnect() {
    if (!m_client) {
        return;
    }
    m_client->disconnect();
    setState(MFRedisState::Disconnected);
}
