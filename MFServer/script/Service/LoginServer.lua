require "Common.GlobalRequire"
require "Common.HttpRouter"

local function onHttpServerDispatch(viewData, ip, fd)
    if string.isBlank(viewData) then
        MF.core.log("viewData is null or empty")
        return
    end
    local decodeData = MF.util.string2Table(viewData)
    local router = decodeData["cmd"]
    MF.router.executeHandle(router, decodeData, fd, ip)
end

local messageProto = {
    {
        messageType = LuaMessageTypeHttpServerReq,
        dispatch = onHttpServerDispatch,
    }
}


function main(serviceId, serviceName)
    MF.core.start(serviceId, serviceName)
    MF.core.registerMessageProto(messageProto)
    MF.http.createHttpServer(SocketConfig.webSocket)
end




