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

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class AddonItemValidatorTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    registry_path_ = std::filesystem::temp_directory_path() / ("gmp-items-" + std::to_string(nonce) + ".json");
    std::ofstream output(registry_path_);
    output << R"({"items":[{"instance":"ITAR_TEST","index":42,"mainflag":2,"flags":6,"visual":"TEST.3DS","wear":8,"value":100,"damage":{"total":12,"types":1},"scemename":"ARMOR","protections":[{"type":0,"value":5}],"conditions":[{"attribute":0,"value":0},{"attribute":0,"value":0},{"attribute":3,"value":20}]}]})";
    output.close();
    ASSERT_TRUE(registry_.Load(registry_path_));
  }

  void TearDown() override {
    std::error_code ignored;
    std::filesystem::remove(registry_path_, ignored);
  }

  AddonItemValidator::ExtractedItem MatchingItem() const {
    AddonItemValidator::ExtractedItem item;
    item.instance = "itar_test";
    item.mainflag = 2;
    item.flags = 6;
    item.visual = "test.3ds";
    item.wear = 8;
    item.value = 100;
    item.damage = ItemRegistry::Damage{12, 1};
    item.scemename = "armor";
    item.protections[0] = 5;
    item.requirements[2] = ItemRegistry::Requirement{3, 20};
    return item;
  }

  std::filesystem::path registry_path_;
  ItemRegistry registry_;
};

TEST_F(AddonItemValidatorTest, AcceptsExactSemanticMatchCaseInsensitively) {
  const auto summary = AddonItemValidator::Compare(registry_, {MatchingItem()});
  EXPECT_EQ(summary.matched, 1u);
  EXPECT_EQ(summary.missing, 0u);
  EXPECT_EQ(summary.extra, 0u);
  EXPECT_EQ(summary.field_mismatches, 0u);
}

TEST_F(AddonItemValidatorTest, ReportsFieldMismatches) {
  auto item = MatchingItem();
  item.protections[0] = 9;
  const auto summary = AddonItemValidator::Compare(registry_, {item});
  EXPECT_EQ(summary.matched, 0u);
  EXPECT_EQ(summary.field_mismatches, 1u);
  ASSERT_EQ(summary.differences.size(), 1u);
  EXPECT_NE(summary.differences[0].find("protections"), std::string::npos);
}

TEST_F(AddonItemValidatorTest, ReportsMissingAndExtraInstances) {
  auto extra = MatchingItem();
  extra.instance = "ITMI_EXTRA";
  const auto summary = AddonItemValidator::Compare(registry_, {extra});
  EXPECT_EQ(summary.missing, 1u);
  EXPECT_EQ(summary.extra, 1u);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
