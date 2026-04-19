local coroutineRunning = coroutine.running
local coroutineYield = coroutine.yield


---@class MFLuaSql
local MFLuaSql = {}
MFLuaSql.sqlAsyncMapFn = {}

local coMap = {}


function MFLuaSql.queryAsyncCall(sql, dbName, fn)
    local sessionId = MFNativeMysql.queryAsync(sql, MF.core.serviceId, dbName)
    MFLuaSql.sqlAsyncMapFn[sessionId] = fn
end

function MFLuaSql.queryOneAsyncCall(sql, dbName, fn)
    local sessionId = MFNativeMysql.queryOneAsync(sql, MF.core.serviceId, dbName)
    MFLuaSql.sqlAsyncMapFn[sessionId] = fn
end

function MFLuaSql.executeAsyncCall(sql, dbName, fn)
    local sessionId = MFNativeMysql.executeAsync(sql, MF.core.serviceId, dbName)
    if fn then
        MFLuaSql.sqlAsyncMapFn[sessionId] = fn
    end
end

--------------------------------Coroutine------------------------------------
function MFLuaSql.queryCoroutine(sql, db)
    local sessionId = MFNativeMysql.queryAsync(sql, MF.core.serviceId, db)
    coMap[sessionId] = coroutineRunning()
    local result, isSuccess = coroutineYield "suspended"
    if not isSuccess then
        return nil
    end
    return result
end

function MFLuaSql.queryOneCoroutine(sql, db)
    local sessionId = MFNativeMysql.queryOneAsync(sql, MF.core.serviceId, db)
    coMap[sessionId] = coroutineRunning()
    local result, isSuccess = coroutineYield "suspended"
    if not isSuccess then
        return nil
    end
    return result
end

function MFLuaSql.executeCoroutine(sql, dbName)
    local sessionId = MFNativeMysql.executeAsync(sql, MF.core.serviceId, dbName)
    coMap[sessionId] = coroutineRunning()
    return coroutineYield "suspended"
end





--------------------------------Transaction------------------------------------
--- 开启事务
---@param dbName string 数据库名称
---@return number|nil transactionId 事务ID
function MFLuaSql.beginTransaction(dbName)
    local transactionId = MFNativeMysql.beginTransaction(MF.core.serviceId, dbName)
    coMap[transactionId] = coroutineRunning()
    local isSuccess = coroutineYield "suspended"
    if not isSuccess then
        return nil
    end
    return transactionId
end

--- 在事务中执行SQL
---@param transactionId number 事务ID
---@param sql string SQL语句
---@param dbName string 数据库名称
---@return boolean isSuccess 是否执行成功
function MFLuaSql.executeInTransaction(transactionId, sql, dbName)
    local sessionId = MFNativeMysql.executeInTransaction(transactionId, sql, MF.core.serviceId, dbName)
    coMap[sessionId] = coroutineRunning()
    return coroutineYield "suspended"
end

--- 提交事务
---@param transactionId number 事务ID
---@param dbName string 数据库名称
---@return boolean isSuccess 是否提交成功
function MFLuaSql.commitTransaction(transactionId, dbName)
    local sessionId = MFNativeMysql.commitTransaction(transactionId, MF.core.serviceId, dbName)
    coMap[sessionId] = coroutineRunning()
    return coroutineYield "suspended"
end

--- 回滚事务
---@param transactionId number 事务ID
---@param dbName string 数据库名称
---@return boolean isSuccess 是否回滚成功
function MFLuaSql.rollbackTransaction(transactionId, dbName)
    local sessionId = MFNativeMysql.rollbackTransaction(transactionId, MF.core.serviceId, dbName)
    coMap[sessionId] = coroutineRunning()
    return coroutineYield "suspended"
end

---@param execRes boolean 如果为true 则不是查询语句
---@param isSuccess boolean 是否执行语句成功
function MFLuaSql.onExecuteCallBack(sessionId, execRes, isSuccess)
    local co = coMap[sessionId]
    if co then
        MF.core.coResume(co, isSuccess, execRes)
        coMap[sessionId] = nil
    else
        local fn = MFLuaSql.sqlAsyncMapFn[sessionId]
        if fn then
            fn(isSuccess, execRes)
            MFLuaSql.sqlAsyncMapFn[sessionId] = nil
        end
    end
end

function MFLuaSql.onQueryCallBack(sessionId, data, isSuccess)
    local co = coMap[sessionId]
    if co then
        MF.core.coResume(co, data, isSuccess)
        coMap[sessionId] = nil
    else
        local fn = MFLuaSql.sqlAsyncMapFn[sessionId]
        if fn then
            fn(data, isSuccess)
            MFLuaSql.sqlAsyncMapFn[sessionId] = nil
        end
    end
end


return MFLuaSql