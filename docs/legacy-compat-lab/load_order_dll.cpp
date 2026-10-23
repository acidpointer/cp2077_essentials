#include <windows.h>
#include <cstdio>
struct Initializer { Initializer() { std::puts("DLL static initializer"); std::fflush(stdout); } };
Initializer initializer;
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) { std::puts("DllMain attach"); std::fflush(stdout); }
    return TRUE;
}
extern "C" __declspec(dllexport) void Query()
{
    std::puts("Query reached"); std::fflush(stdout);
}
