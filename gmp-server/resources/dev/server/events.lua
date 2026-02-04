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

addEventHandler('onPlayerDropItem', function(playerId, itemInstance, amount)
    LOG_INFO("Player {} dropped item {} x{}", playerId, itemInstance, amount)
end)

addEventHandler('onPlayerTakeItem', function(playerId, itemInstance)
    LOG_INFO("Player {} picked up item {}", playerId, itemInstance)
end)

addEventHandler('onPlayerWeaponModeChange', function(playerId, weaponModeOld, WeaponModeNew)
    LOG_INFO("Player {} changed weapon mode from {} to {}", playerId, weaponModeOld, WeaponModeNew)
end)

addEventHandler('onPlayerHandItemChange', function(playerId, handSlot, itemInstance)
    LOG_INFO("Player {} changed equip state for Hand Item on slot {} for {}", playerId, handSlot, itemInstance)
end)

addEventHandler('onPlayerRingChange', function(playerId, handSlot, itemInstance)
    LOG_INFO("Player {} changed equip state for Ring on slot {} for {}", playerId, handSlot, itemInstance)
end)

addEventHandler('onPlayerShieldChange', function(playerId, itemInstance)
    LOG_INFO("Player {} changed equip state for Shield for {}", playerId, itemInstance)
end)

addEventHandler('onPlayerArmorChange', function(playerId, itemInstance)
    LOG_INFO("Player {} changed equip state for Armor for {}", playerId, itemInstance)
end)

addEventHandler('onPlayerMeleeWeaponChange', function(playerId, itemInstance)
    LOG_INFO("Player {} changed equip state for Melee Weapon for {}", playerId, itemInstance)
end)

addEventHandler('onPlayerRangedWeaponChange', function(playerId, itemInstance)
    LOG_INFO("Player {} changed equip state for Ranged Weapon for {}", playerId, itemInstance)
end)

addEventHandler('onPlayerSpellSlotChange', function(playerId, handSlot, itemInstance)
    LOG_INFO("Player {} changed equip state for a Spell Slot on slot {} for {}", playerId, handSlot, itemInstance)
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


    print("addEvent 'customEvent_Server': ", addEvent("customEvent_Server", true))
addEventHandler('customEvent_Server', function(sourceId, message)
    LOG_INFO("SERVER_EVENT CALL FROM CLIENT {} -> {}", sourceId, message)
end)