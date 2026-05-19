LOG_INFO('[Dev][Server] events.lua initialized.')

addEventHandler('onInit', function()
    LOG_INFO('JOINING SERVER')
end)

addEventHandler('onExit', function()
    LOG_INFO('LEAVING SERVER')
end)

addEventHandler('onKeyUp', function(key)
    LOG_INFO('onKeyUp (key={})', key)
end)

local menuEnabled = false
addEventHandler('onKeyDown', function(key)
    LOG_INFO('onKeyDown (key={})', key)
    if key == KEY_N then
        menuEnabled = not menuEnabled
        enableGMPMenu(menuEnabled)
        LOG_INFO('GMP Menu enabled: {}', menuEnabled)
    end
    if key == KEY_M then
        if(menuEnabled) then
            openGMPMenu()
        --[[ else
            exitGame() ]]
        end
    end
end)

addEventHandler('onPlayerCreate', function(playerId)
    LOG_INFO('Player created: id={}', playerId)
end)

addEventHandler('onPlayerDestroy', function(playerId)
    LOG_INFO('Player destroyed: id={}', playerId)
end)

addEventHandler("onRender", function()
    -- Called every frame; place lightweight UI logic here if needed.
end)

addEventHandler("onTime", function(day, hour, minute)
    LOG_INFO("Time change: day {}, {}:{} ", day, hour, minute)
end)

addEventHandler("onPlayerMessage", function(id, r, g, b, message)
    if id then
        print(string.format("Message from player %d: [%d, %d, %d] %s", id, r, g, b, message))
    else
        print(string.format("Message from server: [%d, %d, %d] %s", r, g, b, message))
    end
end)

addEventHandler("onInventorySlotChange", function(from, to)
    LOG_INFO("onInventorySlotChange from {} to {}", from, to)
end)

addEventHandler("onOpenInventory", function()
    LOG_INFO("onOpenInventory")
end)

addEventHandler("onCloseInventory", function()
    LOG_INFO("onCloseInventory")
end)

addEventHandler("onWorldChange", function(world, waypoint)
    LOG_INFO("onWorldChange -> {}, {}", world, waypoint)
end)

addEventHandler("onWorldEnter", function(world)
    LOG_INFO("onWorldEnter -> {} ", world)
end)

addEventHandler("onEquip", function(item)
    LOG_INFO("onEquip {} ", item)
end)

addEventHandler("onUnequip", function(item)
    LOG_INFO("onUnequip -> {} ", item)
end)

addEventHandler("onDropItem", function(item)
    LOG_INFO("onDropItem -> {} ", item)
end)

addEventHandler("onTakeItem", function(item)
    LOG_INFO("onTakeItem -> {} ", item)
end)

addEventHandler("onUseItem", function(item, scheme, from, to)
    LOG_INFO("onUseItem -> {}, '{}', from {} to {} ", item, scheme, from, to)
end)

addEventHandler("onPlayerRespawn", function(playerid)
    LOG_INFO("onPlayerRespawn -> {} ", playerid)
end)

addEventHandler("onPlayerSpawn", function(playerid)
    LOG_INFO("onPlayerSpawn -> {} ", playerid)
end)

addEventHandler("onPlayerDead", function(playerid)
    LOG_INFO("onPlayerDead -> {} ", playerid)
end)

addEventHandler("onPlayerChangePing", function(playerid, ping)
    LOG_INFO("onPlayerChangePing -> {} : {} ", playerid, ping)
end)

addEventHandler("onWeatherChange", function(from, to)
    LOG_INFO("onWeatherChange -> from {} to {} ", from, to)
end)