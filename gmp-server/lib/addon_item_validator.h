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

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "item_registry.h"

namespace gmp::addon {
class AddonVfs;
}

class AddonItemValidator {
public:
  static constexpr std::size_t kDamageTypeCount = 8;
  static constexpr std::size_t kRequirementCount = 3;

  struct ExtractedItem {
    std::string instance;
    std::int32_t mainflag{0};
    std::int32_t flags{0};
    std::string visual;
    std::int32_t wear{0};
    std::int32_t range{0};
    std::int32_t value{0};
    ItemRegistry::Damage damage;
    std::string munition;
    std::int32_t spell{0};
    std::string scemename;
    std::int32_t mag_circle{0};
    std::array<std::int32_t, kDamageTypeCount> protections{};
    std::array<ItemRegistry::Requirement, kRequirementCount> requirements{};
  };

  struct Summary {
    std::size_t matched{0};
    std::size_t missing{0};
    std::size_t extra{0};
    std::size_t field_mismatches{0};
    std::vector<std::string> differences;
  };

  static bool ValidateDat(const gmp::addon::AddonVfs& vfs, const ItemRegistry& registry, Summary& summary, std::string& error);
  static Summary Compare(const ItemRegistry& registry, const std::vector<ExtractedItem>& dat_items);
};
