// Actual historical RTTI, pool lookup, property/function construction and
// DynArray methods. The resolver supplies fake engine services from the host.
#define RED4EXT_STATIC_LIB
#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Scripting/Functions.hpp>
#include <RED4ext/Scripting/CProperty.hpp>
#undef RED4EXT_INLINE
#define RED4EXT_INLINE inline
#include <RED4ext/RTTISystem-inl.hpp>
#include <RED4ext/RTTITypes-inl.hpp>
#include <RED4ext/Scripting/CProperty-inl.hpp>
#include <RED4ext/Memory/Vault-inl.hpp>
#include <RED4ext/SharedSpinLock-inl.hpp>
#include <RED4ext/CNamePool-inl.hpp>
#include <RED4ext/Scripting/Stack.hpp>

struct Counts { int live,copies,calls,badDomain;uint32_t (*current)(); };
static Counts* counts;
static uintptr_t (*resolve)(uint32_t);
uintptr_t RED4ext::UniversalRelocBase::Resolve(uint32_t hash) { return resolve(hash); }
static void Observe() { if(counts->current()!=41)++counts->badDomain; }
struct Payload
{
    Payload() { Observe();++counts->live; }
    Payload(const Payload&) { Observe();++counts->live;++counts->copies; }
    ~Payload() { Observe();--counts->live; }
};
static void Script(RED4ext::IScriptable*,RED4ext::CStackFrame*,int* out,int64_t) { *out=73; }
static void Register()
{
    Observe();++counts->calls;
    auto* rtti=RED4ext::CRTTISystem::Get();
    auto* cls=rtti->GetClass("CompatHost");
    auto* type=rtti->GetType("Int32");
    auto* property=RED4ext::CProperty::Create(type,"m_answer",cls,24,"compat");
    cls->props.PushBack(property);
    auto* function=RED4ext::CClassStaticFunction::Create(cls,"CompatAnswer","CompatAnswer",&Script);
    cls->RegisterFunction(function);
}
extern "C" __declspec(dllexport) void ConfigureRegistration(uintptr_t (*resolver)(uint32_t),Counts* value)
{ resolve=resolver;counts=value; }
extern "C" __declspec(dllexport) void InstallRegistration(bool post)
{
    auto* rtti=RED4ext::CRTTISystem::Get();
    if(post)rtti->AddPostRegisterCallback(RED4ext::Callback<void(*)()>([owned=Payload()]{Register();}));
    else rtti->AddRegisterCallback(RED4ext::Callback<void(*)()>([owned=Payload()]{Register();}));
}
extern "C" __declspec(dllexport) bool CheckMetadata()
{
    auto* rtti=RED4ext::CRTTISystem::Get();
    auto* cls=rtti->GetClass("CompatHost");auto* type=rtti->GetType("Int32");
    rtti->RegisterScriptName("CompatHost","ScriptHost");
    return cls && type && rtti->GetType("CompatHost")==cls && rtti->GetTypeByAsyncId(17)==type &&
        rtti->GetClassByScriptName("ScriptHost")==cls && !rtti->GetType("Missing") &&
        type->GetName()==RED4ext::CName("Int32") && type->GetComputedName()==RED4ext::CName("Int32") &&
        type->GetSize()==4 && type->GetAlignment()==4 && type->GetType()==RED4ext::ERTTIType::Fundamental &&
        cls->GetSize()==64 && cls->GetType()==RED4ext::ERTTIType::Class &&
        rtti->ConvertNativeToScriptName("CompatHost")==RED4ext::CName("ScriptHost") &&
        rtti->ConvertScriptToNativeName("ScriptHost")==RED4ext::CName("CompatHost");
}
extern "C" __declspec(dllexport) bool CheckRegistration()
{
    auto* rtti=RED4ext::CRTTISystem::Get();auto* cls=rtti->GetClass("CompatHost");
    auto* prop=cls->props[0];
    return cls->props.size() && prop->type==rtti->GetType("Int32") && prop->parent==cls &&
        prop->name==RED4ext::CName("answer") && prop->group==RED4ext::CName("compat") &&
        prop->valueOffset==24 && cls->GetFunction("CompatAnswer");
}
extern "C" __declspec(dllexport) void UnknownVirtual()
{ RED4ext::CRTTISystem::Get()->InitializeScriptRuntime(); }
extern "C" __declspec(dllexport) void RegisterMappedType(void* type,uint32_t id)
{ static_cast<RED4ext::IRTTISystem*>(RED4ext::CRTTISystem::Get())->RegisterType(static_cast<RED4ext::rtti::IType*>(type),id); }
extern "C" __declspec(dllexport) void VoidScript(void*,void* value,void* output,int64_t extra)
{
    Observe();if(output)throw 81;
    auto* frame=static_cast<RED4ext::CStackFrame*>(value);
    ++frame->currentParam;
    if(extra<0)throw 82;
}
