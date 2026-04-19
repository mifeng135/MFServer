--- C++ 导出的 Lua API 声明（EmmyLua 注解，便于 IDE 补全与跳转）
--- 实际实现由 MFLuaExport.cpp 注入；本文件仅声明类型，不覆盖全局表

---@class MFUtil
---@field isDebug fun():boolean
---@field getMilliseconds fun():number
---@field getCurrentPath fun():string
---@field readFile fun(path:string):string
---@field writeFile fun(path:string, content:string):boolean
---@field getFileList fun(path:string):table
---@field isLinux fun():boolean
---@field isWindows fun():boolean
---@field genSessionId fun():number
---@field genConvId fun():number
---@field addSearchPath fun(path:string)
---@field ensureExportHttpRequest fun()
---@field preloadSharedTable fun(rootDir:string)
---@field luaShareTable fun(moduleName:string, filename:string):number
---@field luaUpdateTable fun(moduleName:string, filename:string):number
---@field luaQueryTable fun(moduleName:string):table|nil
---@field luaTableGeneration fun(moduleName:string):number
---@field jsonLoad fun(fullPath:string):boolean, string
MFUtil = {}

---@class MFApplication
---@field log fun(msg:string)
---@field serviceLog fun(serviceName:string, msg:string, ...:any)
---@field scheduleOnce fun(second:number, serviceId:number):number
---@field schedule fun(second:number, serviceId:number):number
---@field stopSchedule fun(timerId:number)
---@field setWorkThread fun(ioTheadCount:number)
---@field shutDown fun()
---@field getScriptRoot fun():string
---@field setScriptRoot fun(path:string)
---@field initSnowflake fun(timestamp:number, workerId:number, datacenterId:number)
---@field nextId fun():number
---设置启动的service查找路径
---@field setServicePaths fun(dstList:table)
---@field setSqlMapperRoot fun(path:string)
---@field getSqlMapperRoot fun():string
---@field preloadProto fun(path:string)
---@field getProtoContent fun():table
---@field scanRouter fun(path:string)
---@field getModulePath fun(key:string):string|nil
MFApplication = {}

---@class MFNativeNet
---@field createTcpServer fun(configId:number, serviceId:number, type:number)
---@field createHttpServer fun(configId:number, serviceId:number)
---@field createWebSocketServer fun(configId:number, serviceId:number)
---@field createUdpServer fun(configId:number, serviceId:number)
---@field createHttpClient fun(host:string, request:HttpRequest, serviceId:number):number
---@field stopTcpServer fun(configId:number)
---@field setTcpServerIdleTime fun(configId:number, time:number)
MFNativeNet = {}

---@class MFShareData
---@field update fun(key:number, data:userdata, len:number)
---@field get fun(key:number):table, table
---@field remove fun(key:number)
MFShareData = {}

---@class MFLuaServiceManager
---@field init fun(threadCount:number)
---@field addService fun(serviceName:string):number
---@field addUniqueService fun(serviceName:string):number
---@field queryUniqueService fun(serviceName:string):number
---@field closeService fun(serviceId:number)
---@field send fun(src:number, dst:number, data:userdata, len:number, msgType:number, genSessionId:boolean, usePriority:boolean):number
---@field ret fun(src:number, dst:number, data:userdata, len:number, sessionId:number, messageType:number, usePriority:boolean)
---@field sendMulti fun(src:number, data:userdata, len:number, dstList:table)
MFLuaServiceManager = {}

---@class MFNativeRedis
---@field executeAsync fun(serviceId:number, key:number, ...:any):number
---@field executeSync fun(key:number, ...:any):any
MFNativeRedis = {}

---@class MFNativeMysql
---@field queryAsync fun(sql:string, service:number, key:number):number
---@field queryOneAsync fun(sql:string, service:number, key:number):number
---@field executeAsync fun(sql:string, service:number, key:number):number
---@field beginTransaction fun(service:number, key:number):number
---@field executeInTransaction fun(transactionId:number, sql:string, service:number, key:number):number
---@field commitTransaction fun(transactionId:number, service:number, key:number):number
---@field rollbackTransaction fun(transactionId:number, service:number, key:number):number
MFNativeMysql = {}

---@class MFSocket
---@field send fun(fd:number, msg:string, len:number)
---@field forceCloseConnection fun(fd:number)
---@field httpResponse fun(fd:number, body:string, code:number)
---@field httpResponseParams fun(fd:number, body:string, code:number, headers:table)
---@field udpSend fun(conv:number, msg:string, len:number)
---@field udpSendAll fun(msg:string, len:number)
---@field webSocketSend fun(fd:number, msg:string, len:number, int sendType)
---@field forceWebCloseConnection fun(fd:number)
---@field rpcClientSend fun(configId:number, msg:string, len:number)
---@field httpRequest fun(serviceId:number, host:string, method:number, path?:string, body?:string, headers?:table)
MFSocket = {}

---@class HttpRequest
---@field setPath fun(self:HttpRequest, path:string)
---@field setMethod fun(self:HttpRequest, method:any)
---@field newHttpRequest fun():HttpRequest
---@field addHeader fun(self:HttpRequest, name:string, value:string)
---@field setBody fun(self:HttpRequest, body:string)

---@class MFProfiler
---@field startProfiler fun()
---@field stopProfiler fun()
---@field resetProfiler fun()
---@field reportProfiler fun(maxDepth?:number)
MFProfiler = {}

return nil
