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
    print(getPlayerVirtualWorld(playerId))

    setPlayerMaxHealth(playerId, 1000)
	setPlayerHealth(playerId, 1000)
    setPlayerMaxMana(playerId, 1000)
    setPlayerMana(playerId, 1000)
    setPlayerStrength(playerId, 100)
    setPlayerDexterity(playerId, 100)
    setPlayerSkillWeapon(playerId, TALENT_1H, 100)
	setPlayerSkillWeapon(playerId, TALENT_BOW, 100)

    giveItem(playerId, "ITAR_THORUS_ADDON", 1)
    equipItem(playerId, "ITAR_THORUS_ADDON")
    giveItem(playerId, "ITMW_1H_BLESSED_01", 1)
    equipItem(playerId, "ITMW_1H_BLESSED_01")
    giveItem(playerId, "ITRW_BOW_M_01", 1)
    equipItem(playerId, "ITRW_BOW_M_01")

    giveItem(playerId, "ITPO_SPEED", 50)
	giveItem(playerId, "ITPO_HEALTH_ADDON_04", 50)
    giveItem(playerId, "ITPO_MANA_ADDON_04", 50)
    giveItem(playerId, "ITBE_ADDON_STR_5", 1)
    giveItem(playerId, "ITRI_STR_01", 1)
    giveItem(playerId, "ITAM_DEX_01", 1)

	giveItem(playerId, "ITRW_ARROW", 100)
    giveItem(playerId, "ITRW_BOLT", 100)

	giveItem(playerId, "ITSC_LIGHT", 10)
    giveItem(playerId, "ITSC_FIREBOLT", 10)
	
	giveItem(playerId, "ITMI_GOLD", 100)
	giveItem(playerId, "ITMI_OLDCOIN", 100)
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
LOG_INFO("HAFEN - x:{} y:{} z:{} a:{}", wp.x, wp.y, wp.z, wp.angle)
local nearwp = getNearestWaypoint("NEWWORLD\\NEWWORLD.ZEN", 0, 0, 0)
LOG_INFO("{} - x:{} y:{} z:{} a:{}", nearwp.name, nearwp.x, nearwp.y, nearwp.z, nearwp.angle)
local nextnearwp = getNextNearestWaypoint("NEWWORLD\\NEWWORLD.ZEN", 0, 0, 0)
LOG_INFO("{} - x:{} y:{} z:{} a:{}", nextnearwp.name, nextnearwp.x, nextnearwp.y, nextnearwp.z, nextnearwp.angle)

local fp = getFreepoint("NEWWORLD\\NEWWORLD.ZEN", "START_NW_ORETRAIL_OW")
LOG_INFO("START_NW_ORETRAIL_OW - x:{} y:{} z:{} a:{}", fp.x, fp.y, fp.z, fp.angle)
local nearfp = getNearestFreepoint("NEWWORLD\\NEWWORLD.ZEN", 0, 0, 0)
LOG_INFO("{} - x:{} y:{} z:{} a:{}", nearfp.name, nearfp.x, nearfp.y, nearfp.z, nearfp.angle)
local nextnearfp = getNextNearestFreepoint("NEWWORLD\\NEWWORLD.ZEN", 0, 0, 0)
LOG_INFO("{} - x:{} y:{} z:{} a:{}", nextnearfp.name, nextnearfp.x, nextnearfp.y, nextnearfp.z, nextnearfp.angle)