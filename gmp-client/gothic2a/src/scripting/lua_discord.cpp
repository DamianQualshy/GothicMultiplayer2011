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

#include "lua_discord.h"
#include "discord_presence.h"

namespace gmp::gothic {
namespace {

sol::optional<std::string> GetOptionalString(const sol::table& table, const char* lowerKey, const char* upperKey) {
  if (auto value = table.get<sol::optional<std::string>>(lowerKey); value) {
    return value;
  }
  return table.get<sol::optional<std::string>>(upperKey);
}

struct DiscordActivityState {
  std::string state;
  std::string details;
  std::string large_image_key;
  std::string large_image_text;
  std::string small_image_key;
  std::string small_image_text;
};

DiscordActivityState& GetDiscordActivityState() {
  static DiscordActivityState state;
  return state;
}

void ApplyDiscordActivityState(const DiscordActivityState& state) {
  DiscordRichPresence::Instance().UpdateActivity(state.state, state.details, 0, 0, state.large_image_key, state.large_image_text,
                                                 state.small_image_key, state.small_image_text);
}
} // namespace

void BindDiscord(sol::state& lua) {
  /* luagmp (class)
  *
  * This class exposes static methods for updating the user's Discord activity from the game client.
  *
  * @version  0.3.0
  * @name     Discord
  * @side     client
  * @category Game
  *
  */
    auto discord = lua.create_table("Discord");

  /* luagmp (method)
  *
  * This function updates the Discord Rich Presence activity. Missing fields keep their last-set values.
  *
  * @version  0.3.0
  * @name     setActivity
  * @static
  * @param    ({...}) Activity configuration table.
  *
  */
    discord.set_function("setActivity", [](const sol::table& params) {
      auto& activity = GetDiscordActivityState();

      if (auto value = GetOptionalString(params, "state", "State"); value) {
        activity.state = *value;
      }
      if (auto value = GetOptionalString(params, "details", "Details"); value) {
        activity.details = *value;
      }
      if (auto value = GetOptionalString(params, "largeImageKey", "LargeImageKey"); value) {
        activity.large_image_key = *value;
      }
      if (auto value = GetOptionalString(params, "largeImageText", "LargeImageText"); value) {
        activity.large_image_text = *value;
      }
      if (auto value = GetOptionalString(params, "smallImageKey", "SmallImageKey"); value) {
        activity.small_image_key = *value;
      }
      if (auto value = GetOptionalString(params, "smallImageText", "SmallImageText"); value) {
        activity.small_image_text = *value;
      }

      ApplyDiscordActivityState(activity);
    });

  /* luagmp (method)
  *
  * This function will update the activity state text.
  *
  * @version  0.3.0
  * @name     setState
  * @static
  * @param    (string) state
  *
  */
    discord.set_function("setState", [](const std::string& state) {
      auto& activity = GetDiscordActivityState();
      activity.state = state;
      ApplyDiscordActivityState(activity);
    });

  /* luagmp (method)
  *
  * This function will update the activity details text.
  *
  * @version  0.3.0
  * @name     setDetails
  * @static
  * @param    (string) details
  *
  */
    discord.set_function("setDetails", [](const std::string& details) {
      auto& activity = GetDiscordActivityState();
      activity.details = details;
      ApplyDiscordActivityState(activity);
    });

  /* luagmp (method)
  *
  * This function will update the large image entry for the activity.
  *
  * @version  0.3.0
  * @name     setLargeImage
  * @static
  * @param    (string) key    Asset key for the large image.
  * @param    (string) text   Optional tooltip text for the large image.
  *
  */
    discord.set_function("setLargeImage", [](const std::string& key, const sol::optional<std::string>& text) {
      auto& activity = GetDiscordActivityState();
      activity.large_image_key = key;
      if (text) {
        activity.large_image_text = *text;
      }
      ApplyDiscordActivityState(activity);
    });

  /* luagmp (method)
  *
  * This function will update the small image entry for the activity.
  *
  * @version  0.3.0
  * @name     setSmallImage
  * @static
  * @param    (string) key    Asset key for the small image.
  * @param    (string) text   Optional tooltip text for the small image.
  *
  */
    discord.set_function("setSmallImage", [](const std::string& key, const sol::optional<std::string>& text) {
      auto& activity = GetDiscordActivityState();
      activity.small_image_key = key;
      if (text) {
        activity.small_image_text = *text;
      }
      ApplyDiscordActivityState(activity);
    });

  /* luagmp (method)
  *
  * This function will clear the current activity and stored values.
  *
  * @version  0.3.0
  * @name     clearActivity
  * @static
  *
  */
    discord.set_function("clearActivity", []() {
      auto& activity = GetDiscordActivityState();
      activity = DiscordActivityState{};
      DiscordRichPresence::Instance().ClearActivity();
    });
}

}  // namespace gmp::gothic