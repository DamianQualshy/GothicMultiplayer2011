/*
MIT License

Copyright (c) 2026 Gothic Multiplayer Team.

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

#include "animation_registry.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace {

using Json = nlohmann::json;

std::string DescribePath(const std::filesystem::path& path) {
  return std::filesystem::absolute(path).string();
}

const Json& RequireField(const Json& object, const char* field, const std::string& context) {
  const auto it = object.find(field);
  if (it == object.end()) {
    throw std::runtime_error(context + " missing required field '" + field + "'");
  }
  return *it;
}

std::int32_t ReadRequiredInt32(const Json& object, const char* field, const std::string& context) {
  const Json& value = RequireField(object, field, context);
  if (!value.is_number_integer()) {
    throw std::runtime_error(context + " field '" + field + "' must be an integer");
  }
  const auto number = value.get<std::int64_t>();
  if (number < std::numeric_limits<std::int32_t>::min() || number > std::numeric_limits<std::int32_t>::max()) {
    throw std::runtime_error(context + " field '" + field + "' is outside int32 range");
  }
  return static_cast<std::int32_t>(number);
}

std::string ReadRequiredString(const Json& object, const char* field, const std::string& context) {
  const Json& value = RequireField(object, field, context);
  if (!value.is_string()) {
    throw std::runtime_error(context + " field '" + field + "' must be a string");
  }
  return value.get<std::string>();
}

std::int16_t ClampAnimationId(std::int32_t id, const std::string& context) {
  if (id < 0 || id > std::numeric_limits<std::int16_t>::max()) {
    throw std::runtime_error(context + " field 'id' must be in range 0..32767");
  }
  return static_cast<std::int16_t>(id);
}

std::optional<std::string> GetStemAlias(const std::string& normalized_mds) {
  const std::filesystem::path path(normalized_mds);
  const std::string extension = AnimationRegistry::NormalizeName(path.extension().string());
  if (extension != ".MDS") {
    return std::nullopt;
  }

  const std::string stem = path.stem().string();
  if (stem.empty()) {
    return std::nullopt;
  }
  return stem;
}

AnimationRegistry::Animation ReadAnimationEntry(const Json& object, const std::string& mds, std::size_t ordinal) {
  const std::string context = "mds '" + mds + "' animations[" + std::to_string(ordinal) + "]";
  if (!object.is_object()) {
    throw std::runtime_error(context + " must be an object");
  }

  AnimationRegistry::Animation animation;
  animation.mds = mds;
  animation.name = ReadRequiredString(object, "name", context);
  if (animation.name.empty()) {
    throw std::runtime_error(context + " field 'name' must not be empty");
  }
  if (animation.name.size() > 255) {
    throw std::runtime_error(context + " field 'name' must not exceed 255 characters");
  }

  animation.id = ClampAnimationId(ReadRequiredInt32(object, "id", context), context);
  return animation;
}

}  // namespace

bool AnimationRegistry::Load(const std::filesystem::path& path) {
  Clear();

  try {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
      SPDLOG_CRITICAL("Required animation registry '{}' is missing or cannot be opened.", DescribePath(path));
      return false;
    }

    Json root = Json::parse(input);
    if (!root.is_object()) {
      throw std::runtime_error("root must be an object");
    }

    const Json& mds_list = RequireField(root, "mds", "root");
    if (!mds_list.is_array()) {
      throw std::runtime_error("root field 'mds' must be an array");
    }
    if (mds_list.empty()) {
      throw std::runtime_error("root field 'mds' must not be empty");
    }

    std::unordered_map<std::string, std::int16_t> seen_global;
    std::unordered_map<std::string, std::unordered_map<std::string, std::int16_t>> seen_by_mds;
    for (std::size_t mds_index = 0; mds_index < mds_list.size(); ++mds_index) {
      const Json& mds_object = mds_list[mds_index];
      const std::string context = "mds[" + std::to_string(mds_index) + "]";
      if (!mds_object.is_object()) {
        throw std::runtime_error(context + " must be an object");
      }

      const std::string mds_name = ReadRequiredString(mds_object, "name", context);
      if (mds_name.empty()) {
        throw std::runtime_error(context + " field 'name' must not be empty");
      }
      const std::string normalized_mds = NormalizeMdsName(mds_name);

      const Json& animations = RequireField(mds_object, "animations", context);
      if (!animations.is_array()) {
        throw std::runtime_error(context + " field 'animations' must be an array");
      }

      auto& names_in_mds = seen_by_mds[normalized_mds];
      for (std::size_t animation_index = 0; animation_index < animations.size(); ++animation_index) {
        Animation animation = ReadAnimationEntry(animations[animation_index], mds_name, animation_index);
        const std::string normalized_name = NormalizeName(animation.name);
        if (normalized_name.empty()) {
          throw std::runtime_error(context + " contains animation with empty normalized name");
        }

        const auto [mds_it, inserted_in_mds] = names_in_mds.emplace(normalized_name, animation.id);
        if (!inserted_in_mds && mds_it->second != animation.id) {
          throw std::runtime_error(context + " contains duplicate animation name '" + animation.name + "' with different ids");
        }
        if (!inserted_in_mds) {
          continue;
        }

        by_mds_and_name_[normalized_mds].emplace(normalized_name, animation.id);
        if (std::optional<std::string> stem_alias = GetStemAlias(normalized_mds)) {
          by_mds_and_name_[*stem_alias].emplace(normalized_name, animation.id);
        }

        const auto [global_it, inserted_global] = seen_global.emplace(normalized_name, animation.id);
        if (!inserted_global && global_it->second != animation.id) {
          ambiguous_global_names_.insert(normalized_name);
        } else if (inserted_global) {
          global_by_name_.emplace(normalized_name, animation.id);
        }

        animations_.push_back(std::move(animation));
      }
    }

    SPDLOG_INFO("Loaded {} animation definitions from {}.", animations_.size(), DescribePath(path));
    if (!ambiguous_global_names_.empty()) {
      SPDLOG_WARN("Animation registry contains {} globally ambiguous animation names.", ambiguous_global_names_.size());
    }
    return true;
  } catch (const nlohmann::json::exception& ex) {
    Clear();
    SPDLOG_CRITICAL("Failed to parse required animation registry '{}': {}", DescribePath(path), ex.what());
    return false;
  } catch (const std::exception& ex) {
    Clear();
    SPDLOG_CRITICAL("Failed to load required animation registry '{}': {}", DescribePath(path), ex.what());
    return false;
  }
}

std::optional<std::int16_t> AnimationRegistry::ResolveId(std::string_view animation_name) const {
  const std::string normalized_name = NormalizeName(animation_name);
  if (normalized_name.empty()) {
    return std::nullopt;
  }

  if (ambiguous_global_names_.contains(normalized_name)) {
    return std::nullopt;
  }

  const auto it = global_by_name_.find(normalized_name);
  if (it == global_by_name_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<std::int16_t> AnimationRegistry::ResolveId(std::string_view animation_name, const std::vector<std::string>& preferred_mds) const {
  const std::string normalized_name = NormalizeName(animation_name);
  if (normalized_name.empty()) {
    return std::nullopt;
  }

  for (const std::string& mds_name : preferred_mds) {
    if (std::optional<std::int16_t> animation_id = ResolveFromMds(normalized_name, mds_name)) {
      return animation_id;
    }
  }

  return ResolveId(normalized_name);
}

std::size_t AnimationRegistry::Size() const {
  return animations_.size();
}

std::string AnimationRegistry::NormalizeName(std::string_view name) {
  std::string normalized(name);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return normalized;
}

std::string AnimationRegistry::NormalizeMdsName(std::string_view name) {
  std::filesystem::path path{std::string(name)};
  std::string filename = path.filename().string();
  if (filename.empty()) {
    filename.assign(name.begin(), name.end());
  }
  return NormalizeName(filename);
}

std::optional<std::int16_t> AnimationRegistry::ResolveFromMds(std::string_view normalized_name, std::string_view mds_name) const {
  const std::string normalized_mds = NormalizeMdsName(mds_name);
  if (normalized_mds.empty()) {
    return std::nullopt;
  }

  const auto mds_it = by_mds_and_name_.find(normalized_mds);
  if (mds_it != by_mds_and_name_.end()) {
    const auto animation_it = mds_it->second.find(std::string(normalized_name));
    if (animation_it != mds_it->second.end()) {
      return animation_it->second;
    }
  }

  if (normalized_mds.find('.') == std::string::npos) {
    const auto mds_with_extension_it = by_mds_and_name_.find(normalized_mds + ".MDS");
    if (mds_with_extension_it != by_mds_and_name_.end()) {
      const auto animation_it = mds_with_extension_it->second.find(std::string(normalized_name));
      if (animation_it != mds_with_extension_it->second.end()) {
        return animation_it->second;
      }
    }
  }

  return std::nullopt;
}

void AnimationRegistry::Clear() {
  animations_.clear();
  global_by_name_.clear();
  by_mds_and_name_.clear();
  ambiguous_global_names_.clear();
}
