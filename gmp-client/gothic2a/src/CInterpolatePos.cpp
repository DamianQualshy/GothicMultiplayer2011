
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
** ** *	File name:		CGmpClient/CInterpolatePos.cpp
*** *	Created by:		17/12/11	-	skejt23
*** *	Description:	Position interpolation	(at least pretends to be :-))
***
*****************************************************************************/
#include "CInterpolatePos.h"

#include <algorithm>
#include <cmath>

#include "ZenGin/zGothicAPI.h"
#include "CIngame.h"

// Externs
extern CIngame* global_ingame;

CInterpolatePos::CInterpolatePos(CPlayer* Player) {
  InterpolatingPlayer = Player;
  IsInterpolating = false;
  m_vecPosition = zVEC3();
  m_LastDiff = zVEC3();
  m_uDeltaTime = 0;
  m_fVelocity = 0.0f;
  m_uAnimation = -1;
  m_bPosUpdate = false;
  if (global_ingame)
    global_ingame->Interpolation.push_back(this);
};

CInterpolatePos::~CInterpolatePos() {
  ResetInternal();
  InterpolatingPlayer = NULL;
  if (!global_ingame)
    return;
  for (int i = 0; i < static_cast<int>(global_ingame->Interpolation.size()); i++) {
    if (global_ingame->Interpolation[i] == this) {
      global_ingame->Interpolation.erase(global_ingame->Interpolation.begin() + i);
      break;
    }
  }
};

void CInterpolatePos::DoInterpolate() {
  if (!IsInterpolating || !InterpolatingPlayer || !InterpolatingPlayer->npc)
    return;

  zVEC3 current = InterpolatingPlayer->npc->GetPositionWorld();
  zVEC3 diff = m_vecPosition - current;
  zVEC3 absDiff(std::fabs(diff[VX]), std::fabs(diff[VY]), std::fabs(diff[VZ]));
  const float horizontalDiff = std::sqrt(diff[VX] * diff[VX] + diff[VZ] * diff[VZ]);
  const float verticalDiff = absDiff[VY];
  const float distance = std::sqrt(horizontalDiff * horizontalDiff + verticalDiff * verticalDiff);

  if (distance > 200.0f || (horizontalDiff < 1.0f && verticalDiff < 1.0f)) {
    InterpolatingPlayer->SetPosition(m_vecPosition);
    ResetInternal();
    return;
  }

  if (m_bPosUpdate) {
    m_LastDiff = absDiff;
    m_bPosUpdate = false;
    m_uDeltaTime = 0;
  } else {
    if (Gothic_II_Addon::ztimer)
      m_uDeltaTime += Gothic_II_Addon::ztimer->frameTime;
    else
      m_uDeltaTime += PLAYER_SYNC_TIME;
  }

  float deltaMs = PLAYER_SYNC_TIME;
  float deltaSeconds = static_cast<float>(PLAYER_SYNC_TIME) / 1000.0f;
  if (Gothic_II_Addon::ztimer) {
    if (Gothic_II_Addon::ztimer->frameTime > 0)
      deltaMs = static_cast<float>(Gothic_II_Addon::ztimer->frameTime);
    if (Gothic_II_Addon::ztimer->frameTimeFloatSecs > 0.0f)
      deltaSeconds = Gothic_II_Addon::ztimer->frameTimeFloatSecs;
  }

  const float stepFraction = std::min(deltaMs * PLAYER_SYNC_CONST, 1.0f);
  float horizontalStep = 0.0f;

  if (horizontalDiff >= 1.0f && horizontalDiff <= 200.0f) {
    if (m_fVelocity >= 0.1f)
      horizontalStep = m_fVelocity * deltaSeconds;
    else
      horizontalStep = horizontalDiff * stepFraction;

    const float previousHorizontal = std::sqrt(m_LastDiff[VX] * m_LastDiff[VX] + m_LastDiff[VZ] * m_LastDiff[VZ]);
    if (horizontalDiff > previousHorizontal + 0.25f)
      horizontalStep *= 1.5f;

    if (m_uDeltaTime > PLAYER_SYNC_TIME)
      horizontalStep *= 1.25f;

    horizontalStep = std::min(horizontalStep, horizontalDiff);
  }

  float verticalStep = 0.0f;
  if (verticalDiff >= 1.0f && verticalDiff <= 200.0f) {
    verticalStep = verticalDiff * stepFraction;

    if (verticalDiff > m_LastDiff[VY] + 0.25f)
      verticalStep *= 1.5f;

    if (m_uDeltaTime > PLAYER_SYNC_TIME)
      verticalStep *= 1.25f;

    verticalStep = std::min(verticalStep, verticalDiff);
  }

  if (horizontalStep <= 0.0f && verticalStep <= 0.0f) {
    InterpolatingPlayer->SetPosition(m_vecPosition);
    ResetInternal();
    return;
  }

  zVEC3 newPos = current;

  if (horizontalStep > 0.0f) {
    zVEC3 horizontalDir(diff[VX], 0.0f, diff[VZ]);
    horizontalDir.Normalize();
    newPos[VX] += horizontalDir[VX] * horizontalStep;
    newPos[VZ] += horizontalDir[VZ] * horizontalStep;
  } else {
    newPos[VX] = m_vecPosition[VX];
    newPos[VZ] = m_vecPosition[VZ];
  }

  if (verticalDiff < 1.0f || verticalDiff > 200.0f) {
    newPos[VY] = m_vecPosition[VY];
  } else if (verticalStep > 0.0f) {
    newPos[VY] += (diff[VY] < 0.0f) ? -verticalStep : verticalStep;
  }

  for (int axis = 0; axis < 3; ++axis) {
    if ((m_vecPosition[axis] - current[axis]) >= 0.0f)
      newPos[axis] = std::min(newPos[axis], m_vecPosition[axis]);
    else
      newPos[axis] = std::max(newPos[axis], m_vecPosition[axis]);
  }

  InterpolatingPlayer->SetPosition(newPos);

  zVEC3 remaining = m_vecPosition - newPos;
  m_LastDiff = zVEC3(std::fabs(remaining[VX]), std::fabs(remaining[VY]), std::fabs(remaining[VZ]));

  const float remainingHorizontal = std::sqrt(remaining[VX] * remaining[VX] + remaining[VZ] * remaining[VZ]);
  if (remainingHorizontal < 1.0f && m_LastDiff[VY] < 1.0f) {
    InterpolatingPlayer->SetPosition(m_vecPosition);
    ResetInternal();
  }
};

