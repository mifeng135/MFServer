local setmetatableindex_
setmetatableindex_ = function(t, index)
    local mt = getmetatable(t)
    if not mt then mt = {} end
    if not mt.__index then
        mt.__index = index
        setmetatable(t, mt)
    elseif mt.__index ~= index then
        setmetatableindex_(mt, index)
    end
end
setmetatableindex = setmetatableindex_

local hotfixInstancesByClass = {}

local function registerHotfixInstance(cls, instance)
    local reg = hotfixInstancesByClass[cls]
    if not reg then
        reg = setmetatable({}, {__mode = "k"})
        hotfixInstancesByClass[cls] = reg
    end
    reg[instance] = true
end

local function reinvokeCtorAfterHotfix(mod)
    if type(mod) ~= "table" or type(mod.ctor) ~= "function" or type(mod.new) ~= "function" then
        return
    end

    local reg = hotfixInstancesByClass[mod]
    if not reg then
        reg = setmetatable({}, {__mode = "k"})
        hotfixInstancesByClass[mod] = reg
    end

    for instance in pairs(reg) do
        if type(instance) == "table" then
            local shell = {}
            setmetatableindex(shell, mod)
            shell.class = mod
            local ok, err = pcall(mod.ctor, shell)
            if not ok then
                MF.core.log("hotfix re-ctor failed {}", err)
            else
                for k, v in pairs(shell) do
                    if instance[k] == nil then
                        instance[k] = v
                    end
                end
            end
        end
    end
end

function class(classname, ...)
    local cls = {__cname = classname}

    local supers = {...}
    for _, super in ipairs(supers) do
        local superType = type(super)
        assert(
            superType == "nil" or superType == "table" or superType == "function",
            string.format('class() - create class "%s" with invalid super class type "%s"', classname, superType)
        )

        if superType == "function" then
            assert(
                cls.__create == nil,
                string.format('class() - create class "%s" with more than one creating function', classname)
            )
            -- if super is function, set it to __create
            cls.__create = super
        elseif superType == "table" then
            if super[".isclass"] then
                -- super is native class
                assert(
                    cls.__create == nil,
                    string.format(
                        'class() - create class "%s" with more than one creating function or native class',
                        classname
                    )
                )
                cls.__create = function()
                    return super:create()
                end
            else
                -- super is pure lua class
                cls.__supers = cls.__supers or {}
                cls.__supers[#cls.__supers + 1] = super
                if not cls.super then
                    -- set first super pure lua class as class.super
                    cls.super = super
                end
            end
        else
            error(string.format('class() - create class "%s" with invalid super type', classname), 0)
        end
    end

    cls.__index = cls
    if not cls.__supers or #cls.__supers == 1 then
        setmetatable(cls, {__index = cls.super})
    else
        setmetatable(
            cls,
            {
                __index = function(_, key)
                    local supers = cls.__supers
                    for i = 1, #supers do
                        local super = supers[i]
                        if super[key] then
                            return super[key]
                        end
                    end
                end
            }
        )
    end

    if not cls.ctor then
        -- add default constructor
        cls.ctor = function()
        end
    end

    if not cls.getClassName then
        cls.getClassName = function()
            return cls.__cname
        end
    end

    cls.new = function(...)
        local instance
        if cls.__create then
            instance = cls.__create(...)
        else
            instance = {}
        end
        setmetatableindex(instance, cls)
        instance.class = cls
        instance:ctor(...)
        registerHotfixInstance(cls, instance)
        return instance
    end

    return cls
end

function openDebug()
    if MFUtil.isLinux() then
        package.cpath = package.cpath .. ';/usr/work/server/linux-x64/?.so'
        local dbg = require('emmy_core')
        dbg.tcpListen('192.168.61.129', 9966)
    elseif MFUtil.isWindows() then
        package.cpath = package.cpath .. ';C:/Users/admin/AppData/Roaming/JetBrains/Rider2025.1/plugins/EmmyLua/debugger/emmy/windows/x64/?.dll'
        local dbg = require('emmy_core')
        dbg.tcpListen('localhost', 9966)
    else
        package.cpath = package.cpath .. ';/Users/zhoujian/Library/Application Support/JetBrains/IdeaIC2024.3/plugins/EmmyLua/debugger/emmy/mac/arm64/?.dylib'
        local dbg = require('emmy_core')
        dbg.tcpListen('localhost', 9966)
    end
end

function MFRouterHandle(_)  end


local function updateFunc(new_func, old_func)
    assert("function" == type(new_func))
    assert("function" == type(old_func))

    local old_upvalue_map = {}
    for i = 1, math.huge do
        local name, value = debug.getupvalue(old_func, i)
        if not name then
            break
        end
        old_upvalue_map[name] = value
    end

    for i = 1, math.huge do
        local name = debug.getupvalue(new_func, i)
        if not name then
            break
        end
        local old_value = old_upvalue_map[name]
        if old_value then
            debug.setupvalue(new_func, i, old_value)
        end
    end
end

local function updateTable(newTable, oldTable, cacheTables)
    assert("table" == type(newTable))
    assert("table" == type(oldTable))

    for key, value in pairs(newTable) do
        local oldValue = oldTable[key]
        local typeValue = type(value)
        if typeValue == "function" then
            if type(oldValue) == "function" then
                updateFunc(value, oldValue)
            end
            oldTable[key] = value
        elseif typeValue == "table" then
            if (cacheTables[value] == nil) then
                cacheTables[value] = true
                updateTable(value, oldValue, cacheTables)
            end
        end
    end

    local oldMeta = debug.getmetatable(oldTable)
    local newMeta = debug.getmetatable(newTable)
    if type(oldMeta) == "table" and type(newMeta) == "table" then
        updateTable(newMeta, oldMeta, cacheTables)
    end
end

function hotfix(filename)
    local oldModule
    if package.loaded[filename] then
        oldModule = package.loaded[filename]
        package.loaded[filename] = nil
    end
    local ok, err = pcall(require, filename)
    if not ok then
        package.loaded[filename] = oldModule
        MF.core.log('reload lua file failed {}', err)
        return
    end

    local newModule = package.loaded[filename]
    local updatedTables = {}
    updateTable(newModule, oldModule, updatedTables)
    reinvokeCtorAfterHotfix(oldModule)
    package.loaded[filename] = oldModule
end


