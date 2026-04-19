# MFServer

一个基于 **C++17 + Lua** 的高性能、多服务（Actor 模型）游戏/通用服务器框架。C++ 核心负责网络、IO、存储与调度，Lua 层承载业务逻辑，借助 `sol2` 完成双向绑定，支持 **TCP / UDP(KCP) / WebSocket / HTTP(S)** 协议、**MySQL / Redis** 连接池，以及 Protobuf 消息、SqlMapper、RPC、文件热更新、共享数据、协程异步等能力。

---

## 功能特性

- **多服务 Actor 模型**：由 `MFLuaServiceManager` 管理，每个 Service 跑在独立的 Lua 虚拟机上，以消息队列 + 协程驱动，彼此隔离、可横向扩展。
- **全协议网络层**（基于 Drogon / Trantor）：
  - TCP Server/Client（`MFTcpServer`、`MFTcpClient`）
  - UDP Server/Channel，内置 **KCP** 可靠传输（`MFUdpServer`、`MFUdpChannel*`、`3rd/kcp`）
  - WebSocket Server（`MFWebServer`）
  - HTTP/HTTPS Server & Client（`MFWebServer`，Lua 侧 `MF.http`）
  - 内置注解式路由（`MFAnnotationHandle` + `MFRouterHandle`）
- **存储层**：
  - MySQL 异步客户端 + 连接池（`MFMysqlClient` / `MFMysqlConnectPool`）
  - Redis 异步客户端 + 连接池（`MFRedisClient` / `MFRedisConnectPool`）
  - MyBatis 风格的 **SqlMapper**（`script/SqlMapper/*.lua`，`#{param}` 占位符）
- **Protobuf 工作流**：`protoFile/*.proto` → `tools/buildProtoMsg.py` 生成 `generProto/*.pb` 及 `script/Common/ProtoMsg.lua`（`msgDefine` / `msgFactory`）。
- **Lua 生态**：
  - `sol2` 无缝绑定，`coreScript/Core/MF.lua` 统一入口（懒加载子模块 `core / sql / redis / util / router / share / http / rpc* / tcp / udp / websocket`）
  - 协程化 **call/ret**，同步式写法写异步 IO（见 `MFCore.lua` / `MFMsgHandlers.lua`）
  - **字节码缓存**：`MFLuaByteCodeCache` 降低冷启动开销
  - **性能分析**：`MFLuaProfiler`（`MFProfiler.startProfiler / reportProfiler`）
- **并发与同步工具**：`MFSpinLock`、`MFStripedMap`、`MFObjectPool`、无锁队列（`3rd/container`、`3rd/queue`）。
- **跨服务共享**：`MFLuaShareData` 提供只读快照 + 版本更新，多 Service 零拷贝共享配置。
- **文件热更新**：基于 `dmon`（`MFFileWatch`）监听脚本目录，开发期零重启改 Lua。
- **Snowflake ID 生成器**：`MFApplication.initSnowflake` / `nextId`。
- **日志**：`spdlog`（日滚 + 彩色控制台 + 按 Service 打标）。
- **高性能内存**：链接 `jemalloc`。
- **JSON 配置热加载**：`MFJsonConfig` 加载 `script/JsonConfig/*.json`，Lua 侧按名自动可见。

---

## 目录结构

```
MFServer/
├── MFServer/                   # C++ 核心 + Lua 脚本目录（构建单位）
│   ├── CMakeLists.txt          # CMake 构建脚本（macOS/Linux）
│   ├── main.cpp                # C++ 入口 → MFApplication::run()
│   ├── main.lua                # Lua 入口（由核心在启动后加载）
│   ├── MFConfig.lua            # 根路径、线程数、proto 目录等
│   ├── core/                   # C++ 核心实现（MFApplication / 网络 / DB / Lua 绑定 …）
│   ├── coreScript/             # 框架侧 Lua（被所有 Service 复用）
│   │   ├── Core/               # MF.lua / MFCore / MFHttp / MFSqlMapper / …
│   │   └── api.lua             # C++ 导出 Lua API 的 EmmyLua 注解（IDE 补全）
│   ├── script/                 # 业务脚本
│   │   ├── Annotate/           # 数据结构类型注解（ClientParams / RegisterParams）
│   │   ├── Common/             # GlobalRequire / HttpRouter / ProtoMsg / TableConfig
│   │   ├── Config/             # Lua 表格配置（可 preload 共享）
│   │   ├── Handle/             # 路由处理器（按模块拆分，如 Login / Game）
│   │   ├── JsonConfig/         # 运行期 JSON 配置（DB / Redis / RPC / Socket）
│   │   ├── Service/            # 业务 Service（LoginServer / WebSocketServer / Rpc* …）
│   │   └── SqlMapper/          # SQL 映射器（Player / PlayerAccount / PlayerUid）
│   ├── protoFile/              # .proto 源文件
│   ├── generProto/             # 生成的 .pb 二进制描述符
│   ├── tools/                  # protoc 与 buildProtoMsg.py
│   ├── 3rd/                    # 第三方头/源（sol、lua、drogon、trantor、spdlog、cjson、kcp、jemalloc、dmon …）
│   ├── lib/                    # 预编译静态/动态库（liblua / libdrogon / libtrantor / libjemalloc / libjsoncpp）
│   ├── bin/                    # 可执行输出
│   └── log/                    # 运行期日志
├── MFServer.xcodeproj/         # Xcode 工程（macOS）
├── server/                     # Visual Studio 工程（Windows）
├── server.slnx                 # VS 解决方案
└── README.md
```

