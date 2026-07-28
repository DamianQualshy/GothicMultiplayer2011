
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
#include <cstddef>

#include "CIngame.h"

// Externs
extern CIngame* global_ingame;

namespace {
constexpr float kInterpolationSnapDistance = 1.0f;
constexpr float kInterpolationTeleportDistance = 400.0f;
constexpr float kMaxExtrapolationDistance = 200.0f;
constexpr std::size_t kMaxBufferedPositionSamples = 8;
constexpr std::chrono::milliseconds kInterpolationDelay(120);
constexpr std::chrono::milliseconds kMaxExtrapolationTime(120);

float DistanceBetween(const zVEC3& from, const zVEC3& to) {
  const float x = to[VX] - from[VX];
  const float y = to[VY] - from[VY];
  const float z = to[VZ] - from[VZ];
  return std::sqrt(x * x + y * y + z * z);
}

zVEC3 LerpPosition(const zVEC3& from, const zVEC3& to, float alpha) {
  return zVEC3(from[VX] + (to[VX] - from[VX]) * alpha,
               from[VY] + (to[VY] - from[VY]) * alpha,
               from[VZ] + (to[VZ] - from[VZ]) * alpha);
}

zVEC3 AddScaledVelocity(const zVEC3& position, const zVEC3& velocity, float seconds) {
  return zVEC3(position[VX] + velocity[VX] * seconds,
               position[VY] + velocity[VY] * seconds,
               position[VZ] + velocity[VZ] * seconds);
}

zVEC3 VelocityBetween(const zVEC3& from, const zVEC3& to, float seconds) {
  if (seconds <= 0.001f) {
    return zVEC3(0.0f, 0.0f, 0.0f);
  }

  return zVEC3((to[VX] - from[VX]) / seconds,
               (to[VY] - from[VY]) / seconds,
               (to[VZ] - from[VZ]) / seconds);
}
}  // namespace

CInterpolatePos::CInterpolatePos(Gothic2APlayer* Player) {
  InterpolatingPlayer = Player;
  IsInterpolating = false;
  LastVelocity = zVEC3(0.0f, 0.0f, 0.0f);
  HasVelocity = false;
  if (global_ingame) {
    global_ingame->Interpolation.push_back(this);
  }
};

CInterpolatePos::~CInterpolatePos() {
  StopInterpolation();
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
    StopInterpolation();
    return;
  }

  if (PositionSamples.empty()) {
    StopInterpolation();
    return;
  }

  const auto now = Clock::now();
  const auto render_time = now - kInterpolationDelay;

  while (PositionSamples.size() >= 2 && PositionSamples[1].received_at <= render_time) {
    PositionSamples.pop_front();
  }

  zVEC3 render_position = PositionSamples.front().position;
  bool stop_after_render = false;

  if (PositionSamples.size() >= 2) {
    const PositionSample& from = PositionSamples[0];
    const PositionSample& to = PositionSamples[1];
    const float sample_span = std::chrono::duration<float>(to.received_at - from.received_at).count();

    if (sample_span <= 0.001f) {
      render_position = to.position;
    } else {
      const float elapsed = std::chrono::duration<float>(render_time - from.received_at).count();
      const float alpha = std::clamp(elapsed / sample_span, 0.0f, 1.0f);
      render_position = LerpPosition(from.position, to.position, alpha);
    }
  } else {
    const PositionSample& sample = PositionSamples.front();
    const auto extrapolation_time = render_time - sample.received_at;
    const float extrapolation_seconds = std::chrono::duration<float>(extrapolation_time).count();

    if (HasVelocity && extrapolation_seconds > 0.0f) {
      const float max_extrapolation_seconds = std::chrono::duration<float>(kMaxExtrapolationTime).count();
      const zVEC3 extrapolated_position =
          AddScaledVelocity(sample.position, LastVelocity, std::min(extrapolation_seconds, max_extrapolation_seconds));
      const float extrapolated_distance = DistanceBetween(sample.position, extrapolated_position);

      if (extrapolated_distance > kMaxExtrapolationDistance) {
        render_position = LerpPosition(sample.position, extrapolated_position, kMaxExtrapolationDistance / extrapolated_distance);
      } else {
        render_position = extrapolated_position;
      }

      if (extrapolation_time > kMaxExtrapolationTime && now - sample.received_at > kInterpolationDelay + kMaxExtrapolationTime) {
        stop_after_render = true;
      }
    } else if (now - sample.received_at > kInterpolationDelay + kMaxExtrapolationTime) {
      stop_after_render = true;
    }
  }

  const zVEC3 current_position = InterpolatingPlayer->npc->GetPositionWorld();
  if (DistanceBetween(current_position, render_position) > kInterpolationTeleportDistance) {
    InterpolatingPlayer->SetPosition(render_position);
    StopInterpolation();
    return;
  }

  InterpolatingPlayer->SetPosition(render_position);

  if (stop_after_render) {
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
  PositionSamples.clear();
  LastVelocity = zVEC3(0.0f, 0.0f, 0.0f);
  HasVelocity = false;
}

void CInterpolatePos::EnqueueSample(const zVEC3& position, Clock::time_point received_at, bool authoritative) {
  if (!PositionSamples.empty()) {
    const PositionSample& previous = PositionSamples.back();
    const float seconds = std::chrono::duration<float>(received_at - previous.received_at).count();
    if (previous.authoritative && authoritative) {
      LastVelocity = VelocityBetween(previous.position, position, seconds);
      HasVelocity = seconds > 0.001f;
    } else {
      LastVelocity = zVEC3(0.0f, 0.0f, 0.0f);
      HasVelocity = false;
    }
  }

  PositionSamples.push_back(PositionSample{position, received_at, authoritative});
  while (PositionSamples.size() > kMaxBufferedPositionSamples) {
    PositionSamples.pop_front();
  }

  IsInterpolating = true;
}

void CInterpolatePos::UpdateInterpolation(float x, float y, float z) {
  if (!InterpolatingPlayer || !InterpolatingPlayer->npc) {
    StopInterpolation();
    return;
  }

  const auto now = Clock::now();
  const zVEC3 target(x, y, z);

  if (PositionSamples.empty()) {
    const zVEC3 current_position = InterpolatingPlayer->npc->GetPositionWorld();
    const float distance = DistanceBetween(current_position, target);
    if (distance <= kInterpolationSnapDistance || distance > kInterpolationTeleportDistance) {
      InterpolatingPlayer->SetPosition(target);
      StopInterpolation();
      return;
    }

    EnqueueSample(current_position, now - kInterpolationDelay, false);
  } else if (DistanceBetween(PositionSamples.back().position, target) > kInterpolationTeleportDistance) {
    InterpolatingPlayer->SetPosition(target);
    StopInterpolation();
    return;
  }

  EnqueueSample(target, now, true);
};
