LOG_INFO('[Dev][Server] events.lua initialized.')

local function vectorToString(x, y, z)
    return string.format("(%.2f, %.2f, %.2f)", x, y, z)
end

local function optionalIdToString(id)
    if id == nil then
        return "nil"
    end

    return tostring(id)
end

addEventHandler('onClockUpdate', function(day, hour, minute)
    LOG_INFO("Clock update: Day {} Time {:02d}:{:02d}", day, hour, minute)

    if(hour == 8 and minute == 10) then
        Sky.weatherType = WEATHER_RAIN
    elseif (hour == 8 and minute == 15) then
        Sky.weatherType = WEATHER_SNOW
    elseif (hour == 8 and minute == 20) then
        Sky.dontRain = true
    end
end)

addEventHandler('onPlayerConnect', function(playerId)
    LOG_INFO("Player {} connected", playerId)
end)

addEventHandler('onPlayerDisconnect', function(playerId)
    LOG_INFO("Player {} disconnected", playerId)
end)

addEventHandler('onPlayerMessage', function(playerId, text)
    LOG_INFO("{} says: {}", playerId, text)
end)

addEventHandler('onPlayerCommand', function(playerId, command, params)
    LOG_INFO("Command from {}: /{} {}", playerId, command, params)
end)

addEventHandler('onPlayerUnconscious', function(attackerId, victimId)
    LOG_INFO("{} knocked {} unconscious", optionalIdToString(attackerId), victimId)
end)

addEventHandler('onPlayerStandUp', function(playerId)
    LOG_INFO("Player {} stood up", playerId)
end)

addEventHandler('onPlayerDeath', function(playerId, killerId)
    LOG_INFO("Player {} died (killer: {})", playerId, optionalIdToString(killerId))
end)

addEventHandler('onPlayerDropItem', function(playerId, itemGround)
    LOG_INFO("Player {} dropped item ground {}: {} x{}", playerId, itemGround.id, itemGround.instance, itemGround.amount)
end)

addEventHandler('onPlayerTakeItem', function(playerId, itemGround)
    LOG_INFO("Player {} picked up item ground {}: {} x{}", playerId, itemGround.id, itemGround.instance, itemGround.amount)
end)

addEventHandler('onPlayerWeaponModeChange', function(playerId, weaponModeOld, WeaponModeNew)
    LOG_INFO("Player {} changed weapon mode from {} to {}", playerId, weaponModeOld, WeaponModeNew)
end)

addEventHandler('onPlayerEquipHandItem', function(playerId, handSlot, item)
    LOG_INFO("Player {} equipped hand item on slot {}: {}", playerId, handSlot, item.instance)
end)

addEventHandler('onPlayerEquipAmulet', function(playerId, item)
    LOG_INFO("Player {} equipped amulet: {}", playerId, item.instance)
end)

addEventHandler('onPlayerEquipShield', function(playerId, item)
    LOG_INFO("Player {} equipped shield: {}", playerId, item.instance)
end)

addEventHandler('onPlayerEquipHelmet', function(playerId, item)
    LOG_INFO("Player {} equipped helmet: {}", playerId, item.instance)
end)

addEventHandler('onPlayerEquipBelt', function(playerId, item)
    LOG_INFO("Player {} equipped belt: {}", playerId, item.instance)
end)

addEventHandler('onPlayerEquipRing', function(playerId, ringSlot, item)
    LOG_INFO("Player {} equipped ring on slot {}: {}", playerId, ringSlot, item.instance)
end)

addEventHandler('onPlayerEquipArmor', function(playerId, item)
    LOG_INFO("Player {} equipped armor: {}", playerId, item.instance)
end)

addEventHandler('onPlayerEquipMeleeWeapon', function(playerId, item)
    LOG_INFO("Player {} equipped melee weapon: {}", playerId, item.instance)
end)

addEventHandler('onPlayerEquipRangedWeapon', function(playerId, item)
    LOG_INFO("Player {} equipped ranged weapon: {}", playerId, item.instance)
end)

addEventHandler('onPlayerEquipSpellSlot', function(playerId, spellSlot, item)
    LOG_INFO("Player {} equipped spell slot {}: {}", playerId, spellSlot, item.instance)
end)

addEventHandler('onPlayerCastSpell', function(casterId, spellId, targetId)
    LOG_INFO("Player {} cast spell {} on {}", casterId, spellId, optionalIdToString(targetId))
end)

addEventHandler('onPlayerSpawn', function(playerId, posX, posY, posZ)
    LOG_INFO("Player {} spawned at {}", playerId, vectorToString(posX, posY, posZ))
end)

addEventHandler('onPlayerRespawn', function(playerId, posX, posY, posZ)
    LOG_INFO("Player {} respawned at {}", playerId, vectorToString(posX, posY, posZ))
end)

addEventHandler('onPlayerSpawnFor', function(playerId, spawnedId)
    LOG_INFO("Player {} respawned for {}", spawnedId, playerId)
end)

addEventHandler('onPlayerUnspawnFor', function(playerId, spawnedId)
    LOG_INFO("Player {} despawned for {}", spawnedId, playerId)
end)

addEventHandler('onPlayerHit', function(attackerId, victimId, damage)
    LOG_INFO("{} hit {} for {} HP", optionalIdToString(attackerId), victimId, damage)
end)

addEventHandler("onPlayerWorldChange", function(playerId, world, waypoint)
    LOG_INFO("Player {} requested world change to {} at {}", playerId, world, waypoint)
end)

addEventHandler("onPlayerWorldEnter", function(playerId, world)
    LOG_INFO("Player {} entered world {}", playerId, world)
end)


    print("addEvent 'customEvent_Server': ", addEvent("customEvent_Server", true))
addEventHandler('customEvent_Server', function(sourceId, message)
    LOG_INFO("SERVER_EVENT CALL FROM CLIENT {} -> {}", sourceId, message)
end)
