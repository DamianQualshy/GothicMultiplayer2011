#include "process_input.h"

#include <windows.h>

#include <array>
#include <cstring>
#include <string>

#include "CChat.h"
#include "gothic_events.h"
#include "shared/event.h"

#include "lua_cursor.h"

namespace gmp::gothic {

bool s_prevPressed[kMaxTrackedCode + 1] = {};
bool s_pressedThisFrame[kMaxTrackedCode + 1] = {};
bool s_toggledThisFrame[kMaxTrackedCode + 1] = {};
std::array<bool, kMaxTrackedCode + 1> s_disabledKeys = {};

namespace {
constexpr int kMaxLogicalInputCode = GAME_LAME_HEAL;
constexpr std::array<int, 8> kMouseButtonCodes = {
    MOUSE_BUTTONLEFT, MOUSE_BUTTONRIGHT, MOUSE_BUTTONMID, MOUSE_XBUTTON1,
    MOUSE_XBUTTON2,  MOUSE_XBUTTON3,    MOUSE_XBUTTON4,  MOUSE_XBUTTON5};

std::array<bool, kMaxTrackedCode + 1> s_prevRawPressed = {};
std::array<bool, kMaxTrackedCode + 1> s_rawPressedThisFrame = {};
std::array<bool, kMaxLogicalInputCode + 1> s_prevLogicalPressed = {};
std::array<bool, kMaxLogicalInputCode + 1> s_logicalPressedThisFrame = {};
std::array<bool, kMaxLogicalInputCode + 1> s_logicalToggledThisFrame = {};
std::array<bool, kMaxLogicalInputCode + 1> s_disabledLogicalKeys = {};

bool IsLogicalInputCode(int code) {
  for (const auto& key : kGameKeys) {
    if (key.code == code) {
      return true;
    }
  }
  return false;
}

bool ReadLogicalInputPressed(zCInput* zinput, int code, bool chat_input_active) {
  if (!zinput || chat_input_active) {
    return false;
  }

  return zinput->GetState(static_cast<unsigned short>(code)) != 0.0f;
}

std::string WideClipboardTextToAnsi(const wchar_t* text) {
  if (!text) {
    return {};
  }

  const int required_size = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (required_size <= 1) {
    return {};
  }

  std::string result(static_cast<std::size_t>(required_size - 1), '\0');
  WideCharToMultiByte(CP_ACP, 0, text, -1, result.data(), required_size, nullptr, nullptr);
  return result;
}

std::string ReadClipboardText() {
  if (!OpenClipboard(nullptr)) {
    return {};
  }

  std::string result;
  if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
      if (auto* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
        result = WideClipboardTextToAnsi(text);
        GlobalUnlock(handle);
      }
    }
  } else if (IsClipboardFormatAvailable(CF_TEXT)) {
    if (HANDLE handle = GetClipboardData(CF_TEXT)) {
      if (auto* text = static_cast<const char*>(GlobalLock(handle))) {
        result = text;
        GlobalUnlock(handle);
      }
    }
  }

  CloseClipboard();
  return result;
}

bool ShouldEmitWhileChatInputActive(int code) {
  return code == KEY_UP || code == KEY_UPARROW || code == KEY_DOWN || code == KEY_DOWNARROW ||
         code == KEY_PRIOR || code == KEY_PGUP || code == KEY_NEXT || code == KEY_PGDN ||
         code == KEY_HOME || code == KEY_END || code == KEY_RETURN || code == KEY_NUMPADENTER ||
         code == KEY_ESCAPE;
}

void NormalizeCursorDelta(zCInput* zinput, float& dx, float& dy) {
  float sensitivity_x = 1.0f;
  float sensitivity_y = 1.0f;
  zinput->GetMouseSensitivity(sensitivity_x, sensitivity_y);
  if (sensitivity_x != 0.0f) {
    dx /= sensitivity_x;
  }
  if (sensitivity_y != 0.0f) {
    dy /= sensitivity_y;
  }

  int flip_x = 0;
  int flip_y = 0;
  zinput->GetMouseFlipXY(flip_x, flip_y);
  if (flip_x) {
    dx = -dx;
  }
  if (flip_y) {
    dy = -dy;
  }
}
}  // namespace

