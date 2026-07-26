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

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class AnimationRegistry {
public:
  struct Animation {
    std::string mds;
    std::string name;
    std::int16_t id{0};
  };

  bool Load(const std::filesystem::path& path);
  std::optional<std::int16_t> ResolveId(std::string_view animation_name) const;
  std::optional<std::int16_t> ResolveId(std::string_view animation_name, const std::vector<std::string>& preferred_mds) const;
  std::size_t Size() const;

  static std::string NormalizeName(std::string_view name);
  static std::string NormalizeMdsName(std::string_view name);

private:
  void Clear();
  std::optional<std::int16_t> ResolveFromMds(std::string_view normalized_name, std::string_view mds_name) const;

  std::vector<Animation> animations_;
  std::unordered_map<std::string, std::int16_t> global_by_name_;
  std::unordered_map<std::string, std::unordered_map<std::string, std::int16_t>> by_mds_and_name_;
  std::unordered_set<std::string> ambiguous_global_names_;
};
