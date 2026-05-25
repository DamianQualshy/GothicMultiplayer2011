#pragma once

#include <cstdint>
#include <optional>

namespace gmp::gothic {

constexpr std::int32_t kHudAll = 0;
constexpr std::int32_t kHudHealthBar = 1;
constexpr std::int32_t kHudManaBar = 2;
constexpr std::int32_t kHudSwimBar = 3;
constexpr std::int32_t kHudFocusBar = 4;

bool SetHudEnabled(std::int32_t hud_type, bool enabled);
std::optional<bool> GetHudEnabled(std::int32_t hud_type);
bool IsHudTypeEnabled(std::int32_t hud_type);

}  // namespace gmp::gothic
