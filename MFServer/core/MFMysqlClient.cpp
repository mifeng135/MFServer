#include "MFMysqlClient.hpp"

#include <algorithm>
#include <charconv>

#include "MFApplication.hpp"
#include "MFLuaMessage.hpp"
#include "MFSHA1.hpp"
#include "MFSHA256.hpp"
#include "MFUtil.hpp"


#include <cstring>

#define COM_QUERY   0x03


MFMysqlClient::MFMysqlClient()
: m_state(MFMysqlState::Disconnected)
, m_packetNumber(0)
, m_port(3306)
, m_queryPhase(QueryPhase::Initial)
, m_columnCount(0)
, m_eventLoopThread(nullptr) {
}

MFMysqlClient::~MFMysqlClient() {
    disconnect();
    m_client.reset();
    if (m_eventLoopThread) {
        m_eventLoopThread->getLoop()->quit();
        delete m_eventLoopThread;
        m_eventLoopThread = nullptr;
    }
}

void MFMysqlClient::init(const std::string& host, uint16_t port,
                         const std::string& user, const std::string& password,
                         const std::string& database,
                         ReadyCallback onReady) {
    m_host = host;
    m_port = port;
    m_user = user;
    m_password = password;
    m_database = database;
    m_onReady = std::move(onReady);

    m_eventLoopThread = new trantor::EventLoopThread("MFMysqlClientEventLoop");
    m_eventLoopThread->run();

    trantor::InetAddress addr(host, port);
    m_client = std::make_shared<trantor::TcpClient>(m_eventLoopThread->getLoop(), addr, "MysqlClient");
    
    m_client->setConnectionCallback([this](const trantor::TcpConnectionPtr& conn) {
        conn->connected() ? onConnect(conn) : onDisconnect(conn);
    });
    
    m_client->setMessageCallback([this](const trantor::TcpConnectionPtr& conn, trantor::MsgBuffer* buf) {
        onMessage(conn, buf);
    });

    m_client->setConnectionErrorCallback([this]() {
        MFApplication::getInstance()->logInfo("MySQL connected fail from {}:{}", m_host, m_port);
        setState(MFMysqlState::Disconnected);
    });

    m_client->enableRetry();
    setState(MFMysqlState::Connecting);
    m_client->connect();
}

void MFMysqlClient::onConnect(const trantor::TcpConnectionPtr& conn) {
    m_connection = conn;
    m_connection->setTcpNoDelay(true);
    m_packetNumber = 0;
    setState(MFMysqlState::Handshaking);
}

void MFMysqlClient::onDisconnect(const trantor::TcpConnectionPtr& conn) {
    MFApplication::getInstance()->logInfo("MySQL disconnected from database {}:{}:{}", m_host, m_port, m_database);
    setState(MFMysqlState::Disconnected);
}

void MFMysqlClient::setState(MFMysqlState newState) {
    if (m_state == newState) {
        return;
    }
    m_state = newState;
    switch (newState) {
        case MFMysqlState::Connected:
            fireReady(true);
            break;
        case MFMysqlState::Disconnected:
            m_connection = nullptr;
            failInflight("Connection closed");
            fireReady(false);
            break;
        default:
            break;
    }
}

void MFMysqlClient::fireReady(bool success) {
    if (!m_onReady) {
        return;
    }
    ReadyCallback cb = std::move(m_onReady);
    m_onReady = nullptr;
    cb(this, success);
}

void MFMysqlClient::failInflight(const char* reason) {
    if (!m_inflight) {
        return;
    }
    MFMysqlQuery query = std::move(*m_inflight);
    m_inflight.reset();
    m_pendingResult.reset();
    m_queryPhase = QueryPhase::Initial;
    MFMysqlResult result(query.sessionId, query.serviceId);
    result.success = false;
    result.error = reason;
    query.fn(std::move(result));
}

