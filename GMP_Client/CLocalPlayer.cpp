
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
**	File name:		CGmpClient/CLocalPlayer.cpp		   						**
**																			**
**	Created by:		23/12/11	-	skejt23									**
**																			**
**	Description:	Local Player class	 									**
**																			**
*****************************************************************************/
#include "CLocalPlayer.h"
#include "CObservation.h"
#include "game_client.h"
#include "CIngame.h"

CLocalPlayer* LocalPlayer = NULL;

// Externs
extern GameClient* client;
extern CIngame* global_ingame;

CLocalPlayer::CLocalPlayer()
{
	if(LocalPlayer){
		delete this;
		return;
	}
	this->IsObserving = false;
	this->Observe = NULL;
	LocalPlayer = this;
};

CLocalPlayer::~CLocalPlayer()
{
	LocalPlayer = NULL;
	this->IsObserving = false;
	this->Observe = NULL;
};

void CLocalPlayer::EnterObserveMode()
{
	if(client->ObserveMode == CObservation::NO_OBSERVATION) return;
	if(LocalPlayer->IsInObserveMode()) return;
	if(client->player.size() < 2) return;
	LocalPlayer->Observe = new CObservation();
	LocalPlayer->IsObserving = true;
};

bool CLocalPlayer::IsInObserveMode()
{
	if(IsObserving == true && Observe) return true;
	return false;
};

void CLocalPlayer::LeaveObserveMode()
{
	LocalPlayer->IsObserving = false;
	delete LocalPlayer->Observe;
	LocalPlayer->Observe = NULL;
};