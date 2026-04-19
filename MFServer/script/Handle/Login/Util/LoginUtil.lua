---@class LoginUtil
local LoginUtil = {}
local M = LoginUtil

local times = 0

---@param playerAccount SqlPlayerAccount
---@param playerUid SqlPlayerUid
function M.createReigsterParams(playerAccount, playerUid)
    ---@type RegisterParams
    local result = {}
    result.regCity = playerAccount.city
    result.regDeviceId = playerAccount.deviceId
    result.regFireBaseId = playerAccount.firebaseId
    result.GAID = playerAccount.GAID
    result.accountId = playerAccount.accountId

    result.showUid = playerUid.showUid
    result.regServerId = playerUid.serverId
    result.isRestart = playerUid.isRestart
    result.regTime = playerUid.regTime
    result.regCountry = playerUid.country
    result.regLanguage = playerUid.language
    result.regClientVersion = playerUid.clientVersion
    result.regPlatformType = playerUid.platformType
    result.name = playerUid.playerName
    return result
end

---@param playerAccount SqlPlayerAccount
---@param playerUid SqlPlayerUid
---@param requestParams ClientParams
function M.login(uid, playerAccount, playerUid, requestParams, fd)
    times = times + 1

    local token = uid .. "_" .. playerUid.serverId
    local retResult = {}
    retResult.ip = "127.0.0.1"
    retResult.port = 9999
    retResult.accountId = playerAccount.accountId
    retResult.regClientVersion = playerAccount.clientVersion
    retResult.times = times
    retResult.token = token

    local registerParamsJson = MF.util.table2String(M.createReigsterParams(playerAccount, playerUid))
    local requestParamsJson = MF.util.table2String(requestParams)

    local key = "token:" .. token
    local value = registerParamsJson .. "#" .. requestParamsJson
    MF.redis.executeSync(RedisConfig.Login, "SET", key, value)
    MF.http.httpResponse(fd, retResult)
end

return M