void MFMysqlClient::onMessage(const trantor::TcpConnectionPtr& conn, trantor::MsgBuffer* buffer) {
    while (buffer->readableBytes() >= 4) {
        try {
            size_t bytesBefore = buffer->readableBytes();
            bool progressed = false;
            switch (m_state) {
                case MFMysqlState::Handshaking:
                    handleHandshake(buffer);
                    progressed = buffer->readableBytes() != bytesBefore;
                    break;
                case MFMysqlState::Authenticating:
                    handleAuthResponse(buffer);
                    progressed = buffer->readableBytes() != bytesBefore;
                    break;
                case MFMysqlState::Querying:
                    progressed = handleQueryResponse(buffer);
                    break;
                case MFMysqlState::Connected:
                    MFApplication::getInstance()->logInfo("MySQL unexpected data while idle, disconnect {}:{}", m_host, m_port);
                    disconnect();
                    return;
                default:
                    return;
            }
            if (!progressed) {
                break;
            }
        } catch (const std::exception& e) {
            MFApplication::getInstance()->logInfo("MySQL onMessage error: {}", e.what());
            disconnect();
            return;
        }
    }
}

bool MFMysqlClient::readLogicalPacket(trantor::MsgBuffer* buffer, std::vector<uint8_t>& payload) {
    payload.clear();
    size_t readable = buffer->readableBytes();
    const uint8_t* base = reinterpret_cast<const uint8_t*>(buffer->peek());
    size_t scan = 0;
    uint8_t lastSeq = 0;
    for (;;) {
        if (readable < scan + 4) {
            return false;
        }
        uint32_t len = base[scan] | (base[scan + 1] << 8) | (base[scan + 2] << 16);
        lastSeq = base[scan + 3];
        if (readable < scan + 4 + len) {
            return false;
        }
        size_t before = payload.size();
        payload.resize(before + len);
        if (len > 0) {
            memcpy(payload.data() + before, base + scan + 4, len);
        }
        scan += 4 + len;
        if (len < 0xFFFFFF) {
            break;
        }
    }
    buffer->retrieve(scan);
    m_packetNumber = lastSeq;
    return true;
}

bool MFMysqlClient::isEofPacket(const std::vector<uint8_t>& packet) {
    return !packet.empty() && packet[0] == 0xFE && packet.size() < 9;
}

void MFMysqlClient::handleHandshake(trantor::MsgBuffer* buffer) {
    std::vector<uint8_t> packet;
    if (!readLogicalPacket(buffer, packet)) {
        return;
    }
    if (packet.empty()) {
        MFApplication::getInstance()->logInfo("MySQL handshake empty packet");
        disconnect();
        return;
    }
    const uint8_t* data = packet.data();
    const uint8_t* end = data + packet.size();
    const uint8_t* ptr = data;
    if (ptr >= end) {
        disconnect(); 
        return; 
    }
    ptr++;
    while (ptr < end && *ptr != 0) {
        ptr++;
    }
    if (ptr >= end) {
        disconnect();
        return;
    }
    ptr++;
    if (end - ptr < 4 + 8 + 1) {
        disconnect();
        return;
    }
    ptr += 4;
    m_scramble.assign(reinterpret_cast<const char*>(ptr), 8);
    ptr += 8;
    ptr += 1; // filler
    if (end - ptr < 1 + 2 + 2 + 1 + 10) {
        m_authPlugin = "mysql_native_password";
        sendAuthPacket();
        setState(MFMysqlState::Authenticating);
        return;
    }
    ptr += 1 + 2 + 2 + 1 + 10;

    if (ptr < end) {
        const uint8_t* scrambleStart = ptr;
        size_t remaining = std::min<size_t>(13, static_cast<size_t>(end - ptr));
        size_t scrambleLen = 0;
        while (scrambleLen < remaining && ptr < end && *ptr != 0) {
            scrambleLen++;
            ptr++;
        }
        m_scramble.append(reinterpret_cast<const char*>(scrambleStart), scrambleLen);
        if (ptr < end && *ptr == 0) {
            ptr++;
        }
    }
    if (ptr < end) {
        const char* start = reinterpret_cast<const char*>(ptr);
        const uint8_t* nul = ptr;
        while (nul < end && *nul != 0) {
            nul++;
        }
        m_authPlugin.assign(start, reinterpret_cast<const char*>(nul) - start);
        ptr = (nul < end) ? nul + 1 : end;
    } else {
        m_authPlugin = "mysql_native_password";
    }

    sendAuthPacket();
    setState(MFMysqlState::Authenticating);
}

