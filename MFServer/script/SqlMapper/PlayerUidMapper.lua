---@class SqlPlayerUid
---@field uid number
---@field showUid string
---@field accountId string
---@field playerName string
---@field serverId number
---@field regTime number
---@field clientVersion string
---@field clientVersionCode string
---@field ip string
---@field country string
---@field lastLoginTime number
---@field delete number
---@field language string
---@field platformType string
---@field isRestart number


local PlayerUidMapper = {}

PlayerUidMapper[SqlMapperKey.playerUidSelect] = {
    sql = "SELECT * FROM player_uid WHERE uid = #{uid} limit 1",
    db = SqlConfig.login,
    mode = Sql_QUERY_ONE
}

return PlayerUidMapper