local coroutineRunning = coroutine.running
local coroutineYield = coroutine.yield

---@class MFLuaRedis
local MFLuaRedis = {}

local redisExecuteCall = {}
local coRedisMap = {}

---异步获取结果
function MFLuaRedis.executeAsync(key, fn, ...)
    local sessionId = MFNativeRedis.executeAsync(MF.core.serviceId, key, ...)
    if sessionId > 0 then
        redisExecuteCall[sessionId] = fn
    end    
end

function MFLuaRedis.executeCoroutine(key, ...)
    local sessionId = MFNativeRedis.executeAsync(MF.core.serviceId, key, ...)
    if sessionId <= 0 then
        return nil
    end
    coRedisMap[sessionId] = coroutineRunning()
    return coroutineYield "suspended"
end

---同步获取结果
function MFLuaRedis.executeSync(key, timeOut, ...)
    return MFNativeRedis.executeSync(key, timeOut, ...)
end

function MFLuaRedis.onExecuteCallBack(sessionId, result)
    local co = coRedisMap[sessionId]
    if co then
        MF.core.coResume(co, result)
        coRedisMap[sessionId] = nil
    else
        local fn = redisExecuteCall[sessionId]
        if fn then
            fn(result)
            redisExecuteCall[sessionId] = nil
        end
    end
end

return MFLuaRedis