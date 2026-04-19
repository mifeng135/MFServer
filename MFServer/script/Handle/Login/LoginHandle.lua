local LoginUtil = require "Handle.Login.Util.LoginUtil"

MFRouterHandle({ router = HttpRouter.login })
---@class LoginHandler
local LoginHandler = class("LoginHandler")
local M = LoginHandler

---@param data ClientParams
function M:execute(data, fd)
    self:loginWithUid(data, fd)
end

---@param data ClientParams
function M:loginWithUid(data, fd)
    local uid = tonumber(data.accountId)
    local opt = {}
    opt.id = SqlMapperKey.playerUidSelect
    opt.params = { uid = uid }

    ---@type SqlPlayerUid
    local sqlPlayerUid = MF.sql.run(opt)
    if not sqlPlayerUid then
        MF.http.httpSendError(fd, ErrorCode.LOGIN_ACCOUNT_ABNORMAL)
        return
    end

    opt.id = SqlMapperKey.playerAccountSelect
    opt.params = { accountId = sqlPlayerUid.accountId }
    local sqlPlayerAccount = MF.sql.run(opt)
    if not sqlPlayerAccount then
        MF.http.httpSendError(fd, ErrorCode.LOGIN_ACCOUNT_ABNORMAL)
        return
    end
    LoginUtil.login(uid, sqlPlayerAccount, sqlPlayerUid, data, fd)
end

return M