require "Common.GlobalRequire"

local function dispatch(cmd, fd, viewData, messageType)
    if cmd == SocketRead then
        MF.websocket.sendText(fd, viewData)
        MF.core.log("data = {}, messageType = {}", viewData, messageType)
    end
end

local messageProto = {
    {
        messageType = LuaMessageTypeWebSocket,
        dispatch = dispatch,
    }
}


function main(serviceId, serviceName)
    MF.core.start(serviceId, serviceName)
    MF.core.registerMessageProto(messageProto)
    MF.websocket.createWebSocketServer(SocketConfig.webSocket)
end




