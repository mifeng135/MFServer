local serialize = require "MF.serialize"

MFMessageHandlers = {}

local function register(core, msgType, fn)
    MFMessageHandlers[msgType] = function(...)
        core.coCreate(fn, ...)
    end
end

---@class MFMsgHandlers
local MFMsgHandlers = {}

---@param MFCore MFCore
function MFMsgHandlers.init(MFCore, messageProto, scheduleFnMap, callDstMap, callCoMap)
    register(MFCore, LuaMessageTypeLuaSend, function(data, len)
        messageProto[LuaMessageTypeLuaSend].dispatch(serialize.unpack(data, len))
    end)

    register(MFCore, LuaMessageTypeLuaRequest, function(data, len, sessionId, src)
        callDstMap[sessionId] = src
        messageProto[LuaMessageTypeLuaRequest].dispatch(sessionId, serialize.unpack(data, len))
    end)

    register(MFCore, LuaMessageTypeLuaResponse, function(data, len, sessionId)
        local co = callCoMap[sessionId]
        if co then
            MFCore.coResume(co, data, len)
            callCoMap[sessionId] = nil
        end
    end)

    register(MFCore, LuaMessageTypeSocket, function(socketCmd, fd, viewData)
        messageProto[LuaMessageTypeSocket].dispatch(socketCmd, fd, viewData)
    end)

    register(MFCore, LuaMessageTypeWebSocket, function(socketCmd, fd, viewData, messageType)
        messageProto[LuaMessageTypeWebSocket].dispatch(socketCmd, fd, viewData, messageType)
    end)

    register(MFCore, LuaMessageTypeTimer, function(sessionId)
        local fn = scheduleFnMap[sessionId]
        if fn then
            fn()
        end
    end)

    register(MFCore, LuaMessageTypeHttpServerReq, function(viewData, ip, fd)
        messageProto[LuaMessageTypeHttpServerReq].dispatch(viewData, ip, fd)
    end)

    register(MFCore, LuaMessageTypeHttpClientRsp, function(sessionId, viewData, result)
        MF.http.onHttpRsp(sessionId, viewData, result)
    end)

    register(MFCore, LuaMessageTypeSocketClient, function(cmd, viewData)
        messageProto[LuaMessageTypeSocketClient].dispatch(cmd, viewData)
    end)

    register(MFCore, LuaMessageTypeTimerOnce, function(sessionId)
        local fn = scheduleFnMap[sessionId]
        if fn then
            fn()
            scheduleFnMap[sessionId] = nil
        end
    end)

    register(MFCore, LuaMessageTypeHotReload, function(module, fullPath)
        MFCore.reloadLua(module, fullPath)
    end)

    register(MFCore, LuaMessageTypeMysqlQuery, function(sessionId, success, sqlResult)
        MF.sql.onQueryCallBack(sessionId, sqlResult, success)
    end)

    register(MFCore, LuaMessageTypeMysqlExecute, function(sessionId, execRes, success)
        MF.sql.onExecuteCallBack(sessionId, execRes, success)
    end)

    register(MFCore, LuaMessageTypeMultiSend, function(data, len)
        messageProto[LuaMessageTypeLuaSend].dispatch(serialize.unpack(data, len))
    end)

    register(MFCore, LuaMessageTypeRpcReq, function(cmd, fd, viewData)
        MF.rpcServer.onRevRequest(cmd, fd, viewData, messageProto[LuaMessageTypeRpcReq].dispatch)
    end)

    register(MFCore, LuaMessageTypeRpcRsp, function(cmd, viewData)
        MF.rpcClient.onRevResponse(cmd, viewData)
    end)

    register(MFCore, LuaMessageTypeRedisCmd, function(sessionId, result)
        MF.redis.onExecuteCallBack(sessionId, result)
    end)

    register(MFCore, LuaMessageTypeReloadConfig, function()
        MF.config.clear()
    end)

    register(MFCore, LuaMessageTypeUdpServer, function(socketCmd, fd, viewData)
        messageProto[LuaMessageTypeUdpServer].dispatch(socketCmd, fd, viewData)
    end)
end

return MFMsgHandlers
