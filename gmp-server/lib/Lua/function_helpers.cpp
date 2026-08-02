
/*
MIT License

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


#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <openssl/md5.h>
#include <openssl/sha.h>
#include <spdlog/spdlog.h>

#include "sol/sol.hpp"

namespace {

sol::optional<std::string> GetOptionalString(const sol::table& table, const char* lowerKey, const char* upperKey) {
  if (auto value = table.get<sol::optional<std::string>>(lowerKey); value) {
    return value;
  }
  return table.get<sol::optional<std::string>>(upperKey);
}

std::string BytesToHex(const unsigned char* data, std::size_t length) {
  std::ostringstream stream;
  stream << std::hex << std::nouppercase << std::setfill('0');

  for (std::size_t i = 0; i < length; ++i) {
    stream << std::setw(2) << static_cast<int>(data[i]);
  }

  return stream.str();
}

}  // namespace
