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

#include "addon_item_validator.h"

#include <addon/addon_vfs.h>
#include <zenkit/DaedalusScript.hh>
#include <zenkit/DaedalusVm.hh>
#include <zenkit/Stream.hh>
#include <zenkit/addon/daedalus.hh>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {

constexpr std::size_t kMaximumDatBytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaximumReportedDifferences = 100;

std::string Normalize(std::string_view value) {
  return ItemRegistry::NormalizeInstanceName(value);
}

void AddDifference(AddonItemValidator::Summary& summary, const std::string& instance, const std::string& field,
                   const std::string& json_value, const std::string& dat_value) {
  ++summary.field_mismatches;
  if (summary.differences.size() < kMaximumReportedDifferences) {
    summary.differences.push_back(instance + ": " + field + ": JSON=" + json_value + " DAT=" + dat_value);
  }
}

template <typename T>
void CompareValue(AddonItemValidator::Summary& summary, const std::string& instance, const char* field, const T& json_value,
                  const T& dat_value) {
  if (json_value != dat_value) {
    AddDifference(summary, instance, field, std::to_string(json_value), std::to_string(dat_value));
  }
}

void CompareString(AddonItemValidator::Summary& summary, const std::string& instance, const char* field, std::string_view json_value,
                   std::string_view dat_value) {
  if (Normalize(json_value) != Normalize(dat_value)) {
    AddDifference(summary, instance, field, std::string(json_value), std::string(dat_value));
  }
}

}  // namespace

AddonItemValidator::Summary AddonItemValidator::Compare(const ItemRegistry& registry, const std::vector<ExtractedItem>& dat_items) {
  Summary summary;
  std::unordered_map<std::string, const ExtractedItem*> by_name;
  by_name.reserve(dat_items.size());
  for (const auto& item : dat_items) {
    by_name.emplace(Normalize(item.instance), &item);
  }

  for (const auto& json : registry.Items()) {
    const auto found = by_name.find(Normalize(json.instance));
    if (found == by_name.end()) {
      ++summary.missing;
      if (summary.differences.size() < kMaximumReportedDifferences) {
        summary.differences.push_back(json.instance + ": missing from GOTHIC.DAT");
      }
      continue;
    }

    const auto& dat = *found->second;
    const auto mismatches_before = summary.field_mismatches;
    // ItemRegistry::index is the live ZenGin parser id used by GMP on the
    // network. ZenKit exposes the compiled DAT table id instead; engine
    // externals can shift that table, so those two numbers are not comparable.
    CompareValue(summary, json.instance, "mainflag", json.mainflag, dat.mainflag);
    CompareValue(summary, json.instance, "flags", json.flags, dat.flags);
    CompareString(summary, json.instance, "visual", json.visual, dat.visual);
    CompareValue(summary, json.instance, "wear", json.wear.value_or(0), dat.wear);
    CompareValue(summary, json.instance, "range", json.range.value_or(0), dat.range);
    CompareValue(summary, json.instance, "value", json.value.value_or(0), dat.value);
    CompareValue(summary, json.instance, "damage.total", json.damage ? json.damage->total : 0, dat.damage.total);
    CompareValue(summary, json.instance, "damage.types", json.damage ? json.damage->types : 0, dat.damage.types);
    CompareString(summary, json.instance, "munition", json.munition.value_or(std::string{}), dat.munition);
    CompareValue(summary, json.instance, "spell", json.spell.value_or(0), dat.spell);
    CompareString(summary, json.instance, "scemename", json.scemename, dat.scemename);
    CompareValue(summary, json.instance, "mag_circle", json.mag_circle.value_or(0), dat.mag_circle);

    std::array<std::int32_t, kDamageTypeCount> json_protections{};
    for (const auto& protection : json.protections) {
      if (protection.type >= 0 && static_cast<std::size_t>(protection.type) < json_protections.size()) {
        json_protections[static_cast<std::size_t>(protection.type)] = protection.value;
      } else {
        AddDifference(summary, json.instance, "protections.type", std::to_string(protection.type), "out-of-range");
      }
    }
    for (std::size_t i = 0; i < json_protections.size(); ++i) {
      CompareValue(summary, json.instance, ("protections[" + std::to_string(i) + "]").c_str(), json_protections[i], dat.protections[i]);
    }

    for (std::size_t i = 0; i < kRequirementCount; ++i) {
      const ItemRegistry::Requirement expected = i < json.requirements.size() ? json.requirements[i] : ItemRegistry::Requirement{};
      CompareValue(summary, json.instance, ("conditions[" + std::to_string(i) + "].attribute").c_str(), expected.attribute,
                   dat.requirements[i].attribute);
      CompareValue(summary, json.instance, ("conditions[" + std::to_string(i) + "].value").c_str(), expected.value,
                   dat.requirements[i].value);
    }
    if (json.requirements.size() > kRequirementCount) {
      AddDifference(summary, json.instance, "conditions.count", std::to_string(json.requirements.size()),
                    std::to_string(kRequirementCount));
    }

    if (summary.field_mismatches == mismatches_before) {
      ++summary.matched;
    }
    by_name.erase(found);
  }

  summary.extra = by_name.size();
  for (const auto& [_, item] : by_name) {
    if (summary.differences.size() >= kMaximumReportedDifferences) {
      break;
    }
    summary.differences.push_back(item->instance + ": present in GOTHIC.DAT but absent from data/instances/items.json");
  }
  return summary;
}

