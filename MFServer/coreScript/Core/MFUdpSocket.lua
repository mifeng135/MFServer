local MFCoder = require "Core.MFCoder"

---@class MFUdpSocket
local MFUdpSocket = {}

function MFUdpSocket.createUdpServer(configId)
    MFNativeNet.createUdpServer(configId, MF.core.serviceId)
end

function MFUdpSocket.send(fd, msgId, data)
    local encodeProtoData = MFCoder.protoEncode(msgId, data)
    local encodeData, len = MFCoder.baseEncode(msgId, encodeProtoData)
    MFSocket.udpSend(fd, encodeData, len)
end

---直接发送proto数据
function MFUdpSocket.sendEncodeData(fd, msgId, encodeProtoData)
    local encodeData, len = MFCoder.baseEncode(msgId, encodeProtoData)
    MFSocket.send(fd, encodeData, len)
end

return MFUdpSocket

