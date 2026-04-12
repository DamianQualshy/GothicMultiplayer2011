LOG_INFO('[Dev][Client] vob.lua initialized.')

local parent = Vob.new("addon_canyonobject_car_01.3DS")
parent.objectName = "lua_vob_parent"
parent.cdDynamic = true
parent.cdStatic = true
parent.farClipZScale = 2.0
parent.visual = "addon_canyonobject_car_01.3DS"
parent.visualAlpha = 0.5
parent:setPosition(100.0, 0.0, 100.0)
parent:setRotation(0.0, 0.0, 0.0)

local parentMatrix = parent.matrix
parentMatrix:setTranslation(Vec3.new(100.0, 0.0, 100.0))
parent.matrix = parentMatrix

local child = Vob.new("treasure.3DS")
child.objectName = "lua_vob_child"
child.cdDynamic = false
child.cdStatic = true
child.farClipZScale = 1.0
child.visual = "treasure.3DS"
child.visualAlpha = 1.0
child:setPosition(150.0, 0.0, 200.0)
child:setRotation(0.0, 0.0, 0.0)

print(parent.objectName)
print(parent.parent)

parent:addToWorld()
child:addToWorld(parent)

local parentPos = parent:getPosition()
print(string.format("Parent position: %.2f %.2f %.2f", parentPos.x, parentPos.y, parentPos.z))
local parentRot = parent:getRotation()
print(string.format("Parent rotation: %.2f %.2f %.2f", parentRot.x, parentRot.y, parentRot.z))

print(child.parent)
child:floor()

function onResourceStart()
  --[[ print(parent.objectName)
  print(parent.parent)

  parent:addToWorld()
  child:addToWorld(parent)

  local parentPos = parent:getPosition()
  print(string.format("Parent position: %.2f %.2f %.2f", parentPos.x, parentPos.y, parentPos.z))
  local parentRot = parent:getRotation()
  print(string.format("Parent rotation: %.2f %.2f %.2f", parentRot.x, parentRot.y, parentRot.z))

  print(child.parent)
  child:floor() ]]
end

function onResourceStop()
  child:removeFromWorld()
  child = nil
  
  parent:removeFromWorld()
  parent = nil
end