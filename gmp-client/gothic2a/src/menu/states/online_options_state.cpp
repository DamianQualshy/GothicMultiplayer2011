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

#include "online_options_state.hpp"

#include <spdlog/spdlog.h>

#include <string>

#include "keyboard.h"
#include "language.h"
#include "menu/states/main_menu_loop_state.hpp"
#include "StandardFonts.h"

namespace {
const G2W::Font kFontRed20(255, 0, 0, "FONT_OLD_20_WHITE.TGA");
constexpr int kOptionsTableX = 2200;
constexpr int kOptionsTableY = 3000;
constexpr int kOptionsTableWidth = 3800;
constexpr int kOptionsTableHeight = 3600;
constexpr int kOptionsTableInterline = 700;
constexpr int kOptionsTableVisibleRows = 12;
constexpr int kOptionsTablePaddingWidth = 400;
constexpr int kOptionsTableLabelWidth = 4000;
constexpr int kOptionsTableValueWidth = 3200;
constexpr int kBackButtonX = 3000;
constexpr int kBackButtonY = 7000;
constexpr int kBackButtonWidth = 2200;
constexpr int kBackButtonHeight = 400;
constexpr char kOptionsBackgroundTexture[] = "MENU_INGAME.TGA";
constexpr char kOptionsHighlightTexture[] = "MENU_INGAME.TGA";
}  // namespace

