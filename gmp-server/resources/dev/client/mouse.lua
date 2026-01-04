LOG_INFO('[Dev][Client] mouse.lua initialized.')

local cursorVisible = false
local currentTexture = 'LO.TGA'
local currentPos = {x = 4096, y = 4096}
local sizeVirtual = { w = 96, h = 96 }
local sensitivity = 5.0

addEventHandler('onMouseMove', function(dx, dy)
    if not cursorVisible then return end
    LOG_INFO(string.format('[Example] Mouse moved: %f %f', dx, dy))
end)

addEventHandler('onMouseUp', function(button)
    if not cursorVisible then return end
    LOG_INFO(string.format('[Example] Mouse button %d released', button))
end)

addEventHandler('onMouseDown', function(button)
    if not cursorVisible then return end
    
    if(isMouseBtnPressed(MOUSE_BUTTONMID)) then
        if cursorVisible then
			currentTexture = (currentTexture == 'LO.TGA') and 'DEFAULT.TGA' or 'LO.TGA'
			setCursorTxt(currentTexture)
			LOG_INFO(string.format('[Example] Cursor texture changed to %s', currentTexture))
            
			setCursorSize(sizeVirtual.w, sizeVirtual.h)
            setCursorSensitivity(sensitivity)
            setCursorPosition(currentPos.x, currentPos.y)
            LOG_INFO('[Example] Cursor defaults.')
        else
            cursorVisible = not cursorVisible
            setCursorVisible(cursorVisible)
            LOG_INFO(string.format('[Example] Cursor visibility set to %s', tostring(cursorVisible)))
        end
    end

    if(button == MOUSE_BUTTONLEFT) then
        setCursorSensitivity(sensitivity - 1.0)
    	LOG_INFO(string.format('[Example] Mouse sensitivity: %f', getCursorSensitivity()))
    end
    if(button == MOUSE_BUTTONRIGHT) then
        setCursorSensitivity(sensitivity + 1.0)
    	LOG_INFO(string.format('[Example] Mouse sensitivity: %f', getCursorSensitivity()))
    end
    LOG_INFO(string.format('[Example] Mouse button %d pressed', button))
end)

addEventHandler('onMouseWheel', function(direction)
    if not cursorVisible then return end
    if(direction > 0) then
        setCursorSize(sizeVirtual.w + 2, sizeVirtual.h + 2)
        LOG_INFO(string.format('[Example] Cursor size set to %dx%d (virtual)', sizeVirtual.w, sizeVirtual.h))
    else
        setCursorSize(sizeVirtual.w - 2, sizeVirtual.h - 2)
        LOG_INFO(string.format('[Example] Cursor size set to %dx%d (virtual)', sizeVirtual.w, sizeVirtual.h))
    end
    LOG_INFO(string.format('[Example] Mouse wheel moved: %d', direction))
end)