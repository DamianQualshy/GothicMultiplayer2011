LOG_INFO('[Dev][Server] functions.lua initialized.')

local npc = nil
local npc2 = nil

    print("addEvent 'spawnNPCRequest': ", addEvent("spawnNPCRequest", true))
addEventHandler('spawnNPCRequest', function()
    LOG_INFO("CLIENT_EVENT CALL FROM SERVER")
	if npc == nil then
        npc = createNpc("TestNpc")
    	spawnNpc(npc, "PC_HERO")
	end
	if npc2 == nil then
        npc2 = createNpc("TestNpc2")
    	spawnNpc(npc2, "SCAVENGER")
	end

	triggerServerEvent("customEvent_Server", heroId, "Create NPCs")
end)

    print("addEvent 'removeNPCRequest': ", addEvent("removeNPCRequest", true))
addEventHandler('removeNPCRequest', function()
    LOG_INFO("CLIENT_EVENT CALL FROM SERVER")
        destroyNpc(npc)
        destroyNpc(npc2)
	npc = nil
	npc2 = nil

	triggerServerEvent("customEvent_Server", heroId, "Remove NPCs")
end)