void ProcessInput(zCInput* zinput) {
  if (!zinput) {
    return;
  }

  std::memset(s_pressedThisFrame, 0, sizeof(s_pressedThisFrame));
  std::memset(s_toggledThisFrame, 0, sizeof(s_toggledThisFrame));
  s_rawPressedThisFrame.fill(false);
  s_logicalPressedThisFrame.fill(false);
  s_logicalToggledThisFrame.fill(false);
  std::array<bool, kMaxTrackedCode + 1> processed_codes = {};
  processed_codes.fill(false);
  const bool chat_input_active = CChat::GetInstance()->IsInputActive();

  for (const auto& key : kGameKeys) {
    const int code = key.code;
    if (s_disabledLogicalKeys[code]) {
      continue;
    }

    const bool is_pressed = ReadLogicalInputPressed(zinput, code, chat_input_active);
    s_logicalPressedThisFrame[code] = is_pressed;
    s_logicalToggledThisFrame[code] = is_pressed && !s_prevLogicalPressed[code];
  }

  // Process keyboard keys
  for (const auto& key : kKeyboardKeys) {
    const int code = key.code;
    if (code < 0 || code > kMaxTrackedCode || processed_codes[code]) {
      continue;
    }
    processed_codes[code] = true;
    const bool is_disabled = code >= 0 && code <= kMaxTrackedCode && s_disabledKeys[code];
    const bool raw_pressed = zinput->KeyPressed(code) != 0;
    const bool is_pressed = (!chat_input_active || ShouldEmitWhileChatInputActive(code)) && !is_disabled && raw_pressed;
    const bool was_pressed = s_prevPressed[code];
    s_rawPressedThisFrame[code] = raw_pressed;

    if (is_pressed && !was_pressed) {
      s_toggledThisFrame[code] = true;
    }

    if (is_pressed != was_pressed) {
      if (is_pressed) {
        EventManager::Instance().TriggerEvent(kEventOnKeyDownName, OnKeyEvent{code});
      } else {
        EventManager::Instance().TriggerEvent(kEventOnKeyUpName, OnKeyEvent{code});
      }
    }

    s_pressedThisFrame[code] = is_pressed;
  }

  const bool ctrl_pressed =
      (s_rawPressedThisFrame[KEY_LCONTROL] && !s_disabledKeys[KEY_LCONTROL]) ||
      (s_rawPressedThisFrame[KEY_RCONTROL] && !s_disabledKeys[KEY_RCONTROL]);
  const bool paste_pressed = s_rawPressedThisFrame[KEY_V] && !s_prevRawPressed[KEY_V] && !s_disabledKeys[KEY_V];
  if (ctrl_pressed && paste_pressed) {
    EventManager::Instance().TriggerEvent(kEventOnPasteName, OnPasteEvent{ReadClipboardText()});
  }

  // Process mouse movement and buttons
  for (std::size_t i = 0; i < kMouseButtonCodes.size(); ++i) {
    const int code = kMouseButtonCodes[i];
    const bool raw_pressed = zinput->KeyPressed(code) != 0;
    const bool is_pressed = !chat_input_active && raw_pressed;
    const bool was_pressed = s_prevPressed[code];
    s_rawPressedThisFrame[code] = raw_pressed;

    if (is_pressed && !was_pressed) {
      s_toggledThisFrame[code] = true;
    }

    if (is_pressed != was_pressed) {
      if (is_pressed) {
        EventManager::Instance().TriggerEvent(kEventOnMouseDownName, OnMouseButtonEvent{code});
      } else {
        EventManager::Instance().TriggerEvent(kEventOnMouseUpName, OnMouseButtonEvent{code});
      }
    }

    s_pressedThisFrame[code] = is_pressed;
  }

  float dx = 0.0f;
  float dy = 0.0f;
  float wheel = 0.0f;
  zinput->GetMousePos(dx, dy, wheel);
  float cursor_dx = dx;
  float cursor_dy = dy;
  NormalizeCursorDelta(zinput, cursor_dx, cursor_dy);

  if (!chat_input_active && (dx != 0.0f || dy != 0.0f)) {
    EventManager::Instance().TriggerEvent(kEventOnMouseMoveName, OnMouseMoveEvent{dx, dy});
  }

  if (wheel != 0.0f) {
    EventManager::Instance().TriggerEvent(kEventOnMouseWheelName, OnMouseWheelEvent{wheel});
  }

  std::memcpy(s_prevPressed, s_pressedThisFrame, sizeof(s_prevPressed));
  s_prevRawPressed = s_rawPressedThisFrame;
  s_prevLogicalPressed = s_logicalPressedThisFrame;

  LuaCursor::Instance().UpdateFromInput(cursor_dx, cursor_dy);
}

