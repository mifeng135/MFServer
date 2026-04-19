local cjson = require "cjson"
local extend = require "MF.extend"
local pb = require "pb"


---@class MFLuaUtil
local MFLuaUtil = {}

---@param timestamp number sample 1577836800000
---@param workerId number sample 1
---@param datacenterId number sample 1
function MFLuaUtil.initSnowflake(timestamp, workerId, datacenterId)
    MFApplication.initSnowflake(timestamp, workerId, datacenterId)
end

function MFLuaUtil.loadProto()
    local protoContent = MFApplication.getProtoContent()
    for _, content in pairs(protoContent) do
        local ok, pos = pb.load(content)
        if not ok then
            error("pb.load failed at offset " .. tostring(pos))
        end
    end
end

function MFLuaUtil.genUuid()
    return MFApplication.nextId()
end

---数字转为16进制字符串
function MFLuaUtil.numberToHex(num)
    return string.format("%X", num)
end

function MFLuaUtil.table2String(t)
    if not t then
        return ""
    end
    return cjson.encode(t)
end

function MFLuaUtil.string2Table(str)
    if not str then
        return nil
    end
    return cjson.decode(str)
end

function MFLuaUtil.uidEncode(uint64)
    return extend.uint64Encode(uint64)
end


return MFLuaUtil



