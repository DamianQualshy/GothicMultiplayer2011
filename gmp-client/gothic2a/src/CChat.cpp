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
**                                                                          **
**  File name:      Interface/CChat.cpp                                     **
**                                                                          **
**  Created by:     29/06/11 - skejt23                                     **
**                                                                          **
**  Description:    Multiplayer chat input and message bridge                **
**                                                                          **
*****************************************************************************/

#pragma warning(disable : 4018)

#include "CChat.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "config.h"
#include "keyboard.h"
#include "language.h"
#include "net_game.h"
#include "patch.h"
#include "scripting/gothic_events.h"
#include "shared/event.h"

using namespace Gothic_II_Addon;

namespace {
constexpr auto CHAT_INPUT_OPEN_DELAY = std::chrono::milliseconds(30);
constexpr auto CHAT_CHARACTER_REPEAT = std::chrono::milliseconds(20);
constexpr auto CHAT_BACKSPACE_REPEAT = std::chrono::milliseconds(150);

std::size_t ClampCaretPosition(int position, std::size_t text_size) {
  if (position <= 0) {
    return 0;
  }

  return std::min(static_cast<std::size_t>(position), text_size);
}

std::optional<std::pair<std::string, std::string>> ParseCommand(std::string_view text) {
  if (text.empty() || text.front() != '/') {
    return std::nullopt;
  }

  text.remove_prefix(1);
  const auto command_start = text.find_first_not_of(' ');
  if (command_start == std::string_view::npos) {
    return std::nullopt;
  }

  text.remove_prefix(command_start);
  const auto space_pos = text.find(' ');
  std::string command(text.substr(0, space_pos));
  if (command.empty()) {
    return std::nullopt;
  }

  std::string params;
  if (space_pos != std::string_view::npos) {
    const auto params_start = text.find_first_not_of(' ', space_pos);
    if (params_start != std::string_view::npos) {
      params.assign(text.substr(params_start));
    }
  }

  return std::make_pair(std::move(command), std::move(params));
}

std::string FormatMessage(const char* format, va_list args) {
  if (!format) {
    return {};
  }

  char text[512] = {};
  const int written = std::vsnprintf(text, sizeof(text), format, args);
  if (written < 0) {
    return {};
  }

  return std::string(text, std::min<std::size_t>(static_cast<std::size_t>(written), sizeof(text) - 1));
}

void DispatchSystemMessage(const zCOLOR& color, const std::string& text) {
  EventManager::Instance().TriggerEvent(
      gmp::gothic::kEventOnPlayerMessageName,
      gmp::gothic::OnPlayerMessageEvent{std::nullopt,
                                        static_cast<std::uint8_t>(color.r),
                                        static_cast<std::uint8_t>(color.g),
                                        static_cast<std::uint8_t>(color.b),
                                        text});

  if (Config::Instance().logchat) {
    SPDLOG_INFO("{}", text);
  }
}
}  // namespace

extern zCOLOR Normal;

CChat::CChat() {
  next_character_time_ = std::chrono::steady_clock::now();
  next_backspace_time_ = std::chrono::steady_clock::now();
}

CChat::~CChat() = default;

bool CChat::IsInputActive() const {
  return input_active_;
}

void CChat::OpenInput() {
  if (input_active_) {
    return;
  }

  if (zinput) {
    zinput->ClearKeyBuffer();
  }
  input_active_ = true;
  caret_position_ = std::min(caret_position_, current_text_.size());
  const auto now = std::chrono::steady_clock::now();
  next_character_time_ = now + CHAT_INPUT_OPEN_DELAY;
  next_backspace_time_ = now;
  PrepareForInput();
}

void CChat::CloseInput(bool clear_text) {
  if (!input_active_) {
    if (clear_text) {
      ClearInput();
    }
    return;
  }

  if (clear_text) {
    ClearInput();
  }

  input_active_ = false;
  if (zinput) {
    zinput->ClearKeyBuffer();
  }
  ClearAfterInput();
}

void CChat::ClearInput() {
  current_text_.clear();
  caret_position_ = 0;
}

void CChat::SubmitInput() {
  if (!current_text_.empty()) {
    SendCurrentMessage();
  }

  CloseInput(true);
  if (!input_active_) {
    ClearInput();
  }
}

int CChat::GetInputCaretPosition() const {
  return static_cast<int>(caret_position_);
}

void CChat::SetInputCaretPosition(int position) {
  caret_position_ = ClampCaretPosition(position, current_text_.size());
}

const std::string& CChat::GetInputFont() const {
  return input_font_;
}

void CChat::SetInputFont(const std::string& font) {
  if (!font.empty()) {
    input_font_ = font;
  }
}

int CChat::GetInputX() const {
  return input_x_;
}

int CChat::GetInputY() const {
  if (input_position_custom_) {
    return input_y_;
  }

  return static_cast<int>(Config::Instance().ChatLines) * 200;
}

