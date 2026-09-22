--- @class SharedConfig 跨 VM 共享配置表的读取入口
local SharedConfig = {}

local tbls = {}
local gens = {}

--- @param name string 表名，即 script/Config 下的文件名（不含扩展名）
--- @return table|nil
function SharedConfig.get(name)
    local gen = MFUtil.shareTableGeneration(name)
    if gen == 0 then
        return nil
    end
    if gens[name] == gen then
        return tbls[name]
    end
    local tbl = MFUtil.queryShareTable(name)
    if not tbl then
        return nil
    end
    tbls[name] = tbl
    gens[name] = gen
    return tbl
end

--- @param name string|nil 不传则清空全部
function SharedConfig.release(name)
    if name then
        tbls[name] = nil
        gens[name] = nil
    else
        tbls = {}
        gens = {}
    end
end

return SharedConfig