void MFMysqlClient::handleAuthResponse(trantor::MsgBuffer* buffer) {
    std::vector<uint8_t> packet;
    if (!readLogicalPacket(buffer, packet)) {
        return;
    }
    if (packet.empty()) {
        MFApplication::getInstance()->logInfo("MySQL auth response empty packet");
        disconnect();
        return;
    }
    uint8_t flag = packet[0];
    if (flag == 0x00) {
        MFApplication::getInstance()->logInfo("MySQL auth OK ip = {}, port = {}, database = {}", m_host, m_port, m_database);
        setState(MFMysqlState::Connected);
    } else if (flag == 0xFE) {
        handleAuthSwitch(packet);
    } else if (flag == 0x01) {
        // caching_sha2_password 的进度包，忽略（后续 OK 包再切状态）
    } else if (flag == 0xFF) {
        const uint8_t* ptr = packet.data() + 1;
        const uint8_t* end = packet.data() + packet.size();
        if (end - ptr >= 2) {
            ptr += 2;
            if (ptr < end && *ptr == '#' && end - ptr >= 6) {
                ptr += 6;
            }
        }
        MFApplication::getInstance()->logInfo("MySQL auth failed: {}",std::string(reinterpret_cast<const char*>(ptr), end - ptr));
        disconnect();
    } else {
        MFApplication::getInstance()->logInfo("MySQL auth unexpected packet: 0x{:02X}", flag);
        disconnect();
    }
}

void MFMysqlClient::handleAuthSwitch(const std::vector<uint8_t>& packet) {
    const uint8_t* ptr = packet.data() + 1;
    std::string newPlugin = readNullStr(ptr);
    size_t scrambleLen = packet.data() + packet.size() - ptr;
    if (scrambleLen > 0 && ptr[scrambleLen - 1] == 0) {
        scrambleLen--;
    }
    m_scramble.assign(reinterpret_cast<const char*>(ptr), scrambleLen);
    
    if (newPlugin.empty() && m_authPlugin == "caching_sha2_password") {
        if (packet.size() > 1) {
            uint8_t flag = packet[1];
            if (flag == 0x03) {
                MFApplication::getInstance()->logInfo("MySQL caching_sha2_password fast path");
                return;
            }
            if (flag == 0x04) {
                MFApplication::getInstance()->logInfo("MySQL caching_sha2_password full path (not supported)");
                disconnect();
                return;
            }
        }
    }
    
    if (!newPlugin.empty()) {
        m_authPlugin = newPlugin;
    }
    
    std::vector<uint8_t> authPacket;
    if (!m_password.empty()) {
        std::string auth;
        if (m_authPlugin == "caching_sha2_password") {
            auth = cachingSha2Password(m_password, m_scramble);
        } else if (m_authPlugin == "mysql_native_password") {
            auth = mysqlPassword(m_password, m_scramble);
        } else {
            MFApplication::getInstance()->logInfo("Unsupported auth plugin: {}", m_authPlugin);
            disconnect();
            return;
        }
        authPacket.insert(authPacket.end(), auth.begin(), auth.end());
    }
    
    m_packetNumber++;
    sendPacket(authPacket);
}

