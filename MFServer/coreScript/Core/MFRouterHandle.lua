---@class MFRouterHandle
local MFRouterHandle = {}

local modulePathMap = {}

---已经初始化的
local newModuleList = {}

function MFRouterHandle.getModulePath(router)
    local modulePath = modulePathMap[router]
    if not modulePath then
        modulePath = MFApplication.getModulePath(tostring(router))
        modulePathMap[router] = modulePath
    end
    if not modulePath then
        MF.core.log("can not find router = {}", router)
    end
    return modulePath
end

function MFRouterHandle.getRouter(router)
    local object = newModuleList[router]
    if object then
        return object
    end
    if not newModuleList[router] then
        local modulePath = MFRouterHandle.getModulePath(router)
        if modulePath then
            object = require(modulePath).new()
            newModuleList[router] = object
            return object
        end
    end
    return nil
end

---router是否有效
function MFRouterHandle.isValid(router)
    if not router then
        MF.core.log("can not find router is null")
        return false
    end
    return MFRouterHandle.getModulePath(router) ~= nil
end

function MFRouterHandle.executeHandle(router, ...)
    if not MFRouterHandle.isValid(router) then
        MF.core.log("can not find router = {}", router)
        return
    end
    MFRouterHandle.getRouter(router):execute(...)
end

return MFRouterHandle