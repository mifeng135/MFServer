local serialize = require "MF.serialize"

---@class MFShare
local MFShare = {}

function MFShare.update(key, value)
    local data, len = serialize.pack(value)
    MFShareData.update(key, data, len)
end

---@param key number
function MFShare.remove(key)
    MFShareData.remove(key)
end

function MFShare.get(key)
    local data, len = MFShareData.get(key)
    if not data then
        return nil
    end
    return serialize.unpack(data, len)
end

return MFShare