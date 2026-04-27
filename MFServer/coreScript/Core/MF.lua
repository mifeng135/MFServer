require "Core.MFFunctions"
require "Core.MFMacro"
require "Core.MFString"

---@class MF
---@field core MFCore
---@field sql MFSqlMapper
---@field redis MFLuaRedis
---@field util MFLuaUtil
---@field router MFRouterHandle
---@field share MFShare
---@field http MFHttp
---@field rpcClient MFRpcClient
---@field rpcServer MFRpcServer
---@field tcp MFTcpSocket
---@field udp MFUdpSocket
---@field websocket MFWebSocket
MF = {}
--- 子模块按需懒加载：首次访问 MF.xxx 时 require 并缓存
local MFSubmodules = {
    core = "Core.MFCore",
    sql = "Core.MFSqlMapper",
    redis = "Core.MFLuaRedis",
    util = "Core.MFLuaUtil",
    router = "Core.MFRouterHandle",
    share = "Core.MFShare",
    http = "Core.MFHttp",
    rpcClient = "Core.MFRpcClient",
    rpcServer = "Core.MFRpcServer",
    tcp = "Core.MFTcpSocket",
    udp = "Core.MFUdpSocket",
    websocket = "Core.MFWebSocket",
}

MF = setmetatable({}, {
    __index = function(t, k)
        local modName = MFSubmodules[k]
        if modName then
            local mod = require(modName)
            rawset(t, k, mod)
            return mod
        end
        return nil
    end
})