
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

#include "CPlayerList.h"

#include <algorithm>
#include <vector>

#include "CIngame.h"
#include "net_game.h"

extern zCOLOR Normal;
extern CIngame* global_ingame;

CPlayerList::CPlayerList() {
  PlayerListBackground = new zCView(0, 0, 8192, 8192, VIEW_ITEM);
  PlayerListBackground->SetPos(2500, 600);
  x = 2700, y = 800;
  Opened = false;
  BeginIndex = 0;
  PlayerListBackground->SetSize(3500, 6000);
  PlayerListBackground->InsertBack(zSTRING("MENU_INGAME.TGA"));
};

CPlayerList::~CPlayerList() {
  delete PlayerListBackground;
};

bool CPlayerList::IsPlayerListOpen() {
  return Opened;
};

bool CPlayerList::OpenPlayerList() {
  if (!Opened) {
    screen->InsertItem(PlayerListBackground);
    player->GetAnictrl()->StopTurnAnis();
    Opened = true;
    BeginIndex = 0;
    return true;
  }
  return false;
};

bool CPlayerList::ClosePlayerList() {
  if (Opened) {
    screen->RemoveItem(PlayerListBackground);
    player->SetMovLock(0);
    Opened = false;
    return true;
  }
  return false;
};

void CPlayerList::UpdatePlayerList() {
  if (!player->IsMovLock())
    player->SetMovLock(1);
  std::vector<Gothic2APlayer*> connected_players;
  connected_players.reserve(NetGame::Instance().players.size());
  for (auto* net_player : NetGame::Instance().players) {
    if (net_player && net_player->npc) {
      connected_players.push_back(net_player);
    }
  };
  std::stable_sort(connected_players.begin(), connected_players.end(),
                   [](Gothic2APlayer* left, Gothic2APlayer* right) {
                     return left->base_player().id() < right->base_player().id();
                   });
  const int connected_count = static_cast<int>(connected_players.size());
  const int max_visible_rows = 30;
  int max_scroll_idx = connected_count - max_visible_rows;
  if (max_scroll_idx < 0)
    max_scroll_idx = 0;
  if (BeginIndex > max_scroll_idx)
    BeginIndex = max_scroll_idx;
  if (BeginIndex < 0)
    BeginIndex = 0;
  if (connected_count > max_visible_rows) {
    if (zinput->KeyToggled(KEY_UP)) {
      BeginIndex = std::max(0, BeginIndex - 1);
    }
    if (zinput->KeyToggled(KEY_DOWN)) {
      BeginIndex = std::min(max_scroll_idx, BeginIndex + 1);
    }
    if (zinput->KeyToggled(KEY_PRIOR)) {
      BeginIndex = 0;
    }
    if (zinput->KeyToggled(KEY_NEXT)) {
      BeginIndex = max_scroll_idx;
    }
  }
  // PRINT
  zCView* Screen = screen;
  zSTRING old_font = Screen->GetFontName();
  Screen->SetFont(zSTRING("Font_Old_10_White_Hi.TGA"));
  Screen->SetFontColor(zCOLOR(243, 8, 188, 255));
  Screen->Print(x, y, "ID");
  Screen->Print(x + 300, y, "Players");
  Screen->Print(x + 2800, y, "Ping");
  char buffer[128];
  zSTRING NoOfPlayers;
  int row_y = y;
  if (connected_count > 0) {
    const int max_rows = std::min(connected_count - BeginIndex, max_visible_rows);
    for (int row = 0; row < max_rows; row++) {
      const int i = BeginIndex + row;
      row_y += 200;
      const zCOLOR& name_color = connected_players[i]->GetNameColor();
      Screen->SetFontColor(name_color);
      ZeroMemory(buffer, 128);
      std::string display_name = connected_players[i]->GetName();
      if (display_name.length() > 20)
        display_name.resize(20);
      if (connected_players[i]->IsLocalPlayer()) {
        Screen->Print(x - 250, row_y, "->");
      }
      sprintf(buffer, "%llu", static_cast<unsigned long long>(connected_players[i]->base_player().id()));
      NoOfPlayers = buffer;
      Screen->Print(x, row_y, NoOfPlayers);
      ZeroMemory(buffer, 128);
      sprintf(buffer, "%s", display_name.c_str());
      NoOfPlayers = buffer;
      Screen->Print(x + 300, row_y, NoOfPlayers);
      std::string ping_text = "-";
      if (connected_players[i]->IsLocalPlayer() && NetGame::Instance().game_client) {
        ping_text = std::to_string(NetGame::Instance().game_client->GetPing());
      }
      ZeroMemory(buffer, 128);
      sprintf(buffer, "%s", ping_text.c_str());
      NoOfPlayers = buffer;
      Screen->Print(x + 2800, row_y, NoOfPlayers);
    }
  } else {
    Screen->SetFontColor(zCOLOR(255, 250, 200, 255));
    Screen->Print(x, y + 200, Language::Instance()[Language::NOPLAYERS]);
  }
  Screen->SetFontColor(Normal);
  Screen->SetFont(old_font);
};