#ifndef MFMacro_h
#define MFMacro_h

#include "container/unordered_dense.h"
#include "queue/blockingconcurrentqueue.h"

#define MF_DLL __attribute__ ((visibility("default")))

static constexpr short SUCCESS = 0;
static constexpr short FAIL = 1;

using MFServiceId_t = uint32_t;

#define MFFastMap ankerl::unordered_dense::map
#define MFBlockingQueue moodycamel::BlockingConcurrentQueue
#define MFConcurrentQueue moodycamel::ConcurrentQueue


#define MFProperty(varType, varName, funName) \
protected: varType varName; public: inline varType get##funName(void) const { return varName; } inline void set##funName(varType var){ varName = var; }

#define MFPropertyRef(varType, varName, funName) \
protected: varType varName; \
public: \
    inline const varType& get##funName(void) const { return varName; } \
    inline void set##funName(const varType& var) { varName = var; } \
    inline void set##funName(varType&& var) { varName = std::move(var); } 


#define LuaMessageTypeLuaSend               0 //Lua消息
#define LuaMessageTypeLuaRequest            1 //请求某个service 需要调用ret返回
#define LuaMessageTypeLuaResponse           2 //返回给call

#define LuaMessageTypeSocket                3 //网络消息
#define LuaMessageTypeTimer                 4 //定时器
#define LuaMessageTypeHttpServerReq         5 //http server
#define LuaMessageTypeWebSocket             6 //websocket
#define LuaMessageTypeHttpClientRsp         7 //http client
#define LuaMessageTypeHotReload             8 //lua hotreload

#define LuaMessageTypeTimerOnce             10 //定时器once
#define LuaMessageTypeSocketClient          11 //客户端消息

#define LuaMessageTypeRpcReq                14 //rpc 服务器收到来自客户端的消息
#define LuaMessageTypeRpcRsp                15 //rpc 客户端收到来自服务器回应的消息

#define LuaMessageTypeMysqlQuery            20 //mysql query 查询
#define LuaMessageTypeMysqlExecute          21 //mysql execute

#define LuaMessageTypeMultiSend             22 //Lua给多个service发送消息

#define LuaMessageTypeRedisCmd              23 //redis命令
#define LuaMessageTypeCloseService          24 //关闭服务
#define LuaMessageTypeReloadConfig          25 //更新配置

#define LuaMessageTypeUdpServer             26 //udp服务器收到消息


#define LuaMessageTypeMAX                   255

#define MFNetTcpServer                        100
#define MFNetTcpClient                        101


#define MFNetTypeNormal                       1
#define MFNetTypeRPC                          2

#define MFSocketOpen                          1
#define MFSocketClose                         2
#define MFSocketRead                          3


#define MFRedisConfig   "RedisConfig"
#define MFRpcConfig     "RpcConfig"
#define MFSqlConfig     "SqlConfig"
#define MFSocketConfig  "SocketConfig"


#endif /* MFMacro_h */
