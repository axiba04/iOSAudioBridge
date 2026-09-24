#pragma once
#include "AudioCore.h"
#include <vector>
#include <string>
#include <string_view>
#include <algorithm>
namespace audiobridge {
std::wstring widen(std::string_view text);
std::string narrow(std::wstring_view text);
std::vector<AB_Device> list_phones();
std::vector<AB_OutputDevice> list_outputs();
template<size_t N> void copy_text(wchar_t (&dest)[N],std::wstring_view text) noexcept {
    const auto count=std::min(text.size(),N-1);
    std::copy_n(text.data(),count,dest); dest[count]=0;
}
}
