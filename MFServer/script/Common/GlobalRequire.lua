Constant = {}
Constant.serverId = 29

ErrorCode = {}
ErrorCode.LOGIN_FAIL_FOREVER_BAN = 100000                       --永久封
ErrorCode.LOGIN_FAIL_TEMP_BAN = 100001                          --临时封
ErrorCode.LOGIN_FAIL_ACCOUNT_DESTROY = 100002                   --账号已经被注销
ErrorCode.ILLEGAL_PARAMETER = 900001
ErrorCode.LOGIN_FAIL_ACCOUNT_STATE_ERROR = 100010
ErrorCode.LOGIN_ACCOUNT_ABNORMAL = 100014
ErrorCode.PLAYER_OTHER_DEVICE_LOGIN = 100006                    --账号在其他地方登录

SqlMapperKey = {}
SqlMapperKey.playerSelect = "Player.select"
SqlMapperKey.playerAccountSelect = "PlayerAccount.select"
SqlMapperKey.playerUidSelect = "PlayerUid.select"

SqlConfig = {}
SqlConfig.login = 1
SqlConfig.game29 = 29

RpcConfig = {}
RpcConfig.Gate29 = 29

RedisConfig = {}
RedisConfig.Login = 1

SocketConfig = {}
SocketConfig.webSocket = 1

