# MFServer

一个基于 **C++17 + Lua** 的高性能、多服务（Actor 模型）游戏/通用服务器框架。

C++ 核心负责网络、IO、存储与调度，Lua 层承载业务逻辑

支持 **TCP / UDP(KCP) / WebSocket / HTTP(S)** 协议

支持**MySQL / Redis** 连接池，以及 Protobuf 消息、SqlMapper、RPC、文件热更新、共享数据、协程异步等能力。


## 架构概览

```
                    ┌────────────────────────────────────────────────┐
                    │                  MFApplication                 │
                    │ ┌────────────┐ ┌────────────┐ ┌─────────────┐  │
                    │ │ MFNetMgr   │ │ MFIoPool   │ │ MFFileWatch │  │
                    │ └────────────┘ └────────────┘ └─────────────┘  │
                    │ ┌────────────┐ ┌────────────┐ ┌─────────────┐  │
                    │ │ Snowflake  │ │ ShareData  │ │   spdlog    │  │
                    │ └────────────┘ └────────────┘ └─────────────┘  │
                    └───────────────────────┬────────────────────────┘
                                            │
                   ┌────────────────────────┴────────────────────────┐
                   │                MFLuaServiceManager              │
                   └───┬────────────┬────────────┬────────────┬──────┘
                       ▼            ▼            ▼            ▼
                 ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐┌──────────┐
                 │ LoginSrv │ │ WebSockS │ │ RpcClient│ │  ...     ││  ...     │
                 │ (Lua VM) │ │ (Lua VM) │ │ (Lua VM) │ │ (Lua VM) ││ (Lua VM) │
                 └────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘└────┬─────┘
                                  消息队列 / 协程 / 共享数据        
                                              │
                        ┌─────────────────────┼──────────────────────┐
                        ▼                     ▼                      ▼
                  ┌───────────┐         ┌────────────┐           ┌───────────┐
                  │ MySQL Pool│         │ Redis Pool │           │ Drogon/IO │
                  └───────────┘         └────────────┘           └───────────┘
```

- **消息类型**：`LuaMessageTypeWebSocket` / `LuaMessageTypeHttpServerReq` / `LuaMessageTypeLuaSend` / 定时器 / RPC / DB 回调等。
- **协程模型**：`MF.core.call` 发起请求后挂起协程，回执回到目标 Service 时自动唤醒，写法同步化。
- **热更新**：开发期 `MFFileWatch` 监听脚本路径，触发 Lua 侧重新加载。

## 构建与运行

### Linux (CMake)

```bash
cd MFServer
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j

../bin/MFServer
```

### macOS (Xcode)

```bash
open MFServer.xcodeproj
```

### Windows (Visual Studio)

打开 `server.slnx`。

### 生成 Protobuf（新增 / 修改 `.proto` 后可以通过）
```bash
cd MFServer
python3 tools/buildProtoMsg.py
```

## 配置

### `MFServer/MFConfig.lua`（启动时加载）

```lua
MFConfig = {}
MFConfig.scriptRoot      = "script"        -- 业务脚本根目录
MFConfig.nativeWorkCount = 4               -- Native IO 线程数
MFConfig.scriptWorkCount = 4               -- Lua Service 工作线程数
MFConfig.sqlMapperRoot   = "SqlMapper"     -- SqlMapper 子目录
MFConfig.protoDir        = "generProto"    -- Protobuf 描述符目录
MFConfig.servicePaths    = { "Service" }   -- Service 脚本查找路径
```

### `MFServer/script/JsonConfig/*.json`（运行期 JSON 配置）

- `SocketConfig.json`：TCP / HTTP / WebSocket 监听（如 `webSocket` 配置项）
- `SqlConfig.json`：MySQL 多库配置（`ip / port / userName / password / database / minPoolSize / maxPoolSize / maxIdleTime`）
- `RedisConfig.json`：Redis 配置
- `RpcConfig.json`：RPC 节点配置

示例（`SqlConfig.json`）：

```json
{
  "1": {
    "ip": "127.0.0.1", "port": 3306,
    "userName": "root", "password": "123456",
    "database": "your_db",
    "minPoolSize": 4, "maxPoolSize": 8, "maxIdleTime": 10000
  }
}
```

---

## 快速开始：写一个业务 Service

### 1. 定义 Protobuf

`MFServer/protoFile/login.proto`：

```proto
message LoginReq  { option (msgId) = 1001; string token = 1; }
message LoginPush { option (msgId) = 1002; int64  uid   = 8; }
```

生成：`python3 tools/buildProtoMsg.py`。

### 2. 注册路由 Handler

`MFServer/script/Handle/Login/LoginHandle.lua`：

```lua
MFRouterHandle({ router = HttpRouter.login })
local LoginHandler = class("LoginHandler")
function LoginHandler:execute(data, fd)
    local uid = tonumber(data.accountId)
    local opt = { id = SqlMapperKey.playerUidSelect, params = { uid = uid } }
    local row = MF.sql.run(opt)
    if not row then
        MF.http.httpSendError(fd, ErrorCode.LOGIN_ACCOUNT_ABNORMAL); return
    end
    -- 业务逻辑…
end
return LoginHandler
```

### 3. 启动一个 Service

`MFServer/script/Service/WebSocketServer.lua`：

```lua
require "Common.GlobalRequire"

local function dispatch(cmd, fd, viewData, messageType)
    if cmd == SocketRead then
        MF.websocket.sendText(fd, viewData)
    end
end

function main(serviceId, serviceName)
    MF.core.start(serviceId, serviceName)
    MF.core.registerMessageProto({
        { messageType = LuaMessageTypeWebSocket, dispatch = dispatch },
    })
    MF.websocket.createWebSocketServer(SocketConfig.webSocket)
end
```

在 `main.lua` 中注册为唯一服务：

```lua
MFLuaServiceManager.addUniqueService("WebSocketServer")
```

### 4. 新增 SqlMapper

`MFServer/script/SqlMapper/PlayerMapper.lua`：

```lua
PlayerMapper[SqlMapperKey.playerSelect] = {
    sql  = "SELECT * FROM player WHERE uid = #{uid} limit 1",
    db   = SqlConfig.login,
    mode = Sql_QUERY_ONE,
}
```

---

