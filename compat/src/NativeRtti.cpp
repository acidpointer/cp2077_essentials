#ifdef _WIN32
#include <compat/NativeRtti.hpp>
#include <compat/Profile.hpp>
#include <RED4ext/RTTISystem.hpp>
namespace compat
{
namespace
{
RED4ext::CName Name(uint64_t hash) { RED4ext::CName name;name.hash=hash;return name; }
RttiObject Object(RED4ext::rtti::IType* type)
{
    if(!type)return {};
    return {type,type->GetType()==RED4ext::rtti::ERTTIType::Class ? profile::Class : profile::RttiMetadata};
}
}
RttiServices BindNativeRtti(RED4ext::CRTTISystem* system)
{
    if(!system)throw Failure(Error::UnknownPointer);
    RttiServices services;
    services.getType=[system](uint64_t name){return Object(system->GetType(Name(name)));};
    services.getClass=[system](uint64_t name){return Object(system->GetClass(Name(name)));};
    services.getAsyncType=[system](uint32_t id){return Object(system->GetTypeByAsyncId(id));};
    services.getScriptClass=[system](uint64_t name){return Object(system->GetClassByScriptName(Name(name)));};
    services.metadata=[](void* pointer) {
        auto* type=static_cast<RED4ext::rtti::IType*>(pointer);
        return TypeMetadata{type->GetName().hash,type->GetComputedName().hash,type->GetSize(),
                            type->GetAlignment(),static_cast<uint8_t>(type->GetType())};
    };
    services.registerScriptName=[system](uint64_t native,uint64_t script){system->RegisterScriptName(Name(native),Name(script));};
    services.convertName=[system](uint64_t name,bool toScript) {
        return (toScript ? system->ConvertNativeToScriptName(Name(name)) : system->ConvertScriptToNativeName(Name(name))).hash;
    };
    services.registerType=[system](void* type,uint32_t id) {
        system->RegisterType(static_cast<RED4ext::rtti::IType*>(type),id);
    };
    services.addCallback=[system](std::function<void()> invoke,bool post) {
        if(post)system->AddPostRegisterCallback(RED4ext::Callback<void(*)()>([owned=std::move(invoke)]{owned();}));
        else system->AddRegisterCallback(RED4ext::Callback<void(*)()>([owned=std::move(invoke)]{owned();}));
    };
    return services;
}
}
#endif
