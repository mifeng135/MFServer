--//Lua消息类型
LuaMessageTypeLuaSend           = 0 --Lua消息
LuaMessageTypeLuaRequest        = 1 --请求某个service 需要调用ret返回
LuaMessageTypeLuaResponse       = 2 --返回给Request

LuaMessageTypeSocket            = 3 --tcpServer收到消息
LuaMessageTypeTimer             = 4 --定时器
LuaMessageTypeHttpServerReq     = 5 --http server 收到消息
LuaMessageTypeWebSocket         = 6 --websocket
LuaMessageTypeHttpClientRsp     = 7 --http客户端请求返回
LuaMessageTypeHotReload         = 8 --lua hotreload

LuaMessageTypeTimerOnce         = 10 --定时器once
LuaMessageTypeSocketClient      = 11 --tcpClient 收到消息

LuaMessageTypeRpcReq            = 14 --rpc 服务器收到来自客户端的消息
LuaMessageTypeRpcRsp            = 15 --rpc 客户端收到来自服务器回应的消息

LuaMessageTypeMysqlQuery        = 20 --mysql query 查询
LuaMessageTypeMysqlExecute      = 21 --mysql execute

LuaMessageTypeMultiSend         = 22 --Lua给多个service发送消息
LuaMessageTypeRedisCmd          = 23 --redis命令
LuaMessageTypeCloseService      = 24 --关闭服务
LuaMessageTypeReloadConfig      = 25 --更新配置
LuaMessageTypeUdpServer         = 26 --udp服务器收到消息


LuaMessageTypeMAX               = 255

--luaMsgCmd
SocketOpen                      = 1
SocketClose                     = 2
SocketRead                      = 3

---http请求类型
HttpGet                         = 0
HttpPost                        = 1
HttpHead                        = 2
HttpPut                         = 3
HttpDelete                      = 4
HttpOptions                     = 5
HttpPatch                       = 6
HttpInvalid                     = 7

MFSuccess                       = 1
MFFail                          = 0

---tcp type
MFNetTypeNormal                 = 1
MFNetTypeRPC                    = 2

---websocket sendType
WebSocketText                            = 0
WebSocketBinary                          = 1
WebSocketPing                            = 2
WebSocketPong                            = 3
WebSocketClose                           = 4
