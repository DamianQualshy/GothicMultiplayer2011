
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

#include "CSelectClass.h"

#include <cstdlib>

#include "CLocalPlayer.h"
#include "Interface.h"
#include "ZenGin/zGothicAPI.h"
#include "patch.h"

extern float fWRatio;
CSelectClass *selectmgr = NULL;
extern CLocalPlayer *LocalPlayer;

CSelectClass::CSelectClass(CLanguage *ptr, GameClient *cl) {
  // INITIALIZING LOCAL PLAYER HERE INSTEAD IN JOINGAME
  if (!LocalPlayer) {
    new CLocalPlayer();
    LocalPlayer->SetNpc(player);
  }
  this->lang = ptr;
  this->client = cl;
  selectmgr = this;
  client->classmgr->EquipNPC(0, LocalPlayer, true);
  CameraPrepared = false;
  Patch::SetLookingOnNpcCamera(true);
  Patch::DropVobEnabled(false);
  this->selected = 0;
  ChangeSpawnPointByClass();
}

CSelectClass::~CSelectClass() {
  this->lang = NULL;
  this->client = NULL;
  Patch::SetLookingOnNpcCamera(false);
  Patch::DropVobEnabled(true);
  selectmgr = NULL;
}

size_t CSelectClass::GetSelected() {
  return this->selected;
}

void CSelectClass::Loop() {
  if (selectmgr) {
    screen->SetFont(zfontman->GetFont(0));
    player->SetMovLock(1);
    screen->Print(0, 0, (*selectmgr->lang)[CLanguage::SELECT_CONTROLS]);
    screen->Print(0, 150, (*selectmgr->lang)[CLanguage::CLASS_NAME]);
    screen->Print(0, 300, (*selectmgr->lang)[CLanguage::CLASS_DESCRIPTION]);
    CHeroClass *chc = selectmgr->client->classmgr;
    screen->Print(120 + static_cast<zINT>(static_cast<float>(60 * (*selectmgr->lang)[CLanguage::CLASS_NAME].Length()) * fWRatio), 150,
                  (*chc)[selectmgr->GetSelected()]->class_name);
    screen->Print(120 + static_cast<zINT>(static_cast<float>(60 * (*selectmgr->lang)[CLanguage::CLASS_DESCRIPTION].Length()) * fWRatio), 300,
                  (*chc)[selectmgr->GetSelected()]->class_description);
    selectmgr->HandleInput();  // wyrzucilem na koniec bo za szybko robil delete this;
  }
}

void CSelectClass::CleanUpBeforeNext() {
  player->SetWeaponMode(NPC_WEAPON_NONE);
  if (player->GetRightHand()) {
    zCVob *Ptr = player->GetRightHand();
    zCVob *PtrLeft = player->GetLeftHand();
    player->DropAllInHand();
    if (PtrLeft)
      PtrLeft->RemoveVobFromWorld();
    Ptr->RemoveVobFromWorld();
  }
};

void CSelectClass::ChangeSpawnPointByClass() {
  if (!client->spawnpoint || !client->spawnpoint->GetSize())
    return;

  zVEC3 Pos = *(*client->spawnpoint)[rand() % client->spawnpoint->GetSize()];
  player->trafoObjToWorld.SetTranslation(zVEC3(Pos[VX], Pos[VY], Pos[VZ]));
};

void CSelectClass::HandleInput() {
  if (!CameraPrepared) {
    zCAICamera::GetCurrent()->bestRotY = 180.0f;
    CameraPrepared = true;
  }
  if (zinput->KeyToggled(KEY_ESCAPE)) {
    client->JoinGame(selected);
    ExitToBigMainMenu();
    delete this;
    return;
  }
  if ((this->selected > 0) && (zinput->KeyToggled(KEY_A))) {
    CleanUpBeforeNext();
    client->classmgr->EquipNPC(--this->selected, LocalPlayer, true);
    ChangeSpawnPointByClass();
  }
  if ((this->selected < client->classmgr->GetSize() - 1) && (zinput->KeyToggled(KEY_D))) {
    CleanUpBeforeNext();
    client->classmgr->EquipNPC(++this->selected, LocalPlayer, true);
    ChangeSpawnPointByClass();
  }
  if (zinput->KeyPressed(KEY_RETURN)) {
    zinput->ClearKeyBuffer();
    auto pos = player->trafoObjToWorld.GetTranslation();
    player->ResetPos(pos);
    client->JoinGame(this->selected);
    // dodac przejscie do zarzadzania gameplayem
    delete this;
  }
}
