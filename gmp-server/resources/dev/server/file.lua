LOG_INFO("[Dev][Server] file.lua initialized.")

-- Example data/server.toml:
-- [spawn]
-- world = "NEWWORLD\\NEWWORLD.ZEN"
-- virtual_world = 0
--
-- [spawn.position]
-- x = 0
-- y = 0
-- z = 0
--
-- [messages]
-- welcome = "Welcome to Gothic Multiplayer."
--
local serverConfig = TOML("server.toml")
local playerStore = JSON("players.json")

local function configValue(path, fallback)
    if serverConfig == nil then
        return fallback
    end

    return serverConfig:get_or(path, fallback)
end

local function getSpawnConfig()
    return {
        world = configValue("spawn.world", "NEWWORLD\\NEWWORLD.ZEN"),
        virtualWorld = configValue({ "spawn", "virtual_world" }, 0),
        position = {
            x = configValue({ "spawn", "position", "x" }, 0),
            y = configValue({ "spawn", "position", "y" }, 0),
            z = configValue({ "spawn", "position", "z" }, 0)
        }
    }
end

local function getWelcomeMessage()
    if serverConfig ~= nil and serverConfig.messages ~= nil and serverConfig.messages.welcome ~= nil then
        return serverConfig.messages.welcome
    end

    return "Welcome to Gothic Multiplayer."
end

local function getStorageKey(playerId)
    return string.lower(getPlayerName(playerId))
end

local function applyVisual(playerId, visual)
    if visual == nil then
        return
    end

    if visual.bodyModel == nil or visual.bodyTexture == nil or visual.headModel == nil or visual.headTexture == nil then
        return
    end

    setPlayerVisual(
        playerId,
        visual.bodyModel,
        visual.bodyTexture,
        visual.headModel,
        visual.headTexture
    )
end

local function getPlayerRecord(playerId)
    if playerStore == nil then
        return nil
    end

    return playerStore:getItem(getStorageKey(playerId))
end

local function buildPlayerRecord(playerId)
    local position = getPlayerPosition(playerId)

    return {
        name = getPlayerName(playerId),
        visual = getPlayerVisual(playerId),
        world = getPlayerWorld(playerId),
        virtualWorld = getPlayerVirtualWorld(playerId),
        position = position and {
            x = position.x,
            y = position.y,
            z = position.z
        } or nil
    }
end

local function savePlayer(playerId)
    if playerStore == nil then
        LOG_INFO("Player persistence skipped: players.json could not be opened.")
        return
    end

    playerStore:setItem(getStorageKey(playerId), buildPlayerRecord(playerId))
end

addEventHandler("onPlayerConnect", function(playerId)
    local savedPlayer = getPlayerRecord(playerId)
    local spawn = getSpawnConfig()
    local position = savedPlayer and savedPlayer.position or spawn.position

    if savedPlayer ~= nil and savedPlayer.world ~= nil then
        setPlayerWorld(playerId, savedPlayer.world)
    elseif spawn.world ~= nil then
        setPlayerWorld(playerId, spawn.world)
    end

    setPlayerVirtualWorld(playerId, savedPlayer and savedPlayer.virtualWorld or spawn.virtualWorld)
    spawnPlayer(playerId, position.x, position.y, position.z)

    if savedPlayer ~= nil then
        applyVisual(playerId, savedPlayer.visual)
    end

    sendMessageToPlayer(playerId, 255, 255, 0, getWelcomeMessage())
end)

addEventHandler("onPlayerDisconnect", function(playerId)
    savePlayer(playerId)
end)

addEventHandler("onPlayerCommand", function(playerId, command)
    if string.lower(command) ~= "save" then
        return
    end

    savePlayer(playerId)
    sendMessageToPlayer(playerId, 0, 255, 0, "Your character has been saved.")
end)
