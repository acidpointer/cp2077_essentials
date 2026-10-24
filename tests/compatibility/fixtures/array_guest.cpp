// Unmodified historical DynArray implementation, including its private tail
// allocator lookup and copied-vptr virtual calls. No host publication barriers.
#define RED4EXT_STATIC_LIB
#include <RED4ext/Containers/DynArray.hpp>
namespace RED4ext { struct CClassStaticFunction; }

using Array=RED4ext::DynArray<RED4ext::CClassStaticFunction*>;
static Array* array;
static uintptr_t (*resolve)(uint32_t);
static void* cachedAllocator;
uintptr_t RED4ext::UniversalRelocBase::Resolve(uint32_t hash) { return resolve(hash); }
extern "C" __declspec(dllexport) void ConfigureArray(uintptr_t (*resolver)(uint32_t),void* value)
{ resolve=resolver;array=static_cast<Array*>(value); }
extern "C" __declspec(dllexport) void Append(void* function)
{ array->PushBack(static_cast<RED4ext::CClassStaticFunction*>(function)); }
extern "C" __declspec(dllexport) void Grow(uint32_t capacity) { array->Reserve(capacity); }
extern "C" __declspec(dllexport) void Trim(uint32_t size) { array->Resize(size);array->ShrinkToFit(); }
extern "C" __declspec(dllexport) void* Element(uint32_t index) { return array->At(index); }
extern "C" __declspec(dllexport) void* Data() { return array->Data(); }
extern "C" __declspec(dllexport) uint32_t AllocatorHandle() { return array->GetAllocator()->GetHandle(); }
extern "C" __declspec(dllexport) void DestroyArray() { array->~Array(); }
extern "C" __declspec(dllexport) void CacheAllocator() { cachedAllocator=*reinterpret_cast<void**>(array->GetAllocator()); }
extern "C" __declspec(dllexport) uint32_t CachedHandle() { return reinterpret_cast<RED4ext::Memory::IAllocator*>(&cachedAllocator)->GetHandle(); }
extern "C" __declspec(dllexport) void UnknownArray() { Array local;local.Reserve(4); }
extern "C" __declspec(dllexport) void RejectAllocation() { (void)array->GetAllocator()->Alloc(8); }
