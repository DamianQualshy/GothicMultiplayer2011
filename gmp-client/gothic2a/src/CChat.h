
/*
MIT License

Copyright (c) 2022 Gothic Multiplayer Team (pampi, skejt23, mecio)

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

/*****************************************************************************
**																			**
**	File name:		Interface/CChat.h		   								**
**																			**
**	Created by:		29/06/11	-	skejt23									**
**																			**
**	Description:	Multiplayer chat functionallity	 						**
**																			**
*****************************************************************************/

#pragma once

#include <chrono>
#include <string>
#include <vector>
#include "singleton.h"
#include "ZenGin/zGothicAPI.h"

struct MsgStruct {
  Gothic_II_Addon::zSTRING Message;
  Gothic_II_Addon::zCOLOR MsgColor;
  std::chrono::steady_clock::time_point FadeStart;
  unsigned char CurrentAlpha = 0;
  bool IsFadingIn = false;
};

enum MsgType { NORMAL };

class CChat : public TSingleton<CChat> {
public:
  CChat();
  ~CChat();
  bool IsInputActive() const;
  void HandleInput(bool allow_open);

  void ClearChat();
  void StartChatAnimation(int anim);
  void WriteMessage(MsgType type, bool PrintTimed, const Gothic_II_Addon::zCOLOR& rgb, const char* format, ...);
  void WriteMessage(MsgType type, bool PrintTimed, const char* format, ...);
  void PrintChat();

  static constexpr size_t kMaxInputLength = 84;

  std::vector<MsgStruct> ChatMessages;
  Gothic_II_Addon::zSTRING tmp;
  Gothic_II_Addon::zSTRING tmpanimname;

private:
  void OpenInput();
  void CloseInput(bool clear_text);
  void PrepareForInput();
  void ClearAfterInput();
  void SendCurrentMessage();
  void UpdateCaretBlink(const std::chrono::steady_clock::time_point& now);

  bool input_active_ = false;
  bool caret_visible_ = true;
  std::string current_text_;
  std::chrono::steady_clock::time_point caret_toggle_time_;
  std::chrono::steady_clock::time_point next_backspace_time_;
};