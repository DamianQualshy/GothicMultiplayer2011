LOG_INFO('[Dev][Client] sound.lua initialized.')


local sfx = Sound.new("LEVELUP.WAV")

sfx.volume  = 1.0
sfx.balance = 0.0
sfx.looping = false

local file        = sfx.file
local vol         = sfx.volume
local bal         = sfx.balance
local loopState   = sfx.looping
local timePlayed  = sfx.playingTime

function onResourceStart()
	sfx:play()

--[[ 	print(string.format("sfx.file %s", file))
	print(string.format("sfx.file %.2f", vol))
	print(string.format("sfx.file %.2f", bal))
	print(string.format("sfx.file %s", tostring(loopState)))
	print(string.format("sfx.file %d", timePlayed)) ]]
end

function onResourceStop()
	sfx = nil
end

local menu = Music.new(".\\Multiplayer\\Music\\main_menu_theme_2.mp3")
menu:playLooped()