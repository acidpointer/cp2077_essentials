#include <cstdint>
#include <windows.h>
extern "C" __declspec(dllexport) __declspec(noinline) int read_value(void* object)
{
    return *reinterpret_cast<volatile int*>(static_cast<char*>(object) + 0x10);
}
extern "C" __declspec(dllexport) __declspec(noinline) long atomic_increment(void* object)
{
    return InterlockedIncrement(reinterpret_cast<volatile LONG*>(static_cast<char*>(object) + 0x10));
}
extern "C" __declspec(dllexport) __declspec(noinline) void write_value(void* object, int value)
{
    *reinterpret_cast<volatile int*>(static_cast<char*>(object) + 0x10) = value;
}
