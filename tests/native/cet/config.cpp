#include "common/JsonConfig.h"
#include <Windows.h>
#include <cassert>
#include <iostream>
#include <cstdlib>
#undef assert
#define assert(condition) do { if (!(condition)) { std::cerr << "Check failed: " << #condition << " at " << __FILE__ << ':' << __LINE__ << '\n'; std::exit(1); } } while (0)
#include <iterator>
static std::string read(const std::filesystem::path& path)
{
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
int main()
{
    const auto directory = std::filesystem::temp_directory_path() / (L"cet-config-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto target = directory/L"settings.json";
    { std::ofstream file(target); file << "{broken"; }
    Config::HadInvalidValue = false; Config::Read(target); assert(Config::HadInvalidValue);
    assert(Config::Write(target,{{"first",1}},true));
    assert(read(directory/L"settings.json.invalid-1") == "{broken");
    assert(Config::Read(target)["first"] == 1);
    assert(Config::Write(target,{{"second",2}}));
    assert(Config::Read(directory/L"settings.json.bak")["first"] == 1);
    assert(read(directory/L"settings.json.invalid-1") == "{broken");
    const auto before = read(target);
    assert(!Config::Write(target/L"impossible.json",{{"bad",1}}));
    assert(read(target) == before);
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        assert(entry.path().filename().string().find(".tmp-") == std::string::npos);
    std::filesystem::remove_all(directory);
    std::cout << "CET atomic config, permanent invalid backup and failed-write checks passed\n";
}
