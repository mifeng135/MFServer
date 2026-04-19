local msgPack = require "MF.msgPack"
local MFCoder = require "Core.MFCoder"

local lSocketRead = SocketRead

---@class MFRpcServer
local MFRpcServer = {}

function MFRpcServer.createRpcServer(configId)
    MFNativeNet.createTcpServer(configId, MF.core.serviceId, MFNetTypeRPC)
end

function MFRpcServer.response(sessionId, ...)
    local fd = sessionId >> 32
    local cSid = sessionId & 0xFFFFFFFF
    local packData, len = MFCoder.baseRpcEncode(0, cSid, msgPack.pack(...))
    MFSocket.send(fd, packData, len)
end

---@private
function MFRpcServer.onRevRequest(cmd, fd, viewData, fn)
    if cmd == lSocketRead then
        local msgId, cSid, msgBody = MFCoder.baseRpcDecode(viewData)
        local sessionId = (fd << 32) | (cSid & 0xFFFFFFFF)
        fn(sessionId, msgId, msgPack.unpack(msgBody))
    end
end

return MFRpcServer