void BindInputConstants(sol::state& lua) {
  // Bind keyboard keys
  for (const auto& key : kKeyboardKeys) {
    lua[key.name] = key.code;
  }

  // Bind mouse keys
  for (const auto& key : kMouseKeys) {
    lua[key.name] = key.code;
  }

  // Bind game/logical action keys
  for (const auto& key : kGameKeys) {
    lua[key.name] = key.code;
  }

  // Bind input query functions

/* luagmp (func)
*
* This function will check whether the specified keyboard key is pressed.
*
* @version  0.3.0
* @name     keyPressed
* @side     client
* @category Input
* @param    (number) key      The key code to check. For more information about key codes, see [Key Constants](../../client-constants/Key.md).
* @return   (boolean)         True if the key is currently pressed, false otherwise.
*
*/
  lua.set_function("keyPressed", [](int key) -> bool {
    if (key < 0 || key > kMaxTrackedCode) {
      return false;
    }
    return s_pressedThisFrame[key];
  });

/* luagmp (func)
*
* This function will check whether the specified Gothic logical game action is pressed.
*
* @version  0.3.0
* @name     logicalKeyPressed
* @side     client
* @category Input
* @param    (number) key      The logical game action code to check. For more information, see [Key Constants](../../client-constants/Key.md).
* @return   (boolean)         True if the logical action is currently active, false otherwise.
*
*/
  lua.set_function("logicalKeyPressed", [](int key) -> bool {
    if (key < 0 || key > kMaxLogicalInputCode || !IsLogicalInputCode(key)) {
      return false;
    }
    if (s_disabledLogicalKeys[key]) {
      return false;
    }
    return s_logicalPressedThisFrame[key];
  });

/* luagmp (func)
*
* This function will check whether the specified keyboard key was toggled from unpressed to pressed state.
*
* @version  0.3.0
* @name     keyToggled
* @side     client
* @category Input
* @param    (number) key      The key code to check. For more information about key codes, see [Key Constants](../../client-constants/Key.md).
* @return   (boolean)         True if the key became pressed this frame, false otherwise.
*
*/
  lua.set_function("keyToggled", [](int key) -> bool {
    if (key < 0 || key > kMaxTrackedCode) {
      return false;
    }
    return s_toggledThisFrame[key];
  });

/* luagmp (func)
*
* This function will check whether the specified Gothic logical game action was toggled from unpressed to pressed state.
*
* @version  0.3.0
* @name     logicalKeyToggled
* @side     client
* @category Input
* @param    (number) key      The logical game action code to check. For more information, see [Key Constants](../../client-constants/Key.md).
* @return   (boolean)         True if the logical action became pressed this frame, false otherwise.
*
*/
  lua.set_function("logicalKeyToggled", [](int key) -> bool {
    if (key < 0 || key > kMaxLogicalInputCode || !IsLogicalInputCode(key)) {
      return false;
    }
    if (s_disabledLogicalKeys[key]) {
      return false;
    }
    return s_logicalToggledThisFrame[key];
  });

/* luagmp (func)
*
* This functiuon will disable/enable default game actions that are bound to keys.
*
* @version  0.3.0
* @name     disableControls
* @side     client
* @category Input
* @param    (boolean)          True when you want to disable game keys, otherwise false.
* @return   (boolean)          True on success.
*
*/
  lua.set_function("disableControls", [](bool toggle) -> bool {
    if (!Gothic_II_Addon::player) {
      return false;
    }

    Gothic_II_Addon::player->SetNpcAIDisabled(toggle);
    return true;
  });

/* luagmp (func)
*
* This function will check whether default game actions are disabled.
*
* @version  0.3.0
* @name     isControlsDisabled
* @side     client
* @category Input
* @return   (boolean)           True when disabled, otherwise false.
*
*/
  lua.set_function("isControlsDisabled", []() {
    return Gothic_II_Addon::player && Gothic_II_Addon::player->ai_disabled;
  });

/* luagmp (func)
*
* This function will disable/enable specified keyboard key, like: ESCAPE, TAB, etc.
*
* @version  0.3.0
* @name     disableKey
* @side     client
* @category Input
* @param    (number) keyId        The key code to disable. For more information about key codes, see [Key Constants](../../client-constants/Key.md).
* @param    (boolean) toggle      True when you want to disable specified keyboard key, otherwise false
* @return   (boolean)             True on success.
*
*/
  lua.set_function("disableKey", [](int key, bool toggle) -> bool {
    if (key < 0 || key > kMaxTrackedCode) {
      return false;
    }

    s_disabledKeys[key] = toggle;
    return true;
  });

/* luagmp (func)
*
* This function will disable/enable a Gothic logical game action for Lua input polling.
*
* @version  0.3.0
* @name     disableLogicalKey
* @side     client
* @category Input
* @param    (number) key        The logical game action code to disable.
* @param    (boolean) toggle    True to disable, false to enable.
* @return   (boolean)           True on success.
*
*/
  lua.set_function("disableLogicalKey", [](int key, bool toggle) -> bool {
    if (key < 0 || key > kMaxLogicalInputCode || !IsLogicalInputCode(key)) {
      return false;
    }

    s_disabledLogicalKeys[key] = toggle;
    return true;
  });

/* luagmp (func)
*
* This function will check whether the specified keyboard key is disabled.
*
* @version  0.3.0
* @name     isKeyDisabled
* @side     client
* @category Input
* @param    (number) keyId        The key code to check. For more information about key codes, see [Key Constants](../../client-constants/Key.md).
* @return   (boolean)             True when disabled, otherwise false.
*
*/
  lua.set_function("isKeyDisabled", [](int key) {
    if (key < 0 || key > kMaxTrackedCode) {
      return false;
    }

    return s_disabledKeys[key];
  });

/* luagmp (func)
*
* This function will check whether a Gothic logical game action is disabled for Lua input polling.
*
* @version  0.3.0
* @name     isLogicalKeyDisabled
* @side     client
* @category Input
* @param    (number) key        The logical game action code to check.
* @return   (boolean)           True when disabled, otherwise false.
*
*/
  lua.set_function("isLogicalKeyDisabled", [](int key) -> bool {
    if (key < 0 || key > kMaxLogicalInputCode || !IsLogicalInputCode(key)) {
      return false;
    }

    return s_disabledLogicalKeys[key];
  });
}

