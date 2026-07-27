addEventHandler('onPlayerConnect', function(playerId)
    spawnPlayer(playerId, 0, 0, 0)

    setPlayerColor(playerId, math.random(0, 255), math.random(0, 255), math.random(0, 255))
end)

addEventHandler('onPlayerMessage', function(playerId, message)
    local _color = getPlayerColor(playerId)
    local _name = getPlayerName(playerId)
    sendPlayerMessageToAll(playerId, _color.r, _color.g, _color.b, _name .. ":" .. message)
end)
