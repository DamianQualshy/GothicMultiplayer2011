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
#include <vector>

class ItemRegistry {
public:
  struct Damage {
    std::int32_t total{0};
    std::int32_t types{0};
  };

  struct Protection {
    std::int32_t type{0};
    std::int32_t value{0};
  };

  struct Requirement {
    std::int32_t attribute{0};
    std::int32_t value{0};
  };

  struct Item {
    std::string instance;
    std::int32_t index{0};
    std::int32_t mainflag{0};
    std::int32_t flags{0};
    std::string visual;
    std::optional<std::int32_t> wear;
    std::optional<std::int32_t> range;
    std::optional<std::int32_t> value;
    std::optional<Damage> damage;
    std::optional<std::string> munition;
    std::optional<std::int32_t> spell;
    std::string scemename;
    std::optional<std::int32_t> mag_circle;
    std::vector<Protection> protections;
    std::vector<Requirement> requirements;
  };

  bool Load(const std::filesystem::path& path);
  const Item* Find(std::string_view instance) const;
  const Item* FindByIndex(std::int32_t index) const;
  std::optional<std::string> CanonicalizeInstance(std::string_view instance) const;
  bool Contains(std::string_view instance) const;
  bool ContainsIndex(std::int32_t index) const;
  std::size_t Size() const;

  static std::string NormalizeInstanceName(std::string_view instance);

private:
  void Clear();

  std::vector<Item> items_;
  std::unordered_map<std::string, std::size_t> by_instance_;
  std::unordered_map<std::int32_t, std::size_t> by_index_;
};
