local MFCoder = require "Core.MFCoder"
local cjson = require "cjson"


---@class MFWebSocket
local MFWebSocket = {}

function MFWebSocket.createWebSocketServer(configId)
    MFNativeNet.createWebSocketServer(configId, MF.core.serviceId)
end

---服务器发送消息给客户端
function MFWebSocket.sendBinary(fd, msgId, data)
    local encodeProtoData = MFCoder.protoEncode(msgId, data)
    local encodeData, len = MFCoder.baseEncode(msgId, encodeProtoData)
    MFSocket.webSocketSend(fd, encodeData, len, WebSocketBinary)
end

function MFWebSocket.sendText(fd, data)
    local encodeData = cjson.encode(data)
    MFSocket.webSocketSend(fd, encodeData, #encodeData, WebSocketPing)
end

return MFWebSocket