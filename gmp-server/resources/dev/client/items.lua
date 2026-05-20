LOG_INFO('[Dev][Client] items.lua initialized.')

local function boolToString(value)
    return value and "true" or "false"
end

local function optionalToString(value)
    if value == nil then
        return "nil"
    end

    return tostring(value)
end

local function describeItemGround(prefix, itemGround)
    if itemGround == nil then
        LOG_INFO("{} item ground: nil", prefix)
        return
    end

    LOG_INFO(
        "{} itemGround={} item={} instance={} amount={}",
        prefix,
        itemGround.id,
        itemGround.item,
        itemGround.instance,
        itemGround.amount
    )
end

addEventHandler("onItemGroundCreate", function(itemGround)
    describeItemGround("[Client create]", itemGround)

    local byId = ItemGroundManager.getById(itemGround.id)
    describeItemGround("[Client getById]", byId)

    local byItem = ItemGroundManager.getByItem(itemGround.instance)
    describeItemGround("[Client getByItem]", byItem)
end)

addEventHandler("onItemGroundDestroy", function(itemGround)
    describeItemGround("[Client destroy]", itemGround)
end)

addEventHandler("onItemsGroundDestroy", function()
    LOG_INFO("[Client clear] all streamed server-side item grounds were removed")
end)

addEventHandler("onDropItem", function(item, amount)
    LOG_INFO("onDropItem payload: item={} amount={}", item, amount)

    -- This is a cancellable local interaction event.
    -- cancelEvent()
end)

addEventHandler("onTakeItem", function(item, synchronized, amount, itemGroundId)
    LOG_INFO(
        "onTakeItem payload: item={} synchronized={} amount={} itemGroundId={}",
        item,
        boolToString(synchronized),
        amount,
        optionalToString(itemGroundId)
    )

    if itemGroundId ~= nil then
        describeItemGround("[Client take lookup]", ItemGroundManager.getById(itemGroundId))
    end

    -- This is a cancellable local interaction event.
    -- cancelEvent()
end)