bool CInterpolatePos::IsDistanceSmallerThanRadius(float radius, float bX, float bY, float bZ, float rX, float rY, float rZ) {
  float vector[3];
  vector[0] = rX - bX;
  vector[1] = rY - bY;
  vector[2] = rZ - bZ;
  if ((vector[0] < radius && vector[0] > -radius) && (vector[1] < radius && vector[1] > -radius) && (vector[2] < radius && vector[2] > -radius)) {
    return true;
  } else
    return false;
};

bool CInterpolatePos::IsDistanceSmallerThanRadius(float radius, const zVEC3& Pos, const zVEC3& Pos1) {
  float vector[3];
  vector[0] = Pos1[VX] - Pos[VX];
  vector[1] = Pos1[VY] - Pos[VY];
  vector[2] = Pos1[VZ] - Pos[VZ];
  if ((vector[0] < radius && vector[0] > -radius) && (vector[1] < radius && vector[1] > -radius) && (vector[2] < radius && vector[2] > -radius)) {
    return true;
  } else
    return false;
};

void CInterpolatePos::UpdateInterpolation(float x, float y, float z) {
  m_vecPosition[VX] = x;
  m_vecPosition[VY] = y;
  m_vecPosition[VZ] = z;
  IsInterpolating = true;
  m_bPosUpdate = true;
};

void CInterpolatePos::UpdateAnimation(int animationId) {
  m_uAnimation = animationId;
  m_fVelocity = 0.0f;
  if (!InterpolatingPlayer || !InterpolatingPlayer->npc || animationId <= 0)
    return;

  zCModel* model = InterpolatingPlayer->npc->GetModel();
  if (!model)
    return;

  zCModelAni* ani = model->GetAniFromAniID(animationId);
  if (!ani)
    return;

  float aniVelocity = ani->GetAniVelocity();
  if (aniVelocity > 0.0f)
    m_fVelocity = aniVelocity;
};

void CInterpolatePos::Reset() {
  ResetInternal();
}

void CInterpolatePos::ResetInternal() {
  IsInterpolating = false;
  m_LastDiff = zVEC3();
  m_uDeltaTime = 0;
  m_bPosUpdate = false;
}