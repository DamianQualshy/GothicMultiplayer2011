LOG_INFO('[Dev][Server] items.lua initialized.')

local trackedGroundItems = {}

local function splitParams(params)
    local values = {}
    for value in string.gmatch(params or "", "%S+") do
        values[#values + 1] = value
    end
    return values
end

local function boolToString(value)
    return value and "true" or "false"
end

local function sendUsage(playerId)
    sendMessageToPlayer(playerId, 255, 200, 0, "Usage: /itemground create [instance] [amount]")
    sendMessageToPlayer(playerId, 255, 200, 0, "Usage: /itemground get|move|rotate|physics|vw|destroy id")
end

local function describeItemGround(prefix, itemGround)
    if itemGround == nil then
        LOG_INFO("{} item ground: nil", prefix)
        return
    end

    local position = itemGround:getPosition()
    local rotation = itemGround:getRotation()

    LOG_INFO(
        "{} itemGround={} instance={} amount={} world={} virtualWorld={} physicsEnabled={}",
        prefix,
        itemGround.id,
        itemGround.instance,
        itemGround.amount,
        itemGround.world,
        itemGround.virtualWorld,
        boolToString(itemGround.physicsEnabled)
    )
    LOG_INFO(
        "{} position={} rotation={}",
        prefix,
        string.format("(%.2f, %.2f, %.2f)", position.x, position.y, position.z),
        string.format("(%.2f, %.2f, %.2f)", rotation.x, rotation.y, rotation.z)
    )
end

local function getItemGroundOrNotify(playerId, id)
    local itemGround = ItemGroundManager.getById(id)
    if itemGround == nil then
        sendMessageToPlayer(playerId, 255, 0, 0, string.format("ItemGround %d does not exist", id))
        return nil
    end
    return itemGround
end

addEventHandler("onPlayerCommand", function(playerId, command, params)
    if string.lower(command) ~= "itemground" then
        return
    end

    local args = splitParams(params)
    local action = string.lower(args[1] or "")

    if action == "" then
        sendUsage(playerId)
        return
    end

    if action == "create" then
        local position = getPlayerPosition(playerId)
        if position == nil then
            sendMessageToPlayer(playerId, 255, 0, 0, "Cannot create item ground without player position")
            return
        end

        local instance = string.upper(args[2] or "ITMI_GOLD")
        local amount = tonumber(args[3]) or 1
        local itemGroundId = ItemGroundManager.create({
            instance = instance,
            amount = amount,
            physicsEnabled = true,
            position = {
                x = position.x + 100.0,
                y = position.y,
                z = position.z
            },
            rotation = {
                x = 0.0,
                y = 0.0,
                z = 0.0
            },
            world = getPlayerWorld(playerId) or "",
            virtualWorld = getPlayerVirtualWorld(playerId) or 0
        })

        local itemGround = ItemGroundManager.getById(itemGroundId)
        trackedGroundItems[itemGroundId] = true
        describeItemGround("[Create]", itemGround)
        sendMessageToPlayer(playerId, 0, 255, 0, string.format("Created ItemGround %d", itemGroundId))
        return
    end

    local id = tonumber(args[2])
    if id == nil then
        sendUsage(playerId)
        return
    end

    local itemGround = getItemGroundOrNotify(playerId, id)
    if itemGround == nil then
        return
    end

    if action == "get" then
        describeItemGround("[Get]", itemGround)
        sendMessageToPlayer(playerId, 0, 255, 0, string.format("%s x%d", itemGround.instance, itemGround.amount))
        return
    end

    if action == "move" then
        local position = getPlayerPosition(playerId)
        if position == nil then
            return
        end

        itemGround:setPosition(position.x + 100.0, position.y, position.z)
        describeItemGround("[Move]", itemGround)
        return
    end

    if action == "rotate" then
        local x = tonumber(args[3]) or 0.0
        local y = tonumber(args[4]) or 0.0
        local z = tonumber(args[5]) or 0.0

        itemGround:setRotation(x, y, z)
        describeItemGround("[Rotate]", itemGround)
        return
    end

    if action == "physics" then
        local enabled = args[3] ~= "0"

        itemGround.physicsEnabled = enabled
        describeItemGround("[Physics]", itemGround)
        return
    end

    if action == "vw" then
        local virtualWorld = tonumber(args[3]) or getPlayerVirtualWorld(playerId) or 0

        itemGround.virtualWorld = virtualWorld
        describeItemGround("[VirtualWorld]", itemGround)
        return
    end

    if action == "destroy" then
        if ItemGroundManager.destroy(itemGround) then
            trackedGroundItems[id] = nil
            sendMessageToPlayer(playerId, 0, 255, 0, string.format("Destroyed ItemGround %d", id))
        end
        return
    end

    sendUsage(playerId)
end)

addEventHandler("onPlayerDropItem", function(playerId, itemGround)
    trackedGroundItems[itemGround.id] = true
    describeItemGround(string.format("[Drop by %d]", playerId), itemGround)

    -- Example policy hook:
    -- if itemGround.instance == "ITMI_GOLD" and itemGround.amount > 1000 then
    --     cancelEvent()
    -- end
end)

addEventHandler("onPlayerTakeItem", function(playerId, itemGround)
    describeItemGround(string.format("[Take by %d]", playerId), itemGround)

    -- Example policy hook:
    -- if trackedGroundItems[itemGround.id] and getPlayerVirtualWorld(playerId) ~= itemGround.virtualWorld then
    --     cancelEvent()
    -- end

    trackedGroundItems[itemGround.id] = nil
end)

local item = Item.getByInstance("ITMI_GOLD")
if item then
    LOG_INFO("item: instance {}", item.instance)
    LOG_INFO("item: mainflag {}", item.mainflag)
    LOG_INFO("item: flags {}", item.flags)
    LOG_INFO("item: visual {}", item.visual)
    LOG_INFO("item: wear {}", item.wear)
    LOG_INFO("item: range {}", item.range)
    LOG_INFO("item: value {}", item.value)
    LOG_INFO("item: damage {}", item.damage)
    LOG_INFO("item: damageTotal {}", item.damageTotal)
    LOG_INFO("item: damageTypes {}", item.damageTypes)
    LOG_INFO("item: munition {}", item.munition)
    LOG_INFO("item: munitionItem {}", item.munitionItem)
    LOG_INFO("item: spell {}", item.spell)
    LOG_INFO("item: scemename {}", item.scemename)
    LOG_INFO("item: mag_circle {}", item.mag_circle)
    LOG_INFO("item: protections {}", item.protections)
    LOG_INFO("item: conditions {}", item.conditions)
end