bool MFMysqlClient::handleQueryResponse(trantor::MsgBuffer* buffer) {
    if (!m_inflight) {
        MFApplication::getInstance()->logInfo("MySQL received response but no inflight query, disconnect");
        disconnect();
        return false;
    }
    std::vector<uint8_t> packet;
    if (!readLogicalPacket(buffer, packet)) {
        return false;
    }
    if (packet.empty()) {
        MFApplication::getInstance()->logInfo("MySQL query response empty packet");
        disconnect();
        return false;
    }

    if (!m_pendingResult) {
        m_pendingResult.emplace(m_inflight->sessionId, m_inflight->serviceId);
        m_queryPhase = QueryPhase::Initial;
        m_columnCount = 0;
    }
    MFMysqlResult& result = *m_pendingResult;
    uint8_t first = packet[0];

    switch (m_queryPhase) {
        case QueryPhase::Initial: {
            if (first == 0x00) {
                parseOkPacket(packet, result);
                completeQuery();
                return true;
            }
            if (first == 0xFF) {
                parseErrorPacket(packet, result);
                completeQuery();
                return true;
            }
            const uint8_t* p = packet.data();
            m_columnCount = readLengthEncodedInt(p);
            result.columns.reserve(m_columnCount);
            m_queryPhase = QueryPhase::Columns;
            return true;
        }
        case QueryPhase::Columns: {
            if (first == 0xFF) {
                parseErrorPacket(packet, result);
                completeQuery();
                return true;
            }
            MFMysqlColumn col;
            if (!parseColumnDefinition(packet, col)) {
                result.success = false;
                result.error = "parse column definition failed";
                completeQuery();
                return true;
            }
            result.columns.emplace_back(std::move(col));
            if (result.columns.size() >= m_columnCount) {
                m_queryPhase = QueryPhase::ColumnsEof;
            }
            return true;
        }
        case QueryPhase::ColumnsEof: {
            if (first == 0xFF) {
                parseErrorPacket(packet, result);
                completeQuery();
                return true;
            }
            if (!isEofPacket(packet)) {
                result.success = false;
                result.error = "unexpected packet type after column definitions, EOF expected";
                completeQuery();
                return true;
            }
            m_queryPhase = QueryPhase::Rows;
            return true;
        }
        case QueryPhase::Rows: {
            if (first == 0xFF) {
                parseErrorPacket(packet, result);
                completeQuery();
                return true;
            }
            if (isEofPacket(packet)) {
                result.success = true;
                completeQuery();
                return true;
            }
            std::vector<MFValue> row;
            if (!parseRow(packet, m_columnCount, row)) {
                result.success = false;
                result.error = "parse row failed";
                completeQuery();
                return true;
            }
            result.rows.emplace_back(std::move(row));
            return true;
        }
    }
    return true;
}

void MFMysqlClient::completeQuery() {
    MFMysqlQuery query = std::move(*m_inflight);
    m_inflight.reset();
    std::optional<MFMysqlResult> pending = std::move(m_pendingResult);
    m_pendingResult.reset();
    m_queryPhase = QueryPhase::Initial;
    m_columnCount = 0;
    setState(MFMysqlState::Connected);
    query.fn(std::move(*pending));
}

bool MFMysqlClient::parseColumnDefinition(const std::vector<uint8_t>& packet, MFMysqlColumn& col) {
    if (packet.empty()) {
        return false;
    }
    const uint8_t* p = packet.data();
    const uint8_t* end = p + packet.size();
    for (int i = 0; i < 4; i++) {
        if (p >= end) {
            return false;
        }
        skipLengthEncodedStr(p);
    }
    if (p >= end) {
        return false;
    }
    col.name = readLengthEncodedStr(p);
    if (p >= end) {
        return false;
    }
    skipLengthEncodedStr(p); // org_name
    if (end - p < 10) {
        return false;
    }
    p += 7;
    col.type = static_cast<MFMysqlFieldType>(*p++);
    col.flags = static_cast<uint16_t>(p[0] | (p[1] << 8));
    return true;
}

bool MFMysqlClient::parseRow(const std::vector<uint8_t>& packet, uint64_t columnCount, std::vector<MFValue>& row) {
    row.reserve(columnCount);
    const uint8_t* p = packet.data();
    const uint8_t* end = p + packet.size();
    for (uint64_t i = 0; i < columnCount; i++) {
        if (p >= end) {
            return false;
        }
        if (*p == 0xFB) {
            row.emplace_back(MFValue());
            p++;
            continue;
        }
        if (i >= m_pendingResult->columns.size()) {
            return false;
        }
        const MFMysqlColumn& column = m_pendingResult->columns[i];
        row.emplace_back(parseValue(readLengthEncodedStr(p), column.type, column.flags));
    }
    return true;
}

void MFMysqlClient::sendPacket(const std::vector<uint8_t>& data) {
    if (!m_connection) {
        return;
    }
    
    size_t len = data.size();
    char header[4] = {
        static_cast<char>(len & 0xFF),
        static_cast<char>(len >> 8 & 0xFF),
        static_cast<char>(len >> 16 & 0xFF),
        static_cast<char>(m_packetNumber++)
    };
    
    m_connection->send(header, 4);
    if (!data.empty()) {
        m_connection->send(data.data(), data.size());
    }
}

