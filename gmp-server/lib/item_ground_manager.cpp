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

#include "item_ground_manager.h"

#include <limits>
#include <utility>

ItemGroundManager::ItemGround& ItemGroundManager::Create(CreateOptions options) {
  ItemGround item;
  item.id = AllocateId();
  item.instance = std::move(options.instance);
  item.amount = options.amount;
  item.physics_enabled = options.physics_enabled;
  item.position = options.position;
  item.rotation = options.rotation;
  item.world = std::move(options.world);
  item.virtual_world = options.virtual_world;

  auto [it, _] = items_.emplace(item.id, std::move(item));
  return it->second;
}

bool ItemGroundManager::Destroy(ItemGroundId id) {
  return items_.erase(id) > 0;
}

ItemGroundManager::ItemGround* ItemGroundManager::Get(ItemGroundId id) {
  auto it = items_.find(id);
  return it != items_.end() ? &it->second : nullptr;
}

const ItemGroundManager::ItemGround* ItemGroundManager::Get(ItemGroundId id) const {
  auto it = items_.find(id);
  return it != items_.end() ? &it->second : nullptr;
}

void ItemGroundManager::Clear() {
  items_.clear();
  next_id_ = 1;
}

std::unordered_map<ItemGroundManager::ItemGroundId, ItemGroundManager::ItemGround>& ItemGroundManager::Items() {
  return items_;
}

const std::unordered_map<ItemGroundManager::ItemGroundId, ItemGroundManager::ItemGround>& ItemGroundManager::Items() const {
  return items_;
}

ItemGroundManager::ItemGroundId ItemGroundManager::AllocateId() {
  for (;;) {
    if (next_id_ == 0 || next_id_ == std::numeric_limits<ItemGroundId>::max()) {
      next_id_ = 1;
    }

    ItemGroundId candidate = next_id_++;
    if (!items_.contains(candidate)) {
      return candidate;
    }
  }
}
