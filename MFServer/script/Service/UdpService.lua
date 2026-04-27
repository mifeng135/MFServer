require "Common.GlobalRequire"

local function onMsgDispatch(cmd, fd, viewData)
    if cmd == SocketRead then
        MFSocket.udpSend(fd, "111", 3)
    end
end

local messageProto = {
    {
        messageType = LuaMessageTypeUdpServer,
        dispatch = onMsgDispatch,
    }
}

function main(serviceId, serviceName)
    MF.core.start(serviceId, serviceName)
    MF.core.registerMessageProto(messageProto)
    MF.udp.createUdpServer(SocketConfig.webSocket)
end




