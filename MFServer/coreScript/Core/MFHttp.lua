local cjson = require "cjson"


local coroutineRunning = coroutine.running
local coroutineYield = coroutine.yield

---@class MFHttp
local MFHttp = {}

local httpClientCo = {}
-----------------------------------------------httpServer--------------------------------------------------
function MFHttp.createHttpServer(configId)
    MFNativeNet.createHttpServer(configId, MF.core.serviceId)
end

function MFHttp.httpResponse(fd, data)
    local body = data and cjson.encode(data) or ""
    MFSocket.httpResponse(fd, body, 200)
    MF.core.log("login success result = {}", body)
end

function MFHttp.httpSendError(fd, errorCode, errMsg, params)
    local data = {}
    data.errorCode = errorCode
    data.msg = errMsg
    data.params = params
    local body = data and cjson.encode(data) or ""
    MFSocket.httpResponse(fd, body, 200)
end

---------------------------------------------httpRequest---------------------------------------------
function MFHttp.httpRequest(host, method, path, body, headers)
    local requestId = MFSocket.httpRequest(MF.core.serviceId, host, method, path, body, headers)
    httpClientCo[requestId] = coroutineRunning()
    return coroutineYield "suspended"
end

function MFHttp.onHttpRsp(sessionId, viewData, result)
    local co = httpClientCo[sessionId]
    if co then
        MF.core.coResume(co, viewData, result)
        httpClientCo[sessionId] = nil
    end
end

return MFHttp
