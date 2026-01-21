LOG_INFO('[Dev][Server] main.lua initialized.')

function onResourceStart()
	print("Hello GMP!")
end

function onResourceStop()
	print("Bye GMP!")
end

addEventHandler('onPlayerConnect', function(playerId)
    spawnPlayer(playerId, 0, 0, 0)

	setPlayerRespawnTime(playerId, 5000)

	print(isPlayerConnected(playerId))
	print(isPlayerDead(playerId))
	print(isPlayerSpawned(playerId))
	print(isPlayerUnconscious(playerId))
end)