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

local wp1 = "CITY1"
local wp2 = "CITY2"

local testWay = Way.new("NEWWORLD\\NEWWORLD.ZEN", wp1, wp2)
local waypoints = testWay:getWaypoints()
	
LOG_INFO("Waynet testing: {} to {}", wp1, wp2)
LOG_INFO("Start {} / End {}", testWay:getStart(), testWay:getEnd())
LOG_INFO("getCountWaypoints {}", testWay:getCountWaypoints())
for i, wp in ipairs(waypoints) do
  LOG_INFO("Waypoint {}: {}", i, wp)
end

local wp = getWaypoint("NEWWORLD\\NEWWORLD.ZEN", "HAFEN")
LOG_INFO("HAFEN - x:{} y:{} z:{}", wp.x, wp.y, wp.z)
local nearwp = getNearestWaypoint("NEWWORLD\\NEWWORLD.ZEN", 0, 0, 0)
LOG_INFO("{} - x:{} y:{} z:{}", nearwp.name, nearwp.x, nearwp.y, nearwp.z)
local nextnearwp = getNextNearestWaypoint("NEWWORLD\\NEWWORLD.ZEN", 0, 0, 0)
LOG_INFO("{} - x:{} y:{} z:{}", nextnearwp.name, nextnearwp.x, nextnearwp.y, nextnearwp.z)