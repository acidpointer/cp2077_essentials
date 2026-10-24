// Loaded only by the isolated host. Report uses a preallocated host event buffer.
#include <windows.h>
#ifndef MODULE_ID
#define MODULE_ID 1
#endif
#if MODULE_ID == 2
extern "C" __declspec(dllimport) int DependencyValue();
#endif
static void Report(int phase)
{
    auto record = reinterpret_cast<void (*)(int, int)>(GetProcAddress(GetModuleHandleW(nullptr), "RecordEvent"));
    if (record) record(MODULE_ID, phase);
}
struct Initializer { Initializer() { Report(2); } };
static Initializer initializer;
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) Report(3);
    if (reason == DLL_PROCESS_DETACH) Report(5);
    return TRUE;
}
#if MODULE_ID == 1
extern "C" __declspec(dllexport) int DependencyValue() { return 17; }
#else
extern "C" __declspec(dllexport) int QueryFixture() { Report(4); return DependencyValue(); }
#endif
