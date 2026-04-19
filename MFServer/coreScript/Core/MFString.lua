local extend = require "MF.extend"

function string.isBlank(str)
    if str == nil or string.len(str) == 0 then
        return true
    end
    return false
end

function string.split(str, delimiter)
    return extend.split(str, delimiter)
end

function string.hash(str)
    return extend.strHash(str)
end