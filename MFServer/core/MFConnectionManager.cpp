#include "MFConnectionManager.hpp"

MFConnectionManager* MFConnectionManager::m_instance = nullptr;

MFConnectionManager * MFConnectionManager::getInstance() {
    if (!m_instance) {
        m_instance = new MFConnectionManager();
    }
    return m_instance;
}

void MFConnectionManager::destroyInstance() {
    if (m_instance) {
        delete m_instance;
        m_instance = nullptr;
    }
}

void MFConnectionManager::addConnection(size_t fd, const trantor::TcpConnectionPtr &connPtr) {
    m_connectionMap.insert(fd, connPtr);
}

void MFConnectionManager::removeConnection(size_t fd) {
    m_connectionMap.erase(fd);
}

void MFConnectionManager::forceCloseConnection(size_t fd) {
    trantor::TcpConnectionPtr connPtr;
    if (m_connectionMap.extract(fd, connPtr)) {
        connPtr->forceClose();
    }
}

void MFConnectionManager::send(size_t fd, const char *msg, int len) {
    m_connectionMap.find_fn(fd, [msg, len](const trantor::TcpConnectionPtr& connPtr) {
        connPtr->send(msg, len);
    });
}

void MFConnectionManager::addWebSocketConnection(size_t fd, const drogon::WebSocketConnectionPtr &connPtr) {
    m_webConnectionMap.insert(fd, connPtr);
}

void MFConnectionManager::removeWebSocketConnection(size_t fd) {
    m_webConnectionMap.erase(fd);
}

void MFConnectionManager::forceCloseWebSocketConnection(size_t fd) {
    drogon::WebSocketConnectionPtr connPtr;
    if (m_webConnectionMap.extract(fd, connPtr)) {
        connPtr->forceClose();
    }
}

void MFConnectionManager::sendWebSocketConnection(size_t fd, const char *msg, int len, drogon::WebSocketMessageType type) {
    m_webConnectionMap.find_fn(fd, [msg, len, type](const drogon::WebSocketConnectionPtr& connPtr) {
        connPtr->send(msg, len, type);
    });
}

void MFConnectionManager::addHttpCallback(size_t fd, httpCallback&& callback) {
    m_httpCallbackMap.insert(fd, std::move(callback));
}

void MFConnectionManager::httpResponse(size_t fd, const std::string& body, drogon::HttpStatusCode code) {
    httpCallback callback;
    if (!m_httpCallbackMap.extract(fd, callback)) {
        return;
    }
    drogon::HttpResponsePtr ptr = drogon::HttpResponse::newHttpResponse();
    ptr->setStatusCode(code);
    ptr->addHeader("Content-Type", "application/json;charset=UTF-8");
    ptr->setBody(body);
    callback(ptr);
}

void MFConnectionManager::httpResponseParams(size_t fd, const std::string &body, drogon::HttpStatusCode code, const sol::table &headers) {
    httpCallback callback;
    if (!m_httpCallbackMap.extract(fd, callback)) {
        return;
    }
    drogon::HttpResponsePtr ptr = drogon::HttpResponse::newHttpResponse();
    ptr->setStatusCode(code);
    ptr->setBody(body);
    for (auto& pair : headers) {
        ptr->addHeader(pair.first.as<std::string>(), pair.second.as<std::string>());
    }
    callback(ptr);
}
