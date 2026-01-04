LOG_INFO('[Dev][Client] font.lua initialized.')

local fonts = {
	"CP1250_FONT_10_BOOK",
	"CP1250_FONT_10_BOOK_HI",
	"CP1250_FONT_20_BOOK",
	"CP1250_FONT_20_BOOK_HI",
	"CP1250_FONT_DEFAULT",
	"CP1250_FONT_OLD_10_WHITE",
	"CP1250_FONT_OLD_10_WHITE_HI",
	"CP1250_FONT_OLD_20_WHITE",
	"CP1250_FONT_OLD_20_WHITE_HI",

	"CP1251_FONT_10_BOOK",
	"CP1251_FONT_10_BOOK_HI",
	"CP1251_FONT_20_BOOK",
	"CP1251_FONT_20_BOOK_HI",
	"CP1251_FONT_DEFAULT",
	"CP1251_FONT_OLD_10_WHITE",
	"CP1251_FONT_OLD_10_WHITE_HI",
	"CP1251_FONT_OLD_20_WHITE",
	"CP1251_FONT_OLD_20_WHITE_HI",
	
	"CP1252_FONT_10_BOOK",
	"CP1252_FONT_10_BOOK_HI",
	"CP1252_FONT_20_BOOK",
	"CP1252_FONT_20_BOOK_HI",
	"CP1252_FONT_DEFAULT",
	"CP1252_FONT_OLD_10_WHITE",
	"CP1252_FONT_OLD_10_WHITE_HI",
	"CP1252_FONT_OLD_20_WHITE",
	"CP1252_FONT_OLD_20_WHITE_HI",
	
	"CP1254_FONT_10_BOOK",
	"CP1254_FONT_10_BOOK_HI",
	"CP1254_FONT_20_BOOK",
	"CP1254_FONT_20_BOOK_HI",
	"CP1254_FONT_DEFAULT",
	"CP1254_FONT_OLD_10_WHITE",
	"CP1254_FONT_OLD_10_WHITE_HI",
	"CP1254_FONT_OLD_20_WHITE",
	"CP1254_FONT_OLD_20_WHITE_HI"
}

local testText = 
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ " ..
	"abcdefghijklmnopqrstuvwxyz " ..
	"0123456789 " ..
	"!@#$%^&*()_+-=[]{};:'\",.<>/?\\| `~ " ..
	"ĄąĆćĘęŁłŃńÓóŚśŹźŻż " ..
	"ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖØÙÚÛÜÝÞß " ..
	"ĞğİıŞşÇçÖöÜü " ..
	"АБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ " ..
	"абвгдеёжзийклмнопрстуфхцчшщъыьэюя"

local font = Draw.new(0, 0, testText)
local timerInt = 0
local currentFontIndex = 1

function fontTimer()
	currentFontIndex = currentFontIndex + 1
	if currentFontIndex > #fonts then
		currentFontIndex = 1
	end

	local newFont = fonts[currentFontIndex]
	font:setFont(newFont .. ".TGA")
	font:setText(testText)
end

addEventHandler("onInit", function()
	font:setVisible(true)
	timerInt = setTimer(fontTimer, 1000, 0) -- change font every second
end)

addEventHandler("onExit", function()
	font:setVisible(false)
	font:setPositionPx(200, 200)
	killTimer(timerInt)
end)