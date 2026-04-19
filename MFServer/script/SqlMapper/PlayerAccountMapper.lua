
---@class SqlPlayerAccount
---@field accountId string
---@field deviceId string
---@field clientVersion string
---@field clientVersionCode string
---@field ip string
---@field country string
---@field city string
---@field language string
---@field packageId string
---@field deviceModelId string
---@field deviceOsVersion string
---@field deviceOs string
---@field firebaseId string
---@field IDFA string
---@field GAID string
---@field OAID string
---@field regTime number
---@field appsflyerId string
---@field accountState number
---@field banReason number
---@field banTime number
---@field fromPlatform string
---@field regPlatformType string
---@field lastUid number
---@field serverPlayerCount string
---@field mappingState string
---@field gmFlag number
---@field clientCountry string
---@field whiteCountry string

local PlayerAccountMapper = {}

PlayerAccountMapper[SqlMapperKey.playerAccountSelect] = {
    sql = "SELECT * FROM player_account WHERE accountId = #{accountId} limit 1",
    db = SqlConfig.login,
    mode = Sql_QUERY_ONE
}

return PlayerAccountMapper