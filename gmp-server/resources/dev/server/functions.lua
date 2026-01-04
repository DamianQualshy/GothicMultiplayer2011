LOG_INFO('[Dev][Server] functions.lua initialized.')

addEventHandler("onPlayerCommand", function(playerId, cmd, params)
	cmd = string.lower(cmd)

	if cmd == "test" then
		local args = sscanf("s", params)
		if not args then
			sendMessageToPlayer(playerId, 255, 0, 0, "Usage: /test function")
			return
		end

		local func = string.lower(args[1])

		if func == "log" then
			testLog()
			return
		elseif func == "hash" then
			testHash()
			return
		elseif func == "math" then
			testMath()
			return
		elseif func == "utility" then
			testUtility()
			return
		end
	end

	if cmd == "event" then
		local args = sscanf("s", params)
		if not args then
			sendMessageToPlayer(playerId, 255, 0, 0, "Usage: /event custom_event")
			return
		end

		local event = string.lower(args[1])

		if event == "spawnnpc" then
			triggerClientEvent("spawnNPCRequest")
			return
		elseif event == "removenpc" then
			triggerClientEvent("removeNPCRequest")
			return
		end
	end

	if cmd == "color" then
		local args = sscanf("dddd", params)
		if not args then
			sendMessageToPlayer(playerId, 255, 0, 0, "Usage: /color id R G B")
			return
		end

		local id = args[1]
		local r = args[2]
		local g = args[3]
		local b = args[4]

		setPlayerColor(id, r, g, b)
		sendMessageToPlayer(id, 0, 255, 0, string.format("Your nickname color was changed to rgb(%d,%d,%d)", r, g, b))
	end

	if cmd == "name" then
		local args = sscanf("ds", params)
		if not args then
			sendMessageToPlayer(playerId, 255, 0, 0, "Usage: /name id new_name")
			return
		end

		local id = args[1]
		local name = args[2]

		setPlayerName(id, name)
		sendMessageToPlayer(id, 0, 255, 0, string.format("Your name was changed to %s", name))
	end

	if cmd == "tp" then
		local args = sscanf("dd", params)
		if not args then
			sendMessageToPlayer(playerId, 255, 0, 0, "Usage: /tp from_id to_id")
			return
		end

		local from_id = args[1]
		local to_id = args[2]

		local world = getPlayerWorld(to_id)
		if world ~= getPlayerWorld(from_id) then
			setPlayerWorld(from_id, world)
		end

		local pos = getPlayerPosition(to_id)
		setPlayerPosition(from_id, pos.x, pos.y, pos.z)
		sendMessageToPlayer(from_id, 0, 255, 0, "You were teleported to " .. getPlayerName(to_id))
	end

	if cmd == "give" then
		local args = sscanf("dsd", params)
		if not args then
			sendMessageToPlayer(playerId, 255, 0, 0, "Usage: /give id instance amount")
			return
		end

		local id = args[1]
		local instance = args[2]
		local amount = args[3]

		if amount < 1 then
			amount = 1
		end

		giveItem(id, instance, amount)
		sendMessageToPlayer(id, 0, 255, 0, string.format("You received x%d %s", amount, instance))
	end

	if cmd == "stats" then
		local args = sscanf("dsd", params)
		if not args then
			sendMessageToPlayer(playerId, 255, 0, 0, "Usage: /stats id hp/mp/str/dex amount")
			return
		end

		local id = args[1]
		local stat = string.lower(args[2])
		local amount = args[3]

		if stat == "hp" then
			setPlayerMaxHealth(id, amount)
			setPlayerHealth(id, amount)
				sendMessageToPlayer(id, 0, 255, 0, string.format("Your Health changed to %d", amount))
			return
		elseif stat == "mp" then
			setPlayerMaxMana(id, amount)
			setPlayerMana(id, amount)
				sendMessageToPlayer(id, 0, 255, 0, string.format("Your Mana changed to %d", amount))
			return
		elseif stat == "str" then
        	setPlayerStrength(id, amount)
				sendMessageToPlayer(id, 0, 255, 0, string.format("Your Strength changed to %d", amount))
			return
		elseif stat == "dex" then
        	setPlayerDexterity(id, amount)
				sendMessageToPlayer(id, 0, 255, 0, string.format("Your Dexterity changed to %d", amount))
			return
		end
	end

	if cmd == "world" then
		local args = sscanf("ds", params)
		if not args then
			sendMessageToPlayer(playerId, 255, 0, 0, "Usage: /world id path")
			return
		end

		local id = args[1]
		local path = string.upper(args[2])

		local world = getPlayerWorld(id)
		if world == path then
			return
		else
			setPlayerWorld(id, path)
			sendMessageToPlayer(id, 0, 255, 0, string.format("You were moved to %s", path))
		end
	end

	if cmd == "time" then
		local args = sscanf("dd", params)
		if not args then
			sendMessageToPlayer(playerId, 255, 0, 0, "Usage: /time hour minute")
			return
		end

		local hour = args[1]
		local minute = args[2]

		if hour > 23 then hour = 23 elseif hour < 0 then hour = 0 end
		if minute > 59 then minute = 59 elseif minute < 0 then minute = 0 end
		
		setTime(hour, minute)
		sendMessageToAll(0, 0, 255, string.format("Time was changed to %.2d:%.2d", hour, minute))
	end

	if cmd == "weather" then
		local args = sscanf("d", params)
		if not args then
			sendMessageToPlayer(playerId, 255, 0, 0, "Usage: /weather type")
			return
		end

		local weather = args[1]

		setWeatherType(weather)
		sendMessageToAll(0, 0, 255, string.format("Weather was changed to %d", getWeatherType()))
	end

	if cmd == "level" then
		local args = sscanf("dd", params)
		if not args then
			sendMessageToPlayer(playerId, 255, 0, 0, "Usage: /level id level")
			return
		end

		local id = args[1]
		local level = args[2]

		if level < 1 then level = 1 end

		local exp = 250 * level * level
		local next_exp = exp * (level + 1)
		local lp = level * 10

		setPlayerLevel(id, level)
		setPlayerExp(id, exp)
		setPlayerNextLevelExp(id, next_exp)
		setPlayerLearnPoints(id, lp)

		sendMessageToPlayer(id, 0, 255, 0, string.format("Your level is now %d", level))
	end

	if cmd == "visual" then
		local args = sscanf("dsdsd", params)
		if not args then
			sendMessageToPlayer(playerId, 255, 0, 0, "Usage: /visual id bodyModel bodyTexture headModel headTexture")
			return
		end

		local id = args[1]
		local bModel = string.upper(args[2])
		local bTex = args[3]
		local hModel = string.upper(args[4])
		local hTex = args[5]

		setPlayerVisual(id, bModel, bTex, hModel, hTex)
		sendMessageToPlayer(id, 0, 255, 0, string.format("Your visual is now %s %d %s %d", bModel, bTex, hModel, hTex))
	end

	if cmd == "scale" then
		local args = sscanf("dfff", params)
		if not args then
			sendMessageToPlayer(playerId, 255, 0, 0, "Usage: /scale id x y z")
			return
		end

		local id = args[1]
		local x = args[2]
		local y = args[3]
		local z = args[4]

		setPlayerScale(id, x, y, z)
		sendMessageToPlayer(id, 0, 255, 0, string.format("Your scale is now %.2f %.2f %.2f", x, y, z))
	end
end)