void MFMysqlClient::sendAuthPacket() {
    m_packetNumber++;
    std::vector<uint8_t> packet;
    uint32_t flags = 260047;
    flags |= 0x0008;
    flags |= 0x00080000;

    packet.push_back(flags & 0xFF);
    packet.push_back(flags >> 8 & 0xFF);
    packet.push_back(flags >> 16 & 0xFF);
    packet.push_back(flags >> 24 & 0xFF);
    
    uint32_t maxSize = 16777216; //16M
    packet.push_back(maxSize & 0xFF);
    packet.push_back(maxSize >> 8 & 0xFF);
    packet.push_back(maxSize >> 16 & 0xFF);
    packet.push_back(maxSize >> 24 & 0xFF);
    // 字符集
    packet.push_back(33);
    // 保留字节
    for (int i = 0; i < 23; i++) {
        packet.push_back(0);
    }
    // 用户名
    packet.insert(packet.end(), m_user.begin(), m_user.end());
    packet.push_back(0);
    
    if (!m_password.empty()) {
        std::string auth;
        if (m_authPlugin == "caching_sha2_password") {
            auth = cachingSha2Password(m_password, m_scramble);
        } else {
            auth = mysqlPassword(m_password, m_scramble);
        }
        packet.push_back(auth.size());
        packet.insert(packet.end(), auth.begin(), auth.end());
    } else {
        packet.push_back(0);
    }
    if (!m_database.empty()) {
        packet.insert(packet.end(), m_database.begin(), m_database.end());
        packet.push_back(0);
    }
    if (!m_authPlugin.empty()) {
        packet.insert(packet.end(), m_authPlugin.begin(), m_authPlugin.end());
        packet.push_back(0);
    }
    
    sendPacket(packet);
}

void MFMysqlClient::sendQueryPacket(const std::string& sql) {
    m_packetNumber = 0;
    std::vector<uint8_t> packet;
    packet.push_back(COM_QUERY);
    packet.insert(packet.end(), sql.begin(), sql.end());
    sendPacket(packet);
}

std::string MFMysqlClient::mysqlPassword(const std::string& password, const std::string& scramble) {
    // SHA1(password) XOR SHA1(scramble + SHA1(SHA1(password)))
    std::string hash1 = MFSHA1::hash(password);
    std::string hash2 = MFSHA1::hash(hash1);
    std::string hash3 = MFSHA1::hash(scramble + hash2);
    
    for (size_t i = 0; i < hash1.size(); i++) {
        hash1[i] ^= hash3[i];
    }
    
    return hash1;
}

std::string MFMysqlClient::cachingSha2Password(const std::string& password, const std::string& scramble) {
    // SHA256(password) XOR SHA256(SHA256(SHA256(password)) + scramble)
    std::string hash1 = MFSHA256::hash(password);
    std::string hash2 = MFSHA256::hash(hash1);
    std::string hash3 = MFSHA256::hash(hash2 + scramble);
    
    for (size_t i = 0; i < hash1.size(); i++) {
        hash1[i] ^= hash3[i];
    }
    
    return hash1;
}

void MFMysqlClient::parseOkPacket(const std::vector<uint8_t>& packet, MFMysqlResult& result) {
    result.success = true;
    const uint8_t* ptr = packet.data() + 1;
    result.affectedRows = readLengthEncodedInt(ptr);
    result.insertId = readLengthEncodedInt(ptr);
}

void MFMysqlClient::parseErrorPacket(const std::vector<uint8_t>& packet, MFMysqlResult& result) {
    result.success = false;
    const uint8_t* ptr = packet.data() + 1;
    ptr += 2; // error code
    if (*ptr == '#') {
        ptr += 6; // SQL state
    }
    result.error = std::string(reinterpret_cast<const char*>(ptr), packet.data() + packet.size() - ptr);
}

