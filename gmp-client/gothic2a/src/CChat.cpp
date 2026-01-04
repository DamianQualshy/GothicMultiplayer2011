
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
** ** *	File name:		Interface/CChat.cpp		   								** *
*** *	Created by:		29/06/11	-	skejt23									** *
*** *	Description:	Multiplayer chat functionallity	 						** *
***
*****************************************************************************/

#pragma warning(disable : 4018)

#include "CChat.h"

#include <spdlog/spdlog.h>

#include <algorithm>

#include "keyboard.h"
#include "language.h"
#include "net_game.h"
#include "patch.h"
#include "random_utils.h"
#include "config.h"

using namespace Gothic_II_Addon;
namespace {
constexpr auto CHAT_FADE_DURATION = std::chrono::milliseconds(400);
constexpr auto CHAT_CARET_BLINK = std::chrono::milliseconds(750);
constexpr auto CHAT_BACKSPACE_REPEAT = std::chrono::milliseconds(150);
constexpr int CHAT_INPUT_X = 0;
constexpr int CHAT_INPUT_TEXT_X = 200;
constexpr const char* CHAT_INPUT_ARROW = "->";

void UpdateMessageAlpha(MsgStruct& message, const std::chrono::steady_clock::time_point& now) {
  const unsigned char target_alpha = message.MsgColor.alpha;
  if (!message.IsFadingIn) {
    message.CurrentAlpha = target_alpha;
    return;
  }

  if (target_alpha == 0) {
    message.CurrentAlpha = 0;
    message.IsFadingIn = false;
    return;
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - message.FadeStart);
  if (elapsed.count() >= CHAT_FADE_DURATION.count()) {
    message.CurrentAlpha = target_alpha;
    message.IsFadingIn = false;
    return;
  }

  const float progress = static_cast<float>(elapsed.count()) / static_cast<float>(CHAT_FADE_DURATION.count());
  const float alpha_value = std::min(progress, 1.0f) * static_cast<float>(target_alpha);
  message.CurrentAlpha = static_cast<unsigned char>(alpha_value);
}
}  // namespace

extern zCOLOR Normal;

CChat::CChat() {
  tmpanimname = "NULL";
  caret_toggle_time_ = std::chrono::steady_clock::now() + CHAT_CARET_BLINK;
  next_backspace_time_ = std::chrono::steady_clock::now();
};

CChat::~CChat() {
  ChatMessages.clear();
};

bool CChat::IsInputActive() const {
  return input_active_;
}

void CChat::OpenInput() {
  if (input_active_)
    return;

  zinput->ClearKeyBuffer();
  input_active_ = true;
  caret_visible_ = true;
  caret_toggle_time_ = std::chrono::steady_clock::now() + CHAT_CARET_BLINK;
  next_backspace_time_ = std::chrono::steady_clock::now();
  PrepareForInput();
}

void CChat::CloseInput(bool clear_text) {
  if (!input_active_)
    return;

  if (clear_text)
    current_text_.clear();

  input_active_ = false;
  zinput->ClearKeyBuffer();
  ClearAfterInput();
}

void CChat::PrepareForInput() {
  player->GetAnictrl()->StopTurnAnis();
  Patch::PlayerInterfaceEnabled(false);
}

void CChat::ClearAfterInput() {
  if (player->IsMovLock())
    player->SetMovLock(0);
  Patch::PlayerInterfaceEnabled(true);
}

void CChat::SendCurrentMessage() {
  if (current_text_.empty())
    return;

  NetGame::Instance().SendMessage(current_text_.c_str());
}

void CChat::UpdateCaretBlink(const std::chrono::steady_clock::time_point& now) {
  if (now < caret_toggle_time_)
    return;

  caret_visible_ = !caret_visible_;
  caret_toggle_time_ = now + CHAT_CARET_BLINK;
}

