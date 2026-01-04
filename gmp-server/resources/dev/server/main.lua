LOG_INFO('[Dev][Server] main.lua initialized.')

function onResourceStart()
	print("Hello GMP!")
end

function onResourceStop()
	print("Bye GMP!")
end

addEventHandler('onPlayerConnect', function(playerId)
    spawnPlayer(playerId, 0, 0, 0)
end)