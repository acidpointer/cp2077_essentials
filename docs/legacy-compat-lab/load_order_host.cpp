#include <windows.h>
#include <cstdio>
int main()
{
    std::puts("Before LoadLibrary"); std::fflush(stdout);
    auto module = LoadLibraryW(L"load_order.dll");
    if (!module) { std::printf("LoadLibrary failed: %lu\n", GetLastError()); return 1; }
    std::puts("After LoadLibrary, before Query"); std::fflush(stdout);
    auto query = reinterpret_cast<void (*)()>(GetProcAddress(module, "Query"));
    if (!query) return 2;
    query();
    FreeLibrary(module);
    return 0;
}
