local SqlConfig = {
    {
        ip = "10.23.2.193",
        port = 5432,
        userName = "root",
        password = "123456",
        database = "esg_login",
        minPoolSize = 4,
        maxPoolSize = 8,
        maxIdleTime = 10000,
    },
    [29] = {
        ip = "10.23.2.193",
        port = 5432,
        userName = "root",
        password = "123456",
        database = "esg_game",
        minPoolSize = 2,
        maxPoolSize = 4,
        maxIdleTime = 10000,
    },
}

return SqlConfig