---

## 架构概览

```
                    ┌────────────────────────────────────────────────┐
                    │                  MFApplication                 │
                    │ ┌────────────┐ ┌────────────┐ ┌─────────────┐ │
                    │ │ MFNetMgr   │ │ MFIoPool   │ │ MFFileWatch │ │
                    │ └────────────┘ └────────────┘ └─────────────┘ │
                    │ ┌────────────┐ ┌────────────┐ ┌─────────────┐ │
                    │ │ Snowflake  │ │ ShareData  │ │ spdlog      │ │
                    │ └────────────┘ └────────────┘ └─────────────┘ │
                    └───────────────────────┬────────────────────────┘
                                            │
                   ┌────────────────────────┴────────────────────────┐
                   │           MFLuaServiceManager (线程池)          │
                   └───┬────────────┬────────────┬────────────┬──────┘
                       ▼            ▼            ▼            ▼
                 ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐
                 │ LoginSrv │ │ WebSockS │ │ RpcClient│ │  ...     │
                 │ (Lua VM) │ │ (Lua VM) │ │ (Lua VM) │ │ (Lua VM) │
                 └────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘
                      └──── 消息队列 / 协程 / 共享数据 ──────┘
                                    │
              ┌─────────────────────┼──────────────────────┐
              ▼                     ▼                      ▼
        ┌───────────┐         ┌──────────┐           ┌──────────┐
        │ MySQL Pool│         │ Redis Pool│          │ Drogon/IO│
        └───────────┘         └──────────┘           └──────────┘
```

- **消息类型**：`LuaMessageTypeWebSocket` / `LuaMessageTypeHttpServerReq` / `LuaMessageTypeLuaSend` / 定时器 / RPC / DB 回调等。
- **协程模型**：`MF.core.call` 发起请求后挂起协程，回执回到目标 Service 时自动唤醒，写法同步化。
- **热更新**：开发期 `MFFileWatch` 监听脚本路径，触发 Lua 侧重新加载。

---

## 依赖

C++ 侧（大多数已随仓库提供源码或预编译库）：

- C++17 / CMake 3.12+
- [Drogon](https://github.com/drogonframework/drogon) + [Trantor](https://github.com/an-tao/trantor)（HTTP / TCP / EventLoop）
- [sol2](https://github.com/ThePhD/sol2) + Lua 5.x（Lua 绑定）
- [spdlog](https://github.com/gabime/spdlog)（日志）
- [jemalloc](https://github.com/jemalloc/jemalloc)（内存分配）
- [jsoncpp](https://github.com/open-source-parsers/jsoncpp) / cJSON
- [KCP](https://github.com/skywind3000/kcp)（可靠 UDP）
- [dmon](https://github.com/septag/dmon)（文件监听）
- zlib（macOS 使用系统库）

Python 侧（构建工具）：

- Python 3.9+
- `protoc`（macOS/Linux 在 `MFServer/tools/protoc`；Windows 请自备，或设置 `MF_PROTOC` / `PROTOC` 环境变量）

---

## 构建与运行

### macOS / Linux (CMake)

```bash
cd MFServer
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j

../bin/MFServer
```

- 产物：`MFServer/bin/MFServer`
- Debug 构建：`cmake .. -DCMAKE_BUILD_TYPE=Debug`
- 工作目录需为 `MFServer/`（`main.lua` 会 `getCurrentPath()` 后追加 `script/`）。

### macOS (Xcode)

```bash
open MFServer.xcodeproj
```

### Windows (Visual Studio)

打开 `server.slnx`（或 `server/server.vcxproj`），按 `x64` 编译。Lua、Drogon、Trantor、jsoncpp 等请自行构建或放入 `MFServer/winlib`（仓库中已提供部分）。

### 生成 Protobuf（新增 / 修改 `.proto` 后必做）

```bash
cd MFServer
python3 tools/buildProtoMsg.py
```

脚本会：

1. 调 `protoc` 把 `protoFile/*.proto` 编译为 `generProto/*.pb`（携带 `--include_imports`）；
2. 扫描每个 message 的 `option (msgId) = XXXX;`，生成 `script/Common/ProtoMsg.lua` 中的 `msgDefine` 与 `msgFactory`。

> Windows 可设置 `setx MF_PROTOC "C:\path\to\protoc.exe"`。

---

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

## 开发建议

- **IDE 补全**：`coreScript/api.lua` 是 C++ 导出到 Lua 的 EmmyLua 注解集合，配合 EmmyLua/LuaLS 可得到完整提示与跳转。
- **日志**：生产建议 Release + 按 Service 加标 `MF.core.log`。
- **热更**：改 Lua 即生效；改 C++ / `.proto` / JSON 需重启。
- **性能**：线上打开 `MFProfiler` 做 Lua 采样；`MFSpinLock` / `MFStripedMap` / `MFObjectPool` 用于热点路径。
- **建议 .editorconfig**：C++ 采 4 空格；Lua 采 4 空格。

---

## 常见问题

- **`protoc not found`**：macOS/Linux 确认 `MFServer/tools/protoc` 可执行（`chmod +x`）；Windows 设 `MF_PROTOC`。
- **启动后连不上 MySQL/Redis**：修改 `script/JsonConfig/SqlConfig.json` / `RedisConfig.json` 里的 IP/端口/口令后重启。
- **找不到 `libjemalloc.2.dylib`**：确保 `MFServer/lib/libjemalloc.2.dylib` 与可执行文件共同发布，或通过 `install_name_tool` 修正 rpath