void CChat::SetInputPosition(int x, int y) {
  input_x_ = x;
  input_y_ = y;
  input_position_custom_ = true;
}

const std::string& CChat::GetInputText() const {
  return current_text_;
}

void CChat::SetInputText(const std::string& text) {
  current_text_ = text.substr(0, kMaxInputLength);
  caret_position_ = current_text_.size();
}

void CChat::SendCurrentMessage() {
  if (current_text_.empty()) {
    return;
  }

  const std::string message = current_text_;
  if (auto command = ParseCommand(message)) {
    EventManager::Instance().TriggerEvent(gmp::gothic::kEventOnCommandName,
                                          gmp::gothic::OnCommandEvent{command->first, command->second});
  }

  NetGame::Instance().SendMessage(message.c_str());
}

void CChat::InsertInputCharacter(char ch) {
  if (current_text_.size() >= kMaxInputLength) {
    return;
  }

  caret_position_ = std::min(caret_position_, current_text_.size());
  current_text_.insert(current_text_.begin() + static_cast<std::ptrdiff_t>(caret_position_), ch);
  ++caret_position_;
}

void CChat::DeleteInputCharacterBeforeCaret() {
  if (current_text_.empty() || caret_position_ == 0) {
    return;
  }

  caret_position_ = std::min(caret_position_, current_text_.size());
  current_text_.erase(current_text_.begin() + static_cast<std::ptrdiff_t>(caret_position_ - 1));
  --caret_position_;
}

void CChat::PrepareForInput() {
  camera_mode_change_enabled_ = zCAICamera::bCamChanges != 0;
  zCAICamera::bCamChanges = 0;

  if (player && player->inventory2.IsOpen()) {
    player->inventory2.Close();
  }

  Patch::PlayerInterfaceEnabled(false);
  oCNpc::SetNpcAIDisabled(1);
  KeepInputLocked();
}

void CChat::ClearAfterInput() {
  if (player && player->IsMovLock()) {
    player->SetMovLock(0);
  }

  oCNpc::SetNpcAIDisabled(0);
  Patch::PlayerInterfaceEnabled(true);
  zCAICamera::bCamChanges = camera_mode_change_enabled_ ? 1 : 0;
}

void CChat::KeepInputLocked() {
  if (!player) {
    return;
  }

  if (!player->IsMovLock()) {
    player->SetMovLock(1);
  }

  if (auto* anictrl = player->GetAnictrl()) {
    anictrl->StopTurnAnis();
  }
}

void CChat::HandleInput(bool allow_open) {
  (void)allow_open;

  if (!input_active_) {
    return;
  }

  KeepInputLocked();

  if (zinput && zinput->KeyPressed(KEY_RETURN)) {
    SubmitInput();
    return;
  }

  if (zinput && zinput->KeyPressed(KEY_ESCAPE)) {
    CloseInput(true);
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (now < next_character_time_) {
    return;
  }

  unsigned char key = static_cast<unsigned char>(GInput::GetCharacterFormKeyboard());
  const bool ctrl_pressed = zinput && (zinput->KeyPressed(KEY_LCONTROL) || zinput->KeyPressed(KEY_RCONTROL));

  if (key == 0) {
    if (zinput && zinput->KeyPressed(KEY_BACKSPACE) && now >= next_backspace_time_) {
      DeleteInputCharacterBeforeCaret();
      next_backspace_time_ = now + CHAT_BACKSPACE_REPEAT;
    }
    if (zinput && zinput->KeyToggled(KEY_LEFTARROW) && caret_position_ > 0) {
      --caret_position_;
    }
    if (zinput && zinput->KeyToggled(KEY_RIGHTARROW) && caret_position_ < current_text_.size()) {
      ++caret_position_;
    }
    return;
  }

  if (ctrl_pressed && key != 0x08) {
    return;
  }

  if (key == 0x08) {
    DeleteInputCharacterBeforeCaret();
    next_backspace_time_ = now + CHAT_BACKSPACE_REPEAT;
  } else if ((key >= 0x20) || ((key & 0x80) && (Language::Instance().GetEncoding() != localization::LanguageEncoding::kNone))) {
    InsertInputCharacter(static_cast<char>(key));
  }
  next_character_time_ = now + CHAT_CHARACTER_REPEAT;
}

void CChat::WriteMessage(MsgType type, bool PrintTimed, const zCOLOR& rgb, const char* format, ...) {
  (void)type;
  (void)PrintTimed;

  va_list args;
  va_start(args, format);
  const std::string text = FormatMessage(format, args);
  va_end(args);

  DispatchSystemMessage(rgb, text);
}

void CChat::WriteMessage(MsgType type, bool PrintTimed, const char* format, ...) {
  (void)type;
  (void)PrintTimed;

  va_list args;
  va_start(args, format);
  const std::string text = FormatMessage(format, args);
  va_end(args);

  DispatchSystemMessage(Normal, text);
}

void CChat::ClearChat() {
}

void CChat::PrintChat() {
}
