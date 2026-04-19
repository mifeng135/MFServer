#include "MFMysqlClient.hpp"

#include <charconv>

#include "MFApplication.hpp"
#include "MFLuaMessage.hpp"
#include "MFSHA1.hpp"
#include "MFSHA256.hpp"
#include "MFUtil.hpp"


#include <cstring>

#define COM_QUIT    0x01
#define COM_QUERY   0x03
#define COM_PING    0x0E


MFMysqlClient::MFMysqlClient()
: m_state(MFMysqlState::Disconnected)
, m_packetNumber(0)
, m_port(3306)
, m_eventLoopThread(nullptr) {
}

MFMysqlClient::~MFMysqlClient() {
    disconnect();
    m_client.reset();
    m_eventLoopThread->getLoop()->quit();
    delete m_eventLoopThread;
    m_eventLoopThread = nullptr;
}

void MFMysqlClient::init(const std::string& host, uint16_t port,
                         const std::string& user, const std::string& password,
                         const std::string& database) {
    m_host = host;
    m_port = port;
    m_user = user;
    m_password = password;
    m_database = database;

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
    
    m_client->enableRetry();
    m_state = MFMysqlState::Connecting;
    m_client->connect();
}

void MFMysqlClient::onConnect(const trantor::TcpConnectionPtr& conn) {
    m_connection = conn;
    m_connection->setTcpNoDelay(true);
    m_state = MFMysqlState::Handshaking;
    m_packetNumber = 0;
}

void MFMysqlClient::onDisconnect(const trantor::TcpConnectionPtr& conn) {
    MFApplication::getInstance()->logInfo("MySQL disconnected from database {}:{}:{}", m_host, m_port, m_database);
    m_connection = nullptr;
    m_state = MFMysqlState::Disconnected;
    
    while (!m_queryQueue.empty()) {
        MFMysqlQuery query = m_queryQueue.front();
        m_queryQueue.pop();
        
        MFMysqlResult result(query.sessionId, query.serviceId);
        result.success = false;
        result.error = "Connection lost";
        query.fn(std::move(result));
    }
}
void MFMysqlClient::onMessage(const trantor::TcpConnectionPtr& conn, trantor::MsgBuffer* buffer) {
    while (buffer->readableBytes() >= 4) {
        try {
            size_t bytesBefore = buffer->readableBytes();
            switch (m_state) {
                case MFMysqlState::Handshaking:
                    handleHandshake(buffer);
                    break;
                case MFMysqlState::Authenticating:
                    handleAuthResponse(buffer);
                    break;
                case MFMysqlState::Querying:
                    handleQueryResponse(buffer);
                    break;
                case MFMysqlState::Connected:
                    if (!m_queryQueue.empty()) {
                        m_state = MFMysqlState::Querying;
                        continue;
                    }
                    return;
                default:
                    return;
            }
            if (buffer->readableBytes() == bytesBefore) {
                break;
            }
        } catch (const std::exception& e) {
            MFApplication::getInstance()->logInfo("MySQL onMessage error: {}", e.what());
            disconnect();
            return;
        }
    }
}

void MFMysqlClient::handleHandshake(trantor::MsgBuffer* buffer) {
    if (buffer->readableBytes() < 4) {
        return;
    }
    
    const uint8_t* header = reinterpret_cast<const uint8_t*>(buffer->peek());
    uint32_t len = header[0] | header[1] << 8 | header[2] << 16;
    
    if (buffer->readableBytes() < len + 4) {
        return;
    }
    
    buffer->retrieve(4);
    m_packetNumber = header[3];
    
    const uint8_t* data = reinterpret_cast<const uint8_t*>(buffer->peek());
    const uint8_t* ptr = data;
    ptr++;
    while (*ptr != 0 && ptr - data < static_cast<int>(len)) {
        ptr++;
    }
    ptr++;
    ptr += 4;
    m_scramble.assign(reinterpret_cast<const char*>(ptr), 8);
    ptr += 8;
    ptr += 1 + 2 + 1 + 2 + 2 + 1 + 10;
    if (ptr - data < static_cast<int>(len)) {
        const uint8_t* scramble_start = ptr;
        size_t remaining = std::min(static_cast<size_t>(13), static_cast<size_t>(len - (ptr - data)));
        size_t scramble_len = 0;
        
        while (scramble_len < remaining && *ptr != 0) {
            scramble_len++;
            ptr++;
        }
        m_scramble.append(reinterpret_cast<const char*>(scramble_start), scramble_len);
        if (*ptr == 0) {
            ptr++;
        }
    }
    if (ptr - data < static_cast<int>(len)) {
        m_authPlugin = readNullStr(ptr);
    } else {
        m_authPlugin = "mysql_native_password";
    }
    
    buffer->retrieve(len);
    sendAuthPacket();
    m_state = MFMysqlState::Authenticating;
}

