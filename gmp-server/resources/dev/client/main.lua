-- Main client entrypoint for 'dev' resource
-- Other client scripts are loaded via require()

-- Load client modules
require('client.mouse')
require('client.vob')
require('client.ui')
require('client.sound')
require('client.font')
require('client.events')
require('client.functions')

LOG_INFO('[Dev][Client] Client-side resources initialized from main.lua')

Discord.setState("Playing GMPC")
Discord.setDetails("Testing LUA")
Discord.setLargeImage("gothic_icon")


toggleDrawWaynet(true)
local wp1 = "CITY1"
local wp2 = "CITY2"

local testWay = Way.new(wp1, wp2)
local waypoints = testWay:getWaypoints()
	
LOG_INFO("Waynet testing: {} to {}", wp1, wp2)
LOG_INFO("Start {} / End {}", testWay:getStart(), testWay:getEnd())
LOG_INFO("getCountWaypoints {}", testWay:getCountWaypoints())
for i, wp in ipairs(waypoints) do
  LOG_INFO("Waypoint {}: {}", i, wp)
end

local wp = getWaypoint("HAFEN")
LOG_INFO("HAFEN - x:{} y:{} z:{} a:{}", wp.x, wp.y, wp.z, wp.angle)
--[[ local waypoints2 = getWaypoints()
for i, wp in ipairs(waypoints2) do
  LOG_INFO("Waypoint {}: {}", i, wp)
end ]]
local nearwp = getNearestWaypoint(0, 0, 0)
LOG_INFO("{} - x:{} y:{} z:{} a:{}", nearwp.name, nearwp.x, nearwp.y, nearwp.z, nearwp.angle)
local nextnearwp = getNextNearestWaypoint(0, 0, 0)
LOG_INFO("{} - x:{} y:{} z:{} a:{}", nextnearwp.name, nextnearwp.x, nextnearwp.y, nextnearwp.z, nextnearwp.angle)

local fp = getFreepoint("START_NW_ORETRAIL_OW")
LOG_INFO("START_NW_ORETRAIL_OW - x:{} y:{} z:{} a:{}", fp.x, fp.y, fp.z, fp.angle)
local nearfp = getNearestFreepoint(0, 0, 0)
LOG_INFO("{} - x:{} y:{} z:{} a:{}", nearfp.name, nearfp.x, nearfp.y, nearfp.z, nearfp.angle)
local nextnearfp = getNextNearestFreepoint(0, 0, 0)
LOG_INFO("{} - x:{} y:{} z:{} a:{}", nextnearfp.name, nextnearfp.x, nextnearfp.y, nextnearfp.z, nextnearfp.angle)