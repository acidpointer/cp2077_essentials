#include "App/Utils/Unicode.hpp"

#include <cassert>
#include <cstdio>
#include <utility>
#include <vector>

int main()
{
    const std::vector<std::pair<std::string, std::wstring>> samples{
        {"", L""},
        {"ASCII text", L"ASCII text"},
        {"caf\xC3\xA9", L"caf\u00E9"},
        {"\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD1\x96\xD1\x82", L"\u041F\u0440\u0438\u0432\u0456\u0442"},
        {"\xE6\x97\xA5\xE6\x9C\xAC", L"\u65E5\u672C"},
        {"\xF0\x9F\x98\x80", L"\U0001F600"},
        {std::string("A\0B", 3), std::wstring(L"A\0B", 3)},
    };
    for (const auto& [utf8, wide] : samples)
    {
        assert(App::UTF8ToWide(utf8) == wide);
        assert(App::WideToUTF8(wide) == utf8);
    }
    assert(App::UTF8ToWide(std::string_view{}).empty());
    assert(App::WideToUTF8(std::wstring_view{}).empty());

    bool rejectedUtf8 = false;
    try
    {
        App::UTF8ToWide("\xC3\x28");
    }
    catch (const std::range_error&)
    {
        rejectedUtf8 = true;
    }
    assert(rejectedUtf8);
    bool rejectedUtf16 = false;
    try
    {
        App::WideToUTF8(std::wstring(1, static_cast<wchar_t>(0xD800)));
    }
    catch (const std::range_error&)
    {
        rejectedUtf16 = true;
    }
    assert(rejectedUtf16);
    std::puts("UTF conversion tests passed: empty, ASCII, accents, Cyrillic, CJK, surrogate pairs, embedded NULs and invalid input.");
}