void MFMysqlClient::handleAuthResponse(trantor::MsgBuffer* buffer) {
    if (buffer->readableBytes() < 4) {
        return;
    }

    std::vector<uint8_t> packet = readPacket(buffer);
    if (packet.empty()) {
        return;
    }
    uint8_t flag = packet[0];
    if (flag == 0x00) {
        MFApplication::getInstance()->logInfo("MySQL auth OK ip = {}, port = {}, database = {}", m_host, m_port, m_database);
        m_state = MFMysqlState::Connected;
        processPendingQueries();
    } else if (flag == 0xFE) {
        handleAuthSwitch(packet);
    } else if (flag == 0x01) {
    } else if (flag == 0xFF) {
        const uint8_t* ptr = packet.data() + 1;
        ptr += 2;
        if (*ptr == '#') {
            ptr += 6;
        }
        MFApplication::getInstance()->logInfo("MySQL auth failed: {}", std::string(reinterpret_cast<const char*>(ptr), packet.data() + packet.size() - ptr));
        disconnect();
    } else {
        MFApplication::getInstance()->logInfo("MySQL auth unexpected packet: 0x{:02X}", packet[0]);
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
    
    MFApplication::getInstance()->logInfo("MySQL auth switch to: {}, scramble length: {}", newPlugin, m_scramble.size());
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

void MFMysqlClient::handleQueryResponse(trantor::MsgBuffer* buffer) {
    if (buffer->readableBytes() < 4) {
        return;
    }
    const uint8_t* header = reinterpret_cast<const uint8_t*>(buffer->peek());
    uint32_t len = header[0] | header[1] << 8 | header[2] << 16;

    if (buffer->readableBytes() < len + 4) {
        return;
    }
    uint8_t firstByte = header[4];

    if (m_queryQueue.empty()) {
        MFApplication::getInstance()->logInfo("MySQL received response but query queue is empty");
        return;
    }

    MFMysqlQuery currentQuery = m_queryQueue.front();
    MFMysqlResult result(currentQuery.sessionId, currentQuery.serviceId);

    if (firstByte == 0x00) {
        m_queryQueue.pop();
        auto packet = readPacket(buffer);
        parseOkPacket(packet, result);
        currentQuery.fn(std::move(result));
        m_state = MFMysqlState::Connected;
        processPendingQueries();
        return;
    }
    if (firstByte == 0xFF) {
        m_queryQueue.pop();
        auto packet = readPacket(buffer);
        parseErrorPacket(packet, result);
        currentQuery.fn(std::move(result));
        m_state = MFMysqlState::Connected;
        processPendingQueries();
        return;
    }
    
    const uint8_t* payload = reinterpret_cast<const uint8_t*>(buffer->peek()) + 4;
    const uint8_t* ptr = payload;
    uint64_t colCount = readLengthEncodedInt(ptr);
    size_t totalBytes = computeResultSetLength(buffer, colCount);
    if (totalBytes == 0) {
        return;
    }
    std::vector<uint8_t> block(totalBytes);
    memcpy(block.data(), buffer->peek(), totalBytes);
    buffer->retrieve(totalBytes);
    m_queryQueue.pop();
    parseResultSetFromBlock(block.data(), totalBytes, colCount, result);
    currentQuery.fn(std::move(result));
    m_state = MFMysqlState::Connected;
    processPendingQueries();
}

void MFMysqlClient::handlePingResponse(trantor::MsgBuffer* buffer) {
    if (buffer->readableBytes() < 4) {
        return;
    }
    const uint8_t* header = reinterpret_cast<const uint8_t*>(buffer->peek());
    uint32_t len = header[0] | header[1] << 8 | header[2] << 16;

    if (buffer->readableBytes() < len + 4) {
        return;
    }

    uint8_t firstByte = *reinterpret_cast<const uint8_t*>(buffer->peek() + 4);
    buffer->retrieve(4 + len);
    if (firstByte == 0x00) {
        m_state = MFMysqlState::Connected;
        processPendingQueries();
    } else {
        MFApplication::getInstance()->logInfo("MySQL ping response unexpected packet: 0x{:02X}", firstByte);
    }
}

std::vector<uint8_t> MFMysqlClient::readPacket(trantor::MsgBuffer* buffer) {
    if (buffer->readableBytes() < 4) {
        return {};
    }
    const uint8_t* header = reinterpret_cast<const uint8_t*>(buffer->peek());
    uint32_t len = header[0] | header[1] << 8 | header[2] << 16;
    m_packetNumber = header[3];
    
    if (buffer->readableBytes() < len + 4) {
        return {};
    }
    
    buffer->retrieve(4);
    
    std::vector<uint8_t> packet(len);
    memcpy(packet.data(), buffer->peek(), len);
    buffer->retrieve(len);
    
    return packet;
}

size_t MFMysqlClient::computeResultSetLength(trantor::MsgBuffer* buffer, uint64_t columnCount) {
    size_t readable = buffer->readableBytes();
    if (readable < 4) {
        return 0;
    }
    const uint8_t* p = reinterpret_cast<const uint8_t*>(buffer->peek());
    uint32_t len0 = p[0] | (p[1] << 8) | (p[2] << 16);
    size_t offset = 4 + len0;
    if (readable < offset) {
        return 0;
    } 
    for (uint64_t i = 0; i < columnCount; i++) {
        if (readable < offset + 4) {
            return 0;
        } 
        uint32_t len = p[offset] | p[offset + 1] << 8 | p[offset + 2] << 16;
        offset += 4 + len;
        if (readable < offset) {
            return 0;
        } 
    }
    if (readable < offset + 4) {
        return 0;
    } 
    uint32_t lenEof = p[offset] | (p[offset + 1] << 8) | (p[offset + 2] << 16);
    offset += 4 + lenEof;
    if (readable < offset) { 
        return 0; 
    }
    for (;;) {
        if (readable < offset + 4) {
            return 0;
        } 
        uint32_t len = p[offset] | (p[offset + 1] << 8) | (p[offset + 2] << 16);
        if (readable < offset + 4 + len) {
            return 0;
        } 
        if (len < 9 && p[offset + 4] == 0xFE) {
            return offset + 4 + len;
        } 
        offset += 4 + len;
    }
}

static bool getNextPacketFromBlock(const uint8_t*& ptr, const uint8_t* end, std::vector<uint8_t>& out) {
    if (end - ptr < 4) {
        return false;
    }
    uint32_t len = ptr[0] | ptr[1] << 8 | ptr[2] << 16;
    if (static_cast<size_t>(end - ptr) < 4 + len) {
        return false;
    }
    out.assign(ptr + 4, ptr + 4 + len);
    ptr += 4 + len;
    return true;
}

void MFMysqlClient::parseResultSetFromBlock(const uint8_t* block, size_t blockSize, uint64_t columnCount, MFMysqlResult& result) {
    const uint8_t* ptr = block;
    const uint8_t* end = block + blockSize;
    std::vector<uint8_t> packet;
    if (!getNextPacketFromBlock(ptr, end, packet)) {
        return;
    } 
    result.columns.reserve(columnCount);
    for (uint64_t i = 0; i < columnCount; i++) {
        if (!getNextPacketFromBlock(ptr, end, packet)) {
            result.error = "Failed to read column";
            return;
        }
        uint8_t first = packet[0];
        if (first == 0xFF) {
            parseErrorPacket(packet, result);
            return;
        }
        if (first == 0x00) {
            result.success = false;
            result.error = "unexpected OK packet in column definitions";
            return;
        }
        if (first == 0xFE && packet.size() >= 5 && packet.size() < 9) {
            result.success = false;
            result.error = "unexpected EOF in column definitions";
            return;
        }
        const uint8_t* p = packet.data();
        skipLengthEncodedStr(p);
        skipLengthEncodedStr(p);
        skipLengthEncodedStr(p);
        skipLengthEncodedStr(p);
        MFMysqlColumn col;
        col.name = readLengthEncodedStr(p);
        skipLengthEncodedStr(p);
        p += 7;
        col.type = static_cast<MFMysqlFieldType>(*p++);
        col.flags = p[0] | (p[1] << 8);
        p += 2;
        p += 3;
        result.columns.emplace_back(std::move(col));
    }
    if (!getNextPacketFromBlock(ptr, end, packet)) {
        result.error = "Failed to read EOF after column definitions";
        return;
    }
    if (packet[0] != 0xFE || packet.size() >= 9) {
        result.success = false;
        result.error = "unexpected packet type after column definitions, EOF expected";
        return;
    }
    while (getNextPacketFromBlock(ptr, end, packet)) {
        if (packet[0] == 0xFE && packet.size() < 9) {
            break;
        }
        std::vector<MFValue> rowData;
        rowData.reserve(columnCount);
        const uint8_t* p = packet.data();
        for (size_t i = 0; i < columnCount; i++) {
            if (*p == 0xFB) {
                rowData.emplace_back(MFValue());
                p++;
            } else {
                const MFMysqlColumn& column = result.columns[i];
                rowData.emplace_back(parseValue(readLengthEncodedStr(p), column.type, column.flags));
            }
        }
        result.rows.emplace_back(std::move(rowData));
    }
    result.success = true;
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

void MFMysqlClient::sendPingPacket() {
    m_packetNumber = 0;
    std::vector<uint8_t> packet;
    packet.push_back(COM_PING);
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

void MFMysqlClient::parseResultSet(trantor::MsgBuffer* buffer, uint64_t columnCount, MFMysqlResult& result) {
    result.columns.reserve(columnCount);
    for (uint64_t i = 0; i < columnCount; i++) {
        auto packet = readPacket(buffer);
        if (packet.empty()) {
            result.error = "Failed to read column";
            return;
        }
        uint8_t first = packet[0];
        if (first == 0xFF) {
            parseErrorPacket(packet, result);
            return;
        }
        if (first == 0x00) {
            result.success = false;
            result.error = "unexpected OK packet in column definitions";
            return;
        }
        if (first == 0xFE && packet.size() >= 5 && packet.size() < 9) {
            result.success = false;
            result.error = "unexpected EOF in column definitions";
            return;
        }
        const uint8_t* ptr = packet.data();
        skipLengthEncodedStr(ptr);  // catalog
        skipLengthEncodedStr(ptr);  // schema
        skipLengthEncodedStr(ptr);  // table
        skipLengthEncodedStr(ptr);  // org_table
        MFMysqlColumn col;
        col.name = readLengthEncodedStr(ptr);
        skipLengthEncodedStr(ptr);  // org_name
        ptr += 7;
        col.type = static_cast<MFMysqlFieldType>(*ptr++);
        col.flags = ptr[0] | (ptr[1] << 8);
        ptr += 2;
        ptr += 3;
        result.columns.emplace_back(std::move(col));
    }
    auto eofPacket = readPacket(buffer);
    if (eofPacket.empty()) {
        result.error = "Failed to read EOF after column definitions";
        return;
    }
    if (eofPacket[0] != 0xFE || eofPacket.size() >= 9) {
        result.success = false;
        result.error = "unexpected packet type after column definitions, EOF expected";
        return;
    }

    while (true) {
        auto packet = readPacket(buffer);
        if (packet.empty()) {
            result.error = "Failed to read row";
            return;
        }
        if (packet[0] == 0xFE && packet.size() < 9) {
            break;
        }
        std::vector<MFValue> rowData;
        rowData.reserve(columnCount);
        const uint8_t* ptr = packet.data();
        for (size_t i = 0; i < columnCount; i++) {
            if (*ptr == 0xFB) {
                rowData.emplace_back(MFValue());
                ptr++;
            } else {
                const MFMysqlColumn& column = result.columns[i];
                rowData.emplace_back(parseValue(readLengthEncodedStr(ptr), column.type, column.flags));
            }
        }
        result.rows.emplace_back(std::move(rowData));
    }
    result.success = true;
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
        m_queryQueue.push(MFMysqlQuery(std::move(sql), sessionId, serviceId, std::move(fn)));
        if (m_state == MFMysqlState::Connected) {
            processPendingQueries();
        }
    });
}

void MFMysqlClient::processPendingQueries() {
    if (m_queryQueue.empty()) {
        return;
    }
    m_state = MFMysqlState::Querying;
    const MFMysqlQuery& query = m_queryQueue.front();
    sendQueryPacket(query.sql);
}


void MFMysqlClient::disconnect() {
    if (!m_client) {
        return;
    }
    if (!m_connection) {
        return;
    }
    std::vector<uint8_t> quit = { COM_QUIT };
    sendPacket(quit);
    m_client->disconnect();
    m_state = MFMysqlState::Disconnected;
}
