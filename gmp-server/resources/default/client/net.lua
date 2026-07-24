local Net = {
  x = 3000,
  y = 1000,
  lineHeight = 200,
  font = 'Font_Old_10_White_Hi.TGA',
  visible = false,
  lines = {}
}

local function formatInteger(value)
  return string.format('%.0f', tonumber(value) or 0)
end

local function formatPercent(value)
  return string.format('%.2f%%', (tonumber(value) or 0) * 100)
end

local function makeLine(index, text, r, g, b)
  local draw = Draw.new(Net.x, Net.y + (index - 1) * Net.lineHeight, text)
  draw:setFont(Net.font)
  draw:setColor(r, g, b)
  draw:setVisible(false)
  return draw
end

Net.lines = {
  makeLine(1, 'Network', 243, 8, 188),
  makeLine(2, '', 255, 255, 255),
  makeLine(3, '', 255, 255, 255),
  makeLine(4, '', 255, 255, 255),
  makeLine(5, '', 255, 255, 255),
  makeLine(6, '', 255, 255, 255),
  makeLine(7, '', 255, 255, 255),
  makeLine(8, '', 255, 255, 255),
  makeLine(9, '', 255, 255, 255)
}

function Net:setVisible(visible)
  self.visible = visible and true or false
  for _, line in ipairs(self.lines) do
    line:setVisible(self.visible)
  end
end

function Net:update()
  local stats = getNetworkStats()

  self.lines[2]:setText('FPS: ' .. formatInteger(getFpsRate()))
  self.lines[3]:setText('Message send buffer: ' .. formatInteger(stats.messageInSendBuffer))
  self.lines[4]:setText('Bytes send buffer: ' .. formatInteger(stats.bytesInSendBuffer))
  self.lines[5]:setText('Message resend buffer: ' .. formatInteger(stats.messagesInResendBuffer))
  self.lines[6]:setText('Bytes resend buffer: ' .. formatInteger(stats.bytesInResendBuffer))
  self.lines[7]:setText('Packet loss last second: ' .. formatPercent(stats.packetlossLastSecond))
  self.lines[8]:setText('Packet loss total: ' .. formatPercent(stats.packetlossTotal))
  self.lines[9]:setText('Received packets: ' .. formatInteger(stats.packetReceived))
end

addEventHandler('onRender', function()
  if Net.visible then
    Net:update()
  end
end)

addEventHandler('onKeyDown', function(key)
  if key == KEY_F6 and not isConsoleOpen() then
    Net:update()
    Net:setVisible(true)
  end
end)

addEventHandler('onKeyUp', function(key)
  if key == KEY_F6 then
    Net:setVisible(false)
  end
end)

addEventHandler('onExit', function()
  Net:setVisible(false)
end)

return Net
