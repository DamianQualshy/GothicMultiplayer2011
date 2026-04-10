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


renderWaynet(true)
local wp1 = "HAFEN"
local wp2 = "START"

local testWay = Way.new(wp1, wp2)
local waypoints = testWay:getWaypoints()
	
LOG_INFO("Waynet testing: {} to {}", wp1, wp2)
LOG_INFO("Start {} / End {}", testWay:getStart(), testWay:getEnd())
LOG_INFO("getCountWaypoints {}", testWay:getCountWaypoints())
for i, wp in ipairs(waypoints) do
  LOG_INFO("Waypoint {}: {}", i, wp)
end