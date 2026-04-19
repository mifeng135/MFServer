local msgPack = require "MF.msgPack"
local MFCoder = require "Core.MFCoder"

local lSocketRead = SocketRead

local coroutineRunning = coroutine.running
local coroutineYield = coroutine.yield

---@class MFRpcClient
local MFRpcClient = {}

local rpcClientCo = {}
local cSid = 0

---向rpc服务发送一个消息 不需要远程回复
---@param configId number 发送的目标rpc
---@param msgId number 消息id
function MFRpcClient.send(configId, msgId, ...)
    local packData, len = MFCoder.baseRpcEncode(msgId, 0, msgPack.pack(...))
    MFSocket.rpcClientSend(configId, packData, len)
end

---@param msgId number 消息id
function MFRpcClient.request(configId, msgId, ...)
    cSid = cSid + 1
    rpcClientCo[cSid] = coroutineRunning()
    local packData, len = MFCoder.baseRpcEncode(msgId, cSid, msgPack.pack(...))
    MFSocket.rpcClientSend(configId, packData, len)
    return coroutineYield "suspended"
end

---客户端收到远程服务的回复
---@private
function MFRpcClient.onRevResponse(cmd, viewData)
    if cmd == lSocketRead then
        local _, sId, msgBody = MFCoder.baseRpcDecode(viewData)
        local co = rpcClientCo[sId]
        if co then
            MF.core.coResume(co, msgPack.unpack(msgBody))
            rpcClientCo[sId] = nil
        end
    end
end

return MFRpcClient