MFValue MFMysqlClient::parseValue(std::string&& data, MFMysqlFieldType mysqlType, uint16_t flags) {
    bool isUnsigned = (flags & 0x0020) != 0; 
    if (data.empty() && mysqlType == MFMysqlFieldType::NULL_TYPE) {
        return MFValue();
    }
    switch (mysqlType) {
        case MFMysqlFieldType::TINY:
        case MFMysqlFieldType::SHORT:
        case MFMysqlFieldType::LONG:
        case MFMysqlFieldType::INT24:
        case MFMysqlFieldType::LONGLONG:
        case MFMysqlFieldType::YEAR: {
            if (isUnsigned) {
                uint64_t value = 0;
                auto [ptr, ec] = std::from_chars(data.data(), data.data() + data.size(), value);
                if (ec == std::errc()) {
                    return MFValue(value);
                }
            } else {
                int64_t value = 0;
                auto [ptr, ec] = std::from_chars(data.data(), data.data() + data.size(), value);
                if (ec == std::errc()) {
                    return MFValue(value);
                }
            }
            return MFValue(std::move(data));
        }

        case MFMysqlFieldType::BIT: {
            uint64_t value = 0;
            auto [ptr, ec] = std::from_chars(data.data(), data.data() + data.size(), value);
            return ec == std::errc() ? MFValue(value) : MFValue(std::move(data));
        }

        case MFMysqlFieldType::FLOAT: {
            return MFValue(static_cast<float>(std::atof(data.c_str())));
        }

        case MFMysqlFieldType::DOUBLE: {
            return MFValue(std::atof(data.c_str()));
        }

        case MFMysqlFieldType::DECIMAL:
        case MFMysqlFieldType::NEWDECIMAL:
        case MFMysqlFieldType::TINY_BLOB:
        case MFMysqlFieldType::MEDIUM_BLOB:
        case MFMysqlFieldType::LONG_BLOB:
        case MFMysqlFieldType::BLOB:
        default:
            return MFValue(std::move(data));
    }
}

uint64_t MFMysqlClient::readLengthEncodedInt(const uint8_t*& ptr) {
    uint8_t first = *ptr++;
    
    if (first < 0xFB) {
        return first;
    }
    if (first == 0xFC) {
        uint64_t val = ptr[0] | ptr[1] << 8;
        ptr += 2;
        return val;
    }
    if (first == 0xFD) {
        uint64_t val = ptr[0] | ptr[1] << 8 | ptr[2] << 16;
        ptr += 3;
        return val;
    }
    if (first == 0xFE) {
        uint64_t val = ptr[0] | ptr[1] << 8 | ptr[2] << 16 |
                       static_cast<uint64_t>(ptr[3]) << 24 |
                       static_cast<uint64_t>(ptr[4]) << 32 |
                       static_cast<uint64_t>(ptr[5]) << 40 |
                       static_cast<uint64_t>(ptr[6]) << 48 |
                       static_cast<uint64_t>(ptr[7]) << 56;
        ptr += 8;
        return val;
    }

    return 0;
}

std::string MFMysqlClient::readLengthEncodedStr(const uint8_t*& ptr) {
    uint64_t len = readLengthEncodedInt(ptr);
    std::string result(reinterpret_cast<const char*>(ptr), len);
    ptr += len;
    return result;
}

void MFMysqlClient::skipLengthEncodedStr(const uint8_t*& ptr) {
    uint64_t len = readLengthEncodedInt(ptr);
    ptr += len;
}

std::string MFMysqlClient::readNullStr(const uint8_t*& ptr) {
    const char* start = reinterpret_cast<const char*>(ptr);
    size_t len = strlen(start);
    ptr += len + 1;
    return std::string(start, len);
}

void MFMysqlClient::execute(const std::string& sql, MFServiceId_t serviceId, size_t sessionId, const std::function<void(MFMysqlResult&& result)> &fn) {
    m_eventLoopThread->getLoop()->runInLoop([this, sql, serviceId, sessionId, fn] {
        if (m_state != MFMysqlState::Connected) {
            MFMysqlResult result(sessionId, serviceId);
            result.success = false;
            result.error = "MySQL connection not ready";
            fn(std::move(result));
            return;
        }
        if (m_inflight) {
            MFMysqlResult result(sessionId, serviceId);
            result.success = false;
            result.error = "MySQL connection busy";
            fn(std::move(result));
            return;
        }
        m_inflight.emplace(sql, sessionId, serviceId, fn);
        m_pendingResult.reset();
        m_queryPhase = QueryPhase::Initial;
        m_columnCount = 0;
        setState(MFMysqlState::Querying);
        sendQueryPacket(sql);
    });
}


void MFMysqlClient::disconnect() {
    if (!m_client) {
        return;
    }
    m_client->disconnect();
    setState(MFMysqlState::Disconnected);
}
