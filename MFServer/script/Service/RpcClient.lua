require "Common.GlobalRequire"



function main(serviceId, serviceName)
    MF.core.start(serviceId, serviceName)
    local v1, v2 = MF.rpcClient.send(RpcConfig.Gate29, 1, "22", "33")
    MF.core.coCreate(function()
        v1, v2 = MF.rpcClient.request(RpcConfig.Gate29, 1, "22", "33")
        MF.core.log("v1 = {}, v2 = {}", v1, v2)
    end)
end