bool AddonItemValidator::ValidateDat(const gmp::addon::AddonVfs& vfs, const ItemRegistry& registry, Summary& summary, std::string& error) {
  try {
    auto bytes = vfs.ReadFile("GOTHIC.DAT", kMaximumDatBytes);
    auto reader = zenkit::Read::from(std::move(bytes));
    zenkit::DaedalusScript script;
    script.load(reader.get());
    zenkit::register_all_script_classes(script);
    zenkit::DaedalusVm vm(std::move(script));
    vm.register_default_external([](const zenkit::DaedalusSymbol& external) {
      throw std::runtime_error("item initialization requires engine external '" + external.name() + "'");
    });

    std::vector<zenkit::DaedalusSymbol*> symbols;
    vm.enumerate_instances_by_class_name("C_ITEM", [&](zenkit::DaedalusSymbol& symbol) {
      if (!symbol.is_external() && &symbol != vm.global_item()) {
        symbols.push_back(&symbol);
      }
    });

    std::vector<ExtractedItem> extracted;
    extracted.reserve(symbols.size());
    for (auto* symbol : symbols) {
      try {
        const auto item = vm.init_instance<zenkit::IItem>(symbol);
        ExtractedItem out;
        out.instance = symbol->name();
        out.mainflag = item->main_flag;
        // oCItem::InitByScript folds mainflag into flags after the Daedalus
        // constructor. Match the effective runtime value exported by Gothic.
        out.flags = static_cast<std::int32_t>(item->flags) | item->main_flag;
        out.visual = item->visual;
        out.wear = item->wear;
        out.range = item->range;
        out.value = item->value;
        out.damage = ItemRegistry::Damage{item->damage_total, item->damage_type};
        if (item->munition > 0) {
          const auto* munition = vm.find_symbol_by_index(static_cast<std::uint32_t>(item->munition));
          if (!munition || munition->type() != zenkit::DaedalusDataType::INSTANCE) {
            throw std::runtime_error("munition refers to invalid instance symbol index " + std::to_string(item->munition));
          }
          out.munition = munition->name();
        }
        out.spell = item->spell;
        out.scemename = item->scheme_name;
        out.mag_circle = item->mag_circle;
        std::copy(std::begin(item->protection), std::end(item->protection), out.protections.begin());
        for (std::size_t i = 0; i < out.requirements.size(); ++i) {
          out.requirements[i] = ItemRegistry::Requirement{item->cond_atr[i], item->cond_value[i]};
        }
        extracted.push_back(std::move(out));
      } catch (const std::exception& ex) {
        error = "Failed to initialize C_ITEM '" + symbol->name() + "': " + ex.what();
        return false;
      }
    }

    summary = Compare(registry, extracted);
    if (summary.missing != 0 || summary.extra != 0 || summary.field_mismatches != 0) {
      std::ostringstream message;
      message << "ItemRegistry/GOTHIC.DAT mismatch: " << summary.matched << " matched, " << summary.missing << " missing, "
              << summary.extra << " extra, " << summary.field_mismatches << " field mismatches";
      if (summary.differences.size() == kMaximumReportedDifferences) {
        message << " (diagnostics capped at " << kMaximumReportedDifferences << ")";
      }
      error = message.str();
      return false;
    }
    error.clear();
    return true;
  } catch (const std::exception& ex) {
    error = std::string("Failed to parse addon GOTHIC.DAT: ") + ex.what();
    return false;
  }
}
