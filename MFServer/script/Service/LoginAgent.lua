require "Common.GlobalRequire"

local function onLuaSendMsgDispatch(cmd, fd, viewData, ip)
    local decodeData = MF.util.string2Table(viewData)
    MF.router.executeHandle(decodeData["cmd"], decodeData, fd, ip)
end

local messageProto = {
    {
        messageType = LuaMessageTypeLuaSend,
        dispatch = onLuaSendMsgDispatch,
    },
}

function main(serviceId, serviceName)
    MF.core.start(serviceId, serviceName)
    MF.core.registerMessageProto(messageProto)
end