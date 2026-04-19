require "Common.GlobalRequire"

local function onRpcReqDispatch(sessionId, msgId, ...)
    if msgId == 1 then
        local v1, v2 = ...
        MF.core.log("v1 = {}, v2 = {}", v1, v2)
        if sessionId > 0 then
            MF.rpcServer.response(sessionId, "88", "99")
        end
    end
end

local messageProto = {
    {
        messageType = LuaMessageTypeRpcReq,
        dispatch = onRpcReqDispatch,
    }
}

function main(serviceId, serviceName)
    MF.core.start(serviceId, serviceName)
    MF.core.registerMessageProto(messageProto)
    MF.rpcServer.createRpcServer(RpcConfig.Gate29)
    MF.core.scheduleOnce(3, function()
        MFLuaServiceManager.addUniqueService("RpcClient")
    end)
end




