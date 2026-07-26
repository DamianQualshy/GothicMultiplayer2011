LOG_INFO('[Dev][Client] ui.lua initialized.')


local title = Draw.new(0, 0, "NewDraw")

local logo = Texture.new(40, 80, 256, 256, "DEFAULT.TGA")

local posX, posY        = title.position
local pxX, pxY          = title.positionPx
local dr, dg, db        = title:getColor()
local dText      		= title.text
local dFont      		= title.font
local dAlpha     		= title.alpha
local tVisible	 		= title.visible

local tx, ty           = logo.position
local tpx, tpy         = logo.positionPx
local tw, th           = logo.size
local tpw, tph         = logo.sizePx
local rx, ry, rw, rh   = logo.rect
local prx, pry, prw, prh = logo.rectPx

local lr, lg, lb       = logo:getColor()
local la               = logo.alpha
local lVisible         = logo.visible
local currentFile      = logo.file

addEventHandler("onInit", function()
	logo:setPosition(0.05, 0.2)
	logo:setPositionPx(40, 100)

	logo:setSize(256, 256)
	logo:setSizePx(256, 256)

	logo:setRect(0, 0, 1, 1)
	logo:setRectPx(0, 0, 256, 256)

	logo:setColor(255, 255, 255)
	logo:setAlpha(230)
	logo:setVisible(true)
	logo:setFile("MENU_INGAME.TGA")


	title:setPosition(0.1, 0.1)
	title:setPositionPx(100, 60)

	title:setText("Hello UI")
	title:setFont("FONT_OLD_20_WHITE_HI.TGA")
	title:setColor(255, 200, 120)
	title:setAlpha(255)
	title:setVisible(true)

	
	logo:top()  -- bring to front
end)

addEventHandler("onExit", function()
	title:setVisible(false)
	title = nil

	logo:setVisible(false)
	logo = nil
end)

--[[ LOG_INFO("{}", isHudEnabled())
enableHud(HUD_ALL, false)
LOG_INFO("{}", isHudEnabled()) ]]

local draw3dTest = Draw3d.new(150.0, 0.0, 200.0, "Draw3d Test")
draw3dTest:setColor(255, 0, 0)
draw3dTest:setVisible(true)

function onResourceStart()
--[[ 	if tVisible then
		print(string.format("title.position %d %d", posX, posY))
		print(string.format("title.positionPx %d %d", pxX, pxY))
		print(string.format("title:getColor() %d %d %d", dr, dg, db))
		print(string.format("title.text %d", dText))
		print(string.format("title.font %s", dFont))
		print(string.format("title.alpha %d", dAlpha))
	end

	if lVisible then
		print(string.format("logo.position %d %d", tx, ty))
		print(string.format("logo.positionPx %d %d", tpx, tpy))
		print(string.format("logo.size %d %d", tw, th))
		print(string.format("logo.sizePx %d %d", tpw, tph))
		print(string.format("logo.rect %d %d %d %d", rx, ry, rw, rh))
		print(string.format("logo.rectPx %d %d %d %d", prx, pry, prw, prh))
		print(string.format("logo:getColor() %d %d %d", lr, lg, lb))
		print(string.format("logo.alpha %d", la))
		print(string.format("logo.file %s", currentFile))
	end ]]
end

function onResourceStop()
	if draw3dTest then
		draw3dTest:setVisible(false)
		draw3dTest = nil
	end
end