namespace menu {
namespace states {

OnlineOptionsState::OnlineOptionsState(MenuContext& context)
    : context_(context),
      selectedOption_(OptionItem::NICKNAME),
      shouldReturnToMainMenu_(false),
      optionsTable_(nullptr),
      backButton_(nullptr) {
}

OnlineOptionsState::~OnlineOptionsState() {
  delete optionsTable_;
  delete backButton_;
}

void OnlineOptionsState::OnEnter() {
  SPDLOG_INFO("Entering online options state");
  InitializeControls();
}

void OnlineOptionsState::OnExit() {
  SPDLOG_INFO("Exiting online options state");
}

StateResult OnlineOptionsState::Update() {
  context_.sceneManager.Update();
  RenderOptionsMenu();
  HandleInput();
  return StateResult::Continue;
}

MenuState* OnlineOptionsState::CheckTransition() {
  if (shouldReturnToMainMenu_) {
    return new MainMenuLoopState(context_);
  }
  return nullptr;
}

void OnlineOptionsState::InitializeControls() {
  if (!optionsTable_) {
    optionsTable_ =
        new G2W::Table(kOptionsTableX, kOptionsTableY, kOptionsTableWidth, kOptionsTableHeight, kOptionsTableInterline, kOptionsTableVisibleRows);
    optionsTable_->addColumn("", kOptionsTablePaddingWidth);
    optionsTable_->addColumn("", kOptionsTableLabelWidth);
    optionsTable_->addColumn("", kOptionsTableValueWidth);
    optionsTable_->setBackground(kOptionsBackgroundTexture);
    optionsTable_->setFont(FNT_WHITE_20);
    optionsTable_->setHighlightFont(FNT_GREEN_20);
  }

  if (!backButton_) {
    backButton_ = new G2W::Button(kBackButtonX, kBackButtonY, kBackButtonWidth, kBackButtonHeight);
    backButton_->setTexture(kOptionsBackgroundTexture);
    backButton_->setHighlightTexture(kOptionsHighlightTexture);
    backButton_->setFont(FNT_WHITE_20);
    backButton_->setHighlightFont(FNT_GREEN_20);
  }
}

void OnlineOptionsState::RenderOptionsMenu() {
  InitializeControls();
  optionsTable_->clear();
  optionsTable_->setHighlightFont(context_.writingNickname ? kFontRed20 : FNT_GREEN_20);

  auto addRow = [&](OptionItem option, std::string label, std::string value = {}) {
    optionsTable_->addRow(G2W::TableRow{{"", std::move(label), std::move(value)}, option == selectedOption_});
  };
  addRow(OptionItem::NICKNAME, Language::Instance()[Language::MMENU_NICKNAME].ToChar(), context_.config.Nickname.ToChar());

  std::string languageValue;
  const auto& languages = LanguageManager::Instance().GetAvailableLanguages();
  if (context_.config.lang >= 0 && context_.config.lang < static_cast<int>(languages.size())) {
    languageValue = languages[context_.config.lang].displayName;
  } else {
    languageValue = Language::Instance()[Language::LANGUAGE].ToChar();
  }
  addRow(OptionItem::LANGUAGE, Language::Instance()[Language::MMENU_LANGUAGE].ToChar(), languageValue);

  addRow(OptionItem::ANTIALIASING,
         Language::Instance()[Language::MMENU_ANTIALIASING].ToChar(),
         (zoptions->ReadInt("ENGINE", "zVidEnableAntiAliasing", 0) == 1) ? Language::Instance()[Language::MMENU_YES].ToChar()
                                                                         : Language::Instance()[Language::MMENU_NO].ToChar());
  addRow(OptionItem::JOYSTICK,
         Language::Instance()[Language::MMENU_JOYSTICK].ToChar(),
         (zoptions->ReadBool(zOPT_SEC_GAME, "joystick", 0) == 1) ? Language::Instance()[Language::MMENU_YES].ToChar()
                                                                 : Language::Instance()[Language::MMENU_NO].ToChar());
  addRow(OptionItem::INTRO_VIDEOS,
         Language::Instance()[Language::MMENU_INTROVIDEOS].ToChar(),
         (zoptions->ReadBool(zOPT_SEC_GAME, "playLogoVideos", 1) == 1) ? Language::Instance()[Language::MMENU_YES].ToChar()
                                                                       : Language::Instance()[Language::MMENU_NO].ToChar());

  optionsTable_->render();

  backButton_->setText(Language::Instance()[Language::MMENU_BACK].ToChar());
  backButton_->highlight = (selectedOption_ == OptionItem::BACK);
  backButton_->render();
}

void OnlineOptionsState::HandleInput() {
  if (context_.writingNickname) {
    char x[2] = {0, 0};
    x[0] = GInput::GetCharacterFormKeyboard();

    // Backspace
    if ((x[0] == 8) && (context_.config.Nickname.Length() > 0)) {
      context_.config.Nickname.DeleteRight(1);
    }

    // Add character (printable ASCII)
    if ((x[0] >= 0x20) && (context_.config.Nickname.Length() < 24)) {
      context_.config.Nickname += x;
    }

    // Enter - confirm
    if ((x[0] == 0x0D) && (!context_.config.Nickname.IsEmpty())) {
      context_.config.SaveConfigToFile();
      context_.writingNickname = false;
    }
    return;
  }

  // Navigation
  if (context_.input->KeyToggled(KEY_UP)) {
    if (selectedOption_ == OptionItem::NICKNAME) {
      selectedOption_ = OptionItem::BACK;
    } else {
      selectedOption_ = static_cast<OptionItem>(static_cast<int>(selectedOption_) - 1);
    }
  }

  if (context_.input->KeyToggled(KEY_DOWN)) {
    if (selectedOption_ == OptionItem::BACK) {
      selectedOption_ = OptionItem::NICKNAME;
    } else {
      selectedOption_ = static_cast<OptionItem>(static_cast<int>(selectedOption_) + 1);
    }
  }

  // Left/Right for adjustable options
  if (context_.input->KeyToggled(KEY_LEFT)) {
    AdjustOption(selectedOption_, -1);
  }

  if (context_.input->KeyToggled(KEY_RIGHT)) {
    AdjustOption(selectedOption_, 1);
  }

  // Execute option on Enter
  if (context_.input->KeyPressed(KEY_RETURN)) {
    context_.input->ClearKeyBuffer();
    ExecuteOption(selectedOption_);
  }
}

void OnlineOptionsState::ExecuteOption(OptionItem option) {
  switch (option) {
    case OptionItem::NICKNAME:
      context_.writingNickname = true;
      break;

    case OptionItem::ANTIALIASING: {
      int current = zoptions->ReadInt("ENGINE", "zVidEnableAntiAliasing", 0);
      zoptions->WriteInt("ENGINE", "zVidEnableAntiAliasing", (current == 0) ? 1 : 0);
      gameMan->ApplySomeSettings();
      break;
    }

    case OptionItem::JOYSTICK: {
      int current = zoptions->ReadBool(zOPT_SEC_GAME, "enableJoystick", 0);
      zoptions->WriteBool(zOPT_SEC_GAME, "enableJoystick", !current);
      gameMan->ApplySomeSettings();
      break;
    }

    case OptionItem::INTRO_VIDEOS: {
      int current = zoptions->ReadBool(zOPT_SEC_GAME, "playLogoVideos", 1);
      zoptions->WriteBool(zOPT_SEC_GAME, "playLogoVideos", !current);
      gameMan->ApplySomeSettings();
      break;
    }

    case OptionItem::BACK:
      shouldReturnToMainMenu_ = true;
      break;

    default:
      // Other options handled by left/right
      break;
  }
}

void OnlineOptionsState::AdjustOption(OptionItem option, int direction) {
  switch (option) {
    case OptionItem::LANGUAGE: {
      const auto& languages = LanguageManager::Instance().GetAvailableLanguages();
      if (languages.empty())
        return;

      int newIndex = context_.config.lang + direction;
      if (newIndex < 0)
        newIndex = static_cast<int>(languages.size()) - 1;
      if (newIndex >= static_cast<int>(languages.size()))
        newIndex = 0;

      if (newIndex == context_.config.lang)
        return;

      // Load new language
      LanguageManager::Instance().LoadLanguages(LanguageManager::Instance().GetLanguageDir().c_str(), newIndex);

      context_.config.lang = newIndex;
      context_.config.SaveConfigToFile();

      // Rebuild server list UI so translated labels refresh immediately
      delete context_.extendedServerList;
      context_.extendedServerList = new ExtendedServerList(context_.serverList);
      context_.extendedServerList->RefreshList();
      break;
    }

    default:
      // Other options don't support left/right
      break;
  }
}

}  // namespace states
}  // namespace menu
