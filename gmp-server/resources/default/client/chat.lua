local Chat = {
  x = 0,
  y = 0,
  maxLines = 6,
  lineHeight = 200,
  inputTextOffset = 200,
  inputLimit = 84,
  font = 'FONT_DEFAULT.TGA',
  visible = true,
  fadeMs = 400,
  lines = {}
}

local inputPrompt = Draw.new(Chat.x, Chat.y + Chat.maxLines * Chat.lineHeight, '->')
local inputText = Draw.new(Chat.x + Chat.inputTextOffset, Chat.y + Chat.maxLines * Chat.lineHeight, '')
local lastInputOpen = false
local lastInputText = ''
local lastInputCaret = 0

inputPrompt:setFont(Chat.font)
inputText:setFont(Chat.font)
inputPrompt:setColor(255, 255, 255)
inputText:setColor(255, 255, 255)
inputPrompt:setVisible(false)
inputText:setVisible(false)
chatInputSetFont(Chat.font)

local function clampByte(value)
  value = tonumber(value) or 255
  if value < 0 then
    return 0
  end
  if value > 255 then
    return 255
  end
  return math.floor(value)
end

local function makeDraw(text, r, g, b)
  local draw = Draw.new(0, 0, text or '')
  draw:setFont(Chat.font)
  draw:setColor(clampByte(r), clampByte(g), clampByte(b))
  draw:setAlpha(0)
  draw:setVisible(false)
  return draw
end

local function hideLine(line)
  if line.nameDraw then
    line.nameDraw:setVisible(false)
  end
  if line.messageDraw then
    line.messageDraw:setVisible(false)
  end
end

local function applyLineVisibility(line)
  local visible = Chat.visible
  if line.nameDraw then
    line.nameDraw:setVisible(visible)
  end
  line.messageDraw:setVisible(visible)
end

local function getPlayerDisplayName(playerId)
  local name = getPlayerName(playerId)
  if name == nil or name == '' then
    return tostring(playerId)
  end
  return name
end

local function getPlayerDisplayColor(playerId, r, g, b)
  local color = getPlayerColor(playerId)
  if color then
    return clampByte(color.r), clampByte(color.g), clampByte(color.b)
  end
  return clampByte(r), clampByte(g), clampByte(b)
end

function Chat:layout()
  chatInputSetPosition(self.x, self.y + self.maxLines * self.lineHeight)

  for index, line in ipairs(self.lines) do
    local y = self.y + (index - 1) * self.lineHeight
    if line.nameDraw then
      line.nameDraw:setPosition(self.x, y)
      line.messageDraw:setPosition(self.x + line.nameDraw:getWidth(), y)
    else
      line.messageDraw:setPosition(self.x, y)
    end
  end
end

function Chat:clear()
  for _, line in ipairs(self.lines) do
    hideLine(line)
  end
  self.lines = {}
end

function Chat:setVisible(visible)
  self.visible = visible and true or false
  for _, line in ipairs(self.lines) do
    applyLineVisibility(line)
  end

  if not self.visible and chatInputIsOpen() then
    chatInputClear()
    chatInputClose()
  end
end

function Chat:push(line)
  table.insert(self.lines, line)
  while #self.lines > self.maxLines do
    local old = table.remove(self.lines, 1)
    hideLine(old)
  end
  applyLineVisibility(line)
  self:layout()
end

function Chat:print(message, r, g, b)
  self:push({
    messageDraw = makeDraw(message, r, g, b),
    createdAt = getTickCount()
  })
end

function Chat:printPlayer(playerId, r, g, b, message)
  local nr, ng, nb = getPlayerDisplayColor(playerId, r, g, b)
  local name = getPlayerDisplayName(playerId) .. ': '

  self:push({
    nameDraw = makeDraw(name, nr, ng, nb),
    messageDraw = makeDraw(message, r, g, b),
    createdAt = getTickCount()
  })
end

function Chat:updateLines()
  local now = getTickCount()
  for _, line in ipairs(self.lines) do
    local alpha = 255
    if self.fadeMs > 0 then
      alpha = math.min(255, math.floor(((now - line.createdAt) / self.fadeMs) * 255))
    end
    if line.nameDraw then
      line.nameDraw:setAlpha(alpha)
    end
    line.messageDraw:setAlpha(alpha)
  end
end

function Chat:updateInput()
  local open = chatInputIsOpen()
  local pos = chatInputGetPosition()
  local font = chatInputGetFont()
  local text = chatInputGetText()
  local caret = chatInputGetCaretPosition()
  local shownText = text

  if open and math.floor(getTickCount() / 750) % 2 == 0 then
    shownText = string.sub(text, 1, caret) .. '_' .. string.sub(text, caret + 1)
  end

  inputPrompt:setFont(font)
  inputText:setFont(font)
  inputPrompt:setPosition(pos.x, pos.y)
  inputText:setPosition(pos.x + self.inputTextOffset, pos.y)
  inputText:setText(shownText)
  inputPrompt:setVisible(open)
  inputText:setVisible(open)
end

local function insertPasteText(text)
  if not chatInputIsOpen() then
    return
  end

  text = tostring(text or '')
  text = string.gsub(text, '[\r\n]+', ' ')
  if text == '' then
    return
  end

  local current = chatInputGetText()
  local caret = chatInputGetCaretPosition()
  local nextText = string.sub(current, 1, caret) .. text .. string.sub(current, caret + 1)

  if #nextText > Chat.inputLimit then
    nextText = string.sub(nextText, 1, Chat.inputLimit)
  end

  chatInputSetText(nextText)
  chatInputSetCaretPosition(math.min(caret + #text, #nextText))
end

local function playChatGesture()
  if heroId == nil then
    return
  end

  playGesticulation(heroId)
end

local function updateChatGesture(open, text, caret)
  if open and lastInputOpen and (text ~= lastInputText or caret ~= lastInputCaret) then
    playChatGesture()
  end

  lastInputOpen = open
  lastInputText = open and text or ''
  lastInputCaret = open and caret or 0
end

addEventHandler('onRender', function()
  Chat:updateLines()
  Chat:updateInput()
  updateChatGesture(chatInputIsOpen(), chatInputGetText(), chatInputGetCaretPosition())
end)

addEventHandler('onPlayerMessage', function(playerId, r, g, b, message)
  if playerId ~= nil then
    Chat:printPlayer(playerId, r, g, b, message)
  else
    Chat:print(message, r, g, b)
  end
end)

addEventHandler('onPaste', function(text)
  insertPasteText(text)
end)

addEventHandler('onKeyDown', function(key)
  if key == KEY_T and Chat.visible and not isConsoleOpen() then
    chatInputOpen()
    return
  end

  if key == KEY_F7 then
    Chat:setVisible(not Chat.visible)
  end
end)

addEventHandler('onExit', function()
  Chat:clear()
  chatInputClear()
  chatInputClose()
end)

function onResourceStop()
  Chat:clear()
  inputPrompt:setVisible(false)
  inputText:setVisible(false)
  chatInputClear()
  chatInputClose()
end

Chat:layout()

return Chat