void BindCursor(sol::state& lua) {
/* luagmp (func)
*
* This function will set the cursor position in virtual (screen-scaled) coordinates.
*
* @version  0.3.0
* @name     setCursorPosition
* @side     client
* @category Cursor
* @param    (number) x      X position.
* @param    (number) y      Y position.
*
*/
  lua.set_function("setCursorPosition", [](int x, int y) {
    LuaCursor::Instance().setPosition(x, y); 
  });
  
/* luagmp (func)
*
* This function will return the current cursor position in virtual (screen-scaled) coordinates.
*
* @version  0.3.0
* @name     getCursorPosition
* @side     client
* @category Cursor
* @return   ({x, y})    X and Y position.
*
*/
  lua.set_function("getCursorPosition", [](sol::this_state s) {
    return LuaCursor::Instance().getPosition(s); 
  });

/* luagmp (func)
*
* This function will set the cursor position in pixel coordinates.
*
* @version  0.3.0
* @name     setCursorPositionPx
* @side     client
* @category Cursor
* @param    (number) x      X position in pixels.
* @param    (number) y      Y position in pixels.
*
*/
  lua.set_function("setCursorPositionPx", [](int x, int y) {
    LuaCursor::Instance().setPositionPx(x, y);
  });

/* luagmp (func)
*
* This function will return the current cursor position in pixel coordinates.
*
* @version  0.3.0
* @name     getCursorPositionPx
* @side     client
* @category Cursor
* @return   ({x, y})    X and Y position in pixels.
*
*/
  lua.set_function("getCursorPositionPx", [](sol::this_state s) {
    return LuaCursor::Instance().getPositionPx(s);
  });

/* luagmp (func)
*
* This function will set the cursor size in virtual units.
*
* @version  0.3.0
* @name     setCursorSize
* @side     client
* @category Cursor
* @param    (number) width    Cursor width.
* @param    (number) height   Cursor height.
*
*/
  lua.set_function("setCursorSize", [](int width, int height) {
    LuaCursor::Instance().setSize(width, height);
  });
  
/* luagmp (func)
*
* This function will return the cursor size in virtual (screen-scaled) units.
*
* @version  0.3.0
* @name     getCursorSize
* @side     client
* @category Cursor
* @return   ({x, y})     Cursor width and height.
*
*/
  lua.set_function("getCursorSize", [](sol::this_state s) {
    return LuaCursor::Instance().getSize(s);
  });

/* luagmp (func)
*
* This function will set the cursor size in pixel units.
*
* @version  0.3.0
* @name     setCursorSizePx
* @side     client
* @category Cursor
* @param    (number) width      Cursor width in pixels.
* @param    (number) height     Cursor height in pixels.
*
*/
  lua.set_function("setCursorSizePx", [](int width, int height) {
    LuaCursor::Instance().setSizePx(width, height);
  });
  
/* luagmp (func)
*
* This function will return the cursor size in pixel units.
*
* @version  0.3.0
* @name     getCursorSizePx
* @side     client
* @category Cursor
* @return   ({x, y})     Cursor width and height in pixels.
*
*/
  lua.set_function("getCursorSizePx", [](sol::this_state s) {
    return LuaCursor::Instance().getSizePx(s);
  });

 /* luagmp (func)
*
* This function will set the cursor texture.
*
* @version  0.3.0
* @name     setCursorTxt
* @side     client
* @category Cursor
* @param    (string) file    Texture file name.
*
*/
  lua.set_function("setCursorTxt", [](const std::string& file) {
    LuaCursor::Instance().setTexture(file);
  });

/* luagmp (func)
*
* This function will return the current cursor texture.
*
* @version  0.3.0
* @name     getCursorTxt
* @side     client
* @category Cursor
* @return   (string)        Texture file name.
*
*/
  lua.set_function("getCursorTxt", []() {
    return LuaCursor::Instance().getTexture();
  });

/* luagmp (func)
*
* This function will set whether the cursor is visible.
*
* @version  0.3.0
* @name     setCursorVisible
* @side     client
* @category Cursor
* @param    (boolean) toggle    True to show the cursor, false to hide it.
*
*/
  lua.set_function("setCursorVisible", [](bool toggle) {
    LuaCursor::Instance().setVisible(toggle);
  });

/* luagmp (func)
*
* This function will return whether the cursor is currently visible.
*
* @version  0.3.0
* @name     isCursorVisible
* @side     client
* @category Cursor
* @return   (boolean)           True if the cursor is visible.
*
*/
  lua.set_function("isCursorVisible", []() {
    return LuaCursor::Instance().isVisible();
  });

/* luagmp (func)
*
* This function will set the cursor movement sensitivity.
*
* @version  0.3.0
* @name     setCursorSensitivity
* @side     client
* @category Cursor
* @param    (number) sensitivity    Cursor sensitivity multiplier.
*
*/
  lua.set_function("setCursorSensitivity", [](float sensitivity) {
    LuaCursor::Instance().setSensitivity(sensitivity);
  });

/* luagmp (func)
*
* This function will return the cursor movement sensitivity.
*
* @version  0.3.0
* @name     getCursorSensitivity
* @side     client
* @category Cursor
* @return   (number)        Cursor sensitivity multiplier.
*
*/
  lua.set_function("getCursorSensitivity", []() {
    return LuaCursor::Instance().getSensitivity();
  });

/* luagmp (func)
*
* This function will return whether a mouse button is currently pressed.
*
* @version  0.3.0
* @name     isMouseBtnPressed
* @side     client
* @category Cursor
* @param    (number) button     Mouse button identifier.
*
*/
  lua.set_function("isMouseBtnPressed", [](int button) {
    return LuaCursor::Instance().isButtonPressed(button);
  });
}

}  // namespace gmp::gothic
