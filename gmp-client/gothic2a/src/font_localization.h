
/*
MIT License

Copyright (c) 2025 Gothic Multiplayer Team

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

#include <string_view>

namespace font_localization {

inline constexpr std::string_view kFontDefault = "FONT_DEFAULT.TGA";
inline constexpr std::string_view kFontOld20White = "FONT_OLD_20_WHITE.TGA";
inline constexpr std::string_view kFontOld10White = "FONT_OLD_10_WHITE.TGA";
inline constexpr std::string_view kFontOld20WhiteHi = "FONT_OLD_20_WHITE_HI.TGA";
inline constexpr std::string_view kFontOld10WhiteHi = "FONT_OLD_10_WHITE_HI.TGA";
inline constexpr std::string_view kFontBook20 = "FONT_OLD_20_BOOK.TGA";
inline constexpr std::string_view kFontBook10 = "FONT_OLD_10_BOOK.TGA";
inline constexpr std::string_view kFontBook20Hi = "FONT_OLD_20_BOOK_HI.TGA";
inline constexpr std::string_view kFontBook10Hi = "FONT_OLD_10_BOOK_HI.TGA";

void SetLanguagePrefix(std::string prefix);
const char* GetFont(std::string_view baseFont);
void ReloadFonts();

}  // namespace font_localization