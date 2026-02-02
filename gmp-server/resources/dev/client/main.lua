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
