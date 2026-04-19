local serialize = require "MF.serialize"
local MFMsgHandlers = require "Core.MFMsgHandlers"

local coroutineYield = coroutine.yield
local coroutineCreate = coroutine.create
local coroutineResume = coroutine.resume
local coroutineRunning = coroutine.running

local coroutinePool = setmetatable({}, { __mode = "kv" })

local messageProto = {}
local scheduleFnMap = {}    --定时器回调
local callDstMap = {}       --请求记录 用于要回复的目标service
local callCoMap = {}        --记录call方法的协程

---@class MFCore
local MFCore = {}

function MFCore.start(serviceId, path)
    MFCore.serviceId = serviceId
    MFCore.logKey =  path .. "[" .. serviceId .. "]"
    MFMsgHandlers.init(MFCore, messageProto, scheduleFnMap, callDstMap, callCoMap)
end

function MFCore.traceback(msg)
    local traceMsg = debug.traceback(msg)
    MFCore.log(traceMsg)
end

function MFCore.registerMessageProto(protoTable)
    if not protoTable then
        return
    end
    for _, v in pairs(protoTable) do
        messageProto[v.messageType] = v
    end
end

function MFCore.coCreate(fn, ...)
    local co = table.remove(coroutinePool)
    if co then
        MFCore.coResume(co, fn)
    else
        co = coroutineCreate(function(...)
            fn(...)
            while true do
                coroutinePool[#coroutinePool + 1] = co
                fn = coroutineYield "suspended"
                fn(coroutineYield())
            end
        end)
    end
    MFCore.coResume(co, ...)
end

function MFCore.coResume(co, ...)
    local ok, result = coroutineResume(co, ...)
    if not ok then
        MFCore.traceback(result)
    end
    return ok, result
end

--------------------------------------------- send begin ------------------------------------
function MFCore.send(dst, cmd, ...)
    local data, len = serialize.pack(cmd, ...)
    MFLuaServiceManager.send(MFCore.serviceId, dst, data, len, LuaMessageTypeLuaSend, false, false)
end

function MFCore.sendMulti(dstList, cmd, ...)
    local data, len = serialize.pack(cmd, ...)
    MFLuaServiceManager.sendMulti(MFCore.serviceId, data, len, dstList)
end

function MFCore.request(dst, cmd, ...)
    local msg, len = serialize.pack(cmd, ...)
    local sessionId = MFLuaServiceManager.send(MFCore.serviceId, dst, msg, len, LuaMessageTypeLuaRequest, true, false)
    callCoMap[sessionId] = coroutineRunning()
    local data, l = coroutineYield("suspended")
    return serialize.unpack(data, l)
end

function MFCore.response(sessionId, ...)
    if sessionId == 0 then
        return
    end
    local dst = callDstMap[sessionId]
    if not dst then
        MFCore.log("ret can not dst sessionId = {}", sessionId)
        return
    end
    local data, len = serialize.pack(...)
    MFLuaServiceManager.ret(MFCore.serviceId, dst, data, len, sessionId, LuaMessageTypeLuaResponse, false)
    callDstMap[sessionId] = nil
end

----------------------------------------------------schedule-------------------------------------------------
function MFCore.scheduleOnce(time, func)
    local timerId = MFApplication.scheduleOnce(time, MFCore.serviceId)
    scheduleFnMap[timerId] = func
    return timerId
end

function MFCore.schedule(time, func)
    local timerId = MFApplication.schedule(time, MFCore.serviceId)
    scheduleFnMap[timerId] = func
    return timerId
end

function MFCore.stopSchedule(timerId)
    scheduleFnMap[timerId] = nil
    MFApplication.stopSchedule(timerId)
end

function MFCore.reloadLua(moduleName)
    moduleName = string.gsub(moduleName, "/", ".")
    moduleName = string.gsub(moduleName, "\\", ".")
    if not package.loaded[moduleName] then
        MF.core.log("reloadLua not load = {}", moduleName)
        return
    end
    local startTime = MFUtil.getMilliseconds()
    hotfix(moduleName)
    MF.core.log("reload moduleName <{}> success, time = {}", moduleName, MFUtil.getMilliseconds() - startTime)
end

---写入日志
---@param msg string
function MFCore.log(msg, ...)
    MFApplication.serviceLog(MFCore.logKey, msg, ...)
end

---关闭当前虚拟机
function MFCore.killSelf()
    MFLuaServiceManager.closeService(MFCore.serviceId)
end



return MFCore
