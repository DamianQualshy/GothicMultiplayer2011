#include "process_input.h"

#include <array>
#include <cstring>

#include "gothic_events.h"
#include "shared/event.h"

#include "lua_cursor.h"

namespace gmp::gothic {

bool s_prevPressed[kMaxTrackedCode + 1] = {};
bool s_pressedThisFrame[kMaxTrackedCode + 1] = {};
bool s_toggledThisFrame[kMaxTrackedCode + 1] = {};
std::array<bool, kMaxTrackedCode + 1> s_disabledKeys = {};

namespace {
constexpr std::array<int, 8> kMouseButtonCodes = {
    MOUSE_BUTTONLEFT, MOUSE_BUTTONRIGHT, MOUSE_BUTTONMID, MOUSE_XBUTTON1,
    MOUSE_XBUTTON2,  MOUSE_XBUTTON3,    MOUSE_XBUTTON4,  MOUSE_XBUTTON5};
}  // namespace

void ProcessInput(zCInput* zinput) {
  if (!zinput) {
    return;
  }

  std::memset(s_pressedThisFrame, 0, sizeof(s_pressedThisFrame));
  std::memset(s_toggledThisFrame, 0, sizeof(s_toggledThisFrame));
  std::array<bool, kMaxTrackedCode + 1> processed_codes = {};
  processed_codes.fill(false);

  // Process keyboard keys
  for (const auto& key : kKeyboardKeys) {
    const int code = key.code;
    if (code < 0 || code > kMaxTrackedCode || processed_codes[code]) {
      continue;
    }
    processed_codes[code] = true;
    const bool is_disabled = code >= 0 && code <= kMaxTrackedCode && s_disabledKeys[code];
    const bool is_pressed = !is_disabled && zinput->KeyPressed(code) != 0;
    const bool was_pressed = s_prevPressed[code];

    if (is_pressed != was_pressed) {
      s_toggledThisFrame[code] = true;

      if (is_pressed) {
        EventManager::Instance().TriggerEvent(kEventOnKeyDownName, OnKeyEvent{code});
      } else {
        EventManager::Instance().TriggerEvent(kEventOnKeyUpName, OnKeyEvent{code});
      }
    }

    s_pressedThisFrame[code] = is_pressed;
  }

  // Process mouse movement and buttons
  for (std::size_t i = 0; i < kMouseButtonCodes.size(); ++i) {
    const int code = kMouseButtonCodes[i];
    const bool is_pressed = zinput->KeyPressed(code) != 0;
    const bool was_pressed = s_prevPressed[code];

    if (is_pressed != was_pressed) {
      s_toggledThisFrame[code] = true;

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

  if (dx != 0.0f || dy != 0.0f) {
    EventManager::Instance().TriggerEvent(kEventOnMouseMoveName, OnMouseMoveEvent{dx, dy});
  }

  if (wheel != 0.0f) {
    EventManager::Instance().TriggerEvent(kEventOnMouseWheelName, OnMouseWheelEvent{wheel});
  }

  std::memcpy(s_prevPressed, s_pressedThisFrame, sizeof(s_prevPressed));

  LuaCursor::Instance().UpdateFromInput(zinput);
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
* The function is used to check whether the specified keyboard key is pressed.
*
* @version  0.3.0
* @name     KeyPressed
* @side     client
* @category Input
* @param    (number) key      The key code to check. For more information about key codes, see [Key Constants](../../client-constants/Key.md).
* @return   (boolean)             True if the key is currently pressed, false otherwise.
*
*/
  lua.set_function("KeyPressed", [](int key) -> bool {
    if (key < 0 || key > MAX_KEYS_AND_CODES) {
      return false;
    }
    return s_pressedThisFrame[key];
  });

/* luagmp (func)
*
* The function is used to check whether the specified keyboard key was toggled from unpressed to pressed state.
*
* @version  0.3.0
* @name     KeyToggled
* @side     client
* @category Input
* @param    (number) key      The key code to check. For more information about key codes, see [Key Constants](../../client-constants/Key.md).
* @return   (boolean)             True if the key was toggled, false otherwise.
*
*/
  lua.set_function("KeyToggled", [](int key) -> bool {
    if (key < 0 || key > MAX_KEYS_AND_CODES) {
      return false;
    }
    return s_toggledThisFrame[key];
  });

/* luagmp (func)
*
* Disable/enable default game actions that are bound to keys.
*
* @version  0.3.0
* @name     disableControls
* @side     client
* @category Input
* @param   (boolean)             true when you want to disable game keys, otherwise false.
* @return   (boolean)             True on success.
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
* The function is used to check whether default game actions are disabled.
*
* @version  0.3.0
* @name     isControlsDisabled
* @side     client
* @category Input
* @return   (boolean)             true when disabled, otherwise false.
*
*/
  lua.set_function("isControlsDisabled", []() {
    return Gothic_II_Addon::player->ai_disabled;
  });

/* luagmp (func)
*
* Disable/enable specified keyboard key, like: ESCAPE, TAB, etc.
*
* @version  0.3.0
* @name     disableKey
* @side     client
* @category Input
* @param   (number) keyId          The key code to disable. For more information about key codes, see [Key Constants](../../client-constants/Key.md).
* @param   (boolean) toggle          true when you want to disable specified keyboard key, otherwise false
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
* The function is used to check whether the specified keyboard key is disabled.
*
* @version  0.3.0
* @name     isKeyDisabled
* @side     client
* @category Input
* @param   (number) keyId          The key code to check. For more information about key codes, see [Key Constants](../../client-constants/Key.md).
* @return   (boolean)             true when disabled, otherwise false.
*
*/
  lua.set_function("isKeyDisabled", [](int key) {
    if (key < 0 || key > kMaxTrackedCode) {
      return false;
    }

    return s_disabledKeys[key];
  });
}

void BindCursor(sol::state& lua) {
/* luagmp (func)
*
* Sets the cursor position in virtual (screen-scaled) coordinates.
*
* @version  0.3.0
* @name     setCursorPosition
* @side     client
* @category Cursor
* @param    (number) x X position.
* @param    (number) y Y position.
* @return   (boolean) True on success.
*
*/
  lua.set_function("setCursorPosition", [](int x, int y) -> bool {
    LuaCursor::Instance().setPosition(x, y); 
    return true;
  });
  
/* luagmp (func)
*
* Returns the cursor position in virtual (screen-scaled) coordinates.
*
* @version  0.3.0
* @name     getCursorPosition
* @side     client
* @category Cursor
* @return   (number, int) X and Y position.
*
*/
  lua.set_function("getCursorPosition", [](sol::this_state s) {
    return LuaCursor::Instance().getPosition(s); 
  });

/* luagmp (func)
*
* Sets the cursor position in pixel coordinates.
*
* @version  0.3.0
* @name     setCursorPositionPx
* @side     client
* @category Cursor
* @param    (number) x X position in pixels.
* @param    (number) y Y position in pixels.
* @return   (boolean) True on success.
*
*/
  lua.set_function("setCursorPositionPx", [](int x, int y) -> bool {
    LuaCursor::Instance().setPositionPx(x, y);
    return true;
  });

/* luagmp (func)
*
* Returns the cursor position in pixel coordinates.
*
* @version  0.3.0
* @name     getCursorPositionPx
* @side     client
* @category Cursor
* @return   (number, int) X and Y position in pixels.
*
*/
  lua.set_function("getCursorPositionPx", [](sol::this_state s) {
    return LuaCursor::Instance().getPositionPx(s);
  });

/* luagmp (func)
*
* Sets the cursor size in virtual (screen-scaled) units.
*
* @version  0.3.0
* @name     setCursorSize
* @side     client
* @category Cursor
* @param    (number) width Cursor width.
* @param    (number) height Cursor height.
* @return   (boolean) True on success.
*
*/
  lua.set_function("setCursorSize", [](int width, int height) -> bool {
    LuaCursor::Instance().setSize(width, height);
    return true;
  });
  
/* luagmp (func)
*
* Returns the cursor size in virtual (screen-scaled) units.
*
* @version  0.3.0
* @name     getCursorSize
* @side     client
* @category Cursor
* @return   (number, int) Cursor width and height.
*
*/
  lua.set_function("getCursorSize", [](sol::this_state s) {
    return LuaCursor::Instance().getSize(s);
  });

/* luagmp (func)
*
* Sets the cursor size in pixel units.
*
* @version  0.3.0
* @name     setCursorSizePx
* @side     client
* @category Cursor
* @param    (number) width Cursor width in pixels.
* @param    (number) height Cursor height in pixels.
* @return   (boolean) True on success.
*
*/
  lua.set_function("setCursorSizePx", [](int width, int height) -> bool {
    LuaCursor::Instance().setSizePx(width, height);
    return true;
  });
  
/* luagmp (func)
*
* Returns the cursor size in pixel units.
*
* @version  0.3.0
* @name     getCursorSizePx
* @side     client
* @category Cursor
* @return   (number, int) Cursor width and height in pixels.
*
*/
  lua.set_function("getCursorSizePx", [](sol::this_state s) {
    return LuaCursor::Instance().getSizePx(s);
  });

 /* luagmp (func)
*
* Sets the cursor texture.
*
* @version  0.3.0
* @name     setCursorTxt
* @side     client
* @category Cursor
* @param    (string) file Texture file name.
* @return   (boolean) True on success.
*
*/
  lua.set_function("setCursorTxt", [](const std::string& file) -> bool {
    LuaCursor::Instance().setTexture(file);
    return true;
  });

/* luagmp (func)
*
* Returns the current cursor texture.
*
* @version  0.3.0
* @name     getCursorTxt
* @side     client
* @category Cursor
* @return   (string) Texture file name.
*
*/
  lua.set_function("getCursorTxt", []() {
    return LuaCursor::Instance().getTexture();
  });

/* luagmp (func)
*
* Sets whether the cursor is visible.
*
* @version  0.3.0
* @name     setCursorVisible
* @side     client
* @category Cursor
* @param    (boolean) toggle True to show the cursor, false to hide it.
* @return   (boolean) True on success.
*
*/
  lua.set_function("setCursorVisible", [](bool toggle) -> bool {
    LuaCursor::Instance().setVisible(toggle);
    return true;
  });

/* luagmp (func)
*
* Returns whether the cursor is currently visible.
*
* @version  0.3.0
* @name     isCursorVisible
* @side     client
* @category Cursor
* @return   (boolean) True if the cursor is visible.
*
*/
  lua.set_function("isCursorVisible", []() {
    return LuaCursor::Instance().isVisible();
  });

/* luagmp (func)
*
* Sets the cursor movement sensitivity.
*
* @version  0.3.0
* @name     setCursorSensitivity
* @side     client
* @category Cursor
* @param    (number) sensitivity Cursor sensitivity multiplier.
* @return   (boolean) True on success.
*
*/
  lua.set_function("setCursorSensitivity", [](float sensitivity) -> bool {
    LuaCursor::Instance().setSensitivity(sensitivity);
    return true;
  });

/* luagmp (func)
*
* Returns the cursor movement sensitivity.
*
* @version  0.3.0
* @name     getCursorSensitivity
* @side     client
* @category Cursor
* @return   (number) Cursor sensitivity multiplier.
*
*/
  lua.set_function("getCursorSensitivity", []() {
    return LuaCursor::Instance().getSensitivity();
  });

/* luagmp (func)
*
* Returns whether a mouse button is currently pressed.
*
* @version  0.3.0
* @name     isMouseBtnPressed
* @side     client
* @category Cursor
* @param    (number) button Mouse button identifier.
* @return   (boolean) True if the button is pressed.
*
*/
  lua.set_function("isMouseBtnPressed", [](int button) {
    return LuaCursor::Instance().isButtonPressed(button);
  });
}

}  // namespace gmp::gothic