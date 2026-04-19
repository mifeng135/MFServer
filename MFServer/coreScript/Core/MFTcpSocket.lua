local MFCoder = require "Core.MFCoder"

---@class MFTcpSocket
local MFTcpSocket = {}

function MFTcpSocket.closeConnection(fd)
    MFSocket.forceCloseConnection(fd)
end

-----------------------------------------------tcpServer-------------------------------------------------------
function MFTcpSocket.createTcpServer(configId)
    MFNativeNet.createTcpServer(configId, MF.core.serviceId, MFNetTypeNormal)
end

---服务器发送消息给客户端
function MFTcpSocket.send(fd, msgId, data)
    local encodeProtoData = MFCoder.protoEncode(msgId, data)
    local encodeData, len = MFCoder.baseEncode(msgId, encodeProtoData)
    MFSocket.send(fd, encodeData, len)
end

---直接发送proto数据
function MFTcpSocket.sendEncodeData(fd, msgId, encodeProtoData)
    local encodeData, len = MFCoder.baseEncode(msgId, encodeProtoData)
    MFSocket.send(fd, encodeData, len)
end

return MFTcpSocket