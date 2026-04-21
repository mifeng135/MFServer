require "MFConfig"

local projectPath = MFUtil.getCurrentPath()
MFUtil.addSearchPath(projectPath .. "/" .. MFConfig.scriptRoot)

require "Common.HttpRouter"
require "Common.ProtoMsg"

MFApplication.log("========================================================")
MFApplication.log("               MFServer start                           ")
MFApplication.log("========================================================")

MFApplication.setScriptRoot(MFConfig.scriptRoot)
MFApplication.setServicePaths(MFConfig.servicePaths)

MFApplication.setSqlMapperRoot(MFConfig.sqlMapperRoot)
MFApplication.preloadProto(MFConfig.protoDir)
MFApplication.setWorkThread(MFConfig.nativeWorkCount)
MFApplication.scanRouter(MFApplication.getScriptRoot())

MFUtil.jsonLoad(projectPath .. "/script/JsonConfig")
MFUtil.preloadSharedTable(projectPath .. "/script/Config")

MFLuaServiceManager.init(MFConfig.scriptWorkCount)
MFLuaServiceManager.addUniqueService("LoginServer")
