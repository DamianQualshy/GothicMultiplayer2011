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
LOG_INFO("HAFEN - x:{} y:{} z:{}", wp.x, wp.y, wp.z)
--[[ local waypoints2 = getWaypoints()
for i, wp in ipairs(waypoints2) do
  LOG_INFO("Waypoint {}: {}", i, wp)
end ]]
local nearwp = getNearestWaypoint(0, 0, 0)
LOG_INFO("{} - x:{} y:{} z:{}", nearwp.name, nearwp.x, nearwp.y, nearwp.z)
local nextnearwp = getNextNearestWaypoint(0, 0, 0)
LOG_INFO("{} - x:{} y:{} z:{}", nextnearwp.name, nextnearwp.x, nextnearwp.y, nextnearwp.z)