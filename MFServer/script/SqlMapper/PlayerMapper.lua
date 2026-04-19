---@class SqlPlayer
---@field uid number
---@field showUid string
---@field name string
---@field pic string
---@field country string
---@field ip string
---@field language string
---@field serverId number
---@field level number
---@field shipId number
---@field createServerId number
---@field worldServerId number
---@field worldType number
---@field allianceId number
---@field regTime number
---@field lastLoginTime number
---@field lastLogoutTime number
---@field initState number
---@field power number
---@field powerMap string
---@field frameId number
---@field bubbleId number
---@field vipLevel number
---@field gmFlag number
---@field payDollarTotal number
---@field satiety number
---@field sex number
---@field sign string
---@field nationalFlag number
---@field birth string
---@field chatBanTime number
---@field chatBanType number
---@field platformType string
---@field banTime number
---@field deletePlayerApplyTime number
---@field totalOnlineTime number
---@field abTest number
---@field initMovieId number
---@field adventureLevel number
---@field adventureExp number
---@field adventureMainShipBuff number
---@field title number
---@field parseRegisterId string
---@field offlinePush number
---@field offsetTime number
---@field crossServerType number
---@field crossServerReason number
---@field kingdomOfficial number
---@field regCountry string
---@field payTotalTimes number
---@field copyServerId number
---@field copySceneUuid number
---@field chatBanOperator string
---@field chatBanReason string

local PlayerMapper = {}

PlayerMapper[SqlMapperKey.playerSelect] = {
    sql = "SELECT * FROM player WHERE uid = #{uid} limit 1",
    db = SqlConfig.login,
    mode = Sql_QUERY_ONE
}

return PlayerMapper