void CChat::HandleInput(bool allow_open) {
  if (!input_active_ && allow_open && zinput->KeyToggled(KEY_T))
    OpenInput();

  if (!input_active_)
    return;

  if (!player->IsMovLock())
    player->SetMovLock(1);

  const int random_anim = gmp::client::random::Int(1, 10);
  StartChatAnimation(random_anim);

  if (zinput->KeyToggled(KEY_ESCAPE)) {
    CloseInput(true);
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  unsigned char key = static_cast<unsigned char>(GInput::GetCharacterFormKeyboard());

  if (key == 0) {
    if (zinput->KeyPressed(KEY_BACKSPACE) && now >= next_backspace_time_) {
      if (!current_text_.empty())
        current_text_.pop_back();
      next_backspace_time_ = now + CHAT_BACKSPACE_REPEAT;
    }
    UpdateCaretBlink(now);
    return;
  }

  if (key == 0x0D) {
    SendCurrentMessage();
    CloseInput(true);
    return;
  }

  if (key == 0x08) {
    if (!current_text_.empty())
      current_text_.pop_back();
    next_backspace_time_ = now + CHAT_BACKSPACE_REPEAT;
  } else if ((key >= 0x20) || ((key & 0x80) && (Language::Instance().GetEncoding() != localization::LanguageEncoding::kNone))) {
    if (current_text_.size() < kMaxInputLength)
      current_text_.push_back(static_cast<char>(key));
  }

  UpdateCaretBlink(now);
}

void CChat::StartChatAnimation(int anim) {
  if (player->IsDead() || player->GetAnictrl()->IsRunning() || player->GetAnictrl()->IsInWater() || player->GetAnictrl()->IsFallen()) {
    return;
  }
  if (!player->GetModel()->IsAnimationActive(tmpanimname)) {
    tmpanimname = fmt::format("T_DIALOGGESTURE_{:02}", anim).c_str();
    player->GetModel()->StartAnimation(tmpanimname);
  }
}

void CChat::WriteMessage(MsgType type, bool PrintTimed, const zCOLOR& rgb, const char* format, ...) {
  (void)type;
  if (strlen(format) > 512)
    return;
  char text[512];
  va_list args;
  va_start(args, format);
  vsprintf(text, format, args);
  va_end(args);
  MsgStruct msg;
  msg.Message = text;
  msg.MsgColor = rgb;
  msg.FadeStart = std::chrono::steady_clock::now();
  msg.CurrentAlpha = 0;
  msg.IsFadingIn = true;
  if (PrintTimed) {
    const auto font = Language::Instance().ApplyFontPrefix("FONT_DEFAULT.TGA");
    ogame->array_view[oCGame::GAME_VIEW_SCREEN]->SetFont(font.c_str());
    tmp = text;
    ogame->array_view[oCGame::GAME_VIEW_SCREEN]->PrintTimed(3700, 2800, tmp, 3000.0f, 0);
  }
  ChatMessages.push_back(msg);
  if (Config::Instance().logchat) {
    SPDLOG_INFO("{}", text);
  }
};

void CChat::WriteMessage(MsgType type, bool PrintTimed, const char* format, ...) {
  (void)type;
  if (strlen(format) > 512)
    return;
  char text[512];
  va_list args;
  va_start(args, format);
  vsprintf(text, format, args);
  va_end(args);
  MsgStruct msg;
  msg.Message = text;
  msg.MsgColor = Normal;
  msg.FadeStart = std::chrono::steady_clock::now();
  msg.CurrentAlpha = 0;
  msg.IsFadingIn = true;
  if (PrintTimed) {
    const auto font = Language::Instance().ApplyFontPrefix("FONT_DEFAULT.TGA");
    ogame->array_view[oCGame::GAME_VIEW_SCREEN]->SetFont(font.c_str());
    tmp = text;
    ogame->array_view[oCGame::GAME_VIEW_SCREEN]->PrintTimed(3700, 2800, tmp, 3000.0f, 0);
  }
  ChatMessages.push_back(msg);
  if (Config::Instance().logchat) {
    SPDLOG_INFO("{}", text);
  }
};

void CChat::ClearChat() {
  ChatMessages.clear();
};

void CChat::PrintChat() {
  const auto font = Language::Instance().ApplyFontPrefix("FONT_DEFAULT.TGA");
  screen->SetFont(font.c_str());
  const auto now = std::chrono::steady_clock::now();
  if (ChatMessages.size() > Config::Instance().ChatLines)
    ChatMessages.erase(ChatMessages.begin());
  if (!ChatMessages.empty())
    for (size_t v = 0; v < ChatMessages.size(); v++) {
      UpdateMessageAlpha(ChatMessages[v], now);
      zCOLOR color = ChatMessages[v].MsgColor;
      color.alpha = ChatMessages[v].CurrentAlpha;
      screen->SetFontColor(color);
      screen->Print(0, static_cast<int>(v) * 200, ChatMessages[v].Message);
      screen->SetFontColor(Normal);
    }

  if (input_active_) {
    UpdateCaretBlink(now);
    screen->SetFontColor(Normal);
    const auto input_y = static_cast<int>(Config::Instance().ChatLines) * 200;
    screen->Print(CHAT_INPUT_X, input_y, CHAT_INPUT_ARROW);

    if (caret_visible_) {
      std::string input_with_cursor = current_text_;
      input_with_cursor += "_";
      screen->Print(CHAT_INPUT_TEXT_X, input_y, input_with_cursor.c_str());
    } else {
      screen->Print(CHAT_INPUT_TEXT_X, input_y, current_text_.c_str());
    }
  }
};