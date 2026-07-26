
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

#include "CIngame.h"

// Externs
extern CIngame* global_ingame;

namespace {
constexpr float kInterpolationSnapDistance = 1.0f;
constexpr float kInterpolationTeleportDistance = 400.0f;

float DistanceBetween(const zVEC3& from, const zVEC3& to) {
  const float x = to[VX] - from[VX];
  const float y = to[VY] - from[VY];
  const float z = to[VZ] - from[VZ];
  return std::sqrt(x * x + y * y + z * z);
}

float InterpolationSpeedForDistance(float distance) {
  if (distance < 70.0f) {
    return 1200.0f;
  }
  if (distance < 100.0f) {
    return 1600.0f;
  }
  if (distance < 200.0f) {
    return 2200.0f;
  }
  if (distance < 300.0f) {
    return 3000.0f;
  }
  return 3800.0f;
}
}  // namespace

CInterpolatePos::CInterpolatePos(Gothic2APlayer* Player) {
  InterpolatingPlayer = Player;
  IsInterpolating = false;
  if (global_ingame) {
    global_ingame->Interpolation.push_back(this);
  }
  InterCount = 0;
  LastUpdate = std::chrono::steady_clock::now();
};

CInterpolatePos::~CInterpolatePos() {
  IsInterpolating = false;
  InterCount = 0;
  InterpolatingPlayer = NULL;
  if (global_ingame) {
    for (int i = 0; i < (int)global_ingame->Interpolation.size(); i++) {
      if (global_ingame->Interpolation[i] == this) {
        global_ingame->Interpolation.erase(global_ingame->Interpolation.begin() + i);
        break;
      }
    }
  }
};

void CInterpolatePos::DoInterpolate() {
  if (!IsInterpolating)
    return;

  if (!InterpolatingPlayer || !InterpolatingPlayer->npc) {
    IsInterpolating = false;
    InterCount = 0;
    return;
  }

  const float distance = DistanceBetween(InterpolatingPlayer->npc->GetPositionWorld(), InterpolatingTo);
  if (distance <= kInterpolationSnapDistance) {
    InterpolatingPlayer->SetPosition(InterpolatingTo);
    StopInterpolation();
    return;
  }

  if (distance > kInterpolationTeleportDistance) {
    InterpolatingPlayer->SetPosition(InterpolatingTo);
    StopInterpolation();
    return;
  }

  Interpolate(InterpolatingTo[VX], InterpolatingTo[VY], InterpolatingTo[VZ], InterpolationSpeedForDistance(distance));
};

void CInterpolatePos::Interpolate(float x, float y, float z, float value) {
  if (!IsInterpolating)
    return;

  if (!InterpolatingPlayer || !InterpolatingPlayer->npc) {
    IsInterpolating = false;
    InterCount = 0;
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  const float delta_seconds =
      std::clamp(std::chrono::duration<float>(now - LastUpdate).count(), 0.0f, 0.1f);
  LastUpdate = now;

  zVEC3 Pos = InterpolatingPlayer->npc->GetPositionWorld();
  const float delta_x = x - Pos[VX];
  const float delta_y = y - Pos[VY];
  const float delta_z = z - Pos[VZ];
  const float distance = std::sqrt(delta_x * delta_x + delta_y * delta_y + delta_z * delta_z);
  const float max_step = value * delta_seconds;

  zVEC3 target(x, y, z);
  if (distance <= kInterpolationSnapDistance || distance <= max_step) {
    InterpolatingPlayer->SetPosition(target);
    StopInterpolation();
    return;
  } else if (distance > 0.0f) {
    const float ratio = max_step / distance;
    Pos[VX] += delta_x * ratio;
    Pos[VY] += delta_y * ratio;
    Pos[VZ] += delta_z * ratio;
  }

  if (IsDistanceSmallerThanRadius(kInterpolationSnapDistance, Pos, target)) {
    InterpolatingPlayer->SetPosition(target);
    StopInterpolation();
    return;
  }

  InterpolatingPlayer->SetPosition(Pos);
  InterCount++;
  if (InterCount > 3000) {
    InterpolatingPlayer->SetPosition(x, y, z);
    StopInterpolation();
  }
};

bool CInterpolatePos::IsDistanceSmallerThanRadius(float radius, float bX, float bY, float bZ, float rX, float rY, float rZ) {
  const float x = rX - bX;
  const float y = rY - bY;
  const float z = rZ - bZ;
  return (x * x + y * y + z * z) < (radius * radius);
};

bool CInterpolatePos::IsDistanceSmallerThanRadius(float radius, const zVEC3& Pos, const zVEC3& Pos1) {
  const float x = Pos1[VX] - Pos[VX];
  const float y = Pos1[VY] - Pos[VY];
  const float z = Pos1[VZ] - Pos[VZ];
  return (x * x + y * y + z * z) < (radius * radius);
};

void CInterpolatePos::StopInterpolation() {
  IsInterpolating = false;
  InterCount = 0;
  LastUpdate = std::chrono::steady_clock::now();
}

void CInterpolatePos::UpdateInterpolation(float x, float y, float z) {
  if (!IsInterpolating) {
    IsInterpolating = true;
    InterCount = 0;
    LastUpdate = std::chrono::steady_clock::now();
  }
  InterpolatingTo[VX] = x;
  InterpolatingTo[VY] = y;
  InterpolatingTo[VZ] = z;
};
