local MFLuaSql = require "Core.MFLuaSql"

Sql_QUERY_ONE = 1
Sql_QUERY_LIST = 2
Sql_EXECUTE = 3

---@class MFSqlMapper
local MFSqlMapper = {}

MFSqlMapper._registry = {}
MFSqlMapper._loadedNamespaces = {}
MFSqlMapper._mapperBasePath = MFApplication.getSqlMapperRoot()

--- @param id string 如 "user.selectById"
---@private
function MFSqlMapper.ensureNamespaceLoaded(id)
    local namespace = id:match("^([%w_]+)%.")
    if not namespace then
        return
    end
    if MFSqlMapper._loadedNamespaces[namespace] then
        return
    end
    local modName = MFSqlMapper._mapperBasePath .. "." .. namespace .. "Mapper"
    local ok, mod = pcall(require, modName)
    if ok and type(mod) == "table" then
        MFSqlMapper.registerBatch(mod)
        MFSqlMapper._loadedNamespaces[namespace] = true
    end
end

--- 批量注册。值为 string 或 { sql, tableName? } 或 { sql, defaultCols? }
--- @param map table
function MFSqlMapper.registerBatch(map)
    for id, val in pairs(map) do
        MFSqlMapper._registry[id] = val
    end
end


--- 解析模板并替换所有占位符，返回可直接执行的最终 SQL。
--- #{key} → escapeValue(params[key])；${key} → escapeFiledValue 或列列表。
--- @param template string
--- @param params table 命名参数表
--- @return string sql 已替换好的最终 SQL
function MFSqlMapper.parseTemplate(template, params)
    local sql = template:gsub("#{(%w+)}", function(key)
        return MFSqlMapper.escapeValue(params[key])
    end)
    return sql
end

function MFSqlMapper.escapeValue(str)
    if str == nil then
        return "NULL"
    elseif type(str) == "string" then
        local s = str:gsub("\\", "\\\\"):gsub("'", "''")
        return "'" .. s .. "'"
    end
    return tostring(str)
end

--- @param opt table { id, params mode }
---   - id: 注册的语句 id
---   - params: 命名参数，用于 #{key} 与 ${key}
function MFSqlMapper.run(opt)
    local id = opt.id
    MFSqlMapper.ensureNamespaceLoaded(id)
    local entry = MFSqlMapper._registry[id]
    if not entry then
        error("MFSqlMapper: 未找到 SQL id = " .. id)
    end
    local finalSql = MFSqlMapper.parseTemplate(entry.sql, opt.params)
    local dbName = entry.db
    local mode = entry.mode
    if mode == Sql_QUERY_LIST then
        return MFLuaSql.queryCoroutine(finalSql, dbName)
    elseif mode == Sql_QUERY_ONE then
        return MFLuaSql.queryOneCoroutine(finalSql, dbName)
    end
    return MFLuaSql.executeCoroutine(finalSql, dbName)
end

---@param execRes boolean 如果为true 则不是查询语句
---@param isSuccess boolean 是否执行语句成功
function MFSqlMapper.onExecuteCallBack(sessionId, execRes, isSuccess)
    MFLuaSql.onExecuteCallBack(sessionId, execRes, isSuccess)
end

function MFSqlMapper.onQueryCallBack(sessionId, data, isSuccess)
    MFLuaSql.onQueryCallBack(sessionId, data, isSuccess)
end

return MFSqlMapper
