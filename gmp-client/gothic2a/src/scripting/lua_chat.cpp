/*
MIT License

Copyright (c) 2025 Gothic Multiplayer Team.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "lua_chat.h"

#include <string>

#include "CChat.h"
#include "shared/lua_runtime/lua_math.h"

namespace gmp::gothic {

namespace {

CChat& Chat() {
  return *CChat::GetInstance();
}

}  // namespace

void BindChat(sol::state& lua) {
/* luagmp (func)
*
* This function clears the current chat input text.
*
* @version  0.3.0
* @name     chatInputClear
* @side     client
* @category Chat
*
*/
  lua["chatInputClear"] = []() {
    Chat().ClearInput();
  };

/* luagmp (func)
*
* This function closes the chat input.
*
* @version  0.3.0
* @name     chatInputClose
* @side     client
* @category Chat
*
*/
  lua["chatInputClose"] = []() {
    Chat().CloseInput();
  };

/* luagmp (func)
*
* This function returns the current chat input caret position.
*
* @version  0.3.0
* @name     chatInputGetCaretPosition
* @side     client
* @category Chat
* @return   (number) Zero-based caret position.
*
*/
  lua["chatInputGetCaretPosition"] = []() {
    return Chat().GetInputCaretPosition();
  };

/* luagmp (func)
*
* This function returns the font assigned to the chat input.
*
* @version  0.3.0
* @name     chatInputGetFont
* @side     client
* @category Chat
* @return   (string) Font name.
*
*/
  lua["chatInputGetFont"] = []() {
    return Chat().GetInputFont();
  };

/* luagmp (func)
*
* This function returns the chat input position in virtual screen units.
*
* @version  0.3.0
* @name     chatInputGetPosition
* @side     client
* @category Chat
* @return   (Vec2) Position with x and y fields.
*
*/
  lua["chatInputGetPosition"] = []() {
    return ::lua::types::Vec2(static_cast<float>(Chat().GetInputX()), static_cast<float>(Chat().GetInputY()));
  };

/* luagmp (func)
*
* This function returns the current chat input text.
*
* @version  0.3.0
* @name     chatInputGetText
* @side     client
* @category Chat
* @return   (string) Input text.
*
*/
  lua["chatInputGetText"] = []() {
    return Chat().GetInputText();
  };

/* luagmp (func)
*
* This function checks whether the chat input is open.
*
* @version  0.3.0
* @name     chatInputIsOpen
* @side     client
* @category Chat
* @return   (boolean) True when the input is open.
*
*/
  lua["chatInputIsOpen"] = []() {
    return Chat().IsInputActive();
  };

/* luagmp (func)
*
* This function opens the chat input.
*
* @version  0.3.0
* @name     chatInputOpen
* @side     client
* @category Chat
*
*/
  lua["chatInputOpen"] = []() {
    Chat().OpenInput();
  };

/* luagmp (func)
*
* This function submits the current chat input text.
*
* @version  0.3.0
* @name     chatInputSend
* @side     client
* @category Chat
*
*/
  lua["chatInputSend"] = []() {
    Chat().SubmitInput();
  };

/* luagmp (func)
*
* This function changes the chat input caret position.
*
* @version  0.3.0
* @name     chatInputSetCaretPosition
* @side     client
* @category Chat
* @param    (number) position Zero-based caret position.
*
*/
  lua["chatInputSetCaretPosition"] = [](int position) {
    Chat().SetInputCaretPosition(position);
  };

/* luagmp (func)
*
* This function changes the font assigned to the chat input.
*
* @version  0.3.0
* @name     chatInputSetFont
* @side     client
* @category Chat
* @param    (string) font Font name.
*
*/
  lua["chatInputSetFont"] = [](const std::string& font) {
    Chat().SetInputFont(font);
  };

/* luagmp (func)
*
* This function changes the chat input position in virtual screen units.
*
* @version  0.3.0
* @name     chatInputSetPosition
* @side     client
* @category Chat
* @param    (number) x X position.
* @param    (number) y Y position.
*
*/
  lua["chatInputSetPosition"] = [](int x, int y) {
    Chat().SetInputPosition(x, y);
  };

/* luagmp (func)
*
* This function changes the current chat input text.
*
* @version  0.3.0
* @name     chatInputSetText
* @side     client
* @category Chat
* @param    (string) text New input text.
*
*/
  lua["chatInputSetText"] = [](const std::string& text) {
    Chat().SetInputText(text);
  };
}

}  // namespace gmp::gothic
