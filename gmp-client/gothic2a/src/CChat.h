
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
#include <cstddef>
#include <string>
#include "singleton.h"
#include "ZenGin/zGothicAPI.h"

enum MsgType { NORMAL };

class CChat : public TSingleton<CChat> {
public:
  CChat();
  ~CChat();
  bool IsInputActive() const;
  void HandleInput(bool allow_open);
  void OpenInput();
  void CloseInput(bool clear_text = false);
  void ClearInput();
  void SubmitInput();
  int GetInputCaretPosition() const;
  void SetInputCaretPosition(int position);
  const std::string& GetInputFont() const;
  void SetInputFont(const std::string& font);
  int GetInputX() const;
  int GetInputY() const;
  void SetInputPosition(int x, int y);
  const std::string& GetInputText() const;
  void SetInputText(const std::string& text);

  void ClearChat();
  void WriteMessage(MsgType type, bool PrintTimed, const Gothic_II_Addon::zCOLOR& rgb, const char* format, ...);
  void WriteMessage(MsgType type, bool PrintTimed, const char* format, ...);
  void PrintChat();

  static constexpr size_t kMaxInputLength = 84;

private:
  void SendCurrentMessage();
  void InsertInputCharacter(char ch);
  void DeleteInputCharacterBeforeCaret();
  void PrepareForInput();
  void ClearAfterInput();
  void KeepInputLocked();

  bool input_active_ = false;
  bool camera_mode_change_enabled_ = true;
  std::string current_text_;
  std::string input_font_ = "FONT_DEFAULT.TGA";
  int input_x_ = 0;
  int input_y_ = 0;
  bool input_position_custom_ = false;
  std::size_t caret_position_ = 0;
  std::chrono::steady_clock::time_point next_character_time_;
  std::chrono::steady_clock::time_point next_backspace_